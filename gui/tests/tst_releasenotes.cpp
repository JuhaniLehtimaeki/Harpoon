#include "releasenotes.h"

#include <QtTest>

using namespace Harpoon;

class TestReleaseNotes : public QObject
{
    Q_OBJECT

private slots:
    void convert_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QString>("styled");

        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("plain") << "Fixes a crash." << "Fixes a crash.";
        QTest::newRow("escaped") << "a < b & \"c\"" << "a &lt; b &amp; &quot;c&quot;";
        QTest::newRow("heading") << "## What's new ##" << "<b>What's new</b>";
        QTest::newRow("bullets") << "- one\n* two\n  + nested" << "• one<br>• two<br>&nbsp;&nbsp;&nbsp;• nested";
        QTest::newRow("bold") << "**Note** and __this__" << "<b>Note</b> and <b>this</b>";
        QTest::newRow("code") << "Run `harpoon-cli check`" << "Run harpoon-cli check";
        QTest::newRow("link") << "[Changelog](https://example.org/c)"
                              << "<a href=\"https://example.org/c\">Changelog</a>";
        QTest::newRow("bare url") << "See https://example.org/x for more"
                                  << "See <a href=\"https://example.org/x\">https://example.org/x</a> for more";
        QTest::newRow("other scheme stays text") << "[x](javascript:alert(1))" << "[x](javascript:alert(1))";
        QTest::newRow("paragraphs collapse") << "a\n\n\n\nb\n---\nc\n\n" << "a<br><br>b<br><br>c";
        QTest::newRow("html stripped") << "<details><summary>More</summary>text</details><!-- hidden\n-->"
                                       << "Moretext";
        QTest::newRow("crlf") << "a\r\nb" << "a<br>b";
    }

    void convert()
    {
        QFETCH(QString, markdown);
        QFETCH(QString, styled);
        QCOMPARE(releaseNotesToStyledText(markdown), styled);
    }
};

QTEST_APPLESS_MAIN(TestReleaseNotes)
#include "tst_releasenotes.moc"
