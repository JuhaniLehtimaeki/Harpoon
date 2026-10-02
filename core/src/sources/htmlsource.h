#pragma once

#include "sources/source.h"

#include <QVariant>

namespace Harpoon {

struct PageLink
{
    QString url;  // absolute
    QString text; // link text, or the URL's last segment when there is none
};

// ObtainX grabLinksCommon: collects links from a page and filters and sorts
// them by the link options in `options` (the app settings, or one
// intermediateLink hop):
//  - every <a href>, made absolute against baseUrl; when there is none, or
//    with matchLinksOutsideATags, bare URLs from the JSON strings or the text
//  - customLinkFilterRegex against the decoded URL (or the link text with
//    filterByLinkText); without one, installable .rpm links only
//  - sorted ascending unless skipSort: version order when the keys (URLs, or
//    last segments with sortByLastLinkSegment) are consistently ordered,
//    natural alphanumeric order otherwise; reverseSort flips the result
// The newest link is last. An invalid regex is an InvalidSetting error.
Result<QList<PageLink>> grabLinks(const QString &body, const QString &baseUrl, const AppSettings &options);

// requestHeader setting: a string of "Name: value" lines, a list of such
// strings, or ObtainX's list of {"requestHeader": "Name: value"} maps.
QList<QPair<QByteArray, QByteArray>> parseRequestHeaders(const QVariant &value);

// Generic web page scraper, the catch-all source (ObtainX
// lib/app_sources/html.dart). Must stay last in the registry.
//
//  1. Follows up to 10 intermediateLink hops: each hop loads the current
//     page, applies grabLinks() with its own options and continues with the
//     last link.
//  2. Loads the final page, applies grabLinks() with the app settings and
//     the assetFilterRegEx (as upstream filterApks), and selects the last
//     link.
//  3. Returns a single Release. Its assets are the selected link and the
//     links that belong to the same version: with versionExtractionRegEx,
//     links whose decoded URL extracts to the same version; otherwise links
//     that differ from the selected one only by an RPM arch token
//     ("app-1.2-1.aarch64.rpm" / "app-1.2-1.armv7hl.rpm"). AssetFilter then
//     picks the device's package as for any forge.
//
// Version (Release.tag). ReleasePipeline applies versionExtractionRegEx to
// the tag, so the source never extracts itself:
//  - with versionExtractionRegEx: the decoded selected URL, or with
//    versionExtractWholePage the page text (newlines escaped as "\n", as
//    upstream). For the whole page the tag is shortened to the regex's own
//    match when re-extracting from that match gives the same version.
//  - without one: a pseudo-version (upstream defaultPseudoVersioningMethod):
//    "ETag" (default) = first 12 hex of sha256 of the selected link's ETag,
//    fetched with "Range: bytes=0-0"; "linkHash" = first 12 hex of sha256 of
//    the link URL. A response without an ETag falls back to the link hash.
//    Upstream's partial-download hash is not ported.
//
// requestHeader adds headers to every page request (including the ETag
// probe). Unlike upstream there is no Android Chrome User-Agent default; the
// transport's "Harpoon/<version> (SailfishOS)" is sent unless overridden.
// Links whose file name is not *.rpm (e.g. "download.php?id=3" picked by
// customLinkFilterRegex) get ".rpm" appended to the asset name so AssetFilter
// keeps them; the RPM header is checked after download anyway.
class HtmlSource : public Source
{
public:
    QString id() const override { return QStringLiteral("HTML"); }
    QString displayName() const override { return QStringLiteral("HTML"); }
    QStringList defaultHosts() const override { return {}; }
    bool matchesUrlShape(const QString &url) const override;

    Result<QString> standardizeUrl(const QString &url) const override;
    void fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                       Callback done) override;
    // Sends the requestHeader setting with the download too.
    void prepareDownload(const Asset &asset, const AppSettings &settings, DownloadRequest &request) const override;

protected:
    // Builds the release from the selected links (step 3 above). pageBody is
    // only used for versionExtractWholePage.
    void finish(const QString &pageUrl, const QString &pageBody, const QList<PageLink> &links,
                const AppSettings &settings, HttpTransport &transport, Callback done);

private:
    void followHops(const QVariantList &hops, int index, const QString &url, const AppSettings &settings,
                    HttpTransport &transport, Callback done);
};

} // namespace Harpoon
