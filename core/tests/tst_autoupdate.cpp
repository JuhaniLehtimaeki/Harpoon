#include "app/autoupdate.h"
#include "app/harpoonsettings.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace Harpoon;

namespace {

App installedApp()
{
    App app = App::fromUrl(QStringLiteral("https://github.com/me/harbour-tool"));
    app.id = QStringLiteral("harbour-tool");
    app.name = QStringLiteral("harbour-tool");
    app.temporaryId = false;
    app.receipt.evr = QStringLiteral("1.0-1");
    app.latestVersion = QStringLiteral("1.1");
    Asset a;
    a.name = QStringLiteral("harbour-tool-1.1-1.aarch64.rpm");
    app.latestAssets << a;
    return app;
}

UpdateStatus updateAvailable()
{
    UpdateStatus s;
    s.state = UpdateState::UpdateAvailable;
    return s;
}

} // namespace

class TestAutoUpdate : public QObject
{
    Q_OBJECT
private slots:
    void eligibility()
    {
        QString why;
        QVERIFY(autoUpdateEligible(installedApp(), updateAvailable(), &why));
        QVERIFY(why.isEmpty());

        UpdateStatus upToDate;
        upToDate.state = UpdateState::UpToDate;
        QVERIFY(!autoUpdateEligible(installedApp(), upToDate));

        App trackOnly = installedApp();
        trackOnly.settings.set(Keys::trackOnly, true);
        QVERIFY(!autoUpdateEligible(trackOnly, updateAvailable(), &why));
        QCOMPARE(why, QStringLiteral("track only"));

        App excluded = installedApp();
        excluded.settings.set(Keys::excludeFromAutoUpdate, true);
        QVERIFY(!autoUpdateEligible(excluded, updateAvailable(), &why));

        // Installed some other way (no receipt): leave it to the user.
        App foreign = installedApp();
        foreign.receipt = InstallReceipt();
        QVERIFY(!autoUpdateEligible(foreign, updateAvailable(), &why));
        QCOMPARE(why, QStringLiteral("not installed by Harpoon"));

        App temporary = installedApp();
        temporary.temporaryId = true;
        QVERIFY(!autoUpdateEligible(temporary, updateAvailable()));

        App renaming = installedApp();
        renaming.settings.set(Keys::allowIdChange, true);
        QVERIFY(!autoUpdateEligible(renaming, updateAvailable()));

        App noPackage = installedApp();
        noPackage.latestAssets.clear();
        QVERIFY(!autoUpdateEligible(noPackage, updateAvailable()));
    }

    void notification()
    {
        NotificationRequest r = autoUpdateNotification({QStringLiteral("Tides")}, {});
        QCOMPARE(r.summary, QStringLiteral("Tides was updated"));
        QCOMPARE(r.itemCount, 1);
        QCOMPARE(r.remoteMethod, QStringLiteral("showUpdates"));

        r = autoUpdateNotification({QStringLiteral("Tides"), QStringLiteral("Maps")}, {QStringLiteral("Notes")});
        QCOMPARE(r.summary, QStringLiteral("2 apps were updated"));
        QCOMPARE(r.body, QStringLiteral("Tides, Maps\nCould not update Notes"));
        QCOMPARE(r.itemCount, 3);

        r = autoUpdateNotification({}, {QStringLiteral("Notes")});
        QCOMPARE(r.summary, QStringLiteral("Could not update Notes"));
    }

    void settingIsOffByDefaultAndBackedUp()
    {
        QTemporaryDir dir;
        HarpoonSettings settings(dir.filePath(QStringLiteral("harpoon.conf")));
        QVERIFY(!settings.autoUpdate());
        QSignalSpy changed(&settings, &HarpoonSettings::changed);
        settings.setAutoUpdate(true);
        QVERIFY(settings.autoUpdate());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(settings.exportable(false).value(QStringLiteral("autoUpdate")).toBool(), true);

        HarpoonSettings other(dir.filePath(QStringLiteral("other.conf")));
        other.restore(settings.exportable(false));
        QVERIFY(other.autoUpdate());
    }
};

QTEST_GUILESS_MAIN(TestAutoUpdate)
#include "tst_autoupdate.moc"
