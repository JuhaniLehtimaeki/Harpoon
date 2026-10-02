#pragma once

#include "model/appsettings.h"
#include "model/error.h"
#include "model/release.h"
#include "pipeline/deviceinfo.h"

namespace Harpoon {

enum class SortMethod {
    Date,                  // publish date (or newest asset date)
    SmartName,             // version order when consistent, else natural name order
    SmartNameDateFallback, // version order when consistent, else date
    Name,                  // natural name order
    None,                  // keep the forge's order
};

// "date" | "smartname" | "smartname-datefallback" | "name" | "none"; default Date.
SortMethod parseSortMethod(const QString &value);

// Sorts newest first. Ported from ObtainX GitHub.sortGitHubReleases.
void sortReleasesNewestFirst(QList<Release> &releases, SortMethod method, bool useLatestAssetDate);

struct Selection
{
    Release release;
    QList<Asset> assets; // filtered installable assets (may be empty for track-only)
    int index = -1;      // position in the sorted list
};

// Walks the newest-first list and returns the first acceptable release
// (ObtainX GitHub._selectGitHubTargetRelease):
//  - drafts are always skipped, prereleases unless includePrereleases
//  - filterReleaseTitlesByRegEx / filterReleaseNotesByRegEx must match
//  - the release must have a matching asset unless trackOnly
//  - without fallbackToOlderReleases (default on) only the newest
//    non-prerelease candidate is considered
Result<Selection> selectRelease(const QList<Release> &newestFirst, const AppSettings &settings,
                                const DeviceInfo &device);

} // namespace Harpoon
