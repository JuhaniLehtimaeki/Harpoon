#include "sourcetesthelpers.h"
#include "sources/sourcehutsource.h"
#include "sources/sourceutil.h"

#include <QtTest>

using namespace Harpoon;

namespace {
const QString kRepo = QStringLiteral("https://git.sr.ht/~me/harbour-foo");

void serveRepo(FakeTransport &t)
{
    t.respondFixture(kRepo + "/refs/rss.xml", "sourcehut/rss.xml");
    t.respondFixture(kRepo + "/refs/v1.2.0", "sourcehut/ref-v1.2.0.html");
    // v1.1.0 has no page (404): the release simply has no assets.
    t.respondJson(kRepo + "/refs/v1.0.0",
                  "<a href=\"/~me/harbour-foo/refs/download/v1.0.0/harbour-foo-1.0.0-1.aarch64.rpm\">rpm</a>");
}
} // namespace

class TestSourceHutSource : public QObject
{
    Q_OBJECT
private slots:
    void standardize()
    {
        SourceHutSource sh;
        QCOMPARE(sh.standardizeUrl("https://git.sr.ht/~me/harbour-foo/refs/v1.2.0").value, kRepo);
        QCOMPARE(sh.standardizeUrl("git.sr.ht/~me/harbour-foo").value, kRepo);
        QCOMPARE(sh.standardizeUrl("https://git.sr.ht/~me/harbour-foo/tree/main/item/README.md").value, kRepo);
        QCOMPARE(int(sh.standardizeUrl("https://git.sr.ht/~me").error.kind), int(Error::InvalidUrl));
        QCOMPARE(int(sh.standardizeUrl("https://hg.sr.ht/~me/foo").error.kind), int(Error::InvalidUrl));
        sh.setCustomHost("git.example.org");
        QCOMPARE(sh.standardizeUrl("https://git.example.org/~me/foo/refs").value,
                 QStringLiteral("https://git.example.org/~me/foo"));
    }

    void rssParsing()
    {
        const auto items = parseRssItems(readFixture("sourcehut/rss.xml"));
        QVERIFY(items.ok());
        QCOMPARE(items.value.size(), 4);
        QCOMPARE(items.value.at(0).title, QStringLiteral("v1.2.0"));
        QCOMPARE(items.value.at(0).guid, kRepo + "/refs/v1.2.0");
        QCOMPARE(parseRfc822Date(items.value.at(0).pubDate), QDateTime(QDate(2026, 4, 2), QTime(8, 0), Qt::UTC));
        QCOMPARE(parseRfc822Date(items.value.at(3).pubDate), QDateTime(QDate(2026, 1, 5), QTime(12, 0), Qt::UTC));
        QCOMPARE(int(parseRssItems("<rss><channel><item><title>x</title></channel></rss>").error.kind), int(Error::Parse));
    }

    void fetchReleases()
    {
        FakeTransport t;
        serveRepo(t);
        SourceHutSource sh;
        const FetchResult r = fetchFrom(sh, t, kRepo);
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        // feed + three ref pages; the item outside this repo is skipped
        QCOMPARE(t.requests.size(), 4);
        QCOMPARE(r.releases.size(), 3);

        const Release &newest = r.releases.at(0);
        QCOMPARE(newest.tag, QStringLiteral("v1.2.0"));
        QCOMPARE(newest.pageUrl, kRepo + "/refs/v1.2.0");
        QCOMPARE(newest.date, QDateTime(QDate(2026, 4, 2), QTime(8, 0), Qt::UTC));
        QCOMPARE(assetNames(newest), (QStringList{"harbour-foo-1.2.0-1.aarch64.rpm", "harbour-foo-1.2.0-1.armv7hl.rpm",
                                                  "harbour-foo-1.2.0-1.noarch.rpm"}));
        QCOMPARE(newest.assets.at(0).url, kRepo + "/refs/download/v1.2.0/harbour-foo-1.2.0-1.aarch64.rpm");
        QCOMPARE(newest.assets.at(2).url, QStringLiteral("https://mirror.example.org/foo/harbour-foo-1.2.0-1.noarch.rpm?dl=1&x=2"));

        QCOMPARE(r.releases.at(1).tag, QStringLiteral("v1.1.0"));
        QVERIFY(r.releases.at(1).assets.isEmpty());
        QCOMPARE(r.releases.at(2).assets.first().url, kRepo + "/refs/download/v1.0.0/harbour-foo-1.0.0-1.aarch64.rpm");
    }

    void pipeline()
    {
        FakeTransport t;
        serveRepo(t);
        SourceHutSource sh;
        AppSettings s;
        s.set(Keys::versionExtractionRegEx, "[0-9.]+");
        const auto latest = latestFrom(sh, t, kRepo, s, "armv7hl");
        QVERIFY2(latest.ok(), qPrintable(latest.error.message));
        QCOMPARE(latest.value.version, QStringLiteral("1.2.0"));
        QCOMPARE(latest.value.assets.first().name, QStringLiteral("harbour-foo-1.2.0-1.armv7hl.rpm"));
    }

    void withoutFallbackOnlyNewestPage()
    {
        FakeTransport t;
        serveRepo(t);
        SourceHutSource sh;
        AppSettings s;
        s.set(Keys::fallbackToOlderReleases, false);
        const FetchResult r = fetchFrom(sh, t, kRepo, s);
        QVERIFY(r.error.ok());
        QCOMPARE(r.releases.size(), 1);
        QCOMPARE(t.requests.size(), 2);
    }

    void atMostSixRefs()
    {
        QByteArray rss = "<rss version=\"2.0\"><channel>";
        for (int i = 9; i >= 1; --i)
            rss += QStringLiteral("<item><title>v%1</title><guid>%2/refs/v%1</guid></item>").arg(i).arg(kRepo).toUtf8();
        rss += "</channel></rss>";
        FakeTransport t;
        t.respondJson(kRepo + "/refs/rss.xml", rss);
        SourceHutSource sh;
        const FetchResult r = fetchFrom(sh, t, kRepo);
        QVERIFY(r.error.ok());
        QCOMPARE(r.releases.size(), 6);
        QCOMPARE(r.releases.last().tag, QStringLiteral("v4"));
        QCOMPARE(t.requests.size(), 7);
    }

    void errors()
    {
        FakeTransport t;
        SourceHutSource sh;
        QCOMPARE(int(fetchFrom(sh, t, kRepo).error.kind), int(Error::NotFound));

        t.respondJson(kRepo + "/refs/rss.xml", "<rss><channel></channel></rss>");
        QCOMPARE(int(fetchFrom(sh, t, kRepo).error.kind), int(Error::NoReleases));

        // A ref page that fails with a server error fails the check instead of
        // silently falling back to an older ref.
        serveRepo(t);
        t.respondJson(kRepo + "/refs/v1.2.0", "oops", 502);
        QCOMPARE(int(fetchFrom(sh, t, kRepo).error.kind), int(Error::Http));
    }
};

QTEST_GUILESS_MAIN(TestSourceHutSource)
#include "tst_sourcehutsource.moc"
