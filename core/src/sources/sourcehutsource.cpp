#include "sources/sourcehutsource.h"

#include "sources/sourceutil.h"

#include <memory>

namespace Harpoon {

namespace {
const int kMaxRefs = 6; // upstream reads the first 6 feed items
} // namespace

Result<QString> SourceHutSource::standardizeUrl(const QString &url) const
{
    return standardizeWithRegex(preStandardizeUrl(url), QStringLiteral("/[^/?#]+/[^/?#]+"));
}

void SourceHutSource::fetchReleases(const QString &standardUrl, const AppSettings &settings,
                                    HttpTransport &transport, Callback done)
{
    const bool fallback = settings.getBool(Keys::fallbackToOlderReleases, true);
    HttpRequest request;
    request.url = standardUrl + QStringLiteral("/refs/rss.xml");
    transport.get(request, [this, standardUrl, fallback, &transport, done](const HttpResponse &response) {
        FetchResult result;
        if (response.status != 200) {
            result.error = httpErrorFor(response, displayName(), QStringLiteral("Repository not found"));
            done(result);
            return;
        }
        const auto items = parseRssItems(response.body);
        if (!items.ok()) {
            result.error = items.error;
            done(result);
            return;
        }

        auto releases = std::make_shared<QList<Release>>();
        const QString refsPrefix = standardUrl + QStringLiteral("/refs");
        for (int i = 0; i < items.value.size() && i < kMaxRefs; ++i) {
            const RssItem &item = items.value.at(i);
            const QString page = item.guid.isEmpty() ? item.link : item.guid;
            if (!page.startsWith(refsPrefix))
                continue;
            Release release;
            release.tag = item.title;
            release.pageUrl = page;
            release.date = parseRfc822Date(item.pubDate);
            if (release.tag.isEmpty())
                continue;
            *releases << release;
            // Without fallback only the newest ref can be chosen; skip the
            // other page fetches.
            if (!fallback)
                break;
        }
        if (releases->isEmpty()) {
            result.error = Error::make(Error::NoReleases, QStringLiteral("The repository has no refs"));
            done(result);
            return;
        }
        fetchRefPages(releases, 0, standardUrl, transport, done);
    });
}

void SourceHutSource::fetchRefPages(std::shared_ptr<QList<Release>> releases, int index,
                                    const QString &standardUrl, HttpTransport &transport, Callback done)
{
    if (index >= releases->size()) {
        FetchResult result;
        result.releases = *releases;
        done(result);
        return;
    }
    HttpRequest request;
    request.url = releases->at(index).pageUrl;
    transport.get(request, [this, releases, index, standardUrl, &transport, done](const HttpResponse &response) {
        // A missing ref page just has no assets, as upstream. A network or
        // server error fails the check instead, so the selector does not fall
        // back to an older ref only because the newest page did not load.
        if (response.isNetworkError() || response.status >= 500) {
            FetchResult result;
            result.error = httpErrorFor(response, displayName(), QString());
            done(result);
            return;
        }
        if (response.status == 200) {
            Release &release = (*releases)[index];
            const QString base = response.finalUrl.isEmpty() ? release.pageUrl : response.finalUrl;
            for (const HtmlLink &link : extractAnchorLinks(QString::fromUtf8(response.body))) {
                Asset asset;
                asset.url = resolveUrl(base, link.url);
                if (!urlPathEndsWithRpm(asset.url))
                    continue;
                asset.name = lastPathSegment(asset.url);
                bool duplicate = false;
                for (const Asset &existing : release.assets)
                    duplicate = duplicate || existing.url == asset.url;
                if (!duplicate)
                    release.assets << asset;
            }
        }
        fetchRefPages(releases, index + 1, standardUrl, transport, done);
    });
}

} // namespace Harpoon
