// harpoon-cli: command-line front end to the Harpoon core.
//
// Installing needs the "privileged" group on SailfishOS, e.g.:
//   devel-su -p harpoon-cli install <app>
// Everything else runs as the normal user.

#include "app/appchecker.h"
#include "app/appinstaller.h"
#include "app/appstore.h"
#include "app/backgroundscheduler.h"
#include "app/backup.h"
#include "app/harpoonsettings.h"
#include "app/updatenotifications.h"
#include "app/updatestatus.h"
#include "model/identity.h"
#include "net/networktransport.h"
#include "pkg/installhandlerbackend.h"
#include "pkg/packagekitbackend.h"
#include "sources/sourceregistry.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>
#include <memory>

using namespace Harpoon;

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

QTextStream &err()
{
    static QTextStream s(stderr);
    return s;
}

const char *kUsage =
    "Usage: harpoon-cli <command> [arguments]\n"
    "\n"
    "Commands:\n"
    "  add <url> [--source ID] [--set key=value]... [--force]\n"
    "                         Track an app. --source forces a forge type (Forgejo, GitHub)\n"
    "                         for self-hosted servers. --force saves even if the first check fails.\n"
    "  list                   Show tracked apps and their update state.\n"
    "  show <app>             Show details and the latest changelog.\n"
    "  check [app]... [--notify] [--quiet]\n"
    "                         Check for updates (all apps when none given). --notify posts a\n"
    "                         notification for releases not announced before.\n"
    "  install <app> [--reinstall] [--downgrade] [--backend packagekit|handler]\n"
    "                         Install the latest release.\n"
    "  upgrade [--backend B]  Check all apps, then install every available update.\n"
    "  set <app> key=value... Change per-app settings (empty value removes the key).\n"
    "  ack <app>              Mark the latest release of a track-only app as seen.\n"
    "  remove <app> [--uninstall]\n"
    "                         Stop tracking; --uninstall also removes the package.\n"
    "  export <file> [--include-tokens]\n"
    "                         Write a backup of tracked apps and settings.\n"
    "  import <file> [--with-settings]\n"
    "                         Add the apps from a backup (already tracked ones are skipped).\n"
    "  background on|off [--hours N]\n"
    "                         Enable or disable the periodic background check.\n"
    "\n"
    "<app> is an app id (RPM name) or a unique name.\n"
    "Tokens come from the app's settings; HARPOON_TOKEN_<SOURCE> (e.g. HARPOON_TOKEN_GITHUB) overrides.\n"
    "Environment: HARPOON_DATA_DIR, HARPOON_CACHE_DIR, HARPOON_CONFIG_DIR.\n";

struct Args
{
    QStringList positional;
    QHash<QString, QStringList> options; // "--x" -> values ("" for flags)

    bool has(const char *name) const { return options.contains(QString::fromLatin1(name)); }
    QString value(const char *name) const
    {
        const QStringList v = options.value(QString::fromLatin1(name));
        return v.isEmpty() ? QString() : v.last();
    }
    QStringList values(const char *name) const { return options.value(QString::fromLatin1(name)); }
};

Args parseArgs(const QStringList &raw, const QStringList &withValue)
{
    Args a;
    for (int i = 0; i < raw.size(); ++i) {
        const QString &arg = raw.at(i);
        if (arg.startsWith(QLatin1String("--"))) {
            const int eq = arg.indexOf(QLatin1Char('='));
            const QString name = eq > 0 ? arg.left(eq) : arg;
            if (eq > 0)
                a.options[name] << arg.mid(eq + 1);
            else if (withValue.contains(name) && i + 1 < raw.size())
                a.options[name] << raw.at(++i);
            else
                a.options[name] << QString();
        } else {
            a.positional << arg;
        }
    }
    return a;
}

// Runs an async operation to completion on a local event loop.
template <typename T>
T await(std::function<void(std::function<void(const T &)>)> start)
{
    T result{};
    bool finished = false;
    QEventLoop loop;
    start([&](const T &value) {
        result = value;
        finished = true;
        loop.quit();
    });
    if (!finished)
        loop.exec();
    return result;
}

class Cli
{
public:
    Cli()
        : m_store(AppStore::defaultDirectory())
        , m_device(DeviceInfo::detect())
        , m_inspector(m_runner)
    {
    }

    int run(const QStringList &argv)
    {
        if (argv.isEmpty() || argv.first() == QLatin1String("--help") || argv.first() == QLatin1String("-h")) {
            out() << kUsage;
            out().flush();
            return argv.isEmpty() ? 1 : 0;
        }
        const QString command = argv.first();
        const Args args = parseArgs(argv.mid(1), {QStringLiteral("--source"), QStringLiteral("--set"),
                                                  QStringLiteral("--backend"), QStringLiteral("--hours")});
        int rc;
        if (command == QLatin1String("add"))
            rc = add(args);
        else if (command == QLatin1String("list"))
            rc = list();
        else if (command == QLatin1String("show"))
            rc = show(args);
        else if (command == QLatin1String("check"))
            rc = check(args);
        else if (command == QLatin1String("install"))
            rc = install(args);
        else if (command == QLatin1String("upgrade"))
            rc = upgrade(args);
        else if (command == QLatin1String("set"))
            rc = set(args);
        else if (command == QLatin1String("ack"))
            rc = ack(args);
        else if (command == QLatin1String("remove"))
            rc = remove(args);
        else if (command == QLatin1String("export"))
            rc = exportBackup(args);
        else if (command == QLatin1String("import"))
            rc = importBackup(args);
        else if (command == QLatin1String("background"))
            rc = background(args);
        else {
            err() << "Unknown command: " << command << "\n\n" << kUsage;
            rc = 1;
        }
        out().flush();
        err().flush();
        return rc;
    }

private:
    int fail(const QString &message)
    {
        err() << "Error: " << message << "\n";
        return 2;
    }

    AppChecker &checker()
    {
        if (!m_checker) {
            m_checker.reset(new AppChecker(m_registry, m_transport, m_device));
            for (const QString &id : m_registry.ids()) {
                const QByteArray env = qgetenv("HARPOON_TOKEN_" + id.toUpper().toLatin1());
                const QString token = !env.isEmpty() ? QString::fromUtf8(env) : m_settings.token(id);
                if (!token.isEmpty())
                    m_checker->setSourceConfig(id, {{QStringLiteral("token"), token}});
            }
        }
        return *m_checker;
    }

    Result<App> findApp(const QString &key)
    {
        const QList<App> apps = m_store.loadAll();
        for (const App &a : apps)
            if (a.id == key)
                return Result<App>::success(a);
        QList<App> byName;
        for (const App &a : apps)
            if (a.name.compare(key, Qt::CaseInsensitive) == 0)
                byName << a;
        if (byName.size() == 1)
            return Result<App>::success(byName.first());
        return Result<App>::failure(Error::make(
            Error::Storage, byName.isEmpty() ? QStringLiteral("No tracked app \"%1\"").arg(key)
                                             : QStringLiteral("\"%1\" is ambiguous; use the id").arg(key)));
    }

    RpmInfo installedOf(const App &app)
    {
        if (app.temporaryId)
            return RpmInfo();
        const auto r = m_inspector.installedPackage(app.id);
        return r.ok() ? r.value : RpmInfo();
    }

    void printStatusLine(const App &app)
    {
        const UpdateStatus s = updateStatusFor(app, installedOf(app));
        out() << app.id << "  " << app.name << "\n    installed: "
              << (s.installedVersion.isEmpty() ? QStringLiteral("-") : s.installedVersion)
              << "  latest: " << (s.latestVersion.isEmpty() ? QStringLiteral("-") : s.latestVersion)
              << "  [" << updateStateName(s.state) << "]";
        if (!app.lastError.isEmpty())
            out() << "\n    last check failed: " << app.lastError;
        out() << "\n";
    }

    Error applySettings(App &app, const QStringList &pairs)
    {
        QVariantMap values = app.settings.values();
        for (const QString &kv : pairs) {
            const int eq = kv.indexOf(QLatin1Char('='));
            if (eq <= 0)
                return Error::make(Error::InvalidSetting, QStringLiteral("Expected key=value, got \"%1\"").arg(kv));
            const QString key = kv.left(eq);
            const QString value = kv.mid(eq + 1);
            if (value.isEmpty())
                values.remove(key);
            else if (value == QLatin1String("true") || value == QLatin1String("false"))
                values.insert(key, value == QLatin1String("true"));
            else
                values.insert(key, value);
        }
        app.settings = AppSettings(values);
        return Error();
    }

    std::pair<App, Error> checkOne(const App &app)
    {
        using R = std::pair<App, Error>;
        return await<R>([&](std::function<void(const R &)> done) {
            checker().check(app, [done](const App &a, const Error &e) { done(R(a, e)); });
        });
    }

    int add(const Args &args)
    {
        if (args.positional.size() != 1)
            return fail(QStringLiteral("add takes exactly one URL"));
        const auto match = m_registry.match(args.positional.first(), args.value("--source"));
        if (!match.ok())
            return fail(match.error.message);
        for (const App &existing : m_store.loadAll())
            if (existing.url.compare(match.value.standardUrl, Qt::CaseInsensitive) == 0)
                return fail(QStringLiteral("Already tracked as %1").arg(existing.id));

        App app = App::fromUrl(match.value.standardUrl, args.value("--source"));
        const Error bad = applySettings(app, args.values("--set"));
        if (!bad.ok())
            return fail(bad.message);

        const auto checked = checkOne(app);
        if (!checked.second.ok() && !args.has("--force"))
            return fail(checked.second.message + QStringLiteral("\n(use --force to track it anyway)"));
        app = checked.first;

        const Error saved = m_store.save(app);
        if (!saved.ok())
            return fail(saved.message);
        out() << "Tracking " << app.name << " from " << match.value.source->displayName() << "\n";
        printStatusLine(app);
        for (const Asset &a : app.latestAssets)
            out() << "    package: " << a.name << "\n";
        return 0;
    }

    int list()
    {
        QStringList errors;
        const QList<App> apps = m_store.loadAll(&errors);
        for (const QString &e : errors)
            err() << "Warning: " << e << "\n";
        if (apps.isEmpty())
            out() << "No apps tracked. Add one with: harpoon-cli add <url>\n";
        for (const App &app : apps)
            printStatusLine(app);
        return 0;
    }

    int show(const Args &args)
    {
        if (args.positional.size() != 1)
            return fail(QStringLiteral("show takes one app"));
        const auto app = findApp(args.positional.first());
        if (!app.ok())
            return fail(app.error.message);
        const App &a = app.value;
        printStatusLine(a);
        out() << "    url: " << a.url << (a.sourceId.isEmpty() ? QString() : QStringLiteral(" (") + a.sourceId + QLatin1Char(')'))
              << "\n    author: " << a.author
              << "\n    last check: " << (a.lastCheck.isValid() ? a.lastCheck.toString(Qt::ISODate) : QStringLiteral("never"));
        if (!a.latestTag.isEmpty())
            out() << "\n    release: " << a.latestTag << (a.latestPrerelease ? " [prerelease]" : "") << "  "
                  << a.latestDate.toString(Qt::ISODate) << "\n    page: " << a.releasePageUrl;
        for (const Asset &asset : a.latestAssets)
            out() << "\n    package: " << asset.name << " (" << asset.size << " bytes)";
        if (a.receipt.isValid())
            out() << "\n    installed by Harpoon: " << a.receipt.evr << " from " << a.receipt.tag << " at "
                  << a.receipt.installedAt.toString(Qt::ISODate);
        if (!a.settings.values().isEmpty()) {
            out() << "\n    settings:";
            for (auto it = a.settings.values().constBegin(); it != a.settings.values().constEnd(); ++it)
                out() << "\n      " << it.key() << " = " << it.value().toString();
        }
        if (!a.changelog.trimmed().isEmpty())
            out() << "\n\n" << a.changelog.trimmed();
        out() << "\n";
        return 0;
    }

    int check(const Args &args)
    {
        QList<App> apps;
        if (args.positional.isEmpty()) {
            apps = m_store.loadAll();
        } else {
            for (const QString &key : args.positional) {
                const auto a = findApp(key);
                if (!a.ok())
                    return fail(a.error.message);
                apps << a.value;
            }
        }
        const bool quiet = args.has("--quiet");
        int failures = 0;
        QList<App> checked;
        await<bool>([&](std::function<void(const bool &)> done) {
            checker().checkAll(
                apps,
                [&](const App &updated, const Error &e) {
                    if (!e.ok())
                        ++failures;
                    const Error saved = m_store.save(updated);
                    if (!saved.ok())
                        err() << "Warning: " << saved.message << "\n";
                    checked << updated;
                    if (!quiet || !e.ok())
                        printStatusLine(updated);
                },
                [done]() { done(true); });
        });
        if (args.has("--notify") && m_settings.notifyUpdates())
            notifyUpdates();
        return failures == 0 ? 0 : 3;
    }

    // Notifies about releases not announced before, across all apps.
    void notifyUpdates()
    {
        QList<AppWithStatus> all;
        for (const App &app : m_store.loadAll())
            all << AppWithStatus{app, updateStatusFor(app, installedOf(app))};
        const UpdateNotificationPlan plan = planUpdateNotification(all);
        if (!plan.shouldNotify)
            return;
        Notifier notifier;
        const auto sent = notifier.notify(plan.request);
        if (!sent.ok()) {
            err() << "Warning: " << sent.error.message << "\n";
            return;
        }
        for (const App &app : plan.appsToMark)
            m_store.save(app);
    }

    int exportBackup(const Args &args)
    {
        if (args.positional.size() != 1)
            return fail(QStringLiteral("export takes a file name"));
        Backup backup;
        backup.apps = m_store.loadAll();
        backup.settings = m_settings.exportable(args.has("--include-tokens"));
        QFile file(args.positional.first());
        if (!file.open(QIODevice::WriteOnly) || file.write(backup.toJson()) < 0)
            return fail(QStringLiteral("Cannot write %1: %2").arg(file.fileName(), file.errorString()));
        if (args.has("--include-tokens"))
            file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        out() << "Exported " << backup.apps.size() << " app(s) to " << file.fileName() << "\n";
        return 0;
    }

    int importBackup(const Args &args)
    {
        if (args.positional.size() != 1)
            return fail(QStringLiteral("import takes a file name"));
        QFile file(args.positional.first());
        if (!file.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("Cannot read %1: %2").arg(file.fileName(), file.errorString()));
        const auto backup = Backup::fromJson(file.readAll());
        if (!backup.ok())
            return fail(backup.error.message);
        const ImportResult merged = mergeBackupApps(m_store.loadAll(), backup.value.apps);
        for (const App &app : merged.added) {
            const Error saved = m_store.save(app);
            if (!saved.ok())
                return fail(saved.message);
        }
        if (args.has("--with-settings"))
            m_settings.restore(backup.value.settings);
        out() << "Imported " << merged.added.size() << " app(s)";
        if (!merged.skipped.isEmpty())
            out() << "; already tracked: " << merged.skipped.join(QStringLiteral(", "));
        out() << "\n";
        return 0;
    }

    int background(const Args &args)
    {
        if (args.positional.size() != 1 || (args.positional.first() != QLatin1String("on")
                                            && args.positional.first() != QLatin1String("off")))
            return fail(QStringLiteral("background takes on or off"));
        const bool enabled = args.positional.first() == QLatin1String("on");
        if (args.has("--hours"))
            m_settings.setCheckIntervalHours(args.value("--hours").toInt());
        m_settings.setBackgroundChecks(enabled);
        BackgroundScheduler scheduler;
        const Error e = await<Error>([&](std::function<void(const Error &)> done) {
            scheduler.apply(enabled, m_settings.checkIntervalHours(), done);
        });
        if (!e.ok())
            return fail(e.message);
        out() << "Background checks " << (enabled ? "on" : "off");
        if (enabled)
            out() << ", every " << m_settings.checkIntervalHours() << " h";
        out() << "\n";
        return 0;
    }

    PackageBackend *backend(const QString &name)
    {
        if (name.isEmpty() || name == QLatin1String("packagekit")) {
            if (!m_packageKit)
                m_packageKit.reset(new PackageKitBackend());
            return m_packageKit.get();
        }
        if (name == QLatin1String("handler")) {
            if (!m_handler)
                m_handler.reset(new InstallHandlerBackend());
            return m_handler.get();
        }
        return nullptr;
    }

    QString downloadDir() const
    {
        const QByteArray env = qgetenv("HARPOON_CACHE_DIR");
        return (!env.isEmpty() ? QString::fromLocal8Bit(env)
                               : QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
               + QStringLiteral("/downloads");
    }

    int installApp(const App &app, const InstallOptions &options, PackageBackend &b)
    {
        AppInstaller installer(m_downloader, m_inspector, b, downloadDir(), m_device);
        installer.setDownloadPreparer(checker().downloadPreparer(app));
        QString lastStage;
        const auto result = await<Result<InstallResult>>([&](std::function<void(const Result<InstallResult> &)> done) {
            installer.install(
                app, options,
                [&](const QString &stage, qint64 doneBytes, qint64 total) {
                    if (stage != lastStage) {
                        out() << stage << "\n";
                        out().flush();
                        lastStage = stage;
                    }
                    Q_UNUSED(doneBytes)
                    Q_UNUSED(total)
                },
                done);
        });
        if (!result.ok())
            return fail(result.error.message);
        for (const QString &w : result.value.warnings)
            err() << "Warning: " << w << "\n";
        const Error saved = m_store.replace(app.id, result.value.app);
        if (!saved.ok())
            err() << "Warning: " << saved.message << "\n";
        out() << "Installed " << result.value.installed.nevra() << "\n";
        return 0;
    }

    static InstallOptions installOptions(const Args &args)
    {
        InstallOptions o;
        o.allowReinstall = args.has("--reinstall");
        o.allowDowngrade = args.has("--downgrade");
        return o;
    }

    int install(const Args &args)
    {
        if (args.positional.size() != 1)
            return fail(QStringLiteral("install takes one app"));
        const auto app = findApp(args.positional.first());
        if (!app.ok())
            return fail(app.error.message);
        PackageBackend *b = backend(args.value("--backend"));
        if (!b)
            return fail(QStringLiteral("Unknown backend; use packagekit or handler"));
        return installApp(app.value, installOptions(args), *b);
    }

    int upgrade(const Args &args)
    {
        PackageBackend *b = backend(args.value("--backend"));
        if (!b)
            return fail(QStringLiteral("Unknown backend; use packagekit or handler"));
        const int checkRc = check(Args());
        int rc = checkRc;
        int upgraded = 0;
        for (const App &app : m_store.loadAll()) {
            if (app.settings.getBool(Keys::trackOnly))
                continue;
            if (updateStatusFor(app, installedOf(app)).state != UpdateState::UpdateAvailable)
                continue;
            ++upgraded;
            if (installApp(app, installOptions(args), *b) != 0)
                rc = 2;
        }
        if (upgraded == 0)
            out() << "Nothing to upgrade.\n";
        return rc;
    }

    int set(const Args &args)
    {
        if (args.positional.size() < 2)
            return fail(QStringLiteral("set takes an app and key=value pairs"));
        auto app = findApp(args.positional.first());
        if (!app.ok())
            return fail(app.error.message);
        const Error bad = applySettings(app.value, args.positional.mid(1));
        if (!bad.ok())
            return fail(bad.message);
        const Error saved = m_store.save(app.value);
        return saved.ok() ? 0 : fail(saved.message);
    }

    int ack(const Args &args)
    {
        if (args.positional.size() != 1)
            return fail(QStringLiteral("ack takes one app"));
        auto app = findApp(args.positional.first());
        if (!app.ok())
            return fail(app.error.message);
        app.value.acknowledgedVersion = app.value.latestVersion;
        const Error saved = m_store.save(app.value);
        return saved.ok() ? 0 : fail(saved.message);
    }

    int remove(const Args &args)
    {
        if (args.positional.size() != 1)
            return fail(QStringLiteral("remove takes one app"));
        const auto app = findApp(args.positional.first());
        if (!app.ok())
            return fail(app.error.message);
        if (args.has("--uninstall")) {
            PackageBackend *b = backend(args.value("--backend"));
            if (!b)
                return fail(QStringLiteral("Unknown backend; use packagekit or handler"));
            AppInstaller installer(m_downloader, m_inspector, *b, downloadDir(), m_device);
            const Error e = await<Error>([&](std::function<void(const Error &)> done) {
                installer.uninstall(app.value, done);
            });
            if (!e.ok())
                return fail(e.message);
            out() << "Uninstalled " << app.value.id << "\n";
        }
        const Error removed = m_store.remove(app.value.id);
        if (!removed.ok())
            return fail(removed.message);
        out() << "No longer tracking " << app.value.name << "\n";
        return 0;
    }

    SourceRegistry m_registry;
    HarpoonSettings m_settings;
    AppStore m_store;
    DeviceInfo m_device;
    SystemProcessRunner m_runner;
    RpmInspector m_inspector;
    NetworkTransport m_transport;
    Downloader m_downloader;
    std::unique_ptr<AppChecker> m_checker;
    std::unique_ptr<PackageKitBackend> m_packageKit;
    std::unique_ptr<InstallHandlerBackend> m_handler;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // Shared with the GUI: both use the same data and cache folders.
    QCoreApplication::setOrganizationName(organizationName());
    QCoreApplication::setApplicationName(applicationName());

    Cli cli;
    return cli.run(app.arguments().mid(1));
}
