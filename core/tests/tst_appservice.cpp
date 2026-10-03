#include "app/appservice.h"
#include "app/harpoonsettings.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace Harpoon;

class TestAppService : public QObject
{
    Q_OBJECT
private slots:
    void recordAfterInstall()
    {
        App current = App::fromUrl(QStringLiteral("https://github.com/me/tool"));
        current.settings.set(Keys::includePrereleases, true); // changed during the install
        InstallResult result;
        result.app = App::fromUrl(QStringLiteral("https://github.com/me/tool"));
        result.app.id = QStringLiteral("harbour-tool");
        result.app.temporaryId = false;
        result.app.receipt.evr = QStringLiteral("1.0-1");

        App record = Harpoon::recordAfterInstall(current, result, false);
        QCOMPARE(record.id, QStringLiteral("harbour-tool"));
        QVERIFY(!record.temporaryId);
        QCOMPARE(record.receipt.evr, QStringLiteral("1.0-1"));
        QVERIFY(record.settings.getBool(Keys::includePrereleases)); // the current settings win

        // The package is already another tracked app: keep this record's id.
        record = Harpoon::recordAfterInstall(current, result, true);
        QCOMPARE(record.id, current.id);
        QVERIFY(record.temporaryId);
        QCOMPARE(record.receipt.evr, QStringLiteral("1.0-1"));
    }

    void tokens()
    {
        QTemporaryDir dir;
        HarpoonSettings settings(dir.filePath(QStringLiteral("harpoon.conf")));
        settings.setToken(QStringLiteral("GitHub"), QStringLiteral("stored"));
        settings.setToken(QStringLiteral("Forgejo@git.example.org"), QStringLiteral("own"));
        qputenv("HARPOON_TOKEN_GITHUB", "from-env");
        const QStringList ids{QStringLiteral("GitHub"), QStringLiteral("Forgejo")};
        auto configs = sourceConfigs(settings, ids, false);
        QCOMPARE(configs.value(QStringLiteral("GitHub")).value(QStringLiteral("token")).toString(), QStringLiteral("stored"));
        QCOMPARE(configs.value(QStringLiteral("Forgejo@git.example.org")).value(QStringLiteral("token")).toString(),
                 QStringLiteral("own"));
        configs = sourceConfigs(settings, ids, true);
        QCOMPARE(configs.value(QStringLiteral("GitHub")).value(QStringLiteral("token")).toString(), QStringLiteral("from-env"));
        qunsetenv("HARPOON_TOKEN_GITHUB");
    }
};

QTEST_GUILESS_MAIN(TestAppService)
#include "tst_appservice.moc"
