#pragma once

#include "model/appsettings.h"
#include "model/error.h"
#include "model/release.h"
#include "pipeline/deviceinfo.h"

namespace Harpoon {

// Replaces ObtainX's APK/ABI filtering with RPM rules. Steps, in order:
//  1. keep installable RPMs: *.rpm, but not *.src.rpm or -debuginfo/-debugsource
//  2. assetFilterRegEx (matched against name or URL), optionally inverted
//  3. autoAssetFilterByArch (default on): keep the device arch; if none, keep
//     noarch; if no asset declares a recognised arch, keep everything
//  4. preferSfosVersionTag (default on): when several remain and names carry
//     "sfosX.Y" tags, keep the highest tag not newer than the device OS
//
// An empty result is not an error; an invalid regex is.
Result<QList<Asset>> filterAssets(const QList<Asset> &assets, const AppSettings &settings, const DeviceInfo &device);

bool isInstallableRpm(const QString &assetName);

// RPM arch declared by an asset name ("foo-1.0-1.aarch64.rpm" -> "aarch64").
// Falls back to an arch alias anywhere in the name ("foo-arm64.rpm"). Empty
// when no known arch is present.
QString rpmArchOf(const QString &assetName);

// The "sfosX.Y[.Z]" tag in an asset name, e.g. "4.6"; empty if none.
QString sfosTagOf(const QString &assetName);

} // namespace Harpoon
