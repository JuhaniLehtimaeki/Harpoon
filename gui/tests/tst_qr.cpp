// QR decoding (zxing-cpp) and harpoon:// / plain-URL link parsing.

#include "app/addlink.h"
#include "qrdecoder.h"

#include <QPainter>
#include <QtTest>

using namespace Harpoon;

namespace {
QString fixture(const char *name)
{
    return QStringLiteral(HARPOON_GUI_FIXTURE_DIR "/qr/") + QLatin1String(name);
}
} // namespace

class TestQr : public QObject
{
    Q_OBJECT
private slots:
    void decodesQrImages()
    {
        QCOMPARE(QrDecoder::decode(QImage(fixture("repo-url.png"))),
                 QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"));
        QCOMPARE(QrDecoder::decode(QImage(fixture("harpoon-link.png"))),
                 QStringLiteral("harpoon://add?url=https%3A%2F%2Fgit.example.org%2Fme%2Fharbour-tides&source=Forgejo"));
        // Rotated, scaled, blurred and noisy, like a camera frame.
        QCOMPARE(QrDecoder::decode(QImage(fixture("photo-like.png"))),
                 QStringLiteral("harpoon://add?url=https%3A%2F%2Fgit.example.org%2Fme%2Fharbour-tides&source=Forgejo"));
        QVERIFY(QrDecoder::decode(QImage(fixture("no-code.png"))).isEmpty());
        QVERIFY(QrDecoder::decode(QImage()).isEmpty());
    }

    void decodesLargeColourFrames()
    {
        // A big RGB frame with the code in a corner, as from a phone camera.
        QImage frame(3000, 2000, QImage::Format_RGB32);
        frame.fill(QColor(90, 120, 60));
        QPainter p(&frame);
        p.drawImage(QRect(300, 200, 900, 900), QImage(fixture("repo-url.png")));
        p.end();
        QCOMPARE(QrDecoder::decode(frame), QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"));
        QrDecoder decoder;
        QCOMPARE(decoder.decodeImage(QVariant::fromValue(frame)),
                 QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"));
        QCOMPARE(decoder.decodeFile(fixture("repo-url.png")), QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"));
    }

    void parsesLinks()
    {
        auto link = parseAddLink(QStringLiteral(" https://github.com/owner/repo "));
        QVERIFY(link.ok());
        QCOMPARE(link.value.url, QStringLiteral("https://github.com/owner/repo"));
        QVERIFY(link.value.sourceId.isEmpty());

        link = parseAddLink(QStringLiteral("harpoon://add?url=https%3A%2F%2Fgit.example.org%2Fme%2Fapp&source=Forgejo"));
        QVERIFY2(link.ok(), qPrintable(link.error.message));
        QCOMPARE(link.value.url, QStringLiteral("https://git.example.org/me/app"));
        QCOMPARE(link.value.sourceId, QStringLiteral("Forgejo"));

        link = parseAddLink(QStringLiteral("harpoon://add?url=https://repo.example.org/obs/&source=RpmMdRepo&package=harbour-x"));
        QVERIFY2(link.ok(), qPrintable(link.error.message));
        QCOMPARE(link.value.packageName, QStringLiteral("harbour-x"));
    }

    void rejectsBadLinks_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << "";
        QTest::newRow("javascript") << "javascript:alert(1)";
        QTest::newRow("file") << "file:///etc/passwd";
        QTest::newRow("plain text") << "hello world";
        QTest::newRow("other action") << "harpoon://remove?url=https://github.com/a/b";
        QTest::newRow("no url") << "harpoon://add?source=GitHub";
        QTest::newRow("non-web url") << "harpoon://add?url=file:///tmp/x";
        QTest::newRow("bad source") << "harpoon://add?url=https://a.b/c&source=../../x";
        QTest::newRow("bad package") << "harpoon://add?url=https://a.b/c&package=a;rm -rf";
    }

    void rejectsBadLinks()
    {
        QFETCH(QString, text);
        QVERIFY(!parseAddLink(text).ok());
    }

    void roundTrip()
    {
        AddLink link;
        link.url = QStringLiteral("https://git.example.org/me/app?x=1&y=2");
        link.sourceId = QStringLiteral("Forgejo");
        link.packageName = QStringLiteral("harbour-app");
        const QString encoded = link.toString();
        QVERIFY(encoded.startsWith(QLatin1String("harpoon://add?url=https%3A%2F%2F")));
        const auto parsed = parseAddLink(encoded);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value.url, link.url);
        QCOMPARE(parsed.value.sourceId, link.sourceId);
        QCOMPARE(parsed.value.packageName, link.packageName);
    }
};

QTEST_MAIN(TestQr)
#include "tst_qr.moc"
