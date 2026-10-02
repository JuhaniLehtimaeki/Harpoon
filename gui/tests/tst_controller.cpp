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
