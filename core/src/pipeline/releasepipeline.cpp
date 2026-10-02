#include "pipeline/releasepipeline.h"

#include "version/versionextractor.h"

namespace Harpoon {

Result<LatestRelease> resolveLatestRelease(QList<Release> releases, const AppSettings &settings,
                                          const DeviceInfo &device)
{
    sortReleasesNewestFirst(releases, parseSortMethod(settings.getString(Keys::sortMethodChoice)),
                            settings.getBool(Keys::useLatestAssetDateAsReleaseDate));

    const auto selection = selectRelease(releases, settings, device);
    if (!selection.ok())
        return Result<LatestRelease>::failure(selection.error);

    LatestRelease latest;
    latest.release = selection.value.release;
    latest.assets = selection.value.assets;

    const QString versionSource = settings.getString(Keys::versionSource, QStringLiteral("tag"));
    if (versionSource == QLatin1String("date")) {
        if (!latest.release.date.isValid())
            return Result<LatestRelease>::failure(
                Error::make(Error::NoVersion, QStringLiteral("The release has no date to use as version")));
        latest.rawVersion = latest.release.date.toUTC().toString(Qt::ISODate);
        latest.version = latest.rawVersion;
        return Result<LatestRelease>::success(latest);
    }

    if (versionSource == QLatin1String("title")) {
        latest.rawVersion = latest.release.title.trimmed().isEmpty() ? latest.release.tag : latest.release.title;
    } else if (versionSource == QLatin1String("assetName")) {
        if (latest.assets.isEmpty())
            return Result<LatestRelease>::failure(
                Error::make(Error::NoVersion, QStringLiteral("No asset to take the version from")));
        latest.rawVersion = latest.assets.last().name;
    } else {
        latest.rawVersion = latest.release.label();
    }

    const auto extracted = extractVersion(settings.getString(Keys::versionExtractionRegEx),
                                          settings.getString(Keys::matchGroupToUse), latest.rawVersion);
    if (!extracted.ok())
        return Result<LatestRelease>::failure(extracted.error);
    latest.version = extracted.value.trimmed();
    if (latest.version.isEmpty())
        return Result<LatestRelease>::failure(
            Error::make(Error::NoVersion, QStringLiteral("The release has no usable version")));
    return Result<LatestRelease>::success(latest);
}

void fetchLatestRelease(Source &source, HttpTransport &transport, const QString &standardUrl,
                        const AppSettings &settings, const DeviceInfo &device,
                        std::function<void(const Result<LatestRelease> &)> done)
{
    source.fetchReleases(standardUrl, settings, transport, [settings, device, done](const FetchResult &fetched) {
        if (!fetched.error.ok()) {
            done(Result<LatestRelease>::failure(fetched.error));
            return;
        }
        done(resolveLatestRelease(fetched.releases, settings, device));
    });
}

} // namespace Harpoon
