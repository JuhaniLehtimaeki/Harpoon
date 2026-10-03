#include "sourcetesthelpers.h"
#include "sources/rpmmdreposource.h"

#include <QCryptographicHash>
#include <QtTest>

using namespace Harpoon;

namespace {
const QString kBase = QStringLiteral("https://repo.example.org/sfos/5.0");
const QString kPrimaryGz =
    kBase + "/repodata/b7e69d1759f672c58c6742cd8df220baff85b76bd50356927b53206e8664a547-primary.xml.gz";

void serveRepo(FakeTransport &t)
{
    t.respondFixture(kBase + "/repodata/repomd.xml", "rpmmd/repomd.xml");
    t.respondFixture(kPrimaryGz, "rpmmd/primary.xml.gz");
}

AppSettings tracking(const QString &package)
{
    AppSettings s;
    s.set(Keys::packageName, package);
    return s;
}

QStringList tags(const QList<Release> &releases)
{
    QStringList out;
    for (const Release &r : releases)
        out << r.tag;
    return out;
}
} // namespace

class TestRpmMdRepoSource : public QObject
{
    Q_OBJECT
private slots:
    void flags()
    {
        RpmMdRepoSource repo;
        QVERIFY(repo.defaultHosts().isEmpty());
        QVERIFY(repo.neverAutoSelect());
    }

    void standardize_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("standard");
        QTest::newRow("base") << kBase << kBase;
        QTest::newRow("trailing slash") << kBase + "/" << kBase;
        QTest::newRow("repomd.xml") << kBase + "/repodata/repomd.xml" << kBase;
        QTest::newRow("repodata dir") << kBase + "/repodata/" << kBase;
        QTest::newRow("package query kept, others dropped") << kBase + "/?utm=1&package=harbour-foo#x"
                                                            << kBase + "?package=harbour-foo";
        QTest::newRow("no scheme") << "repo.example.org/sfos/5.0" << kBase;
    }

    void standardize()
    {
        QFETCH(QString, url);
        QFETCH(QString, standard);
        RpmMdRepoSource repo;
        const auto r = repo.standardizeUrl(url);
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(r.value, standard);
        QCOMPARE(RpmMdRepoSource::repoBase(r.value), kBase);
    }

    void standardizeInvalid()
    {
        RpmMdRepoSource repo;
        QCOMPARE(int(repo.standardizeUrl("ftp://repo.example.org/x").error.kind), int(Error::InvalidUrl));
    }

    void parseMetadata()
    {
        const auto repomd = parseRepoMd(readFixture("rpmmd/repomd.xml"));
        QVERIFY2(repomd.ok(), qPrintable(repomd.error.message));
        QCOMPARE(repomd.value.href,
                 QStringLiteral("repodata/b7e69d1759f672c58c6742cd8df220baff85b76bd50356927b53206e8664a547-primary.xml.gz"));
        QCOMPARE(repomd.value.sha256, QStringLiteral("b7e69d1759f672c58c6742cd8df220baff85b76bd50356927b53206e8664a547"));
        QCOMPARE(repomd.value.openSha256, QStringLiteral("c1a88ecc10bbb91942ac4e9cdbbdae48977aeba9dcdb0024ac59a34c426e72a5"));
        QCOMPARE(int(parseRepoMd("<repomd xmlns=\"http://linux.duke.edu/metadata/repo\"/>").error.kind), int(Error::Parse));
    }

    void decompress()
    {
        const auto inflated = decompressMetadata(readFixture("rpmmd/primary.xml.gz"));
        QVERIFY2(inflated.ok(), qPrintable(inflated.error.message));
        QCOMPARE(inflated.value, readFixture("rpmmd/primary.xml"));
        // Plain XML passes through.
        QCOMPARE(decompressMetadata("<metadata/>").value, QByteArray("<metadata/>"));
        // Truncated gzip
        const QByteArray gz = readFixture("rpmmd/primary.xml.gz");
        QCOMPARE(int(decompressMetadata(gz.left(gz.size() / 2)).error.kind), int(Error::Parse));
        // Formats that need other libraries are named in the error.
        const auto zstd = decompressMetadata(QByteArray("\x28\xb5\x2f\xfd\x00\x00", 6));
        QCOMPARE(int(zstd.error.kind), int(Error::Parse));
        QVERIFY(zstd.error.message.contains("zstd"));
        QVERIFY(decompressMetadata(QByteArray("\xfd" "7zXZ\x00\x00", 7)).error.message.contains("xz"));
        QVERIFY(decompressMetadata("BZh91AY").error.message.contains("bzip2"));
    }

    void fetchReleases()
    {
        FakeTransport t;
        serveRepo(t);
        RpmMdRepoSource repo;
        const FetchResult r = fetchFrom(repo, t, kBase + "?package=harbour-foo");
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        QCOMPARE(t.requests.size(), 2);
        QCOMPARE(t.requests.at(1).url, kPrimaryGz);

        // Newest EVR first; the epoch outranks any version; source packages
        // and other names are left out.
        QCOMPARE(tags(r.releases), (QStringList{"1:0.5-2", "1.10.0-1", "1.2.0-1"}));

        const Release &oldest = r.releases.at(2);
        QCOMPARE(oldest.title, QStringLiteral("harbour-foo-1.2.0-1"));
        QCOMPARE(oldest.pageUrl, kBase);
        QCOMPARE(assetNames(oldest), (QStringList{"harbour-foo-1.2.0-1.aarch64.rpm", "harbour-foo-1.2.0-1.armv7hl.rpm"}));
        const Asset &a = oldest.assets.first();
        QCOMPARE(a.url, kBase + "/aarch64/harbour-foo-1.2.0-1.aarch64.rpm");
        QCOMPARE(a.size, qint64(183920));
        QCOMPARE(a.sha256, QStringLiteral("aaaa000000000000000000000000000000000000000000000000000000000001"));
        QCOMPARE(a.updatedAt, QDateTime::fromMSecsSinceEpoch(1700000000000LL, Qt::UTC));
        QCOMPARE(oldest.date, QDateTime::fromMSecsSinceEpoch(1700000100000LL, Qt::UTC)); // newest file

        const Release &middle = r.releases.at(1);
        QCOMPARE(middle.assets.first().url,
                 QStringLiteral("https://cdn.example.org/mirror/sfos/aarch64/harbour-foo-1.10.0-1.aarch64.rpm")); // xml:base
        // Its file is older than 1.2.0's, so the date is raised to keep EVR order.
        QCOMPARE(middle.date, oldest.date.addSecs(1));

        const Release &newest = r.releases.at(0);
        QVERIFY(newest.assets.first().sha256.isEmpty()); // sha1 only
        QCOMPARE(newest.date, QDateTime::fromMSecsSinceEpoch(1710000000000LL, Qt::UTC));
    }

    void dateAndNoSortKeepEvrOrder()
    {
        FakeTransport t;
        serveRepo(t);
        RpmMdRepoSource repo;
        const FetchResult r = fetchFrom(repo, t, kBase, tracking("harbour-foo"));
        QVERIFY(r.error.ok());
        for (SortMethod method : {SortMethod::Date, SortMethod::None}) {
            QList<Release> sorted = r.releases;
            sortReleasesNewestFirst(sorted, method, false);
            QCOMPARE(tags(sorted), tags(r.releases));
        }
    }

    void pipeline()
    {
        FakeTransport t;
        serveRepo(t);
        RpmMdRepoSource repo;
        // The noarch 1:0.5-2 is newest and installs anywhere.
        auto latest = latestFrom(repo, t, kBase, tracking("harbour-foo"), "armv7hl");
        QVERIFY2(latest.ok(), qPrintable(latest.error.message));
        QCOMPARE(latest.value.version, QStringLiteral("1:0.5-2"));

        // Without it, an armv7hl device falls back past the aarch64-only 1.10.0-1.
        AppSettings s = tracking("harbour-foo");
        s.set(Keys::assetFilterRegEx, "noarch");
        s.set(Keys::invertAssetFilter, true);
        latest = latestFrom(repo, t, kBase, s, "armv7hl");
        QVERIFY2(latest.ok(), qPrintable(latest.error.message));
        QCOMPARE(latest.value.version, QStringLiteral("1.2.0-1"));
        QCOMPARE(latest.value.assets.size(), 1);
        QCOMPARE(latest.value.assets.first().name, QStringLiteral("harbour-foo-1.2.0-1.armv7hl.rpm"));
        QCOMPARE(latest.value.assets.first().sha256,
                 QStringLiteral("aaaa000000000000000000000000000000000000000000000000000000000002"));

        latest = latestFrom(repo, t, kBase, s, "aarch64");
        QCOMPARE(latest.value.version, QStringLiteral("1.10.0-1"));
    }

    void plainXmlAndInflatedTransfer()
    {
        FakeTransport t;
        t.respondJson(kBase + "/repodata/repomd.xml",
                      "<repomd xmlns=\"http://linux.duke.edu/metadata/repo\"><data type=\"primary\">"
                      "<location href=\"repodata/primary.xml\"/></data></repomd>");
        t.respondFixture(kBase + "/repodata/primary.xml", "rpmmd/primary.xml");
        RpmMdRepoSource repo;
        QCOMPARE(fetchFrom(repo, t, kBase, tracking("harbour-foo")).releases.size(), 3);

        // A .gz the server sent with Content-Encoding arrives inflated: the
        // open-checksum vouches for it.
        FakeTransport inflated;
        inflated.respondFixture(kBase + "/repodata/repomd.xml", "rpmmd/repomd.xml");
        inflated.respondFixture(kPrimaryGz, "rpmmd/primary.xml");
        const FetchResult r = fetchFrom(repo, inflated, kBase, tracking("harbour-foo"));
        QVERIFY2(r.error.ok(), qPrintable(r.error.message));
        QCOMPARE(r.releases.size(), 3);
    }

    void otherChecksumAlgorithms()
    {
        // A repository that publishes only sha512 (or sha1) is still verified.
        const QByteArray primary = readFixture("rpmmd/primary.xml");
        const QByteArray good = QCryptographicHash::hash(primary, QCryptographicHash::Sha512).toHex();
        auto repomd = [](const QByteArray &type, const QByteArray &sum) {
            return "<repomd xmlns=\"http://linux.duke.edu/metadata/repo\"><data type=\"primary\">"
                   "<checksum type=\"" + type + "\">" + sum + "</checksum>"
                   "<location href=\"repodata/primary.xml\"/></data></repomd>";
        };
        RpmMdRepoSource repo;
        FakeTransport ok;
        ok.respondJson(kBase + "/repodata/repomd.xml", repomd("sha512", good));
        ok.respondJson(kBase + "/repodata/primary.xml", primary);
        QVERIFY(fetchFrom(repo, ok, kBase, tracking("harbour-foo")).error.ok());

        FakeTransport bad;
        bad.respondJson(kBase + "/repodata/repomd.xml", repomd("sha512", QByteArray(128, '0')));
        bad.respondJson(kBase + "/repodata/primary.xml", primary);
        QCOMPARE(int(fetchFrom(repo, bad, kBase, tracking("harbour-foo")).error.kind), int(Error::Checksum));

        FakeTransport sha1;
        sha1.respondJson(kBase + "/repodata/repomd.xml",
                         repomd("sha", QCryptographicHash::hash(primary, QCryptographicHash::Sha1).toHex()));
        sha1.respondJson(kBase + "/repodata/primary.xml", primary);
        QVERIFY(fetchFrom(repo, sha1, kBase, tracking("harbour-foo")).error.ok());
    }

    void errors()
    {
        FakeTransport t;
        RpmMdRepoSource repo;
        // No package name: nothing is fetched.
        QCOMPARE(int(fetchFrom(repo, t, kBase).error.kind), int(Error::InvalidSetting));
        QVERIFY(t.requests.isEmpty());

        QCOMPARE(int(fetchFrom(repo, t, kBase, tracking("harbour-foo")).error.kind), int(Error::NotFound));

        serveRepo(t);
        QCOMPARE(int(fetchFrom(repo, t, kBase, tracking("harbour-missing")).error.kind), int(Error::NoReleases));

        t.respondJson(kPrimaryGz, "corrupted");
        QCOMPARE(int(fetchFrom(repo, t, kBase, tracking("harbour-foo")).error.kind), int(Error::Checksum));

        t.respondJson(kBase + "/repodata/repomd.xml",
                      "<repomd xmlns=\"http://linux.duke.edu/metadata/repo\"><data type=\"primary\">"
                      "<location href=\"repodata/primary.xml.zst\"/></data></repomd>");
        t.respondJson(kBase + "/repodata/primary.xml.zst", QByteArray("\x28\xb5\x2f\xfd\x00\x00", 6));
        const FetchResult zstd = fetchFrom(repo, t, kBase, tracking("harbour-foo"));
        QCOMPARE(int(zstd.error.kind), int(Error::Parse));
        QVERIFY(zstd.error.message.contains("zstd"));
    }
};

QTEST_GUILESS_MAIN(TestRpmMdRepoSource)
#include "tst_rpmmdreposource.moc"
