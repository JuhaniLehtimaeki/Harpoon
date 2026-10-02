#include "sourcetesthelpers.h"
#include "sources/jenkinssource.h"

#include <QtTest>

using namespace Harpoon;

namespace {
const QString kJob = QStringLiteral("https://ci.example.org/jenkins/job/sailfish/job/harbour-foo");
}

class TestJenkinsSource : public QObject
{
    Q_OBJECT
private slots:
    void flags()
    {
        JenkinsSource j;
        QVERIFY(j.defaultHosts().isEmpty());
        QVERIFY(j.neverAutoSelect());
    }

    void standardize_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("standard");
        QTest::newRow("job") << "https://ci.example.org/job/app" << "https://ci.example.org/job/app";
        QTest::newRow("build page") << "https://ci.example.org/job/app/42/console" << "https://ci.example.org/job/app";
        QTest::newRow("last build") << "ci.example.org/job/app/lastSuccessfulBuild/" << "https://ci.example.org/job/app";
        QTest::newRow("folders and prefix") << kJob + "/57/artifact/RPMS/" << kJob;
        QTest::newRow("port") << "http://10.0.0.2:8080/job/app/" << "http://10.0.0.2:8080/job/app";
    }

    void standardize()
    {
        QFETCH(QString, url);
        QFETCH(QString, standard);
        JenkinsSource j;
        const auto r = j.standardizeUrl(url);
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(r.value, standard);
    }

    void standardizeInvalid()
    {
        JenkinsSource j;
        QCOMPARE(int(j.standardizeUrl("https://ci.example.org/").error.kind), int(Error::InvalidUrl));
        QCOMPARE(int(j.standardizeUrl("https://ci.example.org/view/all").error.kind), int(Error::InvalidUrl));
    }

    void fetchLastSuccessfulBuild()
    {
        FakeTransport t;
        t.respondFixture(kJob + "/lastSuccessfulBuild/api/json", "jenkins/build.json");
        JenkinsSource j;
        const FetchResult r = fetchFrom(j, t, kJob);
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        QCOMPARE(r.releases.size(), 1);
        const Release &rel = r.releases.first();
        QCOMPARE(rel.tag, QStringLiteral("57"));
        QCOMPARE(rel.title, QStringLiteral("harbour-foo #57"));
        QCOMPARE(rel.date, QDateTime::fromMSecsSinceEpoch(1775124000000LL, Qt::UTC));
        QCOMPARE(rel.pageUrl, QStringLiteral("https://ci.example.org/jenkins/job/sailfish/job/harbour-foo/57/"));
        QCOMPARE(rel.changelog, QStringLiteral("- Fix crash on start\n- Update translations"));
        QCOMPARE(assetNames(rel), (QStringList{"harbour-foo-1.2.0-57.aarch64.rpm", "harbour-foo-1.2.0-57.armv7hl.rpm",
                                               "build log.txt", "harbour-foo-data-1.2.0-57.noarch.rpm"}));
        // Pinned to the build number, path segments encoded.
        QCOMPARE(rel.assets.at(0).url, kJob + "/57/artifact/RPMS/harbour-foo-1.2.0-57.aarch64.rpm");
        QCOMPARE(rel.assets.at(2).url, kJob + "/57/artifact/logs/build%20log.txt");
    }

    void pipeline()
    {
        FakeTransport t;
        t.respondFixture(kJob + "/lastSuccessfulBuild/api/json", "jenkins/build.json");
        JenkinsSource j;
        const auto latest = latestFrom(j, t, kJob, AppSettings(), "aarch64");
        QVERIFY2(latest.ok(), qPrintable(latest.error.message));
        QCOMPARE(latest.value.version, QStringLiteral("57"));
        QCOMPARE(latest.value.assets.size(), 1);
        QCOMPARE(latest.value.assets.first().name, QStringLiteral("harbour-foo-1.2.0-57.aarch64.rpm"));
    }

    void errors()
    {
        FakeTransport t;
        JenkinsSource j;
        QCOMPARE(int(fetchFrom(j, t, kJob).error.kind), int(Error::NotFound));
        t.respondJson(kJob + "/lastSuccessfulBuild/api/json", "{\"artifacts\":[]}");
        QCOMPARE(int(fetchFrom(j, t, kJob).error.kind), int(Error::NoVersion));
        t.respondJson(kJob + "/lastSuccessfulBuild/api/json", "<html>login</html>");
        QCOMPARE(int(fetchFrom(j, t, kJob).error.kind), int(Error::Parse));
    }
};

QTEST_GUILESS_MAIN(TestJenkinsSource)
#include "tst_jenkinssource.moc"
