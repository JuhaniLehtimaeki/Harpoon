#include "sources/sourceforgesource.h"

#include "sources/sourceutil.h"
#include "version/versionextractor.h"

#include <QRegularExpression>
#include <QUrl>

namespace Harpoon {

namespace {

// "https://www.host/path" -> "host/path": feed links always use https and
// no www, whatever the user typed.
QString withoutSchemeAndWww(const QString &url)
{
    static const QRegularExpression prefix(QStringLiteral("^[a-z]+://(www\\.)?"), QRegularExpression::CaseInsensitiveOption);
    QString out = url;
    out.remove(prefix);
    return out;
}

} // namespace

Result<QString> SourceForgeSource::standardizeUrl(const QString &input) const
{
    QString url = preStandardizeUrl(input);
    const int queryOrFragment = url.indexOf(QRegularExpression(QStringLiteral("[?#]")));
    if (queryOrFragment >= 0)
        url.truncate(queryOrFragment);
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);

    const QString prefix = hostPrefixPattern();
    const auto options = QRegularExpression::CaseInsensitiveOption;

    // Short project URLs: /p/{name}/... -> /projects/{name}/...
    const auto shortForm = QRegularExpression(prefix + QStringLiteral("/p/(.+)$"), options).match(url);
    if (shortForm.hasMatch())
        url = QStringLiteral("https://%1/projects/%2").arg(QUrl(url).host(), shortForm.captured(shortForm.lastCapturedIndex()));

    // The bare project page lists the same files as /files.
    if (QRegularExpression(prefix + QStringLiteral("/projects/[^/]+$"), options).match(url).hasMatch())
        url += QStringLiteral("/files");

    const auto files = QRegularExpression(prefix + QStringLiteral("/projects/[^/]+/files(/.+)?$"), options).match(url);
    if (!files.hasMatch())
        return Result<QString>::failure(
            Error::make(Error::InvalidUrl, QStringLiteral("Not a valid %1 URL: %2").arg(displayName(), input)));
    return Result<QString>::success(files.captured(0));
}

QString SourceForgeSource::feedUrl(const QString &standardUrl)
{
    const QUrl url(standardUrl);
    const QStringList segments = url.path().split(QLatin1Char('/'));
    // path is "/projects/{name}/files..." -> segments ["", "projects", name, ...]
    return QStringLiteral("%1://%2/projects/%3/rss?path=/").arg(url.scheme(), url.authority(), segments.value(2));
}

void SourceForgeSource::fetchReleases(const QString &standardUrl, const AppSettings &settings,
                                      HttpTransport &transport, Callback done)
{
    HttpRequest request;
    request.url = feedUrl(standardUrl);
    const QString regex = settings.getString(Keys::versionExtractionRegEx);
    const QString group = settings.getString(Keys::matchGroupToUse);
    transport.get(request, [this, standardUrl, regex, group, done](const HttpResponse &response) {
        FetchResult result;
        if (response.status != 200) {
            result.error = httpErrorFor(response, displayName(), QStringLiteral("Project not found"));
        } else {
            const auto parsed = parseSourceForgeFeed(response.body, standardUrl, regex, group);
            result.releases = parsed.value;
            result.error = parsed.error;
            if (result.error.ok() && result.releases.isEmpty())
                result.error = Error::make(Error::NoReleases, QStringLiteral("No RPM files found in the project feed"));
        }
        done(result);
    });
}

Result<QList<Release>> parseSourceForgeFeed(const QByteArray &xml, const QString &standardUrl,
                                            const QString &versionRegex, const QString &matchGroup)
{
    const auto items = parseRssItems(xml);
    if (!items.ok())
        return Result<QList<Release>>::failure(items.error);
    if (!versionRegex.isEmpty() && !QRegularExpression(versionRegex).isValid())
        return Result<QList<Release>>::failure(
            Error::make(Error::InvalidSetting, QStringLiteral("Invalid version extraction regex")));

    const QString base = withoutSchemeAndWww(standardUrl);
    QList<Release> releases;
    QStringList keys; // grouping key per release: extracted version or folder
    for (const RssItem &item : items.value) {
        const QString link = item.guid.isEmpty() ? item.link : item.guid;
        const QString bareLink = withoutSchemeAndWww(link);
        if (!bareLink.startsWith(base) || !bareLink.endsWith(QLatin1String("/download"), Qt::CaseInsensitive))
            continue;
        const QString filePath = bareLink.mid(base.size(), bareLink.size() - base.size() - 9);
        if (!filePath.startsWith(QLatin1Char('/')))
            continue; // e.g. ".../files-old/..." when the canonical URL ends in "/files"

        if (!filePath.endsWith(QLatin1String(".rpm"), Qt::CaseInsensitive))
            continue;

        QStringList segments;
        for (const QString &segment : filePath.split(QLatin1Char('/')))
            if (!segment.isEmpty())
                segments << decodeUrl(segment);
        if (segments.isEmpty())
            continue;
        const QString fileName = segments.takeLast();
        const QString folder = segments.isEmpty() ? fileName : segments.join(QLatin1Char('/'));

        QString key = folder;
        if (!versionRegex.isEmpty()) {
            const auto extracted = extractVersion(versionRegex, matchGroup, folder);
            if (!extracted.ok())
                continue; // upstream skips files whose folder has no version
            key = extracted.value;
        }

        Asset asset;
        asset.name = fileName;
        asset.url = link;
        asset.size = item.mediaSize;
        asset.updatedAt = parseRfc822Date(item.pubDate);

        const int existing = keys.indexOf(key);
        if (existing >= 0) {
            Release &release = releases[existing];
            release.assets << asset;
            if (asset.updatedAt.isValid() && (!release.date.isValid() || asset.updatedAt > release.date))
                release.date = asset.updatedAt;
            continue;
        }
        Release release;
        release.tag = folder;
        release.date = asset.updatedAt;
        release.pageUrl = standardUrl + QLatin1Char('/') + filePath.mid(1, filePath.lastIndexOf(QLatin1Char('/')));
        release.assets << asset;
        releases << release;
        keys << key;
    }
    return Result<QList<Release>>::success(releases);
}

} // namespace Harpoon
