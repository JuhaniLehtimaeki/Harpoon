#include "sourcetesthelpers.h"
#include "sources/gitlabsource.h"

#include <QtTest>

using namespace Harpoon;

namespace {
const QString kProject = QStringLiteral("https://gitlab.com/sailfish/apps/harbour-foo");
const QString kApi = QStringLiteral("https://gitlab.com/api/v4/projects/sailfish%2Fapps%2Fharbour-foo");

void serveProject(FakeTransport &t)
{
    t.respondFixture(kApi, "gitlab/project.json");
    t.respondFixture(kApi + "/releases?per_page=100", "gitlab/releases.json");
    t.respondFixture(kApi + "/repository/tags?per_page=100", "gitlab/tags.json");
}
} // namespace

class TestGitLabSource : public QObject
{
    Q_OBJECT
private slots:
    void standardize_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("standard");
        QTest::newRow("project") << "https://gitlab.com/owner/app" << "https://gitlab.com/owner/app";
        QTest::newRow("subgroups and sub-page") << "https://gitlab.com/grp/sub/deeper/app/-/releases/v1.0"
                                                << "https://gitlab.com/grp/sub/deeper/app";
        QTest::newRow("no scheme, www, .git") << "www.gitlab.com/owner/app.git" << "https://gitlab.com/owner/app";
        QTest::newRow("query dropped") << "https://gitlab.com/owner/app?tab=readme" << "https://gitlab.com/owner/app";
        QTest::newRow("tree page") << "https://gitlab.com/owner/app/-/tree/main" << "https://gitlab.com/owner/app";
    }

    void standardize()
    {
        QFETCH(QString, url);
        QFETCH(QString, standard);
        GitLabSource gl;
        const auto r = gl.standardizeUrl(url);
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(r.value, standard);
    }

    void standardizeInvalid()
    {
        GitLabSource gl;
        QCOMPARE(int(gl.standardizeUrl("https://gitlab.com/only-owner").error.kind), int(Error::InvalidUrl));
        QCOMPARE(int(gl.standardizeUrl("https://gitlab.com/owner/-/x").error.kind), int(Error::InvalidUrl));
        QCOMPARE(int(gl.standardizeUrl("https://github.com/owner/app").error.kind), int(Error::InvalidUrl));
    }

    void standardizeOverrideHost()
    {
        GitLabSource gl;
        gl.setCustomHost("git.example.org:8443");
        QCOMPARE(gl.standardizeUrl("https://git.example.org:8443/team/app/-/tags").value,
                 QStringLiteral("https://git.example.org:8443/team/app"));
        QCOMPARE(int(gl.standardizeUrl("https://gitlab.com/team/app").error.kind), int(Error::InvalidUrl));
        QCOMPARE(GitLabSource::apiBaseUrl("https://git.example.org:8443/team/app"),
                 QStringLiteral("https://git.example.org:8443/api/v4/projects/team%2Fapp"));
    }

    void apiBaseEncodesPath()
    {
        QCOMPARE(GitLabSource::apiBaseUrl(kProject), kApi);
    }

    void fetchReleases()
    {
        FakeTransport t;
        serveProject(t);
        GitLabSource gl;
        const FetchResult r = fetchFrom(gl, t, kProject);
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        QCOMPARE(t.requests.size(), 2);
        QCOMPARE(t.requests.at(0).url, kApi);
        QCOMPARE(t.requests.at(1).url, kApi + "/releases?per_page=100");
        QCOMPARE(t.requests.at(0).header("Referer"), QByteArray("https://gitlab.com"));
        QCOMPARE(t.requests.at(1).header("Referer"), QByteArray("https://gitlab.com"));

        QCOMPARE(r.releases.size(), 3);
        const Release &upcoming = r.releases.at(0);
        QCOMPARE(upcoming.tag, QStringLiteral("v2.0.0"));
        QVERIFY(upcoming.prerelease); // upcoming_release
        QCOMPARE(upcoming.assets.first().url,
                 QStringLiteral("https://gitlab.com/sailfish/apps/harbour-foo/-/releases/v2.0.0/downloads/harbour-foo-2.0.0-1.aarch64.rpm"));

        const Release &rel = r.releases.at(1);
        QCOMPARE(rel.tag, QStringLiteral("v1.2.0"));
        QCOMPARE(rel.title, QStringLiteral("Foo 1.2"));
        QVERIFY(!rel.prerelease);
        QCOMPARE(rel.date, QDateTime(QDate(2026, 4, 2), QTime(10, 15, 30), Qt::UTC)); // released_at, ms dropped
        QVERIFY(rel.changelog.startsWith("## Changes"));
        QCOMPARE(rel.pageUrl, QStringLiteral("https://gitlab.com/sailfish/apps/harbour-foo/-/releases/v1.2.0"));
        QCOMPARE(assetNames(rel), (QStringList{"harbour-foo-1.2.0-1.aarch64.rpm", "harbour-foo-data-1.2.0-1.noarch.rpm",
                                               "harbour-foo-1.2.0-1.i486.rpm", "harbour-foo-1.2.0-1.armv7hl.rpm"}));
        // direct_asset_url wins over url
        QCOMPARE(rel.assets.at(0).url,
                 QStringLiteral("https://gitlab.com/sailfish/apps/harbour-foo/-/releases/v1.2.0/downloads/harbour-foo-1.2.0-1.aarch64.rpm"));
        // CI artifact /file/ -> /raw/
        QCOMPARE(rel.assets.at(1).url,
                 QStringLiteral("https://gitlab.com/sailfish/apps/harbour-foo/-/jobs/987/artifacts/raw/harbour-foo-data-1.2.0-1.noarch.rpm"));
        QCOMPARE(rel.assets.at(2).url, QStringLiteral("https://gitlab.com/sailfish/apps/harbour-foo/-/package_files/23/download"));
        // markdown upload in the description; the screenshot is not an asset
        QCOMPARE(rel.assets.at(3).url,
                 QStringLiteral("https://gitlab.com/-/project/4242/uploads/0a1b2c3d/harbour-foo-1.2.0-1.armv7hl.rpm"));

        const Release &old = r.releases.at(2);
        QCOMPARE(old.date, QDateTime(QDate(2026, 2, 10), QTime(12, 0), Qt::UTC)); // created_at fallback
        QVERIFY(old.changelog.isEmpty());
        QCOMPARE(old.pageUrl, kProject + "/-/releases/v1.1.0");
        QCOMPARE(old.assets.first().url, QStringLiteral("https://gitlab.com/sailfish/apps/harbour-foo/-/package_files/11/download"));
    }

    void tokenIsQueryParameterOnly()
    {
        FakeTransport t;
        t.respondFixture(kApi + "?private_token=glpat-secret", "gitlab/project.json");
        t.respondFixture(kApi + "/releases?per_page=100&private_token=glpat-secret", "gitlab/releases.json");
        GitLabSource gl;
        gl.setConfig({{"token", " glpat-secret "}});
        const FetchResult r = fetchFrom(gl, t, kProject);
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        QVERIFY(t.requests.first().header("Authorization").isEmpty());
        // Stored asset URLs stay clean; the token is added at download time.
        for (const Release &rel : r.releases)
            for (const Asset &a : rel.assets)
                QVERIFY(!a.url.contains("private_token"));
        QCOMPARE(gl.authorizedAssetUrl("https://gitlab.com/x/-/package_files/1/download"),
                 QStringLiteral("https://gitlab.com/x/-/package_files/1/download?private_token=glpat-secret"));
        QCOMPARE(gl.authorizedAssetUrl("https://gitlab.com/x?a=1"), QStringLiteral("https://gitlab.com/x?a=1&private_token=glpat-secret"));
        GitLabSource anonymous;
        QCOMPARE(anonymous.authorizedAssetUrl("https://gitlab.com/x"), QStringLiteral("https://gitlab.com/x"));
    }

    void trackOnlyUsesTags()
    {
        FakeTransport t;
        serveProject(t);
        GitLabSource gl;
        AppSettings s;
        s.set(Keys::trackOnly, true);
        const FetchResult r = fetchFrom(gl, t, kProject, s);
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        QCOMPARE(t.requests.last().url, kApi + "/repository/tags?per_page=100");
        QCOMPARE(r.releases.size(), 2);
        QCOMPARE(r.releases.at(0).tag, QStringLiteral("v1.3.0"));
        QCOMPARE(r.releases.at(0).changelog, QStringLiteral("Tag notes for 1.3.0")); // release.description
        QCOMPARE(r.releases.at(0).date, QDateTime(QDate(2026, 6, 1), QTime(7, 30), Qt::UTC)); // commit.created_at
        QCOMPARE(r.releases.at(0).pageUrl, kProject + "/-/tags/v1.3.0");
        QCOMPARE(r.releases.at(1).changelog, QStringLiteral("Annotated tag 1.2.0")); // message
        QVERIFY(r.releases.at(1).assets.isEmpty());
    }

    void pipelineSkipsUpcomingAndPicksArch()
    {
        FakeTransport t;
        serveProject(t);
        GitLabSource gl;
        const auto latest = latestFrom(gl, t, kProject, AppSettings(), "aarch64");
        QVERIFY2(latest.ok(), qPrintable(latest.error.message));
        QCOMPARE(latest.value.version, QStringLiteral("v1.2.0"));
        QCOMPARE(latest.value.assets.size(), 1);
        QCOMPARE(latest.value.assets.first().name, QStringLiteral("harbour-foo-1.2.0-1.aarch64.rpm"));

        const auto armv7 = latestFrom(gl, t, kProject, AppSettings(), "armv7hl");
        QVERIFY2(armv7.ok(), qPrintable(armv7.error.message));
        QCOMPARE(armv7.value.assets.first().name, QStringLiteral("harbour-foo-1.2.0-1.armv7hl.rpm"));

        AppSettings pre;
        pre.set(Keys::includePrereleases, true);
        QCOMPARE(latestFrom(gl, t, kProject, pre, "aarch64").value.version, QStringLiteral("v2.0.0"));
    }

    void errors()
    {
        FakeTransport t;
        GitLabSource gl;
        QCOMPARE(int(fetchFrom(gl, t, kProject).error.kind), int(Error::NotFound));

        t.respondJson(kApi, "{\"message\":\"no id\"}");
        QCOMPARE(int(fetchFrom(gl, t, kProject).error.kind), int(Error::Parse));

        t.respondFixture(kApi, "gitlab/project.json");
        t.respondJson(kApi + "/releases?per_page=100", "[]");
        QCOMPARE(int(fetchFrom(gl, t, kProject).error.kind), int(Error::NoReleases));

        t.respondJson(kApi + "/releases?per_page=100", "{}");
        QCOMPARE(int(fetchFrom(gl, t, kProject).error.kind), int(Error::Parse));

        HttpResponse limited;
        limited.status = 429;
        limited.headers.insert("retry-after", "120");
        t.respond(kApi + "/releases?per_page=100", limited);
        const FetchResult r = fetchFrom(gl, t, kProject);
        QCOMPARE(int(r.error.kind), int(Error::RateLimited));
        QCOMPARE(r.error.retryAfterMinutes, 2);
    }
};

QTEST_GUILESS_MAIN(TestGitLabSource)
#include "tst_gitlabsource.moc"
