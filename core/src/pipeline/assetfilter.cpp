#include "pipeline/assetfilter.h"

#include "version/rpmversion.h"

#include <QRegularExpression>

namespace Harpoon {

namespace {

const QStringList &knownArches()
{
    static const QStringList arches = {QStringLiteral("aarch64"), QStringLiteral("armv7hl"), QStringLiteral("i486"),
                                       QStringLiteral("x86_64"), QStringLiteral("noarch")};
    return arches;
}

} // namespace

bool isInstallableRpm(const QString &assetName)
{
    const QString name = assetName.toLower();
    if (!name.endsWith(QLatin1String(".rpm")) || name.endsWith(QLatin1String(".src.rpm")))
        return false;
    static const QRegularExpression debug(QStringLiteral("-debug(info|source)[-.]"));
    return !debug.match(name).hasMatch();
}

QString rpmArchOf(const QString &assetName)
{
    const QString name = assetName.toLower();

    static const QRegularExpression suffix(QStringLiteral("\\.([a-z0-9_]+)\\.rpm$"));
    const auto m = suffix.match(name);
    if (m.hasMatch()) {
        const QString arch = DeviceInfo::normalizeArch(m.captured(1));
        if (knownArches().contains(arch))
            return arch;
    }

    // Non-standard names such as "app-arm64.rpm" or "app_armv7hl_sfos4.rpm".
    static const QRegularExpression alias(QStringLiteral(
        "(?<![a-z0-9])(aarch64|arm64|armv7hl|armv7l|armhf|i486|i586|i686|x86_64|amd64|noarch)(?![a-z0-9])"));
    const auto a = alias.match(name);
    if (a.hasMatch()) {
        const QString arch = a.captured(1) == QLatin1String("armhf") ? QStringLiteral("armv7hl")
                                                                     : DeviceInfo::normalizeArch(a.captured(1));
        if (knownArches().contains(arch))
            return arch;
    }
    return QString();
}

QString sfosTagOf(const QString &assetName)
{
    static const QRegularExpression tag(QStringLiteral("sfos[-_]?(\\d+(?:\\.\\d+)*)"),
                                        QRegularExpression::CaseInsensitiveOption);
    const auto m = tag.match(assetName);
    return m.hasMatch() ? m.captured(1) : QString();
}

Result<QList<Asset>> filterAssets(const QList<Asset> &assets, const AppSettings &settings, const DeviceInfo &device)
{
    QList<Asset> out;
    for (const Asset &asset : assets)
        if (isInstallableRpm(asset.name))
            out << asset;

    const QString filter = settings.getString(Keys::assetFilterRegEx);
    if (!filter.isEmpty()) {
        const QRegularExpression pattern(filter);
        if (!pattern.isValid())
            return Result<QList<Asset>>::failure(Error::make(
                Error::InvalidSetting, QStringLiteral("Invalid asset filter regex: %1").arg(pattern.errorString())));
        const bool invert = settings.getBool(Keys::invertAssetFilter);
        QList<Asset> kept;
        for (const Asset &asset : out) {
            const bool hit = pattern.match(asset.name).hasMatch() || pattern.match(asset.url).hasMatch();
            if (hit != invert)
                kept << asset;
        }
        out = kept;
    }

    if (settings.getBool(Keys::autoAssetFilterByArch, true) && !device.arch.isEmpty()) {
        QList<Asset> native, noarch;
        bool anyKnownArch = false;
        for (const Asset &asset : out) {
            const QString arch = rpmArchOf(asset.name);
            if (!arch.isEmpty())
                anyKnownArch = true;
            if (arch == device.arch)
                native << asset;
            else if (arch == QLatin1String("noarch"))
                noarch << asset;
        }
        if (!native.isEmpty())
            out = native;
        else if (!noarch.isEmpty())
            out = noarch;
        else if (anyKnownArch)
            out.clear(); // packages exist, just not for this device
    }

    if (out.size() > 1 && settings.getBool(Keys::preferSfosVersionTag, true) && !device.osVersion.isEmpty()) {
        QString best;
        for (const Asset &asset : out) {
            const QString tag = sfosTagOf(asset.name);
            if (tag.isEmpty() || rpmVerCmp(tag, device.osVersion) > 0)
                continue;
            // A tag like "4.6" applies to every 4.6.x release.
            if (best.isEmpty() || rpmVerCmp(tag, best) > 0)
                best = tag;
        }
        if (!best.isEmpty()) {
            QList<Asset> tagged;
            for (const Asset &asset : out)
                if (sfosTagOf(asset.name) == best)
                    tagged << asset;
            out = tagged;
        }
    }

    return Result<QList<Asset>>::success(out);
}

} // namespace Harpoon
