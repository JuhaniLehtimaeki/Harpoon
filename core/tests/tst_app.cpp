#include "app/appinstaller.h"
#include "app/appstore.h"
#include "app/autoupdate.h"
#include "app/packagebuilds.h"
#include "app/problemreport.h"
#include "app/updatestatus.h"
#include "pipeline/assetfilter.h"

#include <QTemporaryDir>
#include <QUrlQuery>
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
    void rpmFileNames_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("evr");
        QTest::addColumn<QString>("arch");
        QTest::newRow("plain") << "harbour-thing-1.2.3-1.aarch64.rpm" << "harbour-thing" << "1.2.3-1" << "aarch64";
        QTest::newRow("dotted release") << "harpoon-0.6.0-1.sfos5.1.aarch64.rpm" << "harpoon" << "0.6.0-1.sfos5.1"
                                        << "aarch64";
        QTest::newRow("suffix after arch") << "harbour-bitsailor-1.8.1-1.aarch64-openrepos.rpm" << "harbour-bitsailor"
                                           << "1.8.1-1" << "aarch64";
        QTest::newRow("letters in version") << "rawfish-1.3.1.alpha.9.testing-9.aarch64.rpm" << "rawfish"
                                            << "1.3.1.alpha.9.testing-9" << "aarch64";
        QTest::newRow("git describe") << "harbour-x-0.7.0.6.g56b71a34.dirty-1.aarch64.rpm" << "harbour-x"
                                      << "0.7.0.6.g56b71a34.dirty-1" << "aarch64";
        QTest::newRow("noarch") << "harbour-x-data-2-1.noarch.rpm" << "harbour-x-data" << "2-1" << "noarch";
        QTest::newRow("not nevra") << "harbour-x-arm64.rpm" << "" << "" << "";
    }

    void rpmFileNames()
    {
        QFETCH(QString, file);
        QFETCH(QString, name);
        QFETCH(QString, evr);
        QFETCH(QString, arch);
        const RpmFileName parsed = parseRpmFileName(file);
        QCOMPARE(parsed.name, name);
        QCOMPARE(parsed.isValid() ? parsed.evr.toString() : QString(), evr);
        QCOMPARE(parsed.arch, arch);
    }

    void buildChoices_data()
    {
        QTest::addColumn<QStringList>("files");     // this release, for the device
        QTest::addColumn<QStringList>("nextFiles"); // a later release, same builds in the same order
        QTest::addColumn<QStringList>("labels");
        QTest::addColumn<QString>("package");
        QTest::newRow("bitsailor")
            << QStringList{"harbour-bitsailor-1.8.1-1.aarch64-openrepos.rpm", "harbour-bitsailor-1.8.1-1.aarch64-store.rpm"}
            << QStringList{"harbour-bitsailor-1.9.0-1.aarch64-openrepos.rpm", "harbour-bitsailor-1.9.0-1.aarch64-store.rpm"}
            << QStringList{"openrepos", "store"} << "harbour-bitsailor";
        QTest::newRow("rawfish")
            << QStringList{"rawfish-1.3.1-9.aarch64.rpm", "rawfish-1.3.1.alpha.9.testing-9.aarch64.rpm"}
            << QStringList{"rawfish-1.3.2-1.aarch64.rpm", "rawfish-1.3.2.alpha.1.testing-1.aarch64.rpm"}
            << QStringList{"", "alpha testing"} << "rawfish";
        // A companion package survives the choice when the builds can be told
        // apart by leaving the others out.
        QTest::newRow("with a companion")
            << QStringList{"harbour-a-1-1.aarch64-openrepos.rpm", "harbour-a-1-1.aarch64-store.rpm", "harbour-a-data-1-1.noarch.rpm"}
            << QStringList{"harbour-a-2-1.aarch64-openrepos.rpm", "harbour-a-2-1.aarch64-store.rpm", "harbour-a-data-2-1.noarch.rpm"}
            << QStringList{"openrepos", "store"} << "harbour-a";
    }

    void buildChoices()
    {
        QFETCH(QStringList, files);
        QFETCH(QStringList, nextFiles);
        QFETCH(QStringList, labels);
        QFETCH(QString, package);
        auto assetsOf = [](const QStringList &names) {
            QList<Asset> out;
            for (const QString &n : names) {
                Asset a;
                a.name = n;
                a.url = QStringLiteral("https://github.com/o/r/releases/download/v1/") + n;
                out << a;
            }
            return out;
        };
        QString name;
        const QList<BuildChoice> choices = Harpoon::buildChoices(assetsOf(files), &name);
        QCOMPARE(name, package);
        QCOMPARE(choices.size(), labels.size());
        DeviceInfo device;
        device.arch = QStringLiteral("aarch64");
        for (int i = 0; i < choices.size(); ++i) {
            QCOMPARE(choices.at(i).assetName, files.at(i));
            QCOMPARE(choices.at(i).label, labels.at(i));
            QVariantMap values{{QString::fromLatin1(Keys::assetFilterRegEx), choices.at(i).filter}};
            if (choices.at(i).invert)
                values.insert(QString::fromLatin1(Keys::invertAssetFilter), true);
            const AppSettings settings(values);
            // This release and the next: only the chosen build of the package
            // is left, so there is nothing to choose any more.
            for (const QStringList &release : {files, nextFiles}) {
                const auto kept = filterAssets(assetsOf(release), settings, device);
                QVERIFY(kept.ok());
                QStringList keptNames;
                for (const Asset &a : kept.value)
                    keptNames << a.name;
                QVERIFY2(keptNames.contains(release.at(i)), qPrintable(keptNames.join(QLatin1Char(' '))));
                QVERIFY2(Harpoon::buildChoices(kept.value).isEmpty(), qPrintable(keptNames.join(QLatin1Char(' '))));
            }
        }
        QVERIFY(Harpoon::buildChoices(assetsOf({files.first()})).isEmpty());
    }

    void releasePackageNotNewer()
    {
        // Issue #1: the release is newer by its tag, but its package was built
        // with the installed version number.
        App app = App::fromUrl(QStringLiteral("https://codeberg.org/me/harbour-x"));
        app.id = QStringLiteral("harbour-x");
        app.temporaryId = false;
        app.latestVersion = QStringLiteral("v0.7.1-rc1");
        Asset asset;
        asset.name = QStringLiteral("harbour-x-0.7.0.6.g56b71a34.dirty-1.aarch64.rpm");
        app.latestAssets << asset;
        RpmInfo installed;
        installed.name = QStringLiteral("harbour-x");
        installed.evr = parseEvr(QStringLiteral("0.7.0.6.g56b71a34.dirty-1"));
        QCOMPARE(updateStatusByVersion(app, installed).state, UpdateState::UpdateAvailable);
        UpdateStatus s = updateStatusFor(app, installed);
        QCOMPARE(s.state, UpdateState::UpToDate);
        QCOMPARE(s.reason, QStringLiteral("releasePackageNotNewer"));
        QCOMPARE(s.offeredPackageVersion, QStringLiteral("0.7.0.6.g56b71a34.dirty-1"));

        // An epoch is not in file names, so it does not make them older.
        RpmInfo withEpoch = installed;
        withEpoch.evr.epoch = 1;
        QCOMPARE(updateStatusFor(app, withEpoch).state, UpdateState::UpToDate);
        app.latestAssets.first().name = QStringLiteral("harbour-x-0.7.1-1.aarch64.rpm");
        QCOMPARE(updateStatusFor(app, withEpoch).state, UpdateState::UpdateAvailable);

        // Fixed by the developer: an update again.
        app.latestAssets.first().name = QStringLiteral("harbour-x-0.7.1~rc1-1.aarch64.rpm");
        QCOMPARE(updateStatusFor(app, installed).state, UpdateState::UpdateAvailable);
        // File names that say nothing about the package do not decide.
        app.latestAssets.first().name = QStringLiteral("harbour-x-arm64.rpm");
        QCOMPARE(updateStatusFor(app, installed).state, UpdateState::UpdateAvailable);
    }

    void severalBuildsAreNotUpdatedAutomatically()
    {
        App app = App::fromUrl(QStringLiteral("https://github.com/me/harbour-x"));
        app.id = QStringLiteral("harbour-x");
        app.temporaryId = false;
        app.receipt.evr = QStringLiteral("1-1");
        for (const char *n : {"harbour-x-2-1.aarch64-openrepos.rpm", "harbour-x-2-1.aarch64-store.rpm"}) {
            Asset a;
            a.name = QString::fromLatin1(n);
            app.latestAssets << a;
        }
        UpdateStatus status;
        status.state = UpdateState::UpdateAvailable;
        QString why;
        QVERIFY(!autoUpdateEligible(app, status, &why));
        QCOMPARE(why, QStringLiteral("several builds to choose from"));
    }

    void problemReport()
    {
        InstallProblem p;
        p.app = App::fromUrl(QStringLiteral("https://github.com/me/harbour-thing"));
        p.app.id = QStringLiteral("harbour-thing");
        p.app.latestVersion = QStringLiteral("1.2");
        p.app.latestTag = QStringLiteral("v1.2");
        Asset asset;
        asset.name = QStringLiteral("harbour-thing-1.2-1.aarch64.rpm");
        p.app.latestAssets << asset;
        p.installedEvr = QStringLiteral("1.1-1");
        p.device.arch = QStringLiteral("aarch64");
        p.device.osVersion = QStringLiteral("5.0.0.62");
        p.backend = QStringLiteral("packagekit");
        p.stage = QStringLiteral("Installing harbour-thing-1.2-1.aarch64.rpm");
        p.error = Error::make(Error::WrongArch, QStringLiteral("harbour-thing is built for armv7hl, this device is aarch64"));
        p.when = QDateTime(QDate(2026, 10, 6), QTime(12, 0), Qt::UTC);

        QCOMPARE(installAction(p), QStringLiteral("Update"));
        QCOMPARE(installProblemTitle(p), QStringLiteral("Update of harbour-thing failed: WrongArch"));
        QVERIFY(isAppPackageProblem(p.error.kind));
        QVERIFY(!isAppPackageProblem(Error::Network));
        const QString report = Harpoon::installProblemReport(p);
        for (const char *line : {"failed at \"Installing harbour-thing-1.2-1.aarch64.rpm\".",
                                 "harbour-thing is built for armv7hl, this device is aarch64",
                                 "- SailfishOS: 5.0.0.62 (aarch64)", "- Install backend: packagekit",
                                 "- App: harbour-thing (harbour-thing)", "- Installed: 1.1-1",
                                 "- Release: 1.2 (tag v1.2)", "- Packages: harbour-thing-1.2-1.aarch64.rpm",
                                 "- Error kind: WrongArch", "- Time: 2026-10-06T12:00:00Z"})
            QVERIFY2(report.contains(QLatin1String(line)), line);

        // Title and body survive the trip through the URL, newlines and all.
        const QUrl url = newIssueUrl(QStringLiteral("https://github.com/o/r"), QStringLiteral("A & B"), report);
        QCOMPARE(url.host(), QStringLiteral("github.com"));
        QCOMPARE(url.path(), QStringLiteral("/o/r/issues/new"));
        const QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded), QStringLiteral("A & B"));
        QCOMPARE(query.queryItemValue(QStringLiteral("body"), QUrl::FullyDecoded), report);
    }

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
