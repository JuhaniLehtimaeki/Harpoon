#include "version/rpmversion.h"

#include <QtTest>

using namespace Harpoon;

class TestRpmVersion : public QObject
{
    Q_OBJECT
private slots:
    void vercmp_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("expected");
        auto row = [](const char *a, const char *b, int expected) {
            QTest::addRow("%s vs %s", a, b) << QString::fromLatin1(a) << QString::fromLatin1(b) << expected;
        };
        // Cases from rpm's own test suite (tests/rpmvercmp.at).
        row("1.0", "1.0", 0);
        row("1.0", "2.0", -1);
        row("2.0", "1.0", 1);
        row("2.0.1", "2.0.1", 0);
        row("2.0", "2.0.1", -1);
        row("2.0.1a", "2.0.1", 1);
        row("5.5p1", "5.5p2", -1);
        row("5.5p10", "5.5p1", 1);
        row("10xyz", "10.1xyz", -1);
        row("xyz10", "xyz10.1", -1);
        row("xyz.4", "8", -1);
        row("8", "xyz.4", 1);
        row("1.0aa", "1.0a", 1);
        row("1b.fc17", "1.fc17", -1);
        row("1.0010", "1.9", 1);
        row("1.05", "1.5", 0);
        row("fc4", "fc.4", 0);
        row("FC5", "fc4", -1);
        row("2a", "2.0", -1);
        row("1.0~rc1", "1.0", -1);
        row("1.0~rc1", "1.0~rc2", -1);
        row("1.0~rc1~git123", "1.0~rc1", -1);
        row("1.0^", "1.0", 1);
        row("1.0^git1", "1.0^git2", -1);
        row("1.0^git1", "1.01", -1);
        row("1.0^20160101", "1.0.1", -1);
        row("1.0~rc1^git1", "1.0~rc1", 1);
        row("1.0^git1~pre", "1.0^git1", -1);
    }

    void vercmp()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, expected);
        QCOMPARE(rpmVerCmp(a, b), expected);
        QCOMPARE(rpmVerCmp(b, a), -expected);
    }

    void parse()
    {
        Evr e = parseEvr("2:1.4.2-3.1");
        QCOMPARE(e.epoch, 2);
        QCOMPARE(e.version, QStringLiteral("1.4.2"));
        QCOMPARE(e.release, QStringLiteral("3.1"));
        QCOMPARE(e.toString(), QStringLiteral("2:1.4.2-3.1"));

        e = parseEvr("0.6.12");
        QCOMPARE(e.epoch, 0);
        QCOMPARE(e.release, QString());
        QCOMPARE(e.toString(), QStringLiteral("0.6.12"));
    }

    void evr()
    {
        QCOMPARE(compareEvr(parseEvr("1:1.0-1"), parseEvr("2.0-1")), 1); // epoch wins
        QCOMPARE(compareEvr(parseEvr("1.0-1"), parseEvr("1.0-2")), -1);
        QCOMPARE(compareEvr(parseEvr("1.0"), parseEvr("1.0-7")), 0);     // missing release not compared
        QCOMPARE(compareEvr(parseEvr("0.6.12-1"), parseEvr("0.6.11-9")), 1);
    }
};

QTEST_GUILESS_MAIN(TestRpmVersion)
#include "tst_rpmversion.moc"
