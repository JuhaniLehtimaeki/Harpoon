// harpoon-cli: command-line front end to the Harpoon core.
//
// Installing needs the "privileged" group on SailfishOS, e.g.:
//   sg privileged -c 'harpoon-cli install <app>'
// Everything else runs as the normal user.

#include "app/addlink.h"
#include "app/appchecker.h"
#include "app/appinstaller.h"
#include "app/appstore.h"
#include "app/appservice.h"
#include "app/autoupdate.h"
#include "app/backgroundscheduler.h"
#include "app/backup.h"
#include "app/installedmatch.h"
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
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>
#include <sys/stat.h>
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
    "                         <url> may also be a harpoon://add link.\n"
    "  link <url> [--source ID] [--package NAME]\n"
    "                         Print the harpoon://add link for a repository, to publish as a QR\n"
    "                         code (e.g. qrencode -o harpoon-qr.png \"$(harpoon-cli link URL)\").\n"
    "  list                   Show tracked apps and their update state.\n"
    "  show <app>             Show details and the latest changelog.\n"
    "  check [app]... [--notify] [--quiet] [--auto-update]\n"
    "                         Check for updates (all apps when none given). --notify posts a\n"
    "                         notification for releases not announced before. --auto-update\n"
    "                         installs updates when automatic updates are on (set auto-update).\n"
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
    "  auto-update on|off     Let background checks install updates of apps Harpoon installed\n"
    "                         (PackageKit only). Exclude an app with: set <app> excludeFromAutoUpdate=true\n"
    "  token <source>[@host] [value]\n"
    "                         Store an API token (empty value removes it), e.g. token GitHub ghp_...\n"
    "                         or token Forgejo@git.example.org abc for a self-hosted server.\n"
    "\n"
    "<app> is an app id (RPM name) or a unique name.\n"
    "\n"
    "Tokens come from the app's settings; HARPOON_TOKEN_<SOURCE> (e.g. HARPOON_TOKEN_GITHUB) overrides\n"
    "the token for a source's default host.\n"
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
                                                  QStringLiteral("--backend"), QStringLiteral("--hours"),
                                                  QStringLiteral("--package")});
        int rc;
        if (command == QLatin1String("add"))
            rc = add(args);
        else if (command == QLatin1String("link"))
            rc = link(args);
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
        else if (command == QLatin1String("auto-update"))
            rc = setAutoUpdate(args);
        else if (command == QLatin1String("token"))
            rc = token(args);
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
            m_checker->setSourceConfigs(sourceConfigs(m_settings, m_registry.ids(), true));
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

    // A temporary id becomes the installed package's name when the app's
    // RPM is already on the phone (see adoptInstalledPackage).
    bool adoptInstalled(App &app)
    {
        QStringList taken;
        for (const App &other : m_store.loadAll())
            if (other.id != app.id)
                taken << other.id;
        return adoptInstalledPackage(
            app,
            [this](const QString &name) {
                const auto installed = m_inspector.installedPackage(name);
                return installed.ok() && !installed.value.name.isEmpty();
            },
            taken);
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
              << "  [" << (s.state == UpdateState::NotChecked && !app.lastError.isEmpty()
                               ? QStringLiteral("check failed") : updateStateName(s.state))
              << "]";
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
        const auto link = parseAddLink(args.positional.first());
        if (!link.ok())
            return fail(link.error.message);
        const QString sourceId = args.has("--source") ? args.value("--source") : link.value.sourceId;
        const auto match = m_registry.match(link.value.url, sourceId);
        if (!match.ok())
            return fail(match.error.message);
        for (const App &existing : m_store.loadAll())
            if (existing.url.compare(match.value.standardUrl, Qt::CaseInsensitive) == 0)
                return fail(QStringLiteral("Already tracked as %1").arg(existing.id));

        App app = App::fromUrl(match.value.standardUrl, sourceId);
        if (!link.value.packageName.isEmpty())
            app.settings.set(Keys::packageName, link.value.packageName);
        const Error bad = applySettings(app, args.values("--set"));
        if (!bad.ok())
            return fail(bad.message);

        const auto checked = checkOne(app);
        if (!checked.second.ok() && !args.has("--force"))
            return fail(checked.second.message + QStringLiteral("\n(use --force to track it anyway)"));
        app = checked.first;
        adoptInstalled(app);

        const Error saved = m_store.save(app);
        if (!saved.ok())
            return fail(saved.message);
        out() << "Tracking " << app.name << " from " << match.value.source->displayName() << "\n";
        printStatusLine(app);
        for (const Asset &a : app.latestAssets)
            out() << "    package: " << a.name << "\n";
        return 0;
    }

    int link(const Args &args)
    {
        if (args.positional.size() != 1)
            return fail(QStringLiteral("link takes exactly one URL"));
        AddLink link;
        link.url = args.positional.first().trimmed();
        link.sourceId = args.value("--source");
        link.packageName = args.value("--package");
        // Only hand out links Harpoon can read back and follow.
        const auto parsed = parseAddLink(link.toString());
        if (!parsed.ok())
            return fail(parsed.error.message);
        const auto match = m_registry.match(link.url, link.sourceId);
        if (!match.ok())
            return fail(match.error.message);
        if (match.value.source->id() == QLatin1String("RpmMdRepo") && link.packageName.isEmpty())
            return fail(QStringLiteral("An RPM repository link needs --package NAME"));
        link.url = match.value.standardUrl;
        out() << link.toString() << "\n";
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
              << "\n    last check: " << (a.lastCheck.isValid() ? a.lastCheck.toLocalTime().toString(Qt::ISODate) : QStringLiteral("never"));
        if (!a.latestTag.isEmpty())
            out() << "\n    release: " << a.latestTag << (a.latestPrerelease ? " [prerelease]" : "") << "  "
                  << a.latestDate.toLocalTime().toString(Qt::ISODate) << "\n    page: " << a.releasePageUrl;
        for (const Asset &asset : a.latestAssets)
            out() << "\n    package: " << asset.name << " (" << asset.size << " bytes)";
        if (a.receipt.isValid())
            out() << "\n    installed by Harpoon: " << a.receipt.evr << " from " << a.receipt.tag << " at "
                  << a.receipt.installedAt.toLocalTime().toString(Qt::ISODate);
        if (a.receipt.isValid() && !a.receipt.verification.isEmpty())
            out() << "\n    build provenance: " << a.receipt.verification;
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
                [&](const App &result, const Error &e) {
                    if (!e.ok())
                        ++failures;
                    // The app may have changed on disk while the check ran
                    // (the app installed or renamed it): merge into the
                    // current record, and leave records that are gone alone.
                    const auto current = m_store.load(result.id);
                    if (!current.ok())
                        return;
                    App updated = applyCheckResult(current.value, result);
                    const QString oldId = updated.id;
                    const Error saved = adoptInstalled(updated) ? m_store.replace(oldId, updated)
                                                                : m_store.save(updated);
                    if (!saved.ok())
                        err() << "Warning: " << saved.message << "\n";
                    checked << updated;
                    if (!quiet || !e.ok())
                        printStatusLine(updated);
                },
                [done]() { done(true); });
        });
        if (args.has("--auto-update") && !autoUpdate())
            ++failures;
        if (args.has("--notify") && m_settings.notifyUpdates())
            notifyUpdates();
        return failures == 0 ? 0 : 3;
    }

    // Installs the updates a background run may install by itself (see
    // autoUpdateEligible) and posts one notification about them. Returns
    // false when an install failed.
    bool autoUpdate()
    {
        if (!m_settings.autoUpdate())
            return true;
        if (m_settings.installBackend() != QLatin1String("packagekit")) {
            err() << "Automatic updates need the PackageKit install method\n";
            return false;
        }
        PackageBackend *b = backend(QStringLiteral("packagekit"));
        QStringList updated;
        QStringList failed;
        bool notAuthorized = false;
        for (const App &app : m_store.loadAll()) {
            QString why;
            if (!autoUpdateEligible(app, updateStatusFor(app, installedOf(app)), &why))
                continue;
            if (notAuthorized) {
                failed << app.name;
                continue;
            }
            Error error;
            if (installApp(app, InstallOptions(), *b, &error) == 0) {
                updated << app.name;
            } else {
                failed << app.name;
                // Without the privileged group nothing else will work either.
                notAuthorized = error.kind == Error::NotAuthorized;
            }
        }
        if (updated.isEmpty() && failed.isEmpty())
            return true;
        Notifier notifier;
        const auto sent = notifier.notify(autoUpdateNotification(updated, failed));
        if (!sent.ok())
            err() << "Warning: " << sent.error.message << "\n";
        return failed.isEmpty();
    }

    int setAutoUpdate(const Args &args)
    {
        if (args.positional.size() != 1 || (args.positional.first() != QLatin1String("on")
                                            && args.positional.first() != QLatin1String("off")))
            return fail(QStringLiteral("auto-update takes on or off"));
        m_settings.setAutoUpdate(args.positional.first() == QLatin1String("on"));
        out() << "Automatic updates " << (m_settings.autoUpdate() ? "on" : "off") << "\n";
        if (m_settings.autoUpdate() && !m_settings.backgroundChecks())
            out() << "Background checks are off; turn them on with: harpoon-cli background on\n";
        return 0;
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
        for (const App &app : plan.appsToMark) {
            // Only the announcement is ours to record.
            auto current = m_store.load(app.id);
            if (!current.ok())
                continue;
            current.value.notifiedVersion = app.notifiedVersion;
            m_store.save(current.value);
        }
    }

    int exportBackup(const Args &args)
    {
        if (args.positional.size() != 1)
            return fail(QStringLiteral("export takes a file name"));
        const bool secrets = args.has("--include-tokens");
        const Backup backup = Backup::create(m_store.loadAll(), m_settings.exportable(secrets), secrets);
        const Error written = backup.writeTo(args.positional.first(), secrets);
        if (!written.ok())
            return fail(written.message);
        const QString fileName = args.positional.first();
        out() << "Exported " << backup.apps.size() << " app(s) to " << fileName << "\n";
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
        out() << "Imported " << merged.added.size() << " app(s); run harpoon-cli check to fetch their releases";
        if (!merged.skipped.isEmpty())
            out() << "; already tracked: " << merged.skipped.join(QStringLiteral(", "));
        out() << "\n";
        return 0;
    }

    int token(const Args &args)
    {
        if (args.positional.isEmpty() || args.positional.size() > 2)
            return fail(QStringLiteral("token takes a source key and an optional value"));
        m_settings.setToken(args.positional.first(), args.positional.value(1));
        out() << (args.positional.size() == 2 ? "Token stored for " : "Token removed for ") << args.positional.first()
              << "\n";
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

    QString downloadDir() const { return defaultDownloadDirectory(); }

    int installApp(const App &app, const InstallOptions &options, PackageBackend &b, Error *error = nullptr)
    {
        AppInstaller installer(m_downloader, m_inspector, b, downloadDir(), m_device);
        installer.setDownloadPreparer(checker().downloadPreparer(app));
        installer.setVerifier(checker().verifier(app));
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
        if (!result.ok()) {
            if (error)
                *error = result.error;
            QString message = app.name + QStringLiteral(": ") + result.error.message;
            if (result.error.kind == Error::NotAuthorized && b.isSilent())
                message += QStringLiteral("\n(PackageKit needs the privileged group: sg privileged -c 'harpoon-cli …')");
            return fail(message);
        }
        for (const QString &w : result.value.warnings)
            err() << "Warning: " << w << "\n";
        const QString newId = result.value.app.id;
        const bool idTaken = newId != app.id && m_store.contains(newId);
        // The record as it is now: the app may have changed it meanwhile.
        const auto current = m_store.load(app.id);
        const App record = recordAfterInstall(current.ok() ? current.value : app, result.value, idTaken);
        if (idTaken)
            err() << "Warning: this package is also tracked as " << newId << "; remove one of the two\n";
        const Error saved = idTaken ? m_store.save(record) : m_store.replace(app.id, record);
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
        Error saved = m_store.save(app.value);
        if (!saved.ok())
            return fail(saved.message);
        // Settings decide which release and package are chosen: check again
        // so a following install does not use the old choice.
        const auto checked = checkOne(app.value);
        saved = m_store.save(checked.first);
        if (!saved.ok())
            return fail(saved.message);
        printStatusLine(checked.first);
        return 0;
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
    // Started through invoker (harpoon-autoupdate) the umask is 0000; never
    // write world-writable files.
    umask(022);
#ifdef HARPOON_AUTOUPDATE_ONLY
    // harpoon-autoupdate runs with the privileged group (privileges.d) so
    // PackageKit installs without asking. It does exactly one thing, and
    // takes neither arguments nor the HARPOON_* overrides of harpoon-cli.
    for (const QString &entry : QProcess::systemEnvironment()) {
        const QByteArray key = entry.section(QLatin1Char('='), 0, 0).toLocal8Bit();
        if (key.startsWith("HARPOON_"))
            qunsetenv(key.constData());
    }
    Q_UNUSED(argc)
    int fixedArgc = 1;
    QCoreApplication app(fixedArgc, argv);
    QCoreApplication::setOrganizationName(organizationName());
    QCoreApplication::setApplicationName(applicationName());
    Cli cli;
    // An app whose check or update failed is reported in the notification
    // and the log; it is not a failure of the background job (systemd would
    // mark every such run failed).
    cli.run({QStringLiteral("check"), QStringLiteral("--notify"), QStringLiteral("--quiet"),
             QStringLiteral("--auto-update")});
    return 0;
#else
    QCoreApplication app(argc, argv);
    // Shared with the GUI: both use the same data and cache folders.
    QCoreApplication::setOrganizationName(organizationName());
    QCoreApplication::setApplicationName(applicationName());

    Cli cli;
    return cli.run(app.arguments().mid(1));
#endif
}
