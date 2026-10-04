#include "faketransport.h"
#include "fakerpmdb.h"
#include "minihttpserver.h"
#include "rpmfactory.h"

#include "harpooncontroller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace Harpoon;

namespace {

class RecordingBackend : public PackageBackend
{
public:
    explicit RecordingBackend(FakeRpmDb &db) : m_db(db), m_inspector(db) {}
    QString name() const override { return QStringLiteral("recording"); }
    bool isSilent() const override { return true; }
    void installFiles(const QStringList &paths, const InstallOptions &, Done done) override
    {
        installs << paths;
        for (const QString &p : paths) {
            const auto info = m_inspector.inspectFile(p);
            if (info.ok())
                m_db.installed.insert(info.value.name, info.value);
        }
        done(Error());
    }
    void removePackages(const QStringList &names, Done done) override
    {
        removals << names;
        for (const QString &name : names)
            m_db.installed.remove(name);
        done(Error());
    }
    QList<QStringList> installs;
    QStringList removals;

private:
    FakeRpmDb &m_db;
    RpmInspector m_inspector;
};

// Answers like FakeTransport, but later from the event loop, as the real
// network does; lets tests act while a check is in flight.
class DelayedTransport : public HttpTransport
{
public:
    explicit DelayedTransport(HttpTransport &inner) : m_inner(inner) {}
    void get(const HttpRequest &request, Callback done) override
    {
        ++pending;
        m_inner.get(request, [this, done](const HttpResponse &r) {
            QTimer::singleShot(50, [this, done, r]() {
                --pending;
                done(r);
            });
        });
    }
    int pending = 0;

private:
    HttpTransport &m_inner;
};

QByteArray readFile(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

} // namespace

class TestController : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakeTransport> m_transport;
    std::unique_ptr<FakeRpmDb> m_db;
    std::unique_ptr<RecordingBackend> m_backend;
    std::unique_ptr<HarpoonSettings> m_settings;
    std::unique_ptr<BackgroundScheduler> m_scheduler;
    std::unique_ptr<HarpoonController> m_controller;

    void makeController()
    {
        ControllerEnvironment env;
        env.dataDir = m_dir->filePath(QStringLiteral("data/apps"));
        env.cacheDir = m_dir->filePath(QStringLiteral("cache"));
        env.device.arch = QStringLiteral("aarch64");
        env.device.osVersion = QStringLiteral("5.0.0.62");
        env.transport = m_transport.get();
        env.runner = m_db.get();
        env.backend = m_backend.get();
        env.settings = m_settings.get();
        env.scheduler = m_scheduler.get();
        env.backupDir = m_dir->filePath(QStringLiteral("documents"));
        env.applicationsDir = m_dir->filePath(QStringLiteral("applications"));
        env.iconsDir = m_dir->filePath(QStringLiteral("icons"));
        m_controller.reset(new HarpoonController(env));
        m_controller->reload();
    }

    bool addApp(const QString &url, const QVariantMap &settings = QVariantMap(), QString *idOrError = nullptr)
    {
        QSignalSpy spy(m_controller.get(), &HarpoonController::addFinished);
        m_controller->addApp(url, QString(), settings);
        if (spy.isEmpty() && !spy.wait(5000))
            return false;
        if (idOrError)
            *idOrError = spy.first().at(1).toString();
        return spy.first().at(0).toBool();
    }

private slots:
    void init()
    {
        m_dir.reset(new QTemporaryDir);
        m_transport.reset(new FakeTransport);
        m_db.reset(new FakeRpmDb);
        m_backend.reset(new RecordingBackend(*m_db));
        m_settings.reset(new HarpoonSettings(m_dir->filePath(QStringLiteral("config/harpoon.conf"))));
        // No bus: systemd calls fail fast and nothing touches ~/.config.
        m_scheduler.reset(new BackgroundScheduler(QDBusConnection(QStringLiteral("none")),
                                                  m_dir->filePath(QStringLiteral("xdg-config"))));
        m_transport->respondFixture(
            QStringLiteral("https://api.github.com/repos/sailfishos-chum/sailfishos-chum-gui/releases?per_page=100"),
            QStringLiteral("github/chum-gui-releases.json"));
        makeController();
    }

    void settingsDefaultsAndPrivacy()
    {
        QCOMPARE(m_settings->installBackend(), QStringLiteral("packagekit"));
        QVERIFY(m_settings->backgroundChecks());
        QCOMPARE(m_settings->checkIntervalHours(), 6);
        m_settings->setInstallBackend(QStringLiteral("bogus"));
        QCOMPARE(m_settings->installBackend(), QStringLiteral("packagekit"));
        m_settings->setInstallBackend(QStringLiteral("handler"));
        m_settings->setCheckIntervalHours(1000);
        QCOMPARE(m_settings->checkIntervalHours(), 168);
        m_settings->setToken(QStringLiteral("GitHub"), QStringLiteral(" abc "));
        QCOMPARE(m_settings->token(QStringLiteral("GitHub")), QStringLiteral("abc"));

        HarpoonSettings reread(m_settings->filePath());
        QCOMPARE(reread.installBackend(), QStringLiteral("handler"));
        QCOMPARE(reread.token(QStringLiteral("GitHub")), QStringLiteral("abc"));
        const auto perms = QFileInfo(m_settings->filePath()).permissions();
        QVERIFY(!(perms & QFileDevice::ReadOther));
        QVERIFY(!(perms & QFileDevice::ReadGroup));
    }

    void addCheckAndList()
    {
        QString id;
        QVERIFY(addApp(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"), {}, &id));
        AppListModel *model = m_controller->apps();
        QCOMPARE(model->count(), 1);
        const QModelIndex idx = model->index(0);
        QCOMPARE(model->data(idx, AppListModel::IdRole).toString(), id);
        // Not installed: a name tidied from the repository's.
        QCOMPARE(model->data(idx, AppListModel::NameRole).toString(), QStringLiteral("Sailfishos Chum Gui"));
        QCOMPARE(model->data(idx, AppListModel::LatestVersionRole).toString(), QStringLiteral("0.6.12-1"));
        QCOMPARE(model->data(idx, AppListModel::StateRole).toInt(), int(AppListModel::NotInstalled));

        // Persisted, and reloads identically.
        makeController();
        QCOMPARE(m_controller->apps()->count(), 1);

        // Duplicates are refused before any network call.
        const QVariantMap dup = m_controller->inspectUrl(QStringLiteral("github.com/sailfishos-chum/sailfishos-chum-gui"));
        QVERIFY(!dup.value(QStringLiteral("ok")).toBool());
        QCOMPARE(dup.value(QStringLiteral("existingId")).toString(), id);
    }

    void addFailures()
    {
        QString error;
        QVERIFY(!addApp(QStringLiteral("https://example.com/x/y"), {}, &error));
        QVERIFY(error.contains(QLatin1String("example.com")));
        QVERIFY(!addApp(QStringLiteral("https://github.com/nobody/nothing"), {}, &error));
        QCOMPARE(error, QStringLiteral("Repository not found"));
        QCOMPARE(m_controller->apps()->count(), 0);
    }

    void installedStateAndUpdates()
    {
        RpmInfo installed;
        installed.name = QStringLiteral("sailfishos-chum-gui");
        installed.evr = parseEvr(QStringLiteral("0.6.11-1"));
        installed.arch = QStringLiteral("aarch64");
        m_db->installed.insert(installed.name, installed);

        QString id;
        QVERIFY(addApp(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"), {}, &id));
        // Already installed (from Chum, by hand...): the app takes the package's
        // name at once instead of waiting for Harpoon to install it.
        QCOMPARE(id, installed.name);
        QCOMPARE(m_controller->apps()->updatesCount(), 1);
        m_controller->reload();
        QCOMPARE(m_controller->apps()->updatesCount(), 1);
        const QVariantMap details = m_controller->appDetails(installed.name);
        QCOMPARE(details.value(QStringLiteral("installedVersion")).toString(), QStringLiteral("0.6.11-1"));
        QCOMPARE(details.value(QStringLiteral("state")).toInt(), int(AppListModel::UpdateAvailable));
        QVERIFY(details.value(QStringLiteral("changelog")).toString().contains(QLatin1String("Fix crash")));
        QCOMPARE(details.value(QStringLiteral("sourceName")).toString(), QStringLiteral("GitHub"));
    }

    void checkRecordsErrors()
    {
        QVERIFY(addApp(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui")));
        const QString id = m_controller->apps()->data(m_controller->apps()->index(0), AppListModel::IdRole).toString();
        HttpResponse limited;
        limited.status = 429;
        limited.headers.insert("retry-after", "60");
        m_transport->respond(
            QStringLiteral("https://api.github.com/repos/sailfishos-chum/sailfishos-chum-gui/releases?per_page=100"), limited);
        QSignalSpy spy(m_controller.get(), &HarpoonController::operationFinished);
        m_controller->check(id);
        QTRY_COMPARE(spy.count(), 1);
        QVERIFY(!spy.first().at(1).toBool());
        QVERIFY(spy.first().at(2).toString().isEmpty()); // shown in place, not as a banner
        QCOMPARE(m_controller->apps()->failedCount(), 1);
        const QModelIndex idx = m_controller->apps()->index(0);
        QVERIFY(m_controller->apps()->data(idx, AppListModel::LastErrorRole).toString().contains(QLatin1String("Rate limited")));
        // The previous result is kept.
        QCOMPARE(m_controller->apps()->data(idx, AppListModel::LatestVersionRole).toString(), QStringLiteral("0.6.12-1"));
        QVERIFY(!m_controller->checking());
    }

    void tokensReachTheSource()
    {
        m_settings->setToken(QStringLiteral("GitHub"), QStringLiteral("secret"));
        QVERIFY(addApp(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui")));
        QCOMPARE(m_transport->requests.last().header("Authorization"), QByteArray("Bearer secret"));
    }

    void settingsPerApp()
    {
        QVERIFY(addApp(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"),
                       {{QStringLiteral("includePrereleases"), true}, {QStringLiteral("assetFilterRegEx"), QString()}}));
        const QString id = m_controller->apps()->data(m_controller->apps()->index(0), AppListModel::IdRole).toString();
        QCOMPARE(m_controller->apps()->data(m_controller->apps()->index(0), AppListModel::LatestVersionRole).toString(),
                 QStringLiteral("0.7.0-0.rc1"));
        QVariantMap s = m_controller->appDetails(id).value(QStringLiteral("settings")).toMap();
        QVERIFY(s.value(QStringLiteral("includePrereleases")).toBool());
        QVERIFY(!s.contains(QStringLiteral("assetFilterRegEx"))); // empty values are not stored

        m_controller->setAppSetting(id, QStringLiteral("includePrereleases"), QVariant());
        m_controller->setAppSetting(id, QStringLiteral("trackOnly"), true);
        s = m_controller->appDetails(id).value(QStringLiteral("settings")).toMap();
        QVERIFY(!s.contains(QStringLiteral("includePrereleases")));
        QCOMPARE(m_controller->apps()->updatesCount(), 1); // track-only, not acknowledged
        m_controller->acknowledge(id);
        QCOMPARE(m_controller->apps()->updatesCount(), 0);

        m_controller->setAppName(id, QStringLiteral("  Chum  "));
        QCOMPARE(m_controller->appDetails(id).value(QStringLiteral("name")).toString(), QStringLiteral("Chum"));
    }

    void installUninstallRemove()
    {
        if (!RpmFactory::available())
            QSKIP("rpmbuild not installed");
        RpmFactory factory;
        const QString rpm = factory.build(QStringLiteral("harbour-tool"), QStringLiteral("1.1"), QStringLiteral("1"),
                                          QStringLiteral("aarch64"));
        QVERIFY(!rpm.isEmpty());
        MiniHttpServer server;
        const QByteArray bytes = readFile(rpm);
        server.routeBody(QStringLiteral("/dl/harbour-tool-1.1-1.aarch64.rpm"), bytes);
        QJsonObject asset{{QStringLiteral("name"), QStringLiteral("harbour-tool-1.1-1.aarch64.rpm")},
                          {QStringLiteral("browser_download_url"), server.url(QStringLiteral("/dl/harbour-tool-1.1-1.aarch64.rpm"))},
                          {QStringLiteral("size"), bytes.size()}};
        QJsonObject release{{QStringLiteral("tag_name"), QStringLiteral("v1.1")},
                            {QStringLiteral("published_at"), QStringLiteral("2026-09-01T00:00:00Z")},
                            {QStringLiteral("assets"), QJsonArray{asset}}};
        m_transport->respondJson(QStringLiteral("https://api.github.com/repos/me/harbour-tool/releases?per_page=100"),
                                 QJsonDocument(QJsonArray{release}).toJson());

        QString tmpId;
        QVERIFY(addApp(QStringLiteral("https://github.com/me/harbour-tool"), {}, &tmpId));
        QSignalSpy finished(m_controller.get(), &HarpoonController::operationFinished);
        QSignalSpy idChanged(m_controller.get(), &HarpoonController::appIdChanged);
        m_controller->install(tmpId);
        QVERIFY(m_controller->apps()->data(m_controller->apps()->index(0), AppListModel::BusyRole).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY2(finished.first().at(1).toBool(), qPrintable(finished.first().at(2).toString()));
        QCOMPARE(idChanged.count(), 1);
        QCOMPARE(idChanged.first().at(1).toString(), QStringLiteral("harbour-tool"));
        QCOMPARE(m_controller->apps()->count(), 1);
        const QModelIndex idx = m_controller->apps()->index(0);
        QCOMPARE(m_controller->apps()->data(idx, AppListModel::IdRole).toString(), QStringLiteral("harbour-tool"));
        QCOMPARE(m_controller->apps()->data(idx, AppListModel::StateRole).toInt(), int(AppListModel::UpToDate));
        QVERIFY(!m_controller->apps()->data(idx, AppListModel::BusyRole).toBool());

        m_controller->uninstall(QStringLiteral("harbour-tool"));
        QTRY_COMPARE(finished.count(), 2);
        QCOMPARE(m_backend->removals, QStringList{"harbour-tool"});
        QCOMPARE(m_controller->apps()->data(m_controller->apps()->index(0), AppListModel::StateRole).toInt(),
                 int(AppListModel::NotInstalled));

        m_controller->removeApp(QStringLiteral("harbour-tool"));
        QCOMPARE(m_controller->apps()->count(), 0);
        makeController();
        QCOMPARE(m_controller->apps()->count(), 0);
    }

    void backupExportImport()
    {
        m_settings->setToken(QStringLiteral("GitHub"), QStringLiteral("secret"));
        QVERIFY(addApp(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"),
                       {{QStringLiteral("includePrereleases"), true}}));
        const QVariantMap exported = m_controller->exportBackup(false);
        QVERIFY2(exported.value(QStringLiteral("ok")).toBool(), qPrintable(exported.value(QStringLiteral("error")).toString()));
        const QString path = exported.value(QStringLiteral("path")).toString();
        QVERIFY(path.startsWith(m_dir->filePath(QStringLiteral("documents"))));
        QVERIFY(!readFile(path).contains("secret")); // tokens only on request

        // Import into an empty installation.
        QTemporaryDir other;
        HarpoonSettings otherSettings(other.filePath(QStringLiteral("harpoon.conf")));
        ControllerEnvironment env;
        env.dataDir = other.filePath(QStringLiteral("apps"));
        env.cacheDir = other.filePath(QStringLiteral("cache"));
        env.device.arch = QStringLiteral("aarch64");
        env.transport = m_transport.get();
        env.runner = m_db.get();
        env.backend = m_backend.get();
        env.settings = &otherSettings;
        env.scheduler = m_scheduler.get();
        HarpoonController fresh(env);
        fresh.reload();
        QVariantMap imported = fresh.importBackup(QUrl::fromLocalFile(path).toString(), true);
        QVERIFY2(imported.value(QStringLiteral("ok")).toBool(), qPrintable(imported.value(QStringLiteral("error")).toString()));
        QCOMPARE(imported.value(QStringLiteral("added")).toInt(), 1);
        QCOMPARE(fresh.apps()->count(), 1);
        const QString id = fresh.apps()->data(fresh.apps()->index(0), AppListModel::IdRole).toString();
        QVERIFY(fresh.appDetails(id).value(QStringLiteral("settings")).toMap().value(QStringLiteral("includePrereleases")).toBool());
        QVERIFY(otherSettings.token(QStringLiteral("GitHub")).isEmpty());

        // A second import adds nothing.
        imported = fresh.importBackup(path, false);
        QCOMPARE(imported.value(QStringLiteral("added")).toInt(), 0);
        QCOMPARE(imported.value(QStringLiteral("skipped")).toStringList().size(), 1);

        // With tokens, and a broken file.
        const QVariantMap withTokens = m_controller->exportBackup(true);
        QVERIFY(readFile(withTokens.value(QStringLiteral("path")).toString()).contains("secret"));
        QVERIFY(!fresh.importBackup(m_dir->filePath(QStringLiteral("nothing.json")), false).value(QStringLiteral("ok")).toBool());
    }

    void tokenChangeDuringCheck()
    {
        // Every repository answers with the same releases.
        const QByteArray releases = readFile(QStringLiteral(HARPOON_FIXTURE_DIR "/github/chum-gui-releases.json"));
        m_transport->handler = [releases](const HttpRequest &req, HttpResponse *resp) {
            if (!req.url.endsWith(QLatin1String("/releases?per_page=100")))
                return false;
            resp->status = 200;
            resp->body = releases;
            return true;
        };
        AppStore store(m_dir->filePath(QStringLiteral("data/apps")));
        for (int i = 0; i < 6; ++i)
            QVERIFY(store.save(App::fromUrl(QStringLiteral("https://github.com/owner/app%1").arg(i))).ok());

        DelayedTransport delayed(*m_transport);
        ControllerEnvironment env;
        env.dataDir = store.directory();
        env.cacheDir = m_dir->filePath(QStringLiteral("cache"));
        env.device.arch = QStringLiteral("aarch64");
        env.transport = &delayed;
        env.runner = m_db.get();
        env.backend = m_backend.get();
        env.settings = m_settings.get();
        env.scheduler = m_scheduler.get();
        HarpoonController controller(env);
        controller.reload();
        // More checks than run at once, so some are still queued...
        controller.checkAll();
        QVERIFY(controller.checking());
        // ...when a token changes. Neither running nor queued checks may be lost.
        m_settings->setToken(QStringLiteral("GitHub"), QStringLiteral("new-token"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.checking(), 5000);
        for (int row = 0; row < controller.apps()->count(); ++row) {
            QVERIFY(!controller.apps()->data(controller.apps()->index(row), AppListModel::BusyRole).toBool());
            QCOMPARE(controller.apps()->data(controller.apps()->index(row), AppListModel::LatestVersionRole).toString(),
                     QStringLiteral("0.6.12-1"));
        }
        QCOMPARE(m_transport->requests.last().header("Authorization"), QByteArray("Bearer new-token"));
    }

    void editsDuringACheckAreKept()
    {
        QVERIFY(addApp(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui")));
        const QString id = m_controller->apps()->data(m_controller->apps()->index(0), AppListModel::IdRole).toString();
        DelayedTransport delayed(*m_transport);
        ControllerEnvironment env;
        env.dataDir = m_dir->filePath(QStringLiteral("data/apps"));
        env.cacheDir = m_dir->filePath(QStringLiteral("cache"));
        env.device.arch = QStringLiteral("aarch64");
        env.transport = &delayed;
        env.runner = m_db.get();
        env.backend = m_backend.get();
        env.settings = m_settings.get();
        env.scheduler = m_scheduler.get();
        HarpoonController controller(env);
        controller.reload();
        controller.check(id);
        controller.setAppSetting(id, QStringLiteral("assetFilterRegEx"), QStringLiteral("chum"));
        controller.setAppName(id, QStringLiteral("Renamed"));
        // An edit must not clear the busy state of a running operation.
        QVERIFY(controller.apps()->data(controller.apps()->index(0), AppListModel::BusyRole).toBool());
        QTRY_VERIFY(!controller.checking());
        const QVariantMap details = controller.appDetails(id);
        QCOMPARE(details.value(QStringLiteral("name")).toString(), QStringLiteral("Renamed"));
        QCOMPARE(details.value(QStringLiteral("settings")).toMap().value(QStringLiteral("assetFilterRegEx")).toString(),
                 QStringLiteral("chum"));
        QCOMPARE(details.value(QStringLiteral("latestVersion")).toString(), QStringLiteral("0.6.12-1"));
    }

    void importedAppsAreCheckedBeforeInstall()
    {
        QVERIFY(addApp(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui")));
        const QString path = m_controller->exportBackup(false).value(QStringLiteral("path")).toString();
        // Tamper with the backup: point the asset somewhere else.
        QByteArray json = readFile(path);
        json.replace("https://github.com/sailfishos-chum/sailfishos-chum-gui/releases/download/",
                     "https://evil.example/");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(json);
        f.close();

        QTemporaryDir other;
        HarpoonSettings otherSettings(other.filePath(QStringLiteral("harpoon.conf")));
        DelayedTransport delayed(*m_transport);
        ControllerEnvironment env;
        env.dataDir = other.filePath(QStringLiteral("apps"));
        env.cacheDir = other.filePath(QStringLiteral("cache"));
        env.device.arch = QStringLiteral("aarch64");
        env.transport = &delayed;
        env.runner = m_db.get();
        env.backend = m_backend.get();
        env.settings = &otherSettings;
        env.scheduler = m_scheduler.get();
        HarpoonController fresh(env);
        fresh.reload();
        QVERIFY(fresh.importBackup(path, false).value(QStringLiteral("ok")).toBool());
        const QString id = fresh.apps()->data(fresh.apps()->index(0), AppListModel::IdRole).toString();
        // Nothing from the file is installable...
        QVERIFY(fresh.appDetails(id).value(QStringLiteral("assets")).toList().isEmpty());
        // ...until the automatic check has fetched the real release.
        QTRY_VERIFY(!fresh.checking());
        const QVariantList assets = fresh.appDetails(id).value(QStringLiteral("assets")).toList();
        QCOMPARE(assets.size(), 1);
        QVERIFY(assets.first().toMap().value(QStringLiteral("url")).toString().startsWith(QLatin1String("https://github.com/")));
    }

    void installNeverOverwritesAnotherApp()
    {
        if (!RpmFactory::available())
            QSKIP("rpmbuild not installed");
        RpmFactory factory;
        const QString rpm = factory.build(QStringLiteral("harbour-tool"), QStringLiteral("1.1"), QStringLiteral("1"),
                                          QStringLiteral("aarch64"));
        MiniHttpServer server;
        const QByteArray bytes = readFile(rpm);
        server.routeBody(QStringLiteral("/dl/harbour-tool-1.1-1.aarch64.rpm"), bytes);
        QJsonObject asset{{QStringLiteral("name"), QStringLiteral("harbour-tool-1.1-1.aarch64.rpm")},
                          {QStringLiteral("browser_download_url"), server.url(QStringLiteral("/dl/harbour-tool-1.1-1.aarch64.rpm"))},
                          {QStringLiteral("size"), bytes.size()}};
        QJsonObject release{{QStringLiteral("tag_name"), QStringLiteral("v1.1")},
                            {QStringLiteral("published_at"), QStringLiteral("2026-09-01T00:00:00Z")},
                            {QStringLiteral("assets"), QJsonArray{asset}}};
        const QByteArray releases = QJsonDocument(QJsonArray{release}).toJson();
        m_transport->respondJson(QStringLiteral("https://api.github.com/repos/me/harbour-tool/releases?per_page=100"), releases);
        m_transport->respondJson(QStringLiteral("https://api.github.com/repos/fork/harbour-tool/releases?per_page=100"), releases);

        // The same package, tracked once under its name and once from a fork.
        AppStore store(m_dir->filePath(QStringLiteral("data/apps")));
        App original = App::fromUrl(QStringLiteral("https://github.com/me/harbour-tool"));
        original.id = QStringLiteral("harbour-tool");
        original.temporaryId = false;
        original.name = QStringLiteral("Original");
        QVERIFY(store.save(original).ok());
        m_controller->reload();
        QString forkId;
        QVERIFY(addApp(QStringLiteral("https://github.com/fork/harbour-tool"), {}, &forkId));

        QSignalSpy finished(m_controller.get(), &HarpoonController::operationFinished);
        m_controller->install(forkId);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(finished.first().at(1).toBool());
        QVERIFY(finished.first().at(2).toString().contains(QLatin1String("also tracked")));
        QCOMPARE(m_controller->apps()->count(), 2);
        QCOMPARE(m_controller->appDetails(QStringLiteral("harbour-tool")).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Original"));
        QVERIFY(m_controller->apps()->indexOf(forkId) >= 0);
    }

    void tokenTargetsFollowSelfHostedApps()
    {
        auto keys = [this]() {
            QStringList out;
            for (const QVariant &t : m_controller->tokenTargets())
                out << t.toMap().value(QStringLiteral("key")).toString();
            return out;
        };
        QCOMPARE(keys(), (QStringList{QStringLiteral("Forgejo"), QStringLiteral("GitHub"), QStringLiteral("GitLab")}));

        AppStore store(m_dir->filePath(QStringLiteral("data/apps")));
        App selfHosted = App::fromUrl(QStringLiteral("https://git.example.org/me/tool"), QStringLiteral("Forgejo"));
        QVERIFY(store.save(selfHosted).ok());
        QSignalSpy changed(m_controller.get(), &HarpoonController::tokenTargetsChanged);
        m_controller->reload();
        QVERIFY(changed.count() >= 1);
        QVERIFY(keys().contains(QStringLiteral("Forgejo@git.example.org")));
        QVERIFY(m_controller->tokenTargets().last().toMap().value(QStringLiteral("name")).toString()
                    .endsWith(QLatin1String("(git.example.org)")));

        // The field goes away with the last app on that server.
        m_controller->removeApp(selfHosted.id);
        QCOMPARE(keys().size(), 3);
    }

    void installedAppsUseTheirOwnNameAndIcon()
    {
        RpmInfo installed;
        installed.name = QStringLiteral("sailfishos-chum-gui");
        installed.evr = parseEvr(QStringLiteral("0.6.13-1"));
        installed.arch = QStringLiteral("aarch64");
        m_db->installed.insert(installed.name, installed);
        QDir().mkpath(m_dir->filePath(QStringLiteral("applications")));
        QFile desktop(m_dir->filePath(QStringLiteral("applications/sailfishos-chum-gui.desktop")));
        QVERIFY(desktop.open(QIODevice::WriteOnly));
        desktop.write("[Desktop Entry]\nName=Chum\nIcon=sailfishos-chum-gui\n");
        desktop.close();
        const QString icon = m_dir->filePath(QStringLiteral("icons/172x172/apps/sailfishos-chum-gui.png"));
        QDir().mkpath(QFileInfo(icon).absolutePath());
        QFile iconFile(icon);
        QVERIFY(iconFile.open(QIODevice::WriteOnly));
        iconFile.close();

        QString id;
        QVERIFY(addApp(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"), {}, &id));
        AppListModel *model = m_controller->apps();
        const QModelIndex idx = model->index(model->indexOf(id));
        QCOMPARE(model->data(idx, AppListModel::NameRole).toString(), QStringLiteral("Chum"));
        QCOMPARE(model->data(idx, AppListModel::IconRole).toString(), icon);
        QCOMPARE(m_controller->appDetails(id).value(QStringLiteral("customName")).toString(), QString());

        // A name the user picks wins; clearing it goes back to the automatic one.
        m_controller->setAppName(id, QStringLiteral("  Chum client "));
        QCOMPARE(model->data(model->index(model->indexOf(id)), AppListModel::NameRole).toString(),
                 QStringLiteral("Chum client"));
        QCOMPARE(m_controller->appDetails(id).value(QStringLiteral("customName")).toString(), QStringLiteral("Chum client"));
        m_controller->setAppName(id, QString());
        QCOMPARE(model->data(model->index(model->indexOf(id)), AppListModel::NameRole).toString(), QStringLiteral("Chum"));
    }

    void backgroundChangesAreTakenIn()
    {
        QString tmpId;
        QVERIFY(addApp(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"), {}, &tmpId));
        // The background job installs it and renames the record on disk.
        AppStore store(m_dir->filePath(QStringLiteral("data/apps")));
        App renamed = store.load(tmpId).value;
        renamed.id = QStringLiteral("sailfishos-chum-gui");
        renamed.temporaryId = false;
        renamed.receipt.evr = QStringLiteral("0.6.13-1");
        QVERIFY(store.replace(tmpId, renamed).ok());

        // Editing the stale copy must not recreate the old record...
        QSignalSpy finished(m_controller.get(), &HarpoonController::operationFinished);
        QSignalSpy idChanged(m_controller.get(), &HarpoonController::appIdChanged);
        m_controller->setAppSetting(tmpId, QStringLiteral("includePrereleases"), true);
        QVERIFY(!store.contains(tmpId));
        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(1).toBool());
        // ...and the app follows the record on disk.
        QCOMPARE(idChanged.count(), 1);
        QCOMPARE(idChanged.first().at(1).toString(), QStringLiteral("sailfishos-chum-gui"));
        QVERIFY(m_controller->apps()->indexOf(QStringLiteral("sailfishos-chum-gui")) >= 0);
        QCOMPARE(m_controller->apps()->count(), 1);

        // Removed in the background: gone after a refresh.
        QVERIFY(store.remove(QStringLiteral("sailfishos-chum-gui")).ok());
        m_controller->refresh();
        QCOMPARE(m_controller->apps()->count(), 0);
    }

    void reloadAsksRpmOnce()
    {
        AppStore store(m_dir->filePath(QStringLiteral("data/apps")));
        for (int i = 0; i < 5; ++i) {
            App app = App::fromUrl(QStringLiteral("https://github.com/me/app%1").arg(i));
            app.id = QStringLiteral("harbour-app%1").arg(i);
            app.temporaryId = false;
            QVERIFY(store.save(app).ok());
        }
        m_db->calls = 0;
        m_controller->reload();
        QCOMPARE(m_controller->apps()->count(), 5);
        QCOMPARE(m_db->calls, 1);
        QVERIFY(m_controller->loaded());
    }

    // A repository with nothing to install yet is tracked, not refused.
    void addingARepositoryWithoutBuilds()
    {
        m_transport->respondJson(QStringLiteral("https://api.github.com/repos/someone/harbour-soon/releases?per_page=100"),
                                 "[]");
        QSignalSpy finished(m_controller.get(), &HarpoonController::operationFinished);
        QString id;
        QVERIFY(addApp(QStringLiteral("https://github.com/someone/harbour-soon"), {}, &id));
        AppListModel *model = m_controller->apps();
        QCOMPARE(model->count(), 1);
        const AppListModel::Entry *e = model->entry(id);
        QVERIFY(e && e->app.waitingForBuilds);
        QVERIFY(e->app.lastError.contains(QLatin1String("no releases yet")));
        QCOMPARE(model->failedCount(), 0); // waiting is not a failure
        QVERIFY(m_controller->appDetails(id).value(QStringLiteral("waitingForBuilds")).toBool());
        QVERIFY(!finished.isEmpty());
        QVERIFY(finished.last().at(2).toString().contains(QLatin1String("No builds yet")));

        // Builds appear: the next check makes it a normal, installable app.
        m_transport->respondFixture(QStringLiteral("https://api.github.com/repos/someone/harbour-soon/releases?per_page=100"),
                                    QStringLiteral("github/chum-gui-releases.json"));
        m_controller->check(id);
        QTRY_VERIFY(!m_controller->checking());
        e = model->entry(id);
        QVERIFY(e && !e->app.waitingForBuilds);
        QVERIFY(e->app.lastError.isEmpty());
        QVERIFY(!e->app.latestVersion.isEmpty());
    }

    void harpoonTracksItselfOnce()
    {
        const QString self = QStringLiteral("https://github.com/JuhaniLehtimaeki/Harpoon");
        AppListModel *model = m_controller->apps();
        QSignalSpy added(m_controller.get(), &HarpoonController::addFinished);
        // Offline (the fake answers 404): it is added anyway.
        m_controller->trackSelfOnce();
        QTRY_COMPARE(added.count(), 1);
        QVERIFY(added.first().at(0).toBool());
        QCOMPARE(model->count(), 1);
        QCOMPARE(model->entries().first().app.url, self);

        // Stopped tracking: not added again.
        m_controller->removeApp(added.first().at(1).toString());
        m_controller->trackSelfOnce();
        QTest::qWait(50);
        QCOMPARE(added.count(), 1);
        QCOMPARE(model->count(), 0);

        // Added by hand before: not added twice.
        m_settings->setSelfAdded(false);
        m_controller->addApp(self, QString(), QVariantMap(), true);
        QTRY_COMPARE(model->count(), 1);
        const int before = added.count();
        m_controller->trackSelfOnce();
        QTest::qWait(50);
        QCOMPARE(added.count(), before);
        QCOMPARE(model->count(), 1);
    }

    void changingAnAppsAddress()
    {
        QString id;
        QVERIFY(addApp(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"),
                       {{QStringLiteral("includePrereleases"), true}}, &id));
        AppListModel *model = m_controller->apps();
        QVERIFY(!model->entry(id)->app.latestVersion.isEmpty());
        QSignalSpy finished(m_controller.get(), &HarpoonController::addressChangeFinished);

        // A wrong address (404) changes nothing.
        m_controller->setAppAddress(id, QStringLiteral("https://github.com/someone/typo"), QString());
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(1).toBool());
        QVERIFY(!finished.first().at(2).toString().isEmpty());
        QCOMPARE(model->entry(id)->app.url, QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"));
        QVERIFY(!model->entry(id)->busy);

        // Moved to Codeberg, where nothing is published yet.
        m_transport->respondJson(QStringLiteral("https://codeberg.org/api/v1/repos/someone/chum-gui/releases?per_page=100"),
                                 "[]");
        m_controller->setAppAddress(id, QStringLiteral("https://codeberg.org/someone/chum-gui"), QString());
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY2(finished.at(1).at(1).toBool(), qPrintable(finished.at(1).at(2).toString()));
        const QString newId = finished.at(1).at(0).toString();
        QCOMPARE(model->count(), 1);
        const App &moved = model->entry(newId)->app;
        QCOMPARE(moved.url, QStringLiteral("https://codeberg.org/someone/chum-gui"));
        QVERIFY(moved.settings.getBool("includePrereleases"));
        QVERIFY(moved.waitingForBuilds);
        QVERIFY(moved.latestVersion.isEmpty()); // nothing from the old address
        QVERIFY(!model->entry(newId)->busy);
    }

    void listSummary()
    {
        AppStore store(m_dir->filePath(QStringLiteral("data/apps")));
        const QDateTime older = QDateTime::currentDateTimeUtc().addSecs(-3600);
        const QDateTime newer = QDateTime::currentDateTimeUtc().addSecs(-60);
        for (int i = 0; i < 3; ++i) {
            App app = App::fromUrl(QStringLiteral("https://github.com/me/app%1").arg(i));
            app.id = QStringLiteral("harbour-app%1").arg(i);
            app.temporaryId = false;
            if (i == 1)
                app.lastCheck = older;
            if (i == 2)
                app.lastCheck = newer;
            QVERIFY(store.save(app).ok());
        }
        RpmInfo installed;
        installed.name = QStringLiteral("harbour-app0");
        installed.evr = parseEvr(QStringLiteral("1.0-1"));
        installed.arch = QStringLiteral("noarch");
        m_db->installed.insert(installed.name, installed);
        AppListModel *model = m_controller->apps();
        QSignalSpy summary(model, &AppListModel::summaryChanged);
        m_controller->reload();
        QVERIFY(summary.count() >= 1);
        QCOMPARE(model->property("installedCount").toInt(), 1);
        QCOMPARE(model->property("lastChecked").toDateTime().toSecsSinceEpoch(), newer.toSecsSinceEpoch());
    }

    void installedStateIsAskedWithoutBlocking()
    {
        AppStore store(m_dir->filePath(QStringLiteral("data/apps")));
        App app = App::fromUrl(QStringLiteral("https://github.com/me/tool"));
        app.id = QStringLiteral("harbour-tool");
        app.temporaryId = false;
        QVERIFY(store.save(app).ok());
        RpmInfo v1;
        v1.name = app.id;
        v1.evr = parseEvr(QStringLiteral("1.0-1"));
        v1.arch = QStringLiteral("noarch");
        m_db->installed.insert(app.id, v1);
        m_controller->reload();
        AppListModel *model = m_controller->apps();
        QCOMPARE(model->entry(app.id)->installed.evr.version, QStringLiteral("1.0"));

        // A record change shows at once with what was known; rpm answers later.
        m_db->deferAsync = true;
        RpmInfo v2 = v1;
        v2.evr = parseEvr(QStringLiteral("2.0-1"));
        m_db->installed.insert(app.id, v2);
        m_controller->setAppSetting(app.id, QStringLiteral("includePrereleases"), true);
        QCOMPARE(m_db->pending.size(), 1);
        QVERIFY(model->entry(app.id)->app.settings.getBool("includePrereleases"));
        QCOMPARE(model->entry(app.id)->installed.evr.version, QStringLiteral("1.0"));
        m_db->deliver();
        QCOMPARE(model->entry(app.id)->installed.evr.version, QStringLiteral("2.0"));

        // Answers arriving out of order: only the latest query counts.
        m_controller->setAppSetting(app.id, QStringLiteral("includePrereleases"), false); // asks: 2.0
        m_db->installed.remove(app.id);
        m_controller->setAppSetting(app.id, QStringLiteral("trackOnly"), true); // asks: not installed
        QCOMPARE(m_db->pending.size(), 2);
        m_db->deliver(1);
        QVERIFY(model->entry(app.id)->installed.name.isEmpty());
        m_db->deliver(0);
        QVERIFY(model->entry(app.id)->installed.name.isEmpty());
        m_db->deferAsync = false;
    }

    void progressIsThrottledAndBusyIsReported()
    {
        QString id;
        QVERIFY(addApp(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"), {}, &id));
        AppListModel *model = m_controller->apps();
        QSignalSpy changed(model, &AppListModel::appChanged);
        QSignalSpy data(model, &AppListModel::dataChanged);
        model->setBusy(id, true, QStringLiteral("Downloading"), 0.100);
        model->setBusy(id, true, QStringLiteral("Downloading"), 0.104); // < 1%: no repaint
        model->setBusy(id, true, QStringLiteral("Downloading"), 0.120);
        QCOMPARE(data.count(), 2);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(changed.first().at(0).toString(), id);
        QVERIFY(changed.first().at(1).toBool()); // busy state only

        // Asking a busy app to install says so instead of doing nothing.
        QSignalSpy finished(m_controller.get(), &HarpoonController::operationFinished);
        m_controller->install(id);
        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(1).toBool());
        QVERIFY(finished.first().at(2).toString().contains(QLatin1String("busy")));
        model->setBusy(id, false);
    }

    void sourcesListed()
    {
        const QVariantList sources = m_controller->sources();
        QVERIFY(sources.size() >= 2);
        QStringList ids;
        for (const QVariant &s : sources)
            ids << s.toMap().value(QStringLiteral("id")).toString();
        QVERIFY(ids.contains(QStringLiteral("GitHub")));
        QVERIFY(ids.contains(QStringLiteral("Forgejo")));
    }
};

QTEST_GUILESS_MAIN(TestController)
#include "tst_controller.moc"
