#include "app/appinstaller.h"
#include "app/appstore.h"
#include "app/updatestatus.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace Harpoon;

Q_DECLARE_METATYPE(Harpoon::UpdateState)

namespace {
RpmInfo rpm(const QString &name, const QString &evr, const QString &vendor = QString())
{
    RpmInfo i;
    i.name = name;
    i.evr = parseEvr(evr);
    i.arch = QStringLiteral("aarch64");
    i.vendor = vendor;
    return i;
}
} // namespace

class TestApp : public QObject
{
    Q_OBJECT
private slots:
    void movedToAnotherAddress()
    {
        App app = App::fromUrl(QStringLiteral("https://github.com/me/harbour-thing"));
        app.id = QStringLiteral("harbour-thing");
        app.temporaryId = false;
        app.settings.set("trackOnly", true);
        app.latestVersion = QStringLiteral("1.0");
        app.receipt.evr = QStringLiteral("1.0-1");
        App moved = app.movedTo(QStringLiteral("https://codeberg.org/you/thing"), QStringLiteral("Forgejo"));
        QCOMPARE(moved.id, QStringLiteral("harbour-thing"));
        QVERIFY(!moved.temporaryId);
        QCOMPARE(moved.url, QStringLiteral("https://codeberg.org/you/thing"));
        QCOMPARE(moved.sourceId, QStringLiteral("Forgejo"));
        QCOMPARE(moved.name, QStringLiteral("thing")); // the automatic name follows
        QCOMPARE(moved.author, QStringLiteral("you"));
        QVERIFY(moved.settings.getBool("trackOnly"));
        QCOMPARE(moved.receipt.evr, QStringLiteral("1.0-1"));
        QVERIFY(moved.latestVersion.isEmpty());

        // A chosen name stays; a temporary id follows the address.
        App temp = App::fromUrl(QStringLiteral("https://github.com/me/harbour-thing"));
        temp.name = QStringLiteral("My thing");
        moved = temp.movedTo(QStringLiteral("https://codeberg.org/you/thing"), QString());
        QCOMPARE(moved.name, QStringLiteral("My thing"));
        QVERIFY(moved.temporaryId);
        QCOMPARE(moved.id, App::temporaryIdFor(QStringLiteral("https://codeberg.org/you/thing")));
    }

    void fromUrl()
    {
        const App a = App::fromUrl(QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"));
        QCOMPARE(a.name, QStringLiteral("sailfishos-chum-gui"));
        QCOMPARE(a.author, QStringLiteral("sailfishos-chum"));
        QVERIFY(a.temporaryId);
        QVERIFY(a.id.startsWith(QLatin1String("tmp-")));
        QCOMPARE(a.id.size(), 16);
        QCOMPARE(a.id, App::temporaryIdFor(a.url));
    }

    void jsonRoundTrip()
    {
        App a = App::fromUrl(QStringLiteral("https://codeberg.org/me/tool"), QStringLiteral("Forgejo"));
        a.settings.set(Keys::includePrereleases, true);
        a.settings.set(Keys::assetFilterRegEx, QStringLiteral("^tool-\\d"));
        a.latestVersion = QStringLiteral("1.2.3");
        a.latestTag = QStringLiteral("v1.2.3");
        a.latestDate = QDateTime::fromString(QStringLiteral("2026-01-02T03:04:05Z"), Qt::ISODate);
        a.changelog = QStringLiteral("Line 1\nLine 2");
        Asset asset;
        asset.name = QStringLiteral("tool-1.2.3-1.aarch64.rpm");
        asset.url = QStringLiteral("https://codeberg.org/dl/tool.rpm");
        asset.size = 1234;
        asset.sha256 = QStringLiteral("ab12");
        a.latestAssets << asset;
        a.receipt.version = QStringLiteral("1.2.2");
        a.receipt.evr = QStringLiteral("1.2.2-1");
        a.receipt.assetNames << QStringLiteral("tool-1.2.2-1.aarch64.rpm");
        a.receipt.installedAt = QDateTime::currentDateTimeUtc();
        a.lastError = QStringLiteral("No builds yet");
        a.waitingForBuilds = true;

        const auto b = App::fromJson(a.toJson());
        QVERIFY2(b.ok(), qPrintable(b.error.message));
        QCOMPARE(b.value.toJson(), a.toJson());
        QCOMPARE(b.value.sourceId, QStringLiteral("Forgejo"));
        QVERIFY(b.value.waitingForBuilds);
        QVERIFY(b.value.settings.getBool(Keys::includePrereleases));
        QCOMPARE(b.value.latestAssets.first().size, qint64(1234));
        QCOMPARE(b.value.latestDate, a.latestDate);
    }

    void rejectsFutureSchema()
    {
        QJsonObject o = App::fromUrl(QStringLiteral("https://github.com/a/b")).toJson();
        o.insert(QStringLiteral("schemaVersion"), App::kSchemaVersion + 1);
        QCOMPARE(int(App::fromJson(o).error.kind), int(Error::Storage));
    }

    void store()
    {
        QTemporaryDir dir;
        AppStore store(dir.filePath(QStringLiteral("apps")));
        App a = App::fromUrl(QStringLiteral("https://github.com/a/b"));
        QVERIFY(store.save(a).ok());
        QVERIFY(store.contains(a.id));
        QCOMPARE(store.load(a.id).value.url, a.url);

        // Temporary id -> real RPM name.
        const QString oldId = a.id;
        a.id = QStringLiteral("harbour-b");
        a.temporaryId = false;
        QVERIFY(store.replace(oldId, a).ok());
        QVERIFY(!store.contains(oldId));
        QCOMPARE(store.loadAll().size(), 1);
        QCOMPARE(store.loadAll().first().id, QStringLiteral("harbour-b"));

        // Ids that are not package names are refused (they would escape the
        // directory, or reach rpm's command line).
        App evil = a;
        evil.id = QStringLiteral("../../etc/passwd");
        QVERIFY(!store.save(evil).ok());
        evil.id = QStringLiteral("--eval=%(id)");
        QVERIFY(!store.save(evil).ok());
        QCOMPARE(QDir(dir.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), QStringList{"apps"});

        // Corrupt files are reported, not fatal.
        QFile junk(dir.filePath(QStringLiteral("apps/junk.json")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("{not json");
        junk.close();
        QStringList errors;
        QCOMPARE(store.loadAll(&errors).size(), 1);
        QCOMPARE(errors.size(), 1);

        QVERIFY(store.remove(QStringLiteral("harbour-b")).ok());
        QVERIFY(!store.contains(QStringLiteral("harbour-b")));
    }

    void updateStatus_data()
    {
        QTest::addColumn<QString>("latest");
        QTest::addColumn<QString>("installedEvr"); // empty: not installed
        QTest::addColumn<QString>("receiptVersion");
        QTest::addColumn<QString>("receiptEvr");
        QTest::addColumn<UpdateState>("expected");

        QTest::newRow("not checked") << "" << "" << "" << "" << UpdateState::NotChecked;
        QTest::newRow("not installed") << "1.0" << "" << "" << "" << UpdateState::NotInstalled;
        QTest::newRow("same") << "v1.2.0" << "1.2.0-1" << "" << "" << UpdateState::UpToDate;
        QTest::newRow("newer tag") << "v1.3.0" << "1.2.0-1" << "" << "" << UpdateState::UpdateAvailable;
        QTest::newRow("installed newer") << "1.2.0" << "1.3.0-1" << "" << "" << UpdateState::UpToDate;
        QTest::newRow("tag has release") << "0.6.12-1" << "0.6.12-1" << "" << "" << UpdateState::UpToDate;
        QTest::newRow("tag release bump") << "0.6.12-2" << "0.6.12-1" << "" << "" << UpdateState::UpdateAvailable;
        QTest::newRow("date version, receipt matches")
            << "2026-05-01T00:00:00Z" << "20260501-1" << "2026-05-01T00:00:00Z" << "20260501-1" << UpdateState::UpToDate;
        QTest::newRow("date version, newer release")
            << "2026-06-01T00:00:00Z" << "20260501-1" << "2026-05-01T00:00:00Z" << "20260501-1"
            << UpdateState::UpdateAvailable;
        QTest::newRow("date version, installed elsewhere")
            << "2026-06-01T00:00:00Z" << "20260501-1" << "2026-05-01T00:00:00Z" << "20260401-1" << UpdateState::Unknown;
    }

    void updateStatus()
    {
        QFETCH(QString, latest);
        QFETCH(QString, installedEvr);
        QFETCH(QString, receiptVersion);
        QFETCH(QString, receiptEvr);
        QFETCH(UpdateState, expected);
        App app = App::fromUrl(QStringLiteral("https://github.com/a/b"));
        app.latestVersion = latest;
        app.receipt.version = receiptVersion;
        app.receipt.evr = receiptEvr;
        const RpmInfo installed = installedEvr.isEmpty() ? RpmInfo() : rpm(QStringLiteral("b"), installedEvr);
        const UpdateStatus s = updateStatusFor(app, installed);
        QCOMPARE(updateStateName(s.state), updateStateName(expected));
    }

    void trackOnlyStatus()
    {
        App app = App::fromUrl(QStringLiteral("https://github.com/a/b"));
        app.settings.set(Keys::trackOnly, true);
        app.latestVersion = QStringLiteral("2.0");
        QCOMPARE(updateStatusFor(app, RpmInfo()).state, UpdateState::UpdateAvailable);
        app.acknowledgedVersion = QStringLiteral("2.0");
        QCOMPARE(updateStatusFor(app, RpmInfo()).state, UpdateState::UpToDate);
    }

    void mainPackage()
    {
        const QList<RpmInfo> pkgs{rpm(QStringLiteral("tool-data"), QStringLiteral("1-1")),
                                  rpm(QStringLiteral("tool"), QStringLiteral("1-1")),
                                  rpm(QStringLiteral("tool-plugins"), QStringLiteral("1-1"))};
        QCOMPARE(AppInstaller::mainPackageIndex(pkgs, QString(), QStringLiteral("x")), 1);
        QCOMPARE(AppInstaller::mainPackageIndex(pkgs, QStringLiteral("tool-data"), QString()), 0);
        QCOMPARE(AppInstaller::mainPackageIndex(pkgs, QStringLiteral("other"), QString()), -1);

        const QList<RpmInfo> unrelated{rpm(QStringLiteral("alpha"), QStringLiteral("1-1")),
                                       rpm(QStringLiteral("harbour-beta"), QStringLiteral("1-1"))};
        QCOMPARE(AppInstaller::mainPackageIndex(unrelated, QString(), QStringLiteral("harbour-beta")), 1);
        QCOMPARE(AppInstaller::mainPackageIndex(unrelated, QString(), QStringLiteral("zzz")), 0);
    }
};

QTEST_GUILESS_MAIN(TestApp)
#include "tst_app.moc"
