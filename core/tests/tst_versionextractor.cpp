#include "version/versionextractor.h"

#include <QtTest>

using namespace Harpoon;

class TestVersionExtractor : public QObject
{
    Q_OBJECT
private slots:
    void emptyRegexPassesThrough()
    {
        const auto r = extractVersion(QString(), QString(), "v1.2.3");
        QVERIFY(r.ok());
        QCOMPARE(r.value, QStringLiteral("v1.2.3"));
    }

    void wholeMatchByDefault()
    {
        const auto r = extractVersion("[0-9.]+", QString(), "release-1.2.3-final");
        QVERIFY(r.ok());
        QCOMPARE(r.value, QStringLiteral("1.2.3"));
    }

    void lastMatchIsUsed()
    {
        const auto r = extractVersion("\\d+", QString(), "build 12 of 34");
        QVERIFY(r.ok());
        QCOMPARE(r.value, QStringLiteral("34"));
    }

    void groupTemplate()
    {
        auto r = extractVersion("(\\d+)_(\\d+)", "$1.$2", "app_5_7");
        QVERIFY(r.ok());
        QCOMPARE(r.value, QStringLiteral("5.7"));

        r = extractVersion("v(\\d+\\.\\d+)", "1", "tag v4.2");
        QVERIFY(r.ok());
        QCOMPARE(r.value, QStringLiteral("4.2"));
    }

    void errors()
    {
        QCOMPARE(int(extractVersion("(", QString(), "x").error.kind), int(Error::InvalidSetting));
        QCOMPARE(int(extractVersion("\\d+", QString(), "none").error.kind), int(Error::NoVersion));
        QCOMPARE(int(extractVersion("(a)?b", "$1", "b").error.kind), int(Error::NoVersion));
        // The input (maybe a whole web page) is not copied into the message.
        const QString page = QString(100000, QLatin1Char('x'));
        QVERIFY(extractVersion("\\d+", QString(), page).error.message.size() < 300);
    }
};

QTEST_GUILESS_MAIN(TestVersionExtractor)
#include "tst_versionextractor.moc"
