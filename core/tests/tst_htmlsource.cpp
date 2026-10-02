#include "sourcetesthelpers.h"
#include "sources/directlinksource.h"
#include "sources/htmlsource.h"
#include "sources/sourceutil.h"

#include <QtTest>

using namespace Harpoon;

namespace {
const QString kIndex = QStringLiteral("https://example.org/harbour-foo/");
const QString kDir = kIndex;

QStringList urls(const QList<PageLink> &links)
{
    QStringList out;
    for (const PageLink &l : links)
        out << l.url.mid(l.url.lastIndexOf(QLatin1Char('/')) + 1);
    return out;
}

HttpResponse withEtag(const QByteArray &etag, int status = 206)
{
    HttpResponse r;
    r.status = status;
    if (!etag.isEmpty())
        r.headers.insert("etag", etag);
    return r;
}

AppSettings linkHash()
{
    AppSettings s;
    s.set(Keys::defaultPseudoVersioningMethod, "linkHash");
    return s;
}
} // namespace

class TestHtmlSource : public QObject
{
    Q_OBJECT
private slots:
    // ---- link extraction -------------------------------------------------

    void anchors()
    {
        const auto links = extractAnchorLinks(
            "<p><a href=\"a.rpm\">A <b>file</b></a> <a class=x href='b.rpm'>B</a>"
            "<a href=c.rpm>C</a><a name=anchor>none</a><a\nhref=\"d.rpm?x=1&amp;y=2\">D &amp; more</a>"
            "<a href=\"e.rpm\">unclosed <a href=\"f.rpm\"></a><!-- <a href=\"g.rpm\">g</a> --></p>");
        QCOMPARE(links.size(), 5 + 1);
        QCOMPARE(links.at(0).url, QStringLiteral("a.rpm"));
        QCOMPARE(links.at(0).text, QStringLiteral("A file"));
        QCOMPARE(links.at(1).url, QStringLiteral("b.rpm"));
        QCOMPARE(links.at(2).url, QStringLiteral("c.rpm"));
        QCOMPARE(links.at(3).url, QStringLiteral("d.rpm?x=1&y=2"));
        QCOMPARE(links.at(3).text, QStringLiteral("D & more"));
        QCOMPARE(links.at(4).text, QStringLiteral("unclosed"));
        QCOMPARE(links.at(5).url, QStringLiteral("f.rpm"));
        QCOMPARE(links.at(5).text, QString());
    }

    void urlHelpers()
    {
        QCOMPARE(resolveUrl("https://x.org/dir/page.html", "a.rpm"), QStringLiteral("https://x.org/dir/a.rpm"));
        QCOMPARE(resolveUrl("https://x.org/dir/", "/abs/a.rpm"), QStringLiteral("https://x.org/abs/a.rpm"));
        QCOMPARE(resolveUrl("https://x.org/dir/", "//cdn.org/a.rpm"), QStringLiteral("https://cdn.org/a.rpm"));
        QCOMPARE(resolveUrl("https://x.org/dir/", " https://y.org/b c.rpm "), QStringLiteral("https://y.org/b c.rpm"));
        QCOMPARE(lastPathSegment("https://x.org/a/harbour%20foo.rpm?x=1"), QStringLiteral("harbour foo.rpm"));
        QCOMPARE(extractUrlsInText("see https://x.org/a.rpm, or \"ftp://y.org/b\" <http://z.org/c>"),
                 (QStringList{"https://x.org/a.rpm,", "ftp://y.org/b", "http://z.org/c"}));
        QCOMPARE(shortSha256("abc"), QStringLiteral("ba7816bf8f01"));
    }

    void requestHeaders()
    {
        auto h = parseRequestHeaders(QStringLiteral("User-Agent: Custom/1.0\nX-Token: a:b\ninvalid\n: novalue"));
        QCOMPARE(h.size(), 2);
        QCOMPARE(h.at(0).first, QByteArray("User-Agent"));
        QCOMPARE(h.at(1).second, QByteArray("a:b"));
        // ObtainX shape: a list of {"requestHeader": "..."} maps
        QVariantMap one;
        one.insert("requestHeader", "Accept: text/html");
        h = parseRequestHeaders(QVariantList{one, QStringLiteral("X-A: 1")});
        QCOMPARE(h.size(), 2);
        QCOMPARE(h.at(0).second, QByteArray("text/html"));
        QVERIFY(parseRequestHeaders(QVariant()).isEmpty());
    }

    void grabLinksDefaults()
    {
        const auto links = grabLinks(QString::fromUtf8(readFixture("html/index.html")), kIndex, AppSettings());
        QVERIFY(links.ok());
        // Installable RPMs only (no src, debuginfo, text or navigation links),
        // in natural order: 1.2 < 1.9 < 1.10.
        QCOMPARE(urls(links.value), (QStringList{"harbour-foo-1.2-1.aarch64.rpm", "harbour-foo-1.9-1.aarch64.rpm",
                                                 "harbour-foo-1.9-1.armv7hl.rpm", "harbour-foo-1.10-1.aarch64.rpm",
                                                 "harbour-foo-1.10-1.armv7hl.rpm"}));
        QCOMPARE(links.value.first().url, kDir + "harbour-foo-1.2-1.aarch64.rpm");
        QCOMPARE(links.value.at(2).text, QStringLiteral("harbour-foo-1.9-1.armv7hl.rpm"));
    }

    void grabLinksOptions()
    {
        const QString page = QString::fromUtf8(readFixture("html/index.html"));
        AppSettings s;
        s.set(Keys::skipSort, true);
        QCOMPARE(urls(grabLinks(page, kIndex, s).value).last(), QStringLiteral("harbour-foo-1.9-1.armv7hl.rpm"));
        s.set(Keys::reverseSort, true);
        QCOMPARE(urls(grabLinks(page, kIndex, s).value).first(), QStringLiteral("harbour-foo-1.9-1.armv7hl.rpm"));

        AppSettings custom;
        custom.set(Keys::customLinkFilterRegex, "CHANGES|\\.src\\.rpm$");
        QCOMPARE(urls(grabLinks(page, kIndex, custom).value),
                 (QStringList{"CHANGES.txt", "harbour-foo-1.10-1.src.rpm"}));

        AppSettings byText;
        byText.set(Keys::customLinkFilterRegex, "^Mirror$");
        byText.set(Keys::filterByLinkText, true);
        const auto mirror = grabLinks(QString::fromUtf8(readFixture("html/releases-1.1.html")),
                                      "https://example.org/foo/releases/1.1/", byText);
        QCOMPARE(mirror.value.size(), 1);
        QCOMPARE(mirror.value.first().url, QStringLiteral("https://mirror.example.org/harbour%20foo/harbour-foo-1.1-1.noarch.rpm"));

        AppSettings invalid;
        invalid.set(Keys::customLinkFilterRegex, "([");
        QCOMPARE(int(grabLinks(page, kIndex, invalid).error.kind), int(Error::InvalidSetting));
    }

    void grabLinksSortByLastSegment()
    {
        // Full URLs hold two versions each, so they sort naturally by folder
        // (2.0 < 10.0); the file names sort by version (0.5 < 1.0).
        const QString page = "<a href=\"/2.0/app-1.0.rpm\">x</a><a href=\"/10.0/app-0.5.rpm\">y</a>";
        QCOMPARE(urls(grabLinks(page, "https://h.org/", AppSettings()).value).last(), QStringLiteral("app-0.5.rpm"));
        AppSettings s;
        s.set(Keys::sortByLastLinkSegment, true);
        QCOMPARE(urls(grabLinks(page, "https://h.org/", s).value).last(), QStringLiteral("app-1.0.rpm"));
    }

    void grabLinksVersionAwareSort()
    {
        // Natural order would put "1.0-rc1" after "1.0"; version order knows
        // the release candidate is older.
        const QString page = "<a href=\"1.0.rpm\">a</a><a href=\"1.0-rc1.rpm\">b</a><a href=\"0.9.rpm\">c</a>";
        AppSettings s;
        s.set(Keys::sortByLastLinkSegment, true);
        QCOMPARE(urls(grabLinks(page, "https://h.org/d/", s).value),
                 (QStringList{"0.9.rpm", "1.0-rc1.rpm", "1.0.rpm"}));
    }

    void grabLinksOutsideAnchors()
    {
        const QString json = QString::fromUtf8(readFixture("html/api.json"));
        const auto links = grabLinks(json, "https://example.org/api/foo.json", AppSettings());
        QCOMPARE(urls(links.value), (QStringList{"harbour-foo-2.0.0-1.aarch64.rpm", "harbour-foo-2.1.0-1.aarch64.rpm",
                                                 "harbour-foo-2.1.0-1.armv7hl.rpm"}));
        // Relative paths in JSON are resolved when no absolute URL exists.
        const auto relative = grabLinks("{\"files\":[\"/dl/app-1.0.noarch.rpm\"]}", "https://h.org/api", AppSettings());
        QCOMPARE(relative.value.first().url, QStringLiteral("https://h.org/dl/app-1.0.noarch.rpm"));
        // Plain text, or HTML with matchLinksOutsideATags.
        const auto text = grabLinks("Get it at https://h.org/app-1.0.noarch.rpm today", "https://h.org/", AppSettings());
        QCOMPARE(text.value.size(), 1);
        AppSettings outside;
        outside.set(Keys::matchLinksOutsideATags, true);
        const auto mixed = grabLinks("<a href=\"x.rpm\">x</a> <code>https://cdn.h.org/app-2.0.noarch.rpm</code>",
                                     "https://h.org/", outside);
        QCOMPARE(urls(mixed.value), QStringList{"app-2.0.noarch.rpm"});
    }

    // ---- HtmlSource ------------------------------------------------------

    void standardize()
    {
        HtmlSource html;
        QCOMPARE(html.standardizeUrl("example.org//downloads/").value, QStringLiteral("https://example.org/downloads/"));
        QVERIFY(html.matchesUrlShape("https://anything.example/x?y=1"));
        QCOMPARE(int(html.standardizeUrl("ftp://example.org/x").error.kind), int(Error::InvalidUrl));
        QVERIFY(!html.matchesUrlShape("ftp://example.org/app.rpm"));
    }

    void fetchWithEtagPseudoVersion()
    {
        FakeTransport t;
        t.respondFixture(kIndex, "html/index.html");
        t.respond(kDir + "harbour-foo-1.10-1.armv7hl.rpm", withEtag("\"5f2a-61b\""));
        HtmlSource html;
        const FetchResult r = fetchFrom(html, t, kIndex);
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        QCOMPARE(r.releases.size(), 1);
        const Release &rel = r.releases.first();
        QCOMPARE(rel.tag, shortSha256("5f2a-61b"));
        QCOMPARE(rel.pageUrl, kIndex);
        // The selected (last) link and its other-arch sibling.
        QCOMPARE(assetNames(rel), (QStringList{"harbour-foo-1.10-1.aarch64.rpm", "harbour-foo-1.10-1.armv7hl.rpm"}));
        QCOMPARE(rel.assets.first().url, kDir + "harbour-foo-1.10-1.aarch64.rpm");

        QCOMPARE(t.requests.size(), 2);
        QVERIFY(t.requests.first().header("User-Agent").isEmpty()); // transport default, not Android Chrome
        QCOMPARE(t.requests.at(1).url, kDir + "harbour-foo-1.10-1.armv7hl.rpm");
        QCOMPARE(t.requests.at(1).header("Range"), QByteArray("bytes=0-0"));
    }

    void etagMissingFallsBackToLinkHash()
    {
        FakeTransport t;
        t.respondFixture(kIndex, "html/index.html");
        t.respond(kDir + "harbour-foo-1.10-1.armv7hl.rpm", withEtag(QByteArray(), 200));
        HtmlSource html;
        const FetchResult r = fetchFrom(html, t, kIndex);
        QVERIFY(r.error.ok());
        QCOMPARE(r.releases.first().tag, shortSha256((kDir + "harbour-foo-1.10-1.armv7hl.rpm").toUtf8()));
    }

    void linkHashSkipsTheProbe()
    {
        FakeTransport t;
        t.respondFixture(kIndex, "html/index.html");
        HtmlSource html;
        const FetchResult r = fetchFrom(html, t, kIndex, linkHash());
        QVERIFY(r.error.ok());
        QCOMPARE(t.requests.size(), 1);
        QCOMPARE(r.releases.first().tag, shortSha256((kDir + "harbour-foo-1.10-1.armv7hl.rpm").toUtf8()));
    }

    void versionFromLinkIsExtractedOnceByPipeline()
    {
        FakeTransport t;
        t.respondFixture(kIndex, "html/index.html");
        HtmlSource html;
        AppSettings s;
        s.set(Keys::versionExtractionRegEx, "harbour-foo-([0-9.]+)-");
        s.set(Keys::matchGroupToUse, "$1");
        const FetchResult r = fetchFrom(html, t, kIndex, s);
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        QCOMPARE(t.requests.size(), 1); // a real version needs no probe
        QCOMPARE(r.releases.first().tag, kDir + "harbour-foo-1.10-1.armv7hl.rpm");
        QCOMPARE(r.releases.first().assets.size(), 2); // same extracted version

        const auto latest = latestFrom(html, t, kIndex, s, "aarch64");
        QVERIFY2(latest.ok(), qPrintable(latest.error.message));
        QCOMPARE(latest.value.version, QStringLiteral("1.10"));
        QCOMPARE(latest.value.assets.size(), 1);
        QCOMPARE(latest.value.assets.first().name, QStringLiteral("harbour-foo-1.10-1.aarch64.rpm"));
    }

    void versionFromWholePage()
    {
        FakeTransport t;
        t.respondFixture(kIndex, "html/index.html");
        HtmlSource html;
        AppSettings s;
        s.set(Keys::versionExtractWholePage, true);
        s.set(Keys::versionExtractionRegEx, "Latest version: ([0-9.]+)");
        s.set(Keys::matchGroupToUse, "1");
        FetchResult r = fetchFrom(html, t, kIndex, s);
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        QCOMPARE(r.releases.first().tag, QStringLiteral("Latest version: 1.10")); // shortened to the match
        QCOMPARE(latestFrom(html, t, kIndex, s, "aarch64").value.version, QStringLiteral("1.10"));

        // A lookbehind cannot re-match its own match, so the tag keeps the page.
        s.set(Keys::versionExtractionRegEx, "(?<=Latest version: )[0-9.]+");
        s.set(Keys::matchGroupToUse, "");
        r = fetchFrom(html, t, kIndex, s);
        QVERIFY(r.releases.first().tag.contains("Index of /harbour-foo"));
        QVERIFY(!r.releases.first().tag.contains(QLatin1Char('\n')));
        QCOMPARE(latestFrom(html, t, kIndex, s, "aarch64").value.version, QStringLiteral("1.10"));

        s.set(Keys::versionExtractionRegEx, "Nightly: ([0-9]+)");
        QCOMPARE(int(fetchFrom(html, t, kIndex, s).error.kind), int(Error::NoVersion));
    }

    void sortOptionsPickTheSelectedGroup()
    {
        FakeTransport t;
        t.respondFixture(kIndex, "html/index.html");
        HtmlSource html;
        AppSettings s = linkHash();
        s.set(Keys::reverseSort, true);
        QCOMPARE(assetNames(fetchFrom(html, t, kIndex, s).releases.first()),
                 QStringList{"harbour-foo-1.2-1.aarch64.rpm"});

        s = linkHash();
        s.set(Keys::skipSort, true);
        QCOMPARE(assetNames(fetchFrom(html, t, kIndex, s).releases.first()),
                 (QStringList{"harbour-foo-1.9-1.aarch64.rpm", "harbour-foo-1.9-1.armv7hl.rpm"}));

        s = linkHash();
        s.set(Keys::customLinkFilterRegex, "1\\.9-1");
        QCOMPARE(fetchFrom(html, t, kIndex, s).releases.first().assets.size(), 2);
    }

    void assetFilterAppliesBeforeSelection()
    {
        FakeTransport t;
        t.respondFixture(kIndex, "html/index.html");
        HtmlSource html;
        AppSettings s = linkHash();
        s.set(Keys::assetFilterRegEx, "armv7hl");
        s.set(Keys::invertAssetFilter, true);
        const FetchResult r = fetchFrom(html, t, kIndex, s);
        QCOMPARE(assetNames(r.releases.first()), QStringList{"harbour-foo-1.10-1.aarch64.rpm"});
        s.set(Keys::assetFilterRegEx, "x86_64");
        s.set(Keys::invertAssetFilter, false);
        QCOMPARE(int(fetchFrom(html, t, kIndex, s).error.kind), int(Error::NoReleases));
    }

    void intermediateLinks()
    {
        FakeTransport t;
        t.respondFixture("https://example.org/foo/", "html/releases-root.html");
        t.respondFixture("https://example.org/foo/releases/1.1/", "html/releases-1.1.html");
        HtmlSource html;
        AppSettings s = linkHash();
        QVariantMap hop;
        hop.insert(Keys::customLinkFilterRegex, "releases/[0-9.]+/$");
        QVariantMap ignored; // no regex: skipped, as upstream
        ignored.insert(Keys::skipSort, true);
        s.set(Keys::intermediateLink, QVariantList{ignored, hop});
        s.set(Keys::requestHeader, "X-Api-Key: k1");

        const FetchResult r = fetchFrom(html, t, "https://example.org/foo/", s);
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        QCOMPARE(t.requests.size(), 2);
        QCOMPARE(t.requests.at(1).url, QStringLiteral("https://example.org/foo/releases/1.1/"));
        QCOMPARE(t.requests.at(1).header("X-Api-Key"), QByteArray("k1"));
        const Release &rel = r.releases.first();
        QCOMPARE(rel.pageUrl, QStringLiteral("https://example.org/foo/releases/1.1/"));
        QCOMPARE(rel.assets.size(), 1);
        QCOMPARE(rel.assets.first().name, QStringLiteral("harbour-foo-1.1-1.noarch.rpm"));
        QCOMPARE(rel.assets.first().url, QStringLiteral("https://mirror.example.org/harbour%20foo/harbour-foo-1.1-1.noarch.rpm"));

        hop.insert(Keys::customLinkFilterRegex, "releases/9\\.9/");
        s.set(Keys::intermediateLink, QVariantList{hop});
        QCOMPARE(int(fetchFrom(html, t, "https://example.org/foo/", s).error.kind), int(Error::NoReleases));
    }

    void intermediateLinksStopAfterTen()
    {
        FakeTransport t;
        t.handler = [](const HttpRequest &req, HttpResponse *resp) {
            if (!req.url.startsWith("https://loop.example/"))
                return false;
            resp->status = 200;
            resp->body = "<a href=\"p\">again</a>";
            return true;
        };
        HtmlSource html;
        QVariantMap hop;
        hop.insert(Keys::customLinkFilterRegex, ".");
        QVariantList hops;
        for (int i = 0; i < 12; ++i)
            hops << hop;
        AppSettings s;
        s.set(Keys::intermediateLink, hops);
        const FetchResult r = fetchFrom(html, t, "https://loop.example/p", s);
        QCOMPARE(t.requests.size(), 11); // 10 hops + the final page
        QCOMPARE(int(r.error.kind), int(Error::NoReleases));
    }

    void ambiguousDownloadLinksGetAnRpmName()
    {
        FakeTransport t;
        t.respondJson("https://h.org/app", "<a href=\"/download.php?id=7\">Get it</a>");
        HtmlSource html;
        AppSettings s = linkHash();
        s.set(Keys::customLinkFilterRegex, "download\\.php");
        const FetchResult r = fetchFrom(html, t, "https://h.org/app", s);
        QVERIFY(r.error.ok());
        QCOMPARE(r.releases.first().assets.first().name, QStringLiteral("download.php.rpm"));
        QCOMPARE(r.releases.first().assets.first().url, QStringLiteral("https://h.org/download.php?id=7"));
        QCOMPARE(r.releases.first().title, QStringLiteral("Get it"));
    }

    void errors()
    {
        FakeTransport t;
        HtmlSource html;
        QCOMPARE(int(fetchFrom(html, t, kIndex).error.kind), int(Error::NotFound));

        t.respondJson(kIndex, "<p>nothing here</p>");
        QCOMPARE(int(fetchFrom(html, t, kIndex).error.kind), int(Error::NoReleases));

        t.respondFixture(kIndex, "html/index.html");
        AppSettings s;
        s.set(Keys::customLinkFilterRegex, "(");
        QCOMPARE(int(fetchFrom(html, t, kIndex, s).error.kind), int(Error::InvalidSetting));

        AppSettings regex;
        regex.set(Keys::versionExtractionRegEx, "no-such-version");
        QCOMPARE(int(fetchFrom(html, t, kIndex, regex).error.kind), int(Error::NoVersion));

        // ETag probe of a link that is gone
        QCOMPARE(int(fetchFrom(html, t, kIndex).error.kind), int(Error::NotFound));
    }

    void pipelineWithPseudoVersion()
    {
        FakeTransport t;
        t.respondFixture(kIndex, "html/index.html");
        t.respond(kDir + "harbour-foo-1.10-1.armv7hl.rpm", withEtag("W/\"abc\""));
        HtmlSource html;
        const auto latest = latestFrom(html, t, kIndex, AppSettings(), "aarch64");
        QVERIFY2(latest.ok(), qPrintable(latest.error.message));
        QCOMPARE(latest.value.version, shortSha256("W/abc"));
        QCOMPARE(latest.value.assets.first().name, QStringLiteral("harbour-foo-1.10-1.aarch64.rpm"));
    }

    // ---- DirectLinkSource ------------------------------------------------

    void directLinkShape()
    {
        DirectLinkSource direct;
        QCOMPARE(direct.id(), QStringLiteral("DirectLink"));
        QVERIFY(direct.defaultHosts().isEmpty());
        QVERIFY(direct.matchesUrlShape("https://h.org/a/app-1.0-1.noarch.RPM"));
        QVERIFY(direct.matchesUrlShape("h.org/get/app.rpm?mirror=2"));
        QVERIFY(!direct.matchesUrlShape("https://h.org/app.rpm.sig"));
        QVERIFY(!direct.matchesUrlShape("https://h.org/download?file=app.rpm"));
        // Any URL is accepted once the source is chosen explicitly.
        QVERIFY(direct.standardizeUrl("https://h.org/latest").ok());
    }

    void directLinkEtag()
    {
        const QString url = QStringLiteral("https://h.org/dl/harbour-foo-latest.aarch64.rpm");
        FakeTransport t;
        t.respond(url, withEtag("\"v42\""));
        DirectLinkSource direct;
        AppSettings s;
        s.set(Keys::requestHeader, "Authorization: Basic eDp5");
        s.set(Keys::versionExtractWholePage, true); // meaningless here, ignored
        const FetchResult r = fetchFrom(direct, t, url, s);
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        QCOMPARE(t.requests.size(), 1);
        QCOMPARE(t.requests.first().header("Authorization"), QByteArray("Basic eDp5"));
        QCOMPARE(t.requests.first().header("Range"), QByteArray("bytes=0-0"));
        const Release &rel = r.releases.first();
        QCOMPARE(rel.tag, shortSha256("v42"));
        QCOMPARE(rel.pageUrl, url);
        QCOMPARE(assetNames(rel), QStringList{"harbour-foo-latest.aarch64.rpm"});
        QCOMPARE(rel.assets.first().url, url);

        // No ETag: the link hash, which only changes with the URL.
        t.respond(url, withEtag(QByteArray(), 200));
        QCOMPARE(fetchFrom(direct, t, url).releases.first().tag, shortSha256(url.toUtf8()));
        QCOMPARE(fetchFrom(direct, t, url, linkHash()).releases.first().tag, shortSha256(url.toUtf8()));
        t.respondJson(url, "", 404);
        QCOMPARE(int(fetchFrom(direct, t, url).error.kind), int(Error::NotFound));
    }

    void directLinkVersionFromUrl()
    {
        const QString url = QStringLiteral("https://h.org/dl/harbour-foo-2.3.1-1.noarch.rpm");
        FakeTransport t;
        DirectLinkSource direct;
        AppSettings s;
        s.set(Keys::versionExtractionRegEx, "-([0-9.]+)-");
        s.set(Keys::matchGroupToUse, "$1");
        const auto latest = latestFrom(direct, t, url, s, "armv7hl");
        QVERIFY2(latest.ok(), qPrintable(latest.error.message));
        QVERIFY(t.requests.isEmpty());
        QCOMPARE(latest.value.version, QStringLiteral("2.3.1"));
        QCOMPARE(latest.value.assets.first().name, QStringLiteral("harbour-foo-2.3.1-1.noarch.rpm"));
    }

    void directLinkWithoutRpmName()
    {
        FakeTransport t;
        DirectLinkSource direct;
        const FetchResult r = fetchFrom(direct, t, "https://h.org/latest", linkHash());
        QCOMPARE(r.releases.first().assets.first().name, QStringLiteral("latest.rpm"));
    }
};

QTEST_GUILESS_MAIN(TestHtmlSource)
#include "tst_htmlsource.moc"
