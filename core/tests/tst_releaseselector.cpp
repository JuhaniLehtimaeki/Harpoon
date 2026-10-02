#include "pipeline/releasepipeline.h"
#include "sources/githubsource.h"

#include <QFile>
#include <QtTest>

using namespace Harpoon;

namespace {
QList<Release> fixture(const QString &name)
{
    QFile f(QStringLiteral(HARPOON_FIXTURE_DIR "/") + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture");
    return parseGitHubStyleReleases(f.readAll()).value;
}

Release rel(const QString &tag, const QString &date, bool prerelease = false)
{
    Release r;
    r.tag = tag;
    r.date = QDateTime::fromString(date, Qt::ISODate);
    r.prerelease = prerelease;
    Asset a;
    a.name = QStringLiteral("app-%1-1.noarch.rpm").arg(tag);
    a.url = QStringLiteral("https://example.org/") + a.name;
    r.assets << a;
    return r;
}

QStringList tags(const QList<Release> &list)
{
    QStringList out;
    for (const Release &r : list)
        out << r.tag;
    return out;
}

DeviceInfo aarch64()
{
    DeviceInfo d;
    d.arch = QStringLiteral("aarch64");
    d.osVersion = QStringLiteral("5.0.0.62");
    return d;
}
} // namespace

class TestReleaseSelector : public QObject
{
    Q_OBJECT
private slots:
    void sortByDate()
    {
        QList<Release> list{rel("1.0", "2024-01-01T00:00:00Z"), rel("1.2", "2024-03-01T00:00:00Z"),
                            rel("1.1", "2024-02-01T00:00:00Z")};
        sortReleasesNewestFirst(list, SortMethod::Date, false);
        QCOMPARE(tags(list), (QStringList{"1.2", "1.1", "1.0"}));
    }

    void sortSmartName()
    {
        // A backported 1.9.1 published after 2.0 must not win by date.
        QList<Release> list{rel("v2.0", "2024-01-01T00:00:00Z"), rel("v1.9.1", "2024-02-01T00:00:00Z"),
                            rel("v1.10", "2023-01-01T00:00:00Z")};
        sortReleasesNewestFirst(list, SortMethod::SmartName, false);
        QCOMPARE(tags(list), (QStringList{"v2.0", "v1.10", "v1.9.1"}));

        sortReleasesNewestFirst(list, SortMethod::Date, false);
        QCOMPARE(tags(list).first(), QStringLiteral("v1.9.1"));
    }

    void smartNameFallsBackToDate()
    {
        QList<Release> list{rel("nightly-b", "2024-02-01T00:00:00Z"), rel("1.0", "2024-01-01T00:00:00Z")};
        sortReleasesNewestFirst(list, SortMethod::SmartNameDateFallback, false);
        QCOMPARE(tags(list).first(), QStringLiteral("nightly-b"));
    }

    void sortNoneKeepsOrder()
    {
        QList<Release> list{rel("a", "2024-01-01T00:00:00Z"), rel("b", "2024-03-01T00:00:00Z")};
        sortReleasesNewestFirst(list, SortMethod::None, false);
        QCOMPARE(tags(list), (QStringList{"a", "b"}));
    }

    void chumGuiDefault()
    {
        // Skips the draft and the prerelease, picks 0.6.12-1 for aarch64.
        const auto r = resolveLatestRelease(fixture("github/chum-gui-releases.json"), AppSettings(), aarch64());
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(r.value.version, QStringLiteral("0.6.12-1"));
        QCOMPARE(r.value.assets.size(), 1);
        QCOMPARE(r.value.assets.first().name, QStringLiteral("sailfishos-chum-gui-0.6.12-1.aarch64.rpm"));
        QCOMPARE(r.value.assets.first().sha256,
                 QStringLiteral("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
        QVERIFY(r.value.release.changelog.contains(QLatin1String("Fix crash")));
    }

    void chumGuiPrerelease()
    {
        AppSettings s;
        s.set(Keys::includePrereleases, true);
        const auto r = resolveLatestRelease(fixture("github/chum-gui-releases.json"), s, aarch64());
        QVERIFY(r.ok());
        QCOMPARE(r.value.version, QStringLiteral("0.7.0-0.rc1"));
        QVERIFY(r.value.release.prerelease);
    }

    void prereleaseOnlyForArmFallsBack()
    {
        // i486 has no prerelease build; with prereleases on it still finds 0.6.12-1.
        AppSettings s;
        s.set(Keys::includePrereleases, true);
        DeviceInfo d = aarch64();
        d.arch = QStringLiteral("i486");
        const auto r = resolveLatestRelease(fixture("github/chum-gui-releases.json"), s, d);
        QVERIFY(r.ok());
        QCOMPARE(r.value.version, QStringLiteral("0.6.12-1"));
    }

    void noFallbackStopsAtNewest()
    {
        // Newest release has no package for x86_64; without fallback nothing is found.
        AppSettings s;
        s.set(Keys::fallbackToOlderReleases, false);
        DeviceInfo d = aarch64();
        d.arch = QStringLiteral("x86_64");
        const auto r = resolveLatestRelease(fixture("github/chum-gui-releases.json"), s, d);
        QCOMPARE(int(r.error.kind), int(Error::NoAsset));
    }

    void titleFilter()
    {
        AppSettings s;
        s.set(Keys::filterReleaseTitlesByRegEx, "^0\\.6\\.11");
        const auto r = resolveLatestRelease(fixture("github/chum-gui-releases.json"), s, aarch64());
        QVERIFY(r.ok());
        QCOMPARE(r.value.version, QStringLiteral("0.6.11-1"));
    }

    void notesFilter()
    {
        AppSettings s;
        s.set(Keys::filterReleaseNotesByRegEx, "Maintenance");
        const auto r = resolveLatestRelease(fixture("github/chum-gui-releases.json"), s, aarch64());
        QVERIFY(r.ok());
        QCOMPARE(r.value.version, QStringLiteral("0.6.11-1"));
    }

    void versionSources()
    {
        AppSettings s;
        s.set(Keys::versionSource, "title");
        auto r = resolveLatestRelease(fixture("github/chum-gui-releases.json"), s, aarch64());
        QCOMPARE(r.value.version, QStringLiteral("0.6.12"));

        s.set(Keys::versionSource, "date");
        r = resolveLatestRelease(fixture("github/chum-gui-releases.json"), s, aarch64());
        QCOMPARE(r.value.version, QStringLiteral("2025-02-10T12:30:00Z"));

        s.set(Keys::versionSource, "assetName");
        s.set(Keys::versionExtractionRegEx, "gui-([0-9.]+-\\d+)\\.");
        s.set(Keys::matchGroupToUse, "$1");
        r = resolveLatestRelease(fixture("github/chum-gui-releases.json"), s, aarch64());
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(r.value.version, QStringLiteral("0.6.12-1"));
        QCOMPARE(r.value.rawVersion, QStringLiteral("sailfishos-chum-gui-0.6.12-1.aarch64.rpm"));
    }

    void sfosTaggedNoarch()
    {
        const auto r = resolveLatestRelease(fixture("github/sfos-tagged-noarch.json"), AppSettings(), aarch64());
        QVERIFY(r.ok());
        QCOMPARE(r.value.version, QStringLiteral("v2.1.0"));
        QCOMPARE(r.value.assets.size(), 1);
        QCOMPARE(r.value.assets.first().name, QStringLiteral("harbour-notes-2.1.0-1_sfos5.0.noarch.rpm"));
    }

    void archivesOnlyExplainWhy()
    {
        // Like sailfishos-chum-gui: RPMs only inside SDK build-result zips.
        QList<Release> list{rel("0.6.12", "2024-01-01T00:00:00Z")};
        Asset zip;
        zip.name = QStringLiteral("RPM-build-results_SDK-for-5.0.0.43.zip");
        zip.url = QStringLiteral("https://example.org/") + zip.name;
        list.first().assets = {zip};
        const auto r = resolveLatestRelease(list, AppSettings(), aarch64());
        QCOMPARE(int(r.error.kind), int(Error::NoAsset));
        QVERIFY(r.error.message.contains(QLatin1String("RPM-build-results_SDK-for-5.0.0.43.zip")));
        QVERIFY(r.error.message.contains(QLatin1String("Chum")));
        // No archives: the plain message.
        list.first().assets.clear();
        QVERIFY(!resolveLatestRelease(list, AppSettings(), aarch64()).error.message.contains(QLatin1String("archives")));
    }

    void trackOnlyNeedsNoAssets()
    {
        QList<Release> list{rel("1.0", "2024-01-01T00:00:00Z")};
        list.first().assets.clear();
        QCOMPARE(int(resolveLatestRelease(list, AppSettings(), aarch64()).error.kind), int(Error::NoAsset));
        AppSettings s;
        s.set(Keys::trackOnly, true);
        const auto r = resolveLatestRelease(list, s, aarch64());
        QVERIFY(r.ok());
        QCOMPARE(r.value.version, QStringLiteral("1.0"));
        QVERIFY(r.value.assets.isEmpty());
    }

    void emptyList()
    {
        QCOMPARE(int(resolveLatestRelease({}, AppSettings(), aarch64()).error.kind), int(Error::NoReleases));
    }
};

QTEST_GUILESS_MAIN(TestReleaseSelector)
#include "tst_releaseselector.moc"
