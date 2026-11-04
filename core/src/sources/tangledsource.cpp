#include "sources/tangledsource.h"

#include "sources/sourceutil.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <memory>

namespace Harpoon {

namespace {

const int kMaxTags = 6; // tag pages read after the feed, like SourceHut

// The feed writes <updated> with millisecond precision, which Qt 5.6's
// ISODate parsing refuses; the fraction is cut before parsing.
QDateTime parseAtomDate(const QString &updated)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})(?:\\.\\d+)?(Z|[+-]\\d{2}:?\\d{2})?$"));
    const auto match = pattern.match(updated.trimmed());
    if (!match.hasMatch())
        return QDateTime();
    return QDateTime::fromString(match.captured(1) + match.captured(2), Qt::ISODate).toUTC();
}

QString stripTags(const QString &html)
{
    static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
    QString out = html;
    out.remove(tag);
    return decodeHtmlEntities(out).simplified();
}

// Where a feed link points: the repository ("handle/repo", lower case, without
// the "@" some Tangled URLs put before a handle) and, when it is a tag page,
// the tag's name. Comparing these, not the typed address, keeps entries that
// differ only in letter case or the "@" from being dropped.
struct TagLink
{
    QString host;
    QString repo;
    QString tag; // empty when the link is not a tag page
};

TagLink parseTagLink(const QString &link)
{
    TagLink out;
    const QUrl url(link);
    out.host = url.host().toLower();
    if (out.host.startsWith(QLatin1String("www.")))
        out.host = out.host.mid(4);
    QStringList segments = url.path().split(QLatin1Char('/'), QString::SkipEmptyParts);
    if (segments.size() < 2)
        return out;
    QString handle = segments.at(0);
    if (handle.startsWith(QLatin1Char('@')))
        handle = handle.mid(1);
    out.repo = (handle + QLatin1Char('/') + segments.at(1)).toLower();
    if (segments.size() >= 4 && segments.at(2) == QLatin1String("tags")) {
        segments = segments.mid(3);
        out.tag = decodeUrl(segments.join(QLatin1Char('/')));
    }
    return out;
}

// The feed entry content is "Tag {name} created by {author}:\n\n{message}";
// the message part carries the release notes.
QString messageFromContent(const QString &content)
{
    const int split = content.indexOf(QStringLiteral("\n\n"));
    return split < 0 ? QString() : content.mid(split + 2).trimmed();
}

// The artifacts listed on a tag page, under the tagArtifacts marker: one row
// per settingsRowTitle, whose download link follows the file name.
QList<Asset> artifactsOf(const QString &page, const QString &baseUrl)
{
    QList<Asset> assets;
    const int artifactsAt = page.indexOf(QStringLiteral("data-testid=\"tagArtifacts\""));
    if (artifactsAt < 0)
        return assets;
    const QString section = page.mid(artifactsAt);
    static const QRegularExpression rowPattern(
        QStringLiteral("data-testid=\"settingsRowTitle\"[^>]*>([^<]+)</span>"));
    QRegularExpressionMatch row = rowPattern.match(section);
    while (row.hasMatch()) {
        const int rowStart = row.capturedStart();
        const QRegularExpressionMatch next = rowPattern.match(section, rowStart + row.capturedLength());
        const QString rowChunk = section.mid(rowStart, (next.hasMatch() ? next.capturedStart() : section.size()) - rowStart);
        static const QRegularExpression linkPattern(QStringLiteral("href=\"([^\"]*\\/artifacts\\/[^\"?]*)\""));
        const QRegularExpressionMatch link = linkPattern.match(rowChunk);
        if (link.hasMatch()) {
            Asset asset;
            asset.name = stripTags(row.captured(1));
            asset.url = resolveUrl(baseUrl, decodeHtmlEntities(link.captured(1)));
            if (!asset.name.isEmpty() && !asset.url.isEmpty())
                assets << asset;
        }
        row = next;
    }
    return assets;
}

} // namespace

Result<QString> TangledSource::standardizeUrl(const QString &url) const
{
    return standardizeWithRegex(preStandardizeUrl(url), QStringLiteral("/[^/?#]+/[^/?#]+"));
}

void TangledSource::fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                                  Callback done)
{
    const bool fallback = settings.getBool(Keys::fallbackToOlderReleases, true);
    HttpRequest request;
    request.url = standardUrl + QStringLiteral("/feed.atom?include=tags");
    transport.get(request, [this, standardUrl, fallback, &transport, done](const HttpResponse &response) {
        FetchResult result;
        if (response.status != 200) {
            result.error = httpErrorFor(response, displayName(), QStringLiteral("Repository not found"));
            done(result);
            return;
        }
        const auto entries = parseAtomEntries(response.body);
        if (!entries.ok()) {
            result.error = entries.error;
            done(result);
            return;
        }

        auto releases = std::make_shared<QList<Release>>();
        const TagLink own = parseTagLink(standardUrl);
        for (const AtomEntry &entry : entries.value) {
            // Only this repository's tag entries (as with SourceHut's guid),
            // on this site: the page is fetched from the link.
            QString page;
            TagLink target;
            for (const QString &candidate : {entry.link, entry.id}) {
                const TagLink t = parseTagLink(candidate);
                if (!t.tag.isEmpty() && t.repo == own.repo && t.host == own.host) {
                    page = candidate;
                    target = t;
                    break;
                }
            }
            if (page.isEmpty())
                continue;
            Release release;
            release.tag = target.tag;
            release.pageUrl = page;
            release.date = parseAtomDate(entry.updated);
            // Like the tags page: the message's first line is the title, the
            // rest is the changelog.
            const QString message = messageFromContent(entry.content);
            const int lineEnd = message.indexOf(QLatin1Char('\n'));
            release.title = lineEnd < 0 ? message : message.left(lineEnd).trimmed();
            release.changelog = lineEnd < 0 ? QString() : message.mid(lineEnd + 1).trimmed();
            *releases << release;
        }
        if (releases->isEmpty()) {
            result.error = Error::make(Error::NoReleases, QStringLiteral("The repository has no tags"));
            done(result);
            return;
        }
        // Newest first, whatever order the feed lists them in: the newest are
        // the ones worth a page fetch. (Entries without a date go last.)
        std::stable_sort(releases->begin(), releases->end(), [](const Release &a, const Release &b) {
            if (a.date.isValid() != b.date.isValid())
                return a.date.isValid();
            return a.date > b.date;
        });
        // Without fallback only the newest tag can be chosen: skip the other
        // page fetches.
        const int limit = fallback ? kMaxTags : 1;
        while (releases->size() > limit)
            releases->removeLast();
        fetchTagPages(releases, 0, standardUrl, transport, done);
    });
}

void TangledSource::fetchTagPages(std::shared_ptr<QList<Release>> releases, int index, const QString &standardUrl,
                                  HttpTransport &transport, Callback done)
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
        // A missing tag page just has no assets, as SourceHut's ref pages.
        // Anything else that is not a page fails the check, so the selector
        // does not fall back to an older tag only because the newest page
        // did not load.
        if (response.status != 200 && response.status != 404 && response.status != 410) {
            FetchResult result;
            result.error = httpErrorFor(response, displayName(), QString());
            done(result);
            return;
        }
        if (response.status == 200) {
            Release &release = (*releases)[index];
            const QString base = response.finalUrl.isEmpty() ? release.pageUrl : response.finalUrl;
            release.assets = artifactsOf(QString::fromUtf8(response.body), base);
        }
        fetchTagPages(releases, index + 1, standardUrl, transport, done);
    });
}

} // namespace Harpoon
