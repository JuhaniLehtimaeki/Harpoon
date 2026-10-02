#include "sourcetesthelpers.h"
#include "sources/sourceforgesource.h"

#include <QtTest>

using namespace Harpoon;

namespace {
const QString kFiles = QStringLiteral("https://sourceforge.net/projects/harbourfoo/files");
const QString kFeed = QStringLiteral("https://sourceforge.net/projects/harbourfoo/rss?path=/");

QStringList tags(const QList<Release> &releases)
{
    QStringList out;
    for (const Release &r : releases)
        out << r.tag;
    return out;
}
} // namespace

class TestSourceForgeSource : public QObject
{
    Q_OBJECT
private slots:
    void standardize_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("standard");
        QTest::newRow("files") << "https://sourceforge.net/projects/harbourfoo/files" << kFiles;
        QTest::newRow("project page") << "https://sourceforge.net/projects/harbourfoo/" << kFiles;
        QTest::newRow("short /p/") << "https://sourceforge.net/p/harbourfoo" << kFiles;
        QTest::newRow("short /p/ with files") << "sourceforge.net/p/harbourfoo/files/" << kFiles;
        QTest::newRow("subfolder") << "https://sourceforge.net/projects/harbourfoo/files/v1.2.0/?sort=date"
                                   << kFiles + "/v1.2.0";
        QTest::newRow("www kept") << "https://www.sourceforge.net/projects/harbourfoo"
                                  << "https://www.sourceforge.net/projects/harbourfoo/files";
    }

    void standardize()
    {
        QFETCH(QString, url);
        QFETCH(QString, standard);
        SourceForgeSource sf;
        const auto r = sf.standardizeUrl(url);
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(r.value, standard);
    }

    void standardizeInvalid()
    {
        SourceForgeSource sf;
        QCOMPARE(int(sf.standardizeUrl("https://sourceforge.net/projects/").error.kind), int(Error::InvalidUrl));
        QCOMPARE(int(sf.standardizeUrl("https://sourceforge.net/projects/x/reviews").error.kind), int(Error::InvalidUrl));
        QCOMPARE(int(sf.standardizeUrl("https://example.com/projects/x/files").error.kind), int(Error::InvalidUrl));
        sf.setCustomHost("sf.example.org");
        QCOMPARE(sf.standardizeUrl("https://sf.example.org/p/x").value, QStringLiteral("https://sf.example.org/projects/x/files"));
    }

    void feedUrl()
    {
        QCOMPARE(SourceForgeSource::feedUrl(kFiles + "/v1.2.0"), kFeed);
    }

    void fetchReleases()
    {
        FakeTransport t;
        t.respondFixture(kFeed, "sourceforge/rss.xml");
        SourceForgeSource sf;
        const FetchResult r = fetchFrom(sf, t, kFiles);
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        QCOMPARE(t.requests.size(), 1);
        // Folder versions in feed order; README skipped; a top-level file
        // uses its own name, as upstream.
        QCOMPARE(tags(r.releases), (QStringList{"v1.2.0", "nightly", "v1.1.0", "old builds/1.0",
                                                "harbour-foo-0.9-1.noarch.rpm"}));

        const Release &newest = r.releases.first();
        QCOMPARE(assetNames(newest), (QStringList{"harbour-foo-1.2.0-1.aarch64.rpm", "harbour-foo-1.2.0-1.armv7hl.rpm"}));
        QCOMPARE(newest.assets.first().url, kFiles + "/v1.2.0/harbour-foo-1.2.0-1.aarch64.rpm/download");
        QCOMPARE(newest.assets.first().size, qint64(183920));
        QCOMPARE(newest.assets.at(1).size, qint64(179004));
        QCOMPARE(newest.date, QDateTime(QDate(2026, 4, 2), QTime(10, 0, 5), Qt::UTC)); // newest file
        QCOMPARE(newest.pageUrl, kFiles + "/v1.2.0/");
        QCOMPARE(r.releases.at(2).assets.size(), 2); // AssetFilter drops the src.rpm later
        QCOMPARE(r.releases.at(3).assets.first().url, kFiles + "/old%20builds/1.0/harbour-foo-1.0-1.noarch.rpm/download");
    }

    void versionRegexFiltersAndGroups()
    {
        const QByteArray feed = readFixture("sourceforge/rss.xml");
        auto r = parseSourceForgeFeed(feed, kFiles, "([0-9]+\\.[0-9.]+)", "$1");
        QVERIFY(r.ok());
        // "nightly" has no version and is dropped, as upstream; tags stay raw
        // so ReleasePipeline extracts exactly once.
        QCOMPARE(tags(r.value), (QStringList{"v1.2.0", "v1.1.0", "old builds/1.0", "harbour-foo-0.9-1.noarch.rpm"}));

        // Folders that extract to the same version share one release.
        r = parseSourceForgeFeed(feed, kFiles, "^v?([0-9])", "$1");
        QVERIFY(r.ok());
        QCOMPARE(tags(r.value), QStringList{"v1.2.0"});
        QCOMPARE(r.value.first().assets.size(), 4);

        QCOMPARE(int(parseSourceForgeFeed(feed, kFiles, "([", "").error.kind), int(Error::InvalidSetting));
    }

    void subfolderAndSchemeVariants()
    {
        const QByteArray feed = readFixture("sourceforge/rss.xml");
        // Only files under the canonical subfolder; the version is relative to it.
        auto r = parseSourceForgeFeed(feed, kFiles + "/v1.2.0", QString(), QString());
        QVERIFY(r.ok());
        QCOMPARE(tags(r.value), (QStringList{"harbour-foo-1.2.0-1.aarch64.rpm", "harbour-foo-1.2.0-1.armv7hl.rpm"}));
        // http:// or www. typed by the user still matches the feed's links.
        r = parseSourceForgeFeed(feed, "http://www.sourceforge.net/projects/harbourfoo/files", QString(), QString());
        QCOMPARE(r.value.size(), 5);
    }

    void pipeline()
    {
        FakeTransport t;
        t.respondFixture(kFeed, "sourceforge/rss.xml");
        SourceForgeSource sf;
        AppSettings s;
        s.set(Keys::versionExtractionRegEx, "([0-9]+\\.[0-9.]+)");
        s.set(Keys::matchGroupToUse, "$1");
        const auto latest = latestFrom(sf, t, kFiles, s, "armv7hl");
        QVERIFY2(latest.ok(), qPrintable(latest.error.message));
        QCOMPARE(latest.value.version, QStringLiteral("1.2.0"));
        QCOMPARE(latest.value.assets.size(), 1);
        QCOMPARE(latest.value.assets.first().name, QStringLiteral("harbour-foo-1.2.0-1.armv7hl.rpm"));
    }

    void errors()
    {
        FakeTransport t;
        SourceForgeSource sf;
        QCOMPARE(int(fetchFrom(sf, t, kFiles).error.kind), int(Error::NotFound));
        t.respondJson(kFeed, "<rss><channel><item><guid>https://sourceforge.net/projects/harbourfoo/files/a.zip/download</guid></item></channel></rss>");
        QCOMPARE(int(fetchFrom(sf, t, kFiles).error.kind), int(Error::NoReleases));
        t.respondJson(kFeed, "<rss><channel><item>");
        QCOMPARE(int(fetchFrom(sf, t, kFiles).error.kind), int(Error::Parse));
    }
};

QTEST_GUILESS_MAIN(TestSourceForgeSource)
#include "tst_sourceforgesource.moc"
