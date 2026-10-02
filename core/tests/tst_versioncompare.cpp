#include "version/versioncompare.h"

#include <QtTest>

using namespace Harpoon;

Q_DECLARE_METATYPE(Harpoon::VersionRelation)

class TestVersionCompare : public QObject
{
    Q_OBJECT
private slots:
    void compare_data()
    {
        QTest::addColumn<QString>("installed");
        QTest::addColumn<QString>("latest");
        QTest::addColumn<VersionRelation>("expected");

        QTest::newRow("patch bump") << "1.2.3" << "1.2.4" << VersionRelation::Older;
        QTest::newRow("same with v prefix") << "1.2.3" << "v1.2.3" << VersionRelation::Same;
        QTest::newRow("numeric not lexical") << "1.10" << "1.9" << VersionRelation::Newer;
        QTest::newRow("padding") << "1.2" << "1.2.0" << VersionRelation::Same;
        QTest::newRow("rc before final") << "1.0.0-rc1" << "1.0.0" << VersionRelation::Older;
        QTest::newRow("beta before rc") << "1.0.0-beta.2" << "1.0.0-rc.1" << VersionRelation::Older;
        QTest::newRow("rc numbering") << "2.0-rc.1" << "2.0-rc.2" << VersionRelation::Older;
        QTest::newRow("revision bump") << "0.6.12-1" << "0.6.12-2" << VersionRelation::Older;
        QTest::newRow("missing revision") << "0.6.12" << "0.6.12-1" << VersionRelation::Unknown;
        QTest::newRow("build metadata ignored") << "1.0.0+build5" << "1.0.0" << VersionRelation::Same;
        QTest::newRow("title with one version") << "Release 1.2.3" << "1.2.4" << VersionRelation::Older;
        QTest::newRow("dates") << "2024-01-01" << "2024-02-01" << VersionRelation::Older;
        QTest::newRow("date vs version") << "2024-01-01" << "1.2.3" << VersionRelation::Unknown;
        QTest::newRow("commit hashes") << "abc1234" << "def5678" << VersionRelation::Unknown;
        QTest::newRow("unrecognized") << "foo" << "bar" << VersionRelation::Unknown;
        QTest::newRow("empty") << "" << "1.0" << VersionRelation::Unknown;
        QTest::newRow("build id vs dotted") << "42" << "1.2" << VersionRelation::Unknown;
        QTest::newRow("build ids") << "41" << "42" << VersionRelation::Older;
        QTest::newRow("huge numbers") << "1.99999999999999999999" << "1.100000000000000000000" << VersionRelation::Older;
    }

    void compare()
    {
        QFETCH(QString, installed);
        QFETCH(QString, latest);
        QFETCH(VersionRelation, expected);
        const VersionDecision d = compareVersionStrings(installed, latest);
        QCOMPARE(int(d.relation), int(expected));
    }

    void consistentOrder()
    {
        QVERIFY(versionsHaveConsistentOrder({"v1.0", "v1.1", "1.2.0-rc1"}));
        QVERIFY(versionsHaveConsistentOrder({"2024-01-01", "2024-05-01"}));
        QVERIFY(!versionsHaveConsistentOrder({"1.0", "2024-01-01"}));
        QVERIFY(!versionsHaveConsistentOrder({"1.0", "nightly"}));
        QVERIFY(!versionsHaveConsistentOrder({"1.0", "1.0-1"})); // revision ambiguity
        QVERIFY(versionsHaveConsistentOrder({"only-one"}));
    }

    void alphaNumeric()
    {
        QCOMPARE(compareAlphaNumeric("file2", "file10"), -1);
        QCOMPARE(compareAlphaNumeric("file10", "file10"), 0);
        QCOMPARE(compareAlphaNumeric("abc", "1"), -1); // text before numbers
    }

    void normalize()
    {
        QCOMPARE(normalizeVersionLabel("  V2.0 "), QStringLiteral("2.0"));
        QCOMPARE(normalizeVersionLabel("version"), QStringLiteral("version"));
    }
};

QTEST_GUILESS_MAIN(TestVersionCompare)
#include "tst_versioncompare.moc"
