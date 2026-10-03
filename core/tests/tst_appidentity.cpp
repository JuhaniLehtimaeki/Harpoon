#include "app/appidentity.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace Harpoon;

namespace {
void write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("cannot write %s", qPrintable(path));
    f.write(content);
}
} // namespace

class TestAppIdentity : public QObject
{
    Q_OBJECT
private slots:
    void desktopEntries()
    {
        QTemporaryDir dir;
        const QString apps = dir.filePath(QStringLiteral("applications"));
        const QString icons = dir.filePath(QStringLiteral("hicolor"));
        write(apps + QStringLiteral("/harbour-foilauth.desktop"),
              "[Desktop Entry]\nType=Application\nName=FoilAuth\nName[fi]=FoilAuth FI\nName[de_DE]=FoilAuth DE\n"
              "Icon=harbour-foilauth\nExec=harbour-foilauth\n\n[X-Sailjail]\nName=Not this\n");
        write(icons + QStringLiteral("/86x86/apps/harbour-foilauth.png"), "png");
        write(icons + QStringLiteral("/172x172/apps/harbour-foilauth.png"), "png");

        DesktopEntry e = findDesktopEntry(QStringLiteral("harbour-foilauth"), apps, icons, QLocale(QStringLiteral("en_GB")));
        QVERIFY(e.isValid());
        QCOMPARE(e.name, QStringLiteral("FoilAuth"));
        QCOMPARE(e.iconPath, icons + QStringLiteral("/172x172/apps/harbour-foilauth.png")); // largest
        QCOMPARE(findDesktopEntry(QStringLiteral("harbour-foilauth"), apps, icons, QLocale(QStringLiteral("fi_FI"))).name,
                 QStringLiteral("FoilAuth FI"));
        QCOMPARE(findDesktopEntry(QStringLiteral("harbour-foilauth"), apps, icons, QLocale(QStringLiteral("de_DE"))).name,
                 QStringLiteral("FoilAuth DE"));

        // An absolute icon path, and one that does not exist.
        write(apps + QStringLiteral("/abs.desktop"), "[Desktop Entry]\nName=Abs\nIcon=" + icons.toUtf8()
                                                         + "/86x86/apps/harbour-foilauth.png\n");
        QCOMPARE(findDesktopEntry(QStringLiteral("abs"), apps, icons).iconPath,
                 icons + QStringLiteral("/86x86/apps/harbour-foilauth.png"));
        write(apps + QStringLiteral("/noicon.desktop"), "[Desktop Entry]\nName=No icon\nIcon=missing\n");
        const DesktopEntry noIcon = findDesktopEntry(QStringLiteral("noicon"), apps, icons);
        QVERIFY(noIcon.isValid());
        QVERIFY(noIcon.iconPath.isEmpty());

        QVERIFY(!findDesktopEntry(QStringLiteral("not-installed"), apps, icons).isValid());
        QVERIFY(!findDesktopEntry(QStringLiteral("../escape"), apps, icons).isValid());
        QVERIFY(!findDesktopEntry(QString(), apps, icons).isValid());
    }

    void prettyNames_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QString>("pretty");
        QTest::newRow("harbour prefix") << "harbour-foilauth" << "Foilauth";
        QTest::newRow("words") << "sailfishos-chum-gui" << "Sailfishos Chum Gui";
        QTest::newRow("underscores") << "my_cool_app" << "My Cool App";
        QTest::newRow("already a title") << "FoilAuth" << "FoilAuth";
        QTest::newRow("only a prefix") << "harbour-" << "Harbour";
        QTest::newRow("openrepos") << "openrepos-clock-settings" << "Clock Settings";
    }

    void prettyNames()
    {
        QFETCH(QString, raw);
        QFETCH(QString, pretty);
        QCOMPARE(prettyAppName(raw), pretty);
    }
};

QTEST_GUILESS_MAIN(TestAppIdentity)
#include "tst_appidentity.moc"
