#include "pipeline/assetfilter.h"

#include <QtTest>

using namespace Harpoon;

namespace {
QList<Asset> assets(const QStringList &names)
{
    QList<Asset> out;
    for (const QString &n : names) {
        Asset a;
        a.name = n;
        a.url = QStringLiteral("https://example.org/dl/") + n;
        out << a;
    }
    return out;
}

QStringList names(const QList<Asset> &list)
{
    QStringList out;
    for (const Asset &a : list)
        out << a.name;
    return out;
}

DeviceInfo device(const QString &arch, const QString &os = QString())
{
    DeviceInfo d;
    d.arch = arch;
    d.osVersion = os;
    return d;
}
} // namespace

class TestAssetFilter : public QObject
{
    Q_OBJECT
private slots:
    void installable()
    {
        QVERIFY(isInstallableRpm("app-1.0-1.aarch64.rpm"));
        QVERIFY(isInstallableRpm("APP-1.0-1.NOARCH.RPM"));
        QVERIFY(!isInstallableRpm("app-1.0-1.src.rpm"));
        QVERIFY(!isInstallableRpm("app-debuginfo-1.0-1.aarch64.rpm"));
        QVERIFY(!isInstallableRpm("app-debugsource-1.0-1.aarch64.rpm"));
        QVERIFY(!isInstallableRpm("app-1.0.apk"));
        QVERIFY(!isInstallableRpm("app-1.0.tar.gz"));
    }

    void arch()
    {
        QCOMPARE(rpmArchOf("app-1.0-1.aarch64.rpm"), QStringLiteral("aarch64"));
        QCOMPARE(rpmArchOf("app-1.0-1.armv7hl.rpm"), QStringLiteral("armv7hl"));
        QCOMPARE(rpmArchOf("app-1.0-1.i486.rpm"), QStringLiteral("i486"));
        QCOMPARE(rpmArchOf("app-1.0-1.noarch.rpm"), QStringLiteral("noarch"));
        QCOMPARE(rpmArchOf("app-arm64.rpm"), QStringLiteral("aarch64"));
        QCOMPARE(rpmArchOf("app_armhf_v2.rpm"), QStringLiteral("armv7hl"));
        QCOMPARE(rpmArchOf("app-1.0.rpm"), QString());
    }

    void sfosTag()
    {
        QCOMPARE(sfosTagOf("app-1.0-1_sfos4.6.noarch.rpm"), QStringLiteral("4.6"));
        QCOMPARE(sfosTagOf("app-1.0-1.SFOS-5.0.aarch64.rpm"), QStringLiteral("5.0"));
        QCOMPARE(sfosTagOf("app-1.0-1.aarch64.rpm"), QString());
    }

    void picksDeviceArch()
    {
        const auto in = assets({"a-1-1.aarch64.rpm", "a-1-1.armv7hl.rpm", "a-1-1.i486.rpm", "a-1-1.src.rpm",
                                "a-debuginfo-1-1.aarch64.rpm"});
        QCOMPARE(names(filterAssets(in, AppSettings(), device("aarch64")).value), QStringList{"a-1-1.aarch64.rpm"});
        QCOMPARE(names(filterAssets(in, AppSettings(), device("armv7hl")).value), QStringList{"a-1-1.armv7hl.rpm"});
    }

    void noarchFallback()
    {
        const auto in = assets({"a-1-1.noarch.rpm", "a-1-1.i486.rpm"});
        QCOMPARE(names(filterAssets(in, AppSettings(), device("aarch64")).value), QStringList{"a-1-1.noarch.rpm"});
    }

    void wrongArchOnlyGivesNothing()
    {
        const auto in = assets({"a-1-1.i486.rpm", "a-1-1.armv7hl.rpm"});
        QVERIFY(filterAssets(in, AppSettings(), device("aarch64")).value.isEmpty());
    }

    void unknownArchKept()
    {
        const auto in = assets({"a-1.rpm"});
        QCOMPARE(filterAssets(in, AppSettings(), device("aarch64")).value.size(), 1);
    }

    void archFilterCanBeDisabled()
    {
        AppSettings s;
        s.set(Keys::autoAssetFilterByArch, false);
        const auto in = assets({"a-1-1.i486.rpm", "a-1-1.armv7hl.rpm"});
        QCOMPARE(filterAssets(in, s, device("aarch64")).value.size(), 2);
    }

    void userRegex()
    {
        const auto in = assets({"app-1-1.aarch64.rpm", "app-data-1-1.aarch64.rpm"});
        AppSettings s;
        s.set(Keys::assetFilterRegEx, "^app-\\d");
        QCOMPARE(names(filterAssets(in, s, device("aarch64")).value), QStringList{"app-1-1.aarch64.rpm"});
        s.set(Keys::invertAssetFilter, true);
        QCOMPARE(names(filterAssets(in, s, device("aarch64")).value), QStringList{"app-data-1-1.aarch64.rpm"});
        s.set(Keys::assetFilterRegEx, "(");
        QCOMPARE(int(filterAssets(in, s, device("aarch64")).error.kind), int(Error::InvalidSetting));
    }

    void sfosTagPreference()
    {
        const auto in = assets({"n-2-1_sfos4.6.noarch.rpm", "n-2-1_sfos5.0.noarch.rpm", "n-2-1_sfos5.2.noarch.rpm"});
        QCOMPARE(names(filterAssets(in, AppSettings(), device("aarch64", "5.0.0.62")).value),
                 QStringList{"n-2-1_sfos5.0.noarch.rpm"});
        QCOMPARE(names(filterAssets(in, AppSettings(), device("aarch64", "4.6.0.15")).value),
                 QStringList{"n-2-1_sfos4.6.noarch.rpm"});
        QCOMPARE(names(filterAssets(in, AppSettings(), device("aarch64", "5.2.0.15")).value),
                 QStringList{"n-2-1_sfos5.2.noarch.rpm"});
        // Unknown OS version: no tag preference.
        QCOMPARE(filterAssets(in, AppSettings(), device("aarch64")).value.size(), 3);
        // OS older than every tag: keep all and let the user decide.
        QCOMPARE(filterAssets(in, AppSettings(), device("aarch64", "4.5.0.0")).value.size(), 3);
    }
};

QTEST_GUILESS_MAIN(TestAssetFilter)
#include "tst_assetfilter.moc"
