#include "pipeline/releaseselector.h"

#include "pipeline/assetfilter.h"
#include "version/versioncompare.h"

#include <QRegularExpression>

#include <algorithm>

namespace Harpoon {

namespace {

QDateTime releaseDate(const Release &release, bool useLatestAssetDate)
{
    if (!useLatestAssetDate)
        return release.date;
    QDateTime newest;
    for (const Asset &asset : release.assets)
        if (asset.updatedAt.isValid() && (!newest.isValid() || asset.updatedAt > newest))
            newest = asset.updatedAt;
    return newest;
}

int compareDates(const QDateTime &a, const QDateTime &b)
{
    if (!a.isValid() && !b.isValid())
        return 0;
    if (!a.isValid())
        return -1;
    if (!b.isValid())
        return 1;
    return a < b ? -1 : (a > b ? 1 : 0);
}

Result<QRegularExpression> compileOptional(const QString &pattern, const char *what)
{
    QRegularExpression re(pattern);
    if (!pattern.isEmpty() && !re.isValid())
        return Result<QRegularExpression>::failure(Error::make(
            Error::InvalidSetting, QStringLiteral("Invalid %1: %2").arg(QLatin1String(what), re.errorString())));
    return Result<QRegularExpression>::success(re);
}

} // namespace

SortMethod parseSortMethod(const QString &value)
{
    if (value == QLatin1String("smartname"))
        return SortMethod::SmartName;
    if (value == QLatin1String("smartname-datefallback"))
        return SortMethod::SmartNameDateFallback;
    if (value == QLatin1String("name"))
        return SortMethod::Name;
    if (value == QLatin1String("none"))
        return SortMethod::None;
    return SortMethod::Date;
}

void sortReleasesNewestFirst(QList<Release> &releases, SortMethod method, bool useLatestAssetDate)
{
    if (method == SortMethod::None)
        return;

    bool useVersionOrder = false;
    if (method == SortMethod::SmartName || method == SortMethod::SmartNameDateFallback) {
        QStringList labels;
        for (const Release &r : releases)
            labels << r.label();
        useVersionOrder = versionsHaveConsistentOrder(labels);
    }

    // Ascending comparator, as upstream; the list is reversed afterwards.
    auto compare = [&](const Release &a, const Release &b) -> int {
        const QDateTime da = releaseDate(a, useLatestAssetDate);
        const QDateTime db = releaseDate(b, useLatestAssetDate);
        if (method == SortMethod::Date || (!useVersionOrder && method == SortMethod::SmartNameDateFallback))
            return compareDates(da, db);
        const QString na = a.label();
        const QString nb = b.label();
        if (useVersionOrder) {
            const VersionDecision d = compareVersionStrings(na, nb);
            if (d.isOrdered() && d.comparison() != 0)
                return d.comparison();
            if ((d.isOrdered() && d.comparison() == 0) || method == SortMethod::SmartNameDateFallback)
                return compareDates(da, db);
        }
        const int byName = compareAlphaNumeric(na.toLower(), nb.toLower());
        return byName != 0 ? byName : compareDates(da, db);
    };

    std::stable_sort(releases.begin(), releases.end(),
                     [&](const Release &a, const Release &b) { return compare(a, b) < 0; });
    std::reverse(releases.begin(), releases.end());
}

Result<Selection> selectRelease(const QList<Release> &newestFirst, const AppSettings &settings,
                                const DeviceInfo &device)
{
    if (newestFirst.isEmpty())
        return Result<Selection>::failure(Error::make(Error::NoReleases, QStringLiteral("No releases found")));

    const auto titleFilter = compileOptional(settings.getString(Keys::filterReleaseTitlesByRegEx), "release title filter");
    if (!titleFilter.ok())
        return Result<Selection>::failure(titleFilter.error);
    const auto notesFilter = compileOptional(settings.getString(Keys::filterReleaseNotesByRegEx), "release notes filter");
    if (!notesFilter.ok())
        return Result<Selection>::failure(notesFilter.error);

    const bool fallback = settings.getBool(Keys::fallbackToOlderReleases, true);
    const bool includePrereleases = settings.getBool(Keys::includePrereleases);
    const bool trackOnly = settings.getBool(Keys::trackOnly);

    int prereleasesSkipped = 0;
    for (int i = 0; i < newestFirst.size(); ++i) {
        if (!fallback && i > prereleasesSkipped)
            break;
        const Release &release = newestFirst.at(i);
        if (!includePrereleases && release.prerelease) {
            ++prereleasesSkipped;
            continue;
        }
        if (release.draft)
            continue;

        const QString title = release.title.trimmed().isEmpty() ? release.tag : release.title;
        if (!titleFilter.value.pattern().isEmpty() && !titleFilter.value.match(title.trimmed()).hasMatch())
            continue;
        if (!notesFilter.value.pattern().isEmpty()
            && !notesFilter.value.match(release.changelog.trimmed()).hasMatch())
            continue;

        const auto assets = filterAssets(release.assets, settings, device);
        if (!assets.ok())
            return Result<Selection>::failure(assets.error);
        if (assets.value.isEmpty() && !trackOnly)
            continue;

        Selection selection;
        selection.release = release;
        selection.assets = assets.value;
        selection.index = i;
        return Result<Selection>::success(selection);
    }

    return Result<Selection>::failure(Error::make(
        Error::NoAsset,
        QStringLiteral("No release has an installable package for %1")
            .arg(device.arch.isEmpty() ? QStringLiteral("this device") : device.arch)));
}

} // namespace Harpoon
