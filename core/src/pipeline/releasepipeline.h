#pragma once

#include "pipeline/releaseselector.h"
#include "sources/source.h"

#include <functional>

namespace Harpoon {

struct LatestRelease
{
    Release release;
    QList<Asset> assets; // installable assets for this device
    QString version;     // after versionSource + versionExtractionRegEx
    QString rawVersion;  // before extraction
};

// Everything after the network fetch: sort, select, derive the version.
// Pure, so it is unit-testable without a transport.
//
// versionSource: "tag" (default; tag, or title if no tag), "title",
// "assetName" (last selected asset), "date" (release date, ISO-8601 UTC).
// versionExtractionRegEx/matchGroupToUse apply to all but "date".
Result<LatestRelease> resolveLatestRelease(QList<Release> releases, const AppSettings &settings,
                                          const DeviceInfo &device);

// Fetches from the source, then resolveLatestRelease(). The source and
// transport must outlive the callback.
void fetchLatestRelease(Source &source, HttpTransport &transport, const QString &standardUrl,
                        const AppSettings &settings, const DeviceInfo &device,
                        std::function<void(const Result<LatestRelease> &)> done);

} // namespace Harpoon
