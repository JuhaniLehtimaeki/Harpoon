#include "faketransport.h"
#include "pipeline/releasepipeline.h"
#include "sources/forgejosource.h"
#include "sources/githubsource.h"
#include "sources/gitlabsource.h"
#include "sources/htmlsource.h"
#include "sources/sourceregistry.h"

#include <QtTest>

using namespace Harpoon;

namespace {
const QString kChumApi = QStringLiteral("https://api.github.com/repos/sailfishos-chum/sailfishos-chum-gui");

FetchResult fetch(Source &source, FakeTransport &transport, const QString &url, const AppSettings &settings = {})
{
    FetchResult out;
    bool called = false;
    source.fetchReleases(url, settings, transport, [&](const FetchResult &r) {
        out = r;
        called = true;
    });
    if (!called)
        qFatal("callback not called");
    return out;
}
} // namespace

class TestSources : public QObject
{
    Q_OBJECT
private slots:
    void preStandardize()
    {
        QCOMPARE(preStandardizeUrl("  github.com//a//b "), QStringLiteral("https://github.com/a/b"));
        QCOMPARE(preStandardizeUrl("https://x.org/a?u=http://y"), QStringLiteral("https://x.org/a?u=http://y"));
    }

    void registryMatching_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("override");
        QTest::addColumn<QString>("sourceId");
        QTest::addColumn<QString>("standard");

        QTest::newRow("github") << "https://github.com/sailfishos-chum/sailfishos-chum-gui/releases/tag/0.6.12-1"
                                << "" << "GitHub" << "https://github.com/sailfishos-chum/sailfishos-chum-gui";
        QTest::newRow("github www no scheme .git") << "www.github.com/Owner/Repo.git" << ""
                                                   << "GitHub" << "https://www.github.com/Owner/Repo";
        QTest::newRow("codeberg") << "https://codeberg.org/fishdev/harbour-tides/releases" << ""
                                  << "Forgejo" << "https://codeberg.org/fishdev/harbour-tides";
        QTest::newRow("self-hosted forgejo") << "https://git.example.net/me/app/src/branch/main" << "Forgejo"
                                             << "Forgejo" << "https://git.example.net/me/app";
        QTest::newRow("gitea with port") << "http://192.168.1.5:3000/me/app/releases" << "Forgejo"
                                         << "Forgejo" << "http://192.168.1.5:3000/me/app";
        QTest::newRow("github enterprise") << "https://ghe.corp.example/team/tool" << "GitHub"
                                           << "GitHub" << "https://ghe.corp.example/team/tool";
        QTest::newRow("gitlab subgroup") << "https://gitlab.com/grp/sub/proj/-/releases" << ""
                                         << "GitLab" << "https://gitlab.com/grp/sub/proj";
        QTest::newRow("self-hosted gitlab") << "https://git.example.org/team/app/-/tags" << "GitLab"
                                            << "GitLab" << "https://git.example.org/team/app";
        QTest::newRow("sourcehut") << "https://git.sr.ht/~me/app/refs" << ""
                                   << "SourceHut" << "https://git.sr.ht/~me/app";
        QTest::newRow("sourceforge") << "https://sourceforge.net/p/myapp/" << ""
                                     << "SourceForge" << "https://sourceforge.net/projects/myapp/files";
        QTest::newRow("jenkins override") << "https://ci.example.org/job/app/lastBuild/" << "Jenkins"
                                          << "Jenkins" << "https://ci.example.org/job/app";
        QTest::newRow("rpm-md override") << "https://repo.example.org/sfos/repodata/repomd.xml?package=foo"
                                         << "RpmMdRepo" << "RpmMdRepo" << "https://repo.example.org/sfos?package=foo";
        QTest::newRow("direct rpm link") << "https://example.com/dl/app-1.0-1.noarch.rpm" << ""
                                         << "DirectLink" << "https://example.com/dl/app-1.0-1.noarch.rpm";
        QTest::newRow("direct rpm link with query") << "https://example.com/get/app.rpm?mirror=1" << ""
                                                    << "DirectLink" << "https://example.com/get/app.rpm?mirror=1";
        QTest::newRow("html fallback") << "example.com/downloads/" << ""
                                       << "HTML" << "https://example.com/downloads/";
        QTest::newRow("html override on a forge") << "https://github.com/a/b/releases" << "HTML"
                                                  << "HTML" << "https://github.com/a/b/releases";
    }

    void registryMatching()
    {
        QFETCH(QString, url);
        QFETCH(QString, override);
        QFETCH(QString, sourceId);
        QFETCH(QString, standard);
        SourceRegistry registry;
        const auto m = registry.match(url, override);
        QVERIFY2(m.ok(), qPrintable(m.error.message));
        QCOMPARE(m.value.source->id(), sourceId);
        QCOMPARE(m.value.standardUrl, standard);
    }

    void registryErrors()
    {
        SourceRegistry registry;
        // HTML is the catch-all, so an unknown host is no longer unsupported.
        QCOMPARE(registry.match("https://example.com/a/b").value.source->id(), QStringLiteral("HTML"));
        QCOMPARE(int(registry.match("ftp://example.com/a.rpm").error.kind), int(Error::UnsupportedUrl));
        QCOMPARE(int(registry.match("https://github.com/only-owner").error.kind), int(Error::InvalidUrl));
        QCOMPARE(int(registry.match("https://github.com/a/b", "Nope").error.kind), int(Error::UnsupportedUrl));
        QCOMPARE(int(registry.match("https://example.com/build", "Jenkins").error.kind), int(Error::InvalidUrl));
        // Jenkins-looking URLs are not auto-selected.
        QCOMPARE(registry.match("https://ci.example.org/job/app").value.source->id(), QStringLiteral("HTML"));
        QCOMPARE(registry.ids(), (QStringList{"Forgejo", "GitHub", "GitLab", "Jenkins", "RpmMdRepo", "SourceForge",
                                              "SourceHut", "DirectLink", "HTML"}));
    }

    void apiUrls()
    {
        GitHubSource gh;
        QCOMPARE(gh.apiBaseUrl("https://github.com/a/b"), QStringLiteral("https://api.github.com/repos/a/b"));
        QCOMPARE(gh.apiBaseUrl("https://ghe.corp.example/a/b"), QStringLiteral("https://ghe.corp.example/api/v3/repos/a/b"));
        ForgejoSource fj;
        QCOMPARE(fj.apiBaseUrl("https://codeberg.org/a/b"), QStringLiteral("https://codeberg.org/api/v1/repos/a/b"));
        QCOMPARE(fj.apiBaseUrl("http://192.168.1.5:3000/a/b"), QStringLiteral("http://192.168.1.5:3000/api/v1/repos/a/b"));
    }

    void githubFetch()
    {
        FakeTransport t;
        t.respondFixture(kChumApi + "/releases?per_page=100", "github/chum-gui-releases.json");
        GitHubSource gh;
        const FetchResult r = fetch(gh, t, "https://github.com/sailfishos-chum/sailfishos-chum-gui");
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        QCOMPARE(r.releases.size(), 4);
        QCOMPARE(t.requests.size(), 1);
        QVERIFY(t.requests.first().header("Authorization").isEmpty());
        QVERIFY(t.requests.first().header("Accept").contains("application/vnd.github+json"));

        const Asset &a = r.releases.at(2).assets.first();
        QCOMPARE(a.apiUrl, QStringLiteral("https://api.github.com/repos/sailfishos-chum/sailfishos-chum-gui/releases/assets/2801"));
        QCOMPARE(a.size, qint64(212345));
        QCOMPARE(a.updatedAt, QDateTime::fromString("2025-02-10T12:11:00Z", Qt::ISODate));
    }

    void githubTokenAndRetryWithout401()
    {
        FakeTransport t;
        t.handler = [](const HttpRequest &req, HttpResponse *resp) {
            if (!req.header("Authorization").isEmpty()) {
                resp->status = 401;
                return true;
            }
            return false;
        };
        t.respondJson(kChumApi + "/releases?per_page=100", "[]");
        GitHubSource gh;
        gh.setConfig({{"token", "secret"}});
        const FetchResult r = fetch(gh, t, "https://github.com/sailfishos-chum/sailfishos-chum-gui");
        QCOMPARE(t.requests.size(), 2);
        QCOMPARE(t.requests.at(0).header("Authorization"), QByteArray("Bearer secret"));
        QVERIFY(t.requests.at(1).header("Authorization").isEmpty());
        QCOMPARE(int(r.error.kind), int(Error::NoReleases));
    }

    void githubRateLimit()
    {
        FakeTransport t;
        HttpResponse limited;
        limited.status = 403;
        limited.headers.insert("x-ratelimit-remaining", "0");
        limited.headers.insert("retry-after", "120");
        t.respond(kChumApi + "/releases?per_page=100", limited);
        GitHubSource gh;
        const FetchResult r = fetch(gh, t, "https://github.com/sailfishos-chum/sailfishos-chum-gui");
        QCOMPARE(int(r.error.kind), int(Error::RateLimited));
        QVERIFY(r.error.retryAfterMinutes >= 1);
    }

    void githubNotFound()
    {
        FakeTransport t;
        GitHubSource gh;
        const FetchResult r = fetch(gh, t, "https://github.com/nobody/nothing");
        QCOMPARE(int(r.error.kind), int(Error::NotFound));
    }

    void githubNetworkError()
    {
        FakeTransport t;
        t.handler = [](const HttpRequest &, HttpResponse *resp) {
            resp->status = 0;
            resp->networkError = QStringLiteral("Host not found");
            return true;
        };
        GitHubSource gh;
        const FetchResult r = fetch(gh, t, "https://github.com/a/b");
        QCOMPARE(int(r.error.kind), int(Error::Network));
        QCOMPARE(r.error.message, QStringLiteral("Host not found"));
    }

    void githubTagsFallbackForTrackOnly()
    {
        FakeTransport t;
        t.respondJson("https://api.github.com/repos/example/lib/releases?per_page=100", "[]");
        t.respondFixture("https://api.github.com/repos/example/lib/tags?per_page=100", "github/tags.json");
        GitHubSource gh;

        FetchResult r = fetch(gh, t, "https://github.com/example/lib");
        QCOMPARE(int(r.error.kind), int(Error::NoReleases)); // not track-only: no fallback

        AppSettings s;
        s.set(Keys::trackOnly, true);
        r = fetch(gh, t, "https://github.com/example/lib", s);
        QVERIFY(r.error.ok());
        QCOMPARE(r.releases.size(), 2);
        QCOMPARE(r.releases.first().tag, QStringLiteral("v1.4.0"));
    }

    void githubBadJson()
    {
        FakeTransport t;
        t.respondJson("https://api.github.com/repos/a/b/releases?per_page=100", "{\"message\":\"odd\"}");
        GitHubSource gh;
        QCOMPARE(int(fetch(gh, t, "https://github.com/a/b").error.kind), int(Error::Parse));
    }

    void forgejoFetchAndPipeline()
    {
        FakeTransport t;
        t.respondFixture("https://codeberg.org/api/v1/repos/fishdev/harbour-tides/releases?per_page=100",
                         "forgejo/releases.json");
        ForgejoSource fj;
        fj.setConfig({{"token", "abc"}});
        DeviceInfo device;
        device.arch = QStringLiteral("armv7hl");

        Result<LatestRelease> latest;
        fetchLatestRelease(fj, t, "https://codeberg.org/fishdev/harbour-tides", AppSettings(), device,
                           [&](const Result<LatestRelease> &r) { latest = r; });
        QVERIFY2(latest.ok(), qPrintable(latest.error.message));
        QCOMPARE(t.requests.first().header("Authorization"), QByteArray("token abc"));
        QCOMPARE(latest.value.version, QStringLiteral("v3.0.1"));
        QCOMPARE(latest.value.assets.size(), 1);
        const Asset &a = latest.value.assets.first();
        QCOMPARE(a.name, QStringLiteral("harbour-tides-3.0.1-1.armv7hl.rpm"));
        QVERIFY(a.apiUrl.isEmpty());
        QCOMPARE(a.url, QStringLiteral("https://codeberg.org/fishdev/harbour-tides/releases/download/v3.0.1/harbour-tides-3.0.1-1.armv7hl.rpm"));
        QCOMPARE(a.updatedAt, QDateTime::fromString("2026-05-10T07:03:00Z", Qt::ISODate)); // created_at fallback
    }

    void prepareDownloads()
    {
        const QString ghRepo = QStringLiteral("https://github.com/o/r");
        Asset asset;
        asset.name = QStringLiteral("a-1-1.aarch64.rpm");
        asset.url = QStringLiteral("https://github.com/o/r/releases/download/v1/a-1-1.aarch64.rpm");
        asset.apiUrl = QStringLiteral("https://api.github.com/repos/o/r/releases/assets/7");

        GitHubSource gh;
        DownloadRequest plain;
        plain.url = asset.url;
        gh.prepareDownload(asset, AppSettings(), ghRepo, plain);
        QCOMPARE(plain.url, asset.url); // no token: public URL, no headers
        QVERIFY(plain.headers.isEmpty());

        gh.setConfig({{QStringLiteral("token"), QStringLiteral("t0k")}});
        DownloadRequest authed;
        authed.url = asset.url;
        gh.prepareDownload(asset, AppSettings(), ghRepo, authed);
        QCOMPARE(authed.url, asset.apiUrl);
        QVERIFY(authed.headers.contains(qMakePair(QByteArray("Authorization"), QByteArray("Bearer t0k"))));
        QVERIFY(authed.headers.contains(qMakePair(QByteArray("Accept"), QByteArray("application/octet-stream"))));

        // A record pointing the asset at another host (e.g. a crafted backup)
        // must not receive the token.
        Asset foreign = asset;
        foreign.url = QStringLiteral("https://evil.example/a.rpm");
        foreign.apiUrl = QStringLiteral("https://evil.example/api/asset");
        DownloadRequest foreignReq;
        foreignReq.url = foreign.url;
        gh.prepareDownload(foreign, AppSettings(), ghRepo, foreignReq);
        QCOMPARE(foreignReq.url, foreign.url);
        QVERIFY(foreignReq.headers.isEmpty());

        ForgejoSource fj;
        fj.setConfig({{QStringLiteral("token"), QStringLiteral("abc")}});
        Asset fjAsset;
        fjAsset.url = QStringLiteral("https://codeberg.org/o/r/releases/download/v1/a.rpm");
        DownloadRequest fjReq;
        fjReq.url = fjAsset.url;
        fj.prepareDownload(fjAsset, AppSettings(), QStringLiteral("https://codeberg.org/o/r"), fjReq);
        QCOMPARE(fjReq.url, fjAsset.url);
        QVERIFY(fjReq.headers.contains(qMakePair(QByteArray("Authorization"), QByteArray("token abc"))));

        GitLabSource gl;
        gl.setConfig({{QStringLiteral("token"), QStringLiteral("glpat")}});
        const QString glProject = QStringLiteral("https://gitlab.com/g/p");
        Asset glAsset;
        glAsset.url = QStringLiteral("https://gitlab.com/g/p/-/package_files/1/download");
        DownloadRequest glReq;
        glReq.url = glAsset.url;
        gl.prepareDownload(glAsset, AppSettings(), glProject, glReq);
        QVERIFY(glReq.url.contains(QLatin1String("private_token=glpat")));
        Asset glForeign;
        glForeign.url = QStringLiteral("https://cdn.example.com/p.rpm"); // release links can point anywhere
        DownloadRequest glForeignReq;
        glForeignReq.url = glForeign.url;
        gl.prepareDownload(glForeign, AppSettings(), glProject, glForeignReq);
        QCOMPARE(glForeignReq.url, glForeign.url);

        HtmlSource html;
        AppSettings s;
        s.set(Keys::requestHeader, QStringLiteral("Cookie: consent=yes\nX-Thing: 1"));
        DownloadRequest htmlReq;
        htmlReq.url = QStringLiteral("https://example.org/files/a.rpm");
        html.prepareDownload(Asset(), s, QStringLiteral("https://example.org/downloads/"), htmlReq);
        QVERIFY(htmlReq.headers.contains(qMakePair(QByteArray("Cookie"), QByteArray("consent=yes"))));
        QVERIFY(htmlReq.headers.contains(qMakePair(QByteArray("X-Thing"), QByteArray("1"))));
        DownloadRequest htmlForeign;
        htmlForeign.url = QStringLiteral("https://mirror.other.net/a.rpm");
        html.prepareDownload(Asset(), s, QStringLiteral("https://example.org/downloads/"), htmlForeign);
        QVERIFY(htmlForeign.headers.isEmpty());
    }

    void tokenKeysArePerHost()
    {
        SourceRegistry registry;
        QCOMPARE(registry.match(QStringLiteral("https://github.com/a/b")).value.source->tokenKey(), QStringLiteral("GitHub"));
        QCOMPARE(registry.match(QStringLiteral("https://codeberg.org/a/b")).value.source->tokenKey(), QStringLiteral("Forgejo"));
        QCOMPARE(registry.match(QStringLiteral("https://git.example.org/a/b"), QStringLiteral("Forgejo")).value.source->tokenKey(),
                 QStringLiteral("Forgejo@git.example.org"));
        QCOMPARE(registry.match(QStringLiteral("https://evil.example/a/b"), QStringLiteral("GitHub")).value.source->tokenKey(),
                 QStringLiteral("GitHub@evil.example"));
        QCOMPARE(registry.match(QStringLiteral("http://10.0.0.2:3000/a/b"), QStringLiteral("Forgejo")).value.source->tokenKey(),
                 QStringLiteral("Forgejo@10.0.0.2:3000"));
        QVERIFY(Source::isOwnOrigin(QStringLiteral("https://GitHub.com/x"), QStringLiteral("https://github.com/o/r")));
        QVERIFY(!Source::isOwnOrigin(QStringLiteral("http://github.com/x"), QStringLiteral("https://github.com/o/r")));
        QVERIFY(!Source::isOwnOrigin(QStringLiteral("https://github.com:444/x"), QStringLiteral("https://github.com/o/r")));
        QVERIFY(!Source::isOwnOrigin(QStringLiteral("https://github.com.evil.example/x"), QStringLiteral("https://github.com/o/r")));
    }

    void forgejo403IsNotRateLimit()
    {
        FakeTransport t;
        t.respondJson("https://codeberg.org/api/v1/repos/a/b/releases?per_page=100", "{}", 403);
        ForgejoSource fj;
        QCOMPARE(int(fetch(fj, t, "https://codeberg.org/a/b").error.kind), int(Error::Http));
    }
};

QTEST_GUILESS_MAIN(TestSources)
#include "tst_sources.moc"
