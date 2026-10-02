#include "faketransport.h"
#include "fakerpmdb.h"
#include "minihttpserver.h"
#include "rpmfactory.h"

#include "harpooncontroller.h"

#include <QCryptographicHash>
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
    void removePackage(const QString &name, Done done) override
    {
        removals << name;
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
        QCOMPARE(model->data(idx, AppListModel::NameRole).toString(), QStringLiteral("sailfishos-chum-gui"));
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

        QVERIFY(addApp(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui")));
        // A temporary id does not know its package yet...
        QCOMPARE(m_controller->apps()->updatesCount(), 0);
        // ...but a record whose id is the RPM name does.
        const QString tmpId = m_controller->apps()->data(m_controller->apps()->index(0), AppListModel::IdRole).toString();
        AppStore store(m_dir->filePath(QStringLiteral("data/apps")));
        App app = store.load(tmpId).value;
        app.id = installed.name;
        app.temporaryId = false;
        QVERIFY(store.replace(tmpId, app).ok());
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
