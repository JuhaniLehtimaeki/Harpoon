#include "sourcetesthelpers.h"
#include "sources/tangledsource.h"
#include "sources/sourceregistry.h"
#include "sources/sourceutil.h"

#include <QtTest>

using namespace Harpoon;

namespace {
const QString kRepo = QStringLiteral("https://tangled.org/me.example/harbour-foo");

void serveRepo(FakeTransport &t)
{
    t.respondFixture(kRepo + "/feed.atom?include=tags", "tangled/feed.atom");
    t.respondFixture(kRepo + "/tags/v1.2.0", "tangled/tag-v1.2.0.html");
    t.respondFixture(kRepo + "/tags/v1.1.0", "tangled/tag-v1.1.0.html");
    // v1.0.0 has no page (404): the release simply has no assets.
}
} // namespace

class TestTangledSource : public QObject
{
    Q_OBJECT
private slots:
    void standardize()
    {
        TangledSource ts;
        QCOMPARE(ts.standardizeUrl("https://tangled.org/me.example/harbour-foo/tags/v1.2.0").value, kRepo);
        QCOMPARE(ts.standardizeUrl("tangled.org/me.example/harbour-foo").value, kRepo);
        QCOMPARE(ts.standardizeUrl("https://tangled.org/rikles.eu/harbour-networth").value,
                 QStringLiteral("https://tangled.org/rikles.eu/harbour-networth"));
        QCOMPARE(int(ts.standardizeUrl("https://tangled.org/me.example").error.kind), int(Error::InvalidUrl));
        QCOMPARE(int(ts.standardizeUrl("https://tangled.org/explore").error.kind), int(Error::InvalidUrl));
        ts.setCustomHost("tangled.example.org");
        QCOMPARE(ts.standardizeUrl("https://tangled.example.org/me/app/commits").value,
                 QStringLiteral("https://tangled.example.org/me/app"));
    }

    void registryMatchesTangledHost()
    {
        SourceRegistry registry;
        const auto match = registry.match(QStringLiteral("https://tangled.org/rikles.eu/harbour-networth"), QString());
        QVERIFY(match.ok());
        QCOMPARE(match.value.source->id(), QStringLiteral("Tangled"));
        QCOMPARE(match.value.standardUrl, QStringLiteral("https://tangled.org/rikles.eu/harbour-networth"));
    }

    void atomParsing()
    {
        const auto entries = parseAtomEntries(readFixture("tangled/feed.atom"));
        QVERIFY(entries.ok());
        QCOMPARE(entries.value.size(), 3);
        QCOMPARE(entries.value.at(0).title, QStringLiteral("[Tag] v1.2.0"));
        QCOMPARE(entries.value.at(0).link, kRepo + "/tags/v1.2.0");
        QCOMPARE(entries.value.at(0).id, kRepo + "/tags/v1.2.0");
        QCOMPARE(entries.value.at(0).updated, QStringLiteral("2026-04-02T08:00:00.000Z"));
        QVERIFY(entries.value.at(0).content.contains(QStringLiteral("Foo 1.2.0")));
        QCOMPARE(int(parseAtomEntries("<feed xmlns=\"http://www.w3.org/2005/Atom\"><entry></feed>").error.kind),
                 int(Error::Parse));
    }

    void fetchReleases()
    {
        FakeTransport t;
        serveRepo(t);
        TangledSource ts;
        const FetchResult r = fetchFrom(ts, t, kRepo);
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        // feed + three tag pages
        QCOMPARE(t.requests.size(), 4);
        QCOMPARE(t.requests.at(0).url, kRepo + "/feed.atom?include=tags");
        QCOMPARE(r.releases.size(), 3);

        const Release &newest = r.releases.at(0);
        QCOMPARE(newest.tag, QStringLiteral("v1.2.0"));
        QCOMPARE(newest.title, QStringLiteral("Foo 1.2.0"));
        QCOMPARE(newest.changelog, QStringLiteral("- New chart panel\n- Fixed the export"));
        QCOMPARE(newest.date, QDateTime(QDate(2026, 4, 2), QTime(8, 0), Qt::UTC));
        QCOMPARE(newest.pageUrl, kRepo + "/tags/v1.2.0");
        QCOMPARE(assetNames(newest), (QStringList{"harbour-foo-1.2.0-1.aarch64.rpm", "harbour-foo-1.2.0-1.noarch.rpm"}));
        QCOMPARE(newest.assets.at(0).url, kRepo + "/artifacts/3kabc2def3ghi4j");
        QCOMPARE(newest.assets.at(1).url, kRepo + "/artifacts/3kabc2def3ghi4k");

        const Release &middle = r.releases.at(1);
        QCOMPARE(middle.tag, QStringLiteral("v1.1.0"));
        QCOMPARE(middle.title, QStringLiteral("Foo 1.1.0"));
        QVERIFY(middle.changelog.isEmpty());
        QCOMPARE(middle.date, QDateTime(QDate(2026, 3, 1), QTime(11, 30), Qt::UTC));
        QCOMPARE(assetNames(middle), (QStringList{"harbour-foo-1.1.0-1.noarch.rpm"}));
        QCOMPARE(middle.assets.at(0).url, kRepo + "/artifacts/3jxyz2abc3def4i");

        QCOMPARE(r.releases.at(2).tag, QStringLiteral("v1.0.0"));
        QVERIFY(r.releases.at(2).assets.isEmpty());
    }

    void withoutFallbackOnlyNewestTag()
    {
        FakeTransport t;
        serveRepo(t);
        TangledSource ts;
        AppSettings settings;
        settings.set(Keys::fallbackToOlderReleases, false);
        const FetchResult r = fetchFrom(ts, t, kRepo, settings);
        QVERIFY(r.error.ok());
        QCOMPARE(r.releases.size(), 1);
        QCOMPARE(r.releases.at(0).tag, QStringLiteral("v1.2.0"));
        QCOMPARE(t.requests.size(), 2);
    }

    void noTags()
    {
        FakeTransport t;
        t.respondJson(kRepo + "/feed.atom?include=tags",
                      "<?xml version=\"1.0\"?><feed xmlns=\"http://www.w3.org/2005/Atom\"></feed>");
        TangledSource ts;
        const FetchResult r = fetchFrom(ts, t, kRepo);
        QCOMPARE(int(r.error.kind), int(Error::NoReleases));
    }

    void notFound()
    {
        FakeTransport t;
        TangledSource ts;
        const FetchResult r = fetchFrom(ts, t, kRepo);
        QCOMPARE(int(r.error.kind), int(Error::NotFound));
    }

    void pipeline()
    {
        FakeTransport t;
        serveRepo(t);
        TangledSource ts;
        AppSettings settings;
        settings.set(Keys::versionExtractionRegEx, "[0-9.]+");
        const auto latest = latestFrom(ts, t, kRepo, settings, QStringLiteral("aarch64"));
        QVERIFY2(latest.ok(), qPrintable(latest.error.message));
        QCOMPARE(latest.value.version, QStringLiteral("1.2.0"));
        QCOMPARE(latest.value.release.tag, QStringLiteral("v1.2.0"));
        QCOMPARE(latest.value.assets.size(), 1);
        QCOMPARE(latest.value.assets.first().name, QStringLiteral("harbour-foo-1.2.0-1.aarch64.rpm"));
        QCOMPARE(latest.value.assets.first().url, kRepo + "/artifacts/3kabc2def3ghi4j");
    }
};

QTEST_MAIN(TestTangledSource)
#include "tst_tangledsource.moc"
