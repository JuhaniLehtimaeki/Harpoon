// End to end: a local HTTP "forge" serves a GitHub-style API and real RPM
// files; Harpoon checks for updates, downloads, inspects the RPMs with rpm and
// installs them through a fake package backend that records the result in a
// fake rpm database.

#include "fakerpmdb.h"
#include "minihttpserver.h"
#include "rpmfactory.h"

#include "app/appchecker.h"
#include "app/appinstaller.h"
#include "app/updatestatus.h"
#include "net/networktransport.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace Harpoon;

namespace {

QByteArray fileBytes(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

// "Installs" by reading each package with rpm and recording it as installed.
class FakeBackend : public PackageBackend
{
public:
    explicit FakeBackend(FakeRpmDb &db) : m_db(db), m_inspector(db) {}

    QString name() const override { return QStringLiteral("fake"); }
    bool isSilent() const override { return true; }

    void installFiles(const QStringList &paths, const InstallOptions &options, Done done) override
    {
        installs << paths;
        lastOptions = options;
        if (!failWith.ok()) {
            done(failWith);
            return;
        }
        for (const QString &p : paths) {
            QVERIFY(QFileInfo(p).isAbsolute());
            QVERIFY(QFile::exists(p));
            const auto info = m_inspector.inspectFile(p);
            QVERIFY(info.ok());
            if (!pretendSuccessOnly)
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
    InstallOptions lastOptions;
    Error failWith;
    bool pretendSuccessOnly = false;

private:
    FakeRpmDb &m_db;
    RpmInspector m_inspector;
};

} // namespace

class TestAppInstaller : public QObject
{
    Q_OBJECT

    RpmFactory m_factory;
    QHash<QString, QString> m_rpms; // key -> path

    std::unique_ptr<MiniHttpServer> m_server;
    std::unique_ptr<QTemporaryDir> m_downloads;
    FakeRpmDb m_db;
    std::unique_ptr<FakeBackend> m_backend;
    DeviceInfo m_device;

    // Serves one release whose assets are the given RPM keys.
    void serveRelease(const QString &tag, const QStringList &keys, bool corruptDigest = false)
    {
        QJsonArray assets;
        for (const QString &key : keys) {
            const QString path = m_rpms.value(key);
            const QByteArray bytes = fileBytes(path);
            const QString name = QFileInfo(path).fileName();
            m_server->routeBody(QStringLiteral("/dl/") + name, bytes);
            QJsonObject a;
            a.insert(QStringLiteral("name"), name);
            a.insert(QStringLiteral("browser_download_url"), m_server->url(QStringLiteral("/dl/") + name));
            a.insert(QStringLiteral("size"), bytes.size());
            const QByteArray digest = corruptDigest ? QByteArray(64, '0')
                                                    : QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
            a.insert(QStringLiteral("digest"), QStringLiteral("sha256:") + QString::fromLatin1(digest));
            assets.append(a);
        }
        QJsonObject release;
        release.insert(QStringLiteral("tag_name"), tag);
        release.insert(QStringLiteral("name"), tag);
        release.insert(QStringLiteral("body"), QStringLiteral("Changes in ") + tag);
        release.insert(QStringLiteral("published_at"), QStringLiteral("2026-09-01T10:00:00Z"));
        release.insert(QStringLiteral("assets"), assets);
        m_server->routeBody(QStringLiteral("/api/v3/repos/me/tool/releases?per_page=100"),
                            QJsonDocument(QJsonArray{release}).toJson(), "application/json");
    }

    App checkedApp(const App &base = App())
    {
        SourceRegistry registry;
        App app = base.url.isEmpty() ? App() : base;
        if (app.url.isEmpty()) {
            const auto match = registry.match(m_server->url(QStringLiteral("/me/tool")), QStringLiteral("GitHub"));
            if (!match.ok())
                qFatal("%s", qPrintable(match.error.message));
            app = App::fromUrl(match.value.standardUrl, QStringLiteral("GitHub"));
        }
        NetworkTransport transport;
        AppChecker checker(registry, transport, m_device);
        App result;
        Error error;
        bool done = false;
        checker.check(app, [&](const App &a, const Error &e) {
            result = a;
            error = e;
            done = true;
        });
        if (!QTest::qWaitFor([&]() { return done; }, 10000))
            qWarning("check timed out");
        if (!error.ok())
            qWarning() << "check failed:" << error.message;
        return result;
    }

    Result<InstallResult> install(const App &app, const InstallOptions &options = InstallOptions())
    {
        Downloader downloader;
        RpmInspector inspector(m_db);
        AppInstaller installer(downloader, inspector, *m_backend, m_downloads->path(), m_device);
        Result<InstallResult> result;
        bool done = false;
        installer.install(app, options, nullptr, [&](const Result<InstallResult> &r) {
            result = r;
            done = true;
        });
        if (!QTest::qWaitFor([&]() { return done; }, 10000))
            result = Result<InstallResult>::failure(Error::make(Error::Install, QStringLiteral("test timeout")));
        return result;
    }

    static RpmInfo installedInfo(const QString &name, const QString &evr, const QString &vendor = QStringLiteral("chum"))
    {
        RpmInfo i;
        i.name = name;
        i.evr = parseEvr(evr);
        i.arch = QStringLiteral("aarch64");
        i.vendor = vendor;
        return i;
    }

private slots:
    void initTestCase()
    {
        if (!RpmFactory::available())
            QSKIP("rpmbuild not installed");
        m_rpms.insert(QStringLiteral("tool-2.0-aarch64"),
                      m_factory.build(QStringLiteral("tool"), QStringLiteral("2.0"), QStringLiteral("1"), QStringLiteral("aarch64")));
        m_rpms.insert(QStringLiteral("tool-2.0-armv7hl"),
                      m_factory.build(QStringLiteral("tool"), QStringLiteral("2.0"), QStringLiteral("1"), QStringLiteral("armv7hl")));
        m_rpms.insert(QStringLiteral("tool-data-2.0-noarch"),
                      m_factory.build(QStringLiteral("tool-data"), QStringLiteral("2.0"), QStringLiteral("1"), QStringLiteral("noarch")));
        m_rpms.insert(QStringLiteral("tool-2.0-meego"),
                      m_factory.build(QStringLiteral("tool"), QStringLiteral("2.0"), QStringLiteral("1"), QStringLiteral("aarch64"),
                                      QStringLiteral("meego")));
        for (const QString &p : m_rpms)
            QVERIFY(!p.isEmpty());
        m_device.arch = QStringLiteral("aarch64");
        m_device.osVersion = QStringLiteral("5.0.0.62");
    }

    void init()
    {
        m_server.reset(new MiniHttpServer);
        m_downloads.reset(new QTemporaryDir);
        m_db.installed.clear();
        m_backend.reset(new FakeBackend(m_db));
    }

    void freshInstallAdoptsRpmName()
    {
        serveRelease(QStringLiteral("v2.0"), {QStringLiteral("tool-2.0-aarch64"), QStringLiteral("tool-2.0-armv7hl")});
        const App app = checkedApp();
        QVERIFY(app.lastError.isEmpty());
        QCOMPARE(app.latestVersion, QStringLiteral("v2.0"));
        QCOMPARE(app.latestAssets.size(), 1); // armv7hl filtered out
        QVERIFY(app.temporaryId);
        QCOMPARE(updateStatusFor(app, RpmInfo()).state, UpdateState::NotInstalled);

        const auto r = install(app);
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(r.value.app.id, QStringLiteral("tool"));
        QVERIFY(!r.value.app.temporaryId);
        QCOMPARE(r.value.app.receipt.evr, QStringLiteral("2.0-1"));
        QCOMPARE(r.value.app.receipt.version, QStringLiteral("v2.0"));
        QCOMPARE(r.value.installed.nevra(), QStringLiteral("tool-2.0-1.aarch64"));
        QCOMPARE(m_backend->installs.size(), 1);
        QCOMPARE(m_backend->installs.first().size(), 1);
        // Downloads are removed after a successful install.
        QCOMPARE(QDir(m_downloads->path()).entryList(QDir::Files).size(), 0);
        QCOMPARE(updateStatusFor(r.value.app, m_db.installed.value(QStringLiteral("tool"))).state, UpdateState::UpToDate);
    }

    void updateAvailableThenInstall()
    {
        m_db.installed.insert(QStringLiteral("tool"), installedInfo(QStringLiteral("tool"), QStringLiteral("1.5-1")));
        serveRelease(QStringLiteral("v2.0"), {QStringLiteral("tool-2.0-aarch64")});
        App app = checkedApp();
        app.id = QStringLiteral("tool");
        app.temporaryId = false;
        QCOMPARE(updateStatusFor(app, m_db.installed.value(QStringLiteral("tool"))).state, UpdateState::UpdateAvailable);
        const auto r = install(app);
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(m_db.installed.value(QStringLiteral("tool")).evr.toString(), QStringLiteral("2.0-1"));
        QVERIFY(r.value.warnings.isEmpty());
    }

    void alreadyInstalledNeedsReinstall()
    {
        m_db.installed.insert(QStringLiteral("tool"), installedInfo(QStringLiteral("tool"), QStringLiteral("2.0-1")));
        serveRelease(QStringLiteral("v2.0"), {QStringLiteral("tool-2.0-aarch64")});
        const App app = checkedApp();
        QCOMPARE(int(install(app).error.kind), int(Error::AlreadyInstalled));
        QVERIFY(m_backend->installs.isEmpty());
        InstallOptions o;
        o.allowReinstall = true;
        QVERIFY(install(app, o).ok());
        QVERIFY(m_backend->lastOptions.allowReinstall);
    }

    void downgradeRefused()
    {
        m_db.installed.insert(QStringLiteral("tool"), installedInfo(QStringLiteral("tool"), QStringLiteral("3.0-1")));
        serveRelease(QStringLiteral("v2.0"), {QStringLiteral("tool-2.0-aarch64")});
        const App app = checkedApp();
        const auto r = install(app);
        QCOMPARE(int(r.error.kind), int(Error::Downgrade));
        QVERIFY(r.error.message.contains(QLatin1String("3.0-1")));
        QVERIFY(m_backend->installs.isEmpty());
        QCOMPARE(QDir(m_downloads->path()).entryList(QDir::Files).size(), 0); // cleaned up
        InstallOptions o;
        o.allowDowngrade = true;
        QVERIFY(install(app, o).ok());
    }

    void checksumMismatchStopsBeforeInstall()
    {
        serveRelease(QStringLiteral("v2.0"), {QStringLiteral("tool-2.0-aarch64")}, true);
        const auto r = install(checkedApp());
        QCOMPARE(int(r.error.kind), int(Error::Checksum));
        QVERIFY(m_backend->installs.isEmpty());
    }

    void differentPackageNameNeedsAllowIdChange()
    {
        serveRelease(QStringLiteral("v2.0"), {QStringLiteral("tool-2.0-aarch64")});
        App app = checkedApp();
        app.id = QStringLiteral("harbour-tool");
        app.temporaryId = false;
        const auto r = install(app);
        QCOMPARE(int(r.error.kind), int(Error::IdChanged));
        QVERIFY(r.error.message.contains(QLatin1String("tool")));
        app.settings.set(Keys::allowIdChange, true);
        const auto ok = install(app);
        QVERIFY2(ok.ok(), qPrintable(ok.error.message));
        QCOMPARE(ok.value.app.id, QStringLiteral("tool"));
    }

    void subpackagesInstallTogether()
    {
        serveRelease(QStringLiteral("v2.0"), {QStringLiteral("tool-data-2.0-noarch"), QStringLiteral("tool-2.0-aarch64")});
        const App app = checkedApp();
        // Arch filter keeps native packages only when any exist; the user
        // includes noarch subpackages by disabling it.
        QCOMPARE(app.latestAssets.size(), 1);
        App all = app;
        all.settings.set(Keys::autoAssetFilterByArch, false);
        all = checkedApp(all);
        QCOMPARE(all.latestAssets.size(), 2);
        const auto r = install(all);
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(r.value.app.id, QStringLiteral("tool"));
        QCOMPARE(m_backend->installs.size(), 1);
        QCOMPARE(m_backend->installs.first().size(), 2);
        QVERIFY(m_db.installed.contains(QStringLiteral("tool-data")));
    }

    void wrongArchRejected()
    {
        serveRelease(QStringLiteral("v2.0"), {QStringLiteral("tool-2.0-armv7hl")});
        App app = checkedApp();
        QCOMPARE(app.latestAssets.size(), 0); // filtered at check time...
        app.latestAssets.clear();
        // ...and rejected at install time even if the filter was bypassed.
        App bypass = app;
        bypass.settings.set(Keys::autoAssetFilterByArch, false);
        bypass = checkedApp(bypass);
        QCOMPARE(bypass.latestAssets.size(), 1);
        QCOMPARE(int(install(bypass).error.kind), int(Error::WrongArch));
    }

    void vendorChangeWarns()
    {
        m_db.installed.insert(QStringLiteral("tool"), installedInfo(QStringLiteral("tool"), QStringLiteral("1.0-1"), QStringLiteral("chum")));
        serveRelease(QStringLiteral("v2.0"), {QStringLiteral("tool-2.0-meego")});
        const auto r = install(checkedApp());
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(r.value.warnings.size(), 1);
        QVERIFY(r.value.warnings.first().contains(QLatin1String("meego")));
    }

    void backendFailurePropagates()
    {
        serveRelease(QStringLiteral("v2.0"), {QStringLiteral("tool-2.0-aarch64")});
        m_backend->failWith = Error::make(Error::NotAuthorized, QStringLiteral("not privileged"));
        const auto r = install(checkedApp());
        QCOMPARE(int(r.error.kind), int(Error::NotAuthorized));
        QCOMPARE(QDir(m_downloads->path()).entryList(QDir::Files).size(), 0);
    }

    void successWithoutEffectIsAnError()
    {
        serveRelease(QStringLiteral("v2.0"), {QStringLiteral("tool-2.0-aarch64")});
        m_backend->pretendSuccessOnly = true;
        QCOMPARE(int(install(checkedApp()).error.kind), int(Error::Install));
    }

    void trackOnlyCannotInstall()
    {
        serveRelease(QStringLiteral("v2.0"), {QStringLiteral("tool-2.0-aarch64")});
        App app = checkedApp();
        app.settings.set(Keys::trackOnly, true);
        QCOMPARE(int(install(app).error.kind), int(Error::InvalidSetting));
    }

    void uninstall()
    {
        m_db.installed.insert(QStringLiteral("tool"), installedInfo(QStringLiteral("tool"), QStringLiteral("2.0-1")));
        App app = App::fromUrl(QStringLiteral("https://github.com/me/tool"));
        Downloader downloader;
        RpmInspector inspector(m_db);
        AppInstaller installer(downloader, inspector, *m_backend, m_downloads->path(), m_device);
        Error e;
        installer.uninstall(app, [&](const Error &err) { e = err; });
        QCOMPARE(int(e.kind), int(Error::Package)); // temporary id: never installed by us
        app.id = QStringLiteral("tool");
        app.temporaryId = false;
        installer.uninstall(app, [&](const Error &err) { e = err; });
        QVERIFY(e.ok());
        QCOMPARE(m_backend->removals, QStringList{"tool"});
    }
};

QTEST_GUILESS_MAIN(TestAppInstaller)
#include "tst_appinstaller.moc"
