#pragma once

#include "model/error.h"
#include "net/httptransport.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace Harpoon {

// Helpers shared by the scraping and feed-based sources (GitLab, SourceHut,
// SourceForge, Jenkins, HTML, direct link, rpm-md). QtCore has no HTML
// parser, so links are pulled out with regexes, which is enough for the
// anchor-and-href pages these sources read.

// Maps a non-200 response to an Error: Network, RateLimited (429 only),
// NotFound (404, using notFoundMessage) or Http.
Error httpErrorFor(const HttpResponse &response, const QString &sourceName, const QString &notFoundMessage);

// Explanations for a repository that answers but has nothing to install.
// Users read them in the app, and so may the app's developer, so each says
// what is wrong and how a developer fixes it.
QString noReleasesMessage();
// Releases are turned off in a Forgejo/Gitea repository's settings.
// website: where the app may be published instead; empty if unknown.
QString releasesTurnedOffMessage(const QString &website);

// ObtainX ensureAbsoluteUrl: an absolute URL is returned as-is (trimmed),
// anything else is resolved against base.
QString resolveUrl(const QString &base, const QString &reference);

// Percent-decodes a URL for display and regex matching (Dart Uri.decodeFull).
QString decodeUrl(const QString &url);

// Last non-empty path segment of a URL, percent-decoded; empty if none.
QString lastPathSegment(const QString &url);

// True when the URL's path (not the query) ends in ".rpm".
bool urlPathEndsWithRpm(const QString &url);

struct HtmlLink
{
    QString url;  // href as written (entities decoded), not yet absolute
    QString text; // visible text, tags stripped and whitespace collapsed
};

// Every <a href> in the document, in document order. Comments are skipped.
QList<HtmlLink> extractAnchorLinks(const QString &html);

// URLs (http, https, ftp) found anywhere in plain text (ObtainX
// getLinksInLines). Stops at whitespace, quotes and angle brackets.
QStringList extractUrlsInText(const QString &text);

// Decodes the common named entities and numeric character references.
QString decodeHtmlEntities(const QString &text);

// First 12 hex digits of the sha256 of data: ObtainX's pseudo-version format.
QString shortSha256(const QByteArray &data);

// Parses an RSS/RFC 822 date such as "Tue, 05 Mar 2024 10:00:00 +0000" or
// "... GMT"/"UT". Returns a UTC QDateTime, or an invalid one.
QDateTime parseRfc822Date(const QString &text);

struct RssItem
{
    QString title;
    QString link;
    QString guid;
    QString pubDate;
    qint64 mediaSize = -1; // <media:content filesize="..."> (SourceForge)
};

// Every <item> of an RSS 2.0 feed, in feed order (QXmlStreamReader).
Result<QList<RssItem>> parseRssItems(const QByteArray &xml);

} // namespace Harpoon
