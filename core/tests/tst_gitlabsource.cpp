#include "sourcetesthelpers.h"
#include "app/packagebuilds.h"
#include "pipeline/releasepipeline.h"
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

// Whisperfish: releases with only a source archive, RPMs in the package registry.
const QString kWfProject = QStringLiteral("https://gitlab.com/whisperfish/whisperfish");
const QString kWfApi = QStringLiteral("https://gitlab.com/api/v4/projects/whisperfish%2Fwhisperfish");

void serveWhisperfish(FakeTransport &t)
{
    t.respondFixture(kWfApi, "gitlab/project.json");
    t.respondFixture(kWfApi + "/releases?per_page=100", "gitlab/whisperfish-releases.json");
    t.respondFixture(kWfApi + "/packages?package_type=generic&per_page=100&order_by=created_at&sort=desc",
                     "gitlab/packages.json");
    t.respondFixture(kWfApi + "/repository/tags?per_page=100", "gitlab/registry-tags.json");
    for (const char *id : {"72644449", "72644210", "72164507", "72164039", "74544687", "74543719"})
        t.respondFixture(kWfApi + "/packages/" + QLatin1String(id) + "/package_files?per_page=100",
                         QStringLiteral("gitlab/package-files-%1.json").arg(QLatin1String(id)));
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

    void packageVariantsAreGrouped()
    {
        const auto packages = parseGitLabPackages(readFixture("gitlab/packages.json"));
        QVERIFY(packages.ok());
        QCOMPARE(packages.value.size(), 6);
        const QList<GitLabPackageGroup> groups = groupGitLabPackages(packages.value);
        QStringList versions;
        for (const GitLabPackageGroup &g : groups)
            versions << g.version + (g.prerelease ? QStringLiteral(" (pre)") : QString()) + QStringLiteral(" x")
                            + QString::number(g.packages.size());
        // The Harbour variant is a build of the same version, also for dev builds
        // where its word sits in the middle; "dev" itself is not a variant.
        QCOMPARE(versions, (QStringList{"0.6.0-dev.b6184.057dd01a (pre) x2", "0.6.0-beta.46 x2", "0.6.0-beta.45 x2"}));

        // "beta" is a pre-release marker, not a variant of "1.0".
        GitLabPackage a, b;
        a.id = 1; a.name = QStringLiteral("x"); a.version = QStringLiteral("1.0");
        b.id = 2; b.name = QStringLiteral("x"); b.version = QStringLiteral("1.0-beta");
        QCOMPARE(groupGitLabPackages({a, b}).size(), 2);
    }

    void releasesWithoutRpmsUseThePackageRegistry()
    {
        FakeTransport t;
        serveWhisperfish(t);
        GitLabSource gl;
        const FetchResult r = fetchFrom(gl, t, kWfProject);
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        // The release with only a source archive, plus the two builds with RPMs
        // (the dev build is a prerelease nobody asked for: not even fetched).
        QStringList withRpms;
        for (const Release &rel : r.releases) {
            bool any = false;
            for (const Asset &a : rel.assets)
                any = any || a.name.endsWith(QLatin1String(".rpm"));
            if (any)
                withRpms << rel.tag;
        }
        withRpms.sort();
        QCOMPARE(withRpms, (QStringList{"v0.6.0-beta.45", "v0.6.0-beta.46"}));
        for (const QString &id : {QStringLiteral("74544687"), QStringLiteral("74543719")})
            for (const HttpRequest &req : t.requests)
                QVERIFY(!req.url.contains(QStringLiteral("/packages/") + id));

        const Release *beta46 = nullptr;
        for (const Release &rel : r.releases)
            if (rel.tag == QLatin1String("v0.6.0-beta.46"))
                beta46 = &rel;
        QVERIFY(beta46);
        QVERIFY(!beta46->prerelease);
        QCOMPARE(beta46->changelog, QStringLiteral("fix reconnecting issue on new Signal Server"));
        QCOMPARE(beta46->pageUrl, kWfProject + QStringLiteral("/-/tags/v0.6.0-beta.46"));
        // 9 files in each of the two packages: the standard build and the Harbour one.
        QCOMPARE(beta46->assets.size(), 18);
        QStringList urls;
        for (const Asset &a : beta46->assets)
            if (a.name == QLatin1String("harbour-whisperfish-0.6.0.beta.46-1.aarch64.rpm")) {
                urls << a.url;
                QCOMPARE(a.sha256.size(), 64);
                QVERIFY(a.size > 0);
            }
        urls.sort();
        QCOMPARE(urls, (QStringList{kWfApi + "/packages/generic/harbour-whisperfish/0.6.0-beta.46-harbour/"
                                             "harbour-whisperfish-0.6.0.beta.46-1.aarch64.rpm",
                                    kWfApi + "/packages/generic/harbour-whisperfish/0.6.0-beta.46/"
                                             "harbour-whisperfish-0.6.0.beta.46-1.aarch64.rpm"}));

        // Opted in: the dev builds come too, as prereleases.
        AppSettings pre;
        pre.set(Keys::includePrereleases, true);
        FakeTransport t2;
        serveWhisperfish(t2);
        const FetchResult withPre = fetchFrom(gl, t2, kWfProject, pre);
        bool dev = false;
        for (const Release &rel : withPre.releases)
            if (rel.tag.contains(QLatin1String("dev"))) {
                dev = true;
                QVERIFY(rel.prerelease);
            }
        QVERIFY(dev);
    }

    void standardAndHarbourBuildsCanBeChosen()
    {
        FakeTransport t;
        serveWhisperfish(t);
        GitLabSource gl;
        const FetchResult fetched = fetchFrom(gl, t, kWfProject);
        DeviceInfo device;
        device.arch = QStringLiteral("aarch64");
        device.osVersion = QStringLiteral("5.1.0.11");
        AppSettings settings;
        auto latest = resolveLatestRelease(fetched.releases, settings, device);
        QVERIFY2(latest.ok(), qPrintable(latest.error.message));
        QCOMPARE(latest.value.release.tag, QStringLiteral("v0.6.0-beta.46"));
        // debuginfo and debugsource are not installable: one RPM per build.
        QCOMPARE(latest.value.assets.size(), 2);

        // Same file names: the folders tell the builds apart.
        QString package;
        const QList<BuildChoice> choices = buildChoices(latest.value.assets, &package);
        QCOMPARE(package, QStringLiteral("harbour-whisperfish"));
        QCOMPARE(choices.size(), 2);
        QCOMPARE(choices.at(0).assetName, choices.at(1).assetName);
        QVERIFY(choices.at(0).key != choices.at(1).key);
        QStringList labels{choices.at(0).label, choices.at(1).label};
        labels.sort();
        QCOMPARE(labels, (QStringList{"", "harbour"}));

        // Each choice, as a filter, leaves its build and survives the next release.
        for (const BuildChoice &choice : choices) {
            AppSettings chosen;
            chosen.set(Keys::assetFilterRegEx, choice.filter);
            if (choice.invert)
                chosen.set(Keys::invertAssetFilter, true);
            const auto picked = resolveLatestRelease(fetched.releases, chosen, device);
            QVERIFY2(picked.ok(), qPrintable(picked.error.message));
            QCOMPARE(picked.value.assets.size(), 1);
            QCOMPARE(picked.value.assets.first().url, choice.key);
            QVERIFY(buildChoices(picked.value.assets).isEmpty());
            // The next release (other version folders, same layout).
            QList<Asset> next = latest.value.assets;
            for (Asset &a : next)
                a.url.replace(QLatin1String("beta.46"), QLatin1String("beta.47"))
                     .replace(QLatin1String("0.6.0-beta.46"), QLatin1String("0.6.0-beta.47"));
            Release release;
            release.tag = QStringLiteral("v0.6.0-beta.47");
            release.date = QDateTime::currentDateTimeUtc();
            release.assets = next;
            const auto after = resolveLatestRelease({release}, chosen, device);
            QVERIFY2(after.ok(), qPrintable(after.error.message));
            QCOMPARE(after.value.assets.size(), 1);
            const bool harbour = after.value.assets.first().url.contains(QLatin1String("-harbour/"));
            QCOMPARE(harbour, choice.key.contains(QLatin1String("-harbour/")));
        }
    }

    void registryIsLeftAloneWhenReleasesHaveRpms()
    {
        FakeTransport t;
        serveProject(t);
        GitLabSource gl;
        QVERIFY(fetchFrom(gl, t, kProject).error.ok());
        for (const HttpRequest &req : t.requests)
            QVERIFY(!req.url.contains(QLatin1String("/packages")));
    }

    void noRegistryKeepsTheReleaseAnswer()
    {
        // Packages switched off (404): the usual "no package" answer follows.
        FakeTransport t;
        t.respondFixture(kWfApi, "gitlab/project.json");
        t.respondFixture(kWfApi + "/releases?per_page=100", "gitlab/whisperfish-releases.json");
        GitLabSource gl;
        const FetchResult r = fetchFrom(gl, t, kWfProject);
        QVERIFY(r.error.ok());
        QCOMPARE(r.releases.size(), 2);
        for (const Release &rel : r.releases)
            for (const Asset &a : rel.assets)
                QVERIFY(!a.name.endsWith(QLatin1String(".rpm")));

        // No releases at all, but packages: they are the releases.
        FakeTransport t2;
        serveWhisperfish(t2);
        t2.respondJson(kWfApi + "/releases?per_page=100", "[]");
        const FetchResult fromPackages = fetchFrom(gl, t2, kWfProject);
        QVERIFY2(fromPackages.error.ok(), qPrintable(fromPackages.error.message));
        QCOMPARE(fromPackages.releases.size(), 2);
    }

    void trackOnlyFollowsTagsNotPackages()
    {
        FakeTransport t;
        serveWhisperfish(t);
        t.respondFixture(kWfApi + "/repository/tags?per_page=100", "gitlab/tags.json");
        AppSettings s;
        s.set(Keys::trackOnly, true);
        GitLabSource gl;
        QVERIFY(fetchFrom(gl, t, kWfProject, s).error.ok());
        for (const HttpRequest &req : t.requests)
            QVERIFY(!req.url.contains(QLatin1String("/packages")));
    }
};

QTEST_GUILESS_MAIN(TestGitLabSource)
#include "tst_gitlabsource.moc"
