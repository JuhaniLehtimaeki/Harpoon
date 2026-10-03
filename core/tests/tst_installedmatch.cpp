#include "app/installedmatch.h"

#include <QtTest>

using namespace Harpoon;

namespace {
App addedApp(const QStringList &assetNames)
{
    App app = App::fromUrl(QStringLiteral("https://github.com/poetaster/harbour-dwd"));
    for (const QString &name : assetNames) {
        Asset a;
        a.name = name;
        app.latestAssets << a;
    }
    return app;
}
} // namespace

class TestInstalledMatch : public QObject
{
    Q_OBJECT
private slots:
    void namesFromFileNames_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("name");
        QTest::newRow("noarch") << "harbour-dwd-1.1.1-1.noarch.rpm" << "harbour-dwd";
        QTest::newRow("aarch64") << "harbour-foilauth-1.1.20-1.aarch64.rpm" << "harbour-foilauth";
        QTest::newRow("dashes in name") << "sailfish-browser-plugin-x-2.0-3.armv7hl.rpm" << "sailfish-browser-plugin-x";
        QTest::newRow("path") << "/cache/0123456789ab-harbour-dwd-1.1.1-1.noarch.rpm" << "0123456789ab-harbour-dwd";
        QTest::newRow("release with a tag") << "harpoon-0.2.0-1.sfos5.1.aarch64.rpm" << "harpoon";
        QTest::newRow("not nevra") << "app.rpm" << "";
        QTest::newRow("not rpm") << "harbour-dwd-1.1.1-1.noarch.zip" << "";
    }

    void namesFromFileNames()
    {
        QFETCH(QString, file);
        QFETCH(QString, name);
        QCOMPARE(rpmNameFromFileName(file), name);
    }

    void adoptsAnInstalledPackage()
    {
        App app = addedApp({QStringLiteral("harbour-dwd-1.1.1-1.noarch.rpm")});
        QVERIFY(app.temporaryId);
        auto installed = [](const QString &n) { return n == QLatin1String("harbour-dwd"); };
        QVERIFY(adoptInstalledPackage(app, installed, {}));
        QCOMPARE(app.id, QStringLiteral("harbour-dwd"));
        QVERIFY(!app.temporaryId);
        // Only once.
        QVERIFY(!adoptInstalledPackage(app, installed, {}));
    }

    void leavesOthersAlone()
    {
        auto installed = [](const QString &n) { return n == QLatin1String("harbour-dwd"); };
        // Not installed.
        App app = addedApp({QStringLiteral("harbour-other-1.0-1.noarch.rpm")});
        QVERIFY(!adoptInstalledPackage(app, installed, {}));
        QVERIFY(app.temporaryId);
        // Another tracked app already is that package.
        app = addedApp({QStringLiteral("harbour-dwd-1.1.1-1.noarch.rpm")});
        QVERIFY(!adoptInstalledPackage(app, installed, {QStringLiteral("harbour-dwd")}));
        QVERIFY(app.temporaryId);
        // No packages in the release.
        app = addedApp({});
        QVERIFY(!adoptInstalledPackage(app, installed, {}));
    }
};

QTEST_GUILESS_MAIN(TestInstalledMatch)
#include "tst_installedmatch.moc"
