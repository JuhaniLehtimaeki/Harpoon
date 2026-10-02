#include "sources/htmlsource.h"

#include "pipeline/assetfilter.h"
#include "sources/sourceutil.h"
#include "version/versioncompare.h"
#include "version/versionextractor.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace Harpoon {

namespace {

const int kMaxIntermediateLinks = 10;

void collectJsonStrings(const QJsonValue &value, QStringList &out)
{
    if (value.isString()) {
        out << value.toString();
    } else if (value.isArray()) {
        for (const QJsonValue &item : value.toArray())
            collectJsonStrings(item, out);
    } else if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
            collectJsonStrings(it.value(), out);
    }
}

QList<PageLink> linksFromUrls(const QStringList &urls)
{
    QList<PageLink> links;
    for (const QString &url : urls)
        links << PageLink{url, url.section(QLatin1Char('/'), -1)};
    return links;
}

QString stripPackageExtension(const QString &name)
{
    static const QRegularExpression ext(QStringLiteral("\\.(?:rpm|zip|tar(?:\\.gz)?)$"),
                                        QRegularExpression::CaseInsensitiveOption);
    QString out = name;
    out.remove(ext);
    return out;
}

// ObtainX compareReleaseNames.
int compareReleaseNames(const QString &a, const QString &b)
{
    const VersionDecision d = compareVersionStrings(stripPackageExtension(a), stripPackageExtension(b));
    return d.isOrdered() ? d.comparison() : compareAlphaNumeric(a, b);
}

// The decoded URL without RPM arch tokens: links that only differ by arch
// share this key.
QString archNeutralKey(const QString &url)
{
    static const QRegularExpression arch(QStringLiteral(
        "(?<![a-z0-9])(aarch64|arm64|armv7hl|armv7l|armhf|i486|i586|i686|x86_64|amd64|noarch)(?![a-z0-9])"));
    QString key = decodeUrl(url).toLower();
    key.remove(arch);
    return key;
}

QString assetNameFor(const PageLink &link)
{
    const QString segment = lastPathSegment(link.url);
    if (segment.endsWith(QLatin1String(".rpm"), Qt::CaseInsensitive))
        return segment;
    const QString text = link.text.section(QLatin1Char('/'), -1).trimmed();
    if (text.endsWith(QLatin1String(".rpm"), Qt::CaseInsensitive))
        return text;
    return (segment.isEmpty() ? QUrl(link.url).host() : segment) + QStringLiteral(".rpm");
}

QVariantList intermediateHops(const AppSettings &settings)
{
    const QVariant raw = settings.values().value(QString::fromLatin1(Keys::intermediateLink));
    const QVariantList all = raw.type() == QVariant::Map ? QVariantList{raw} : raw.toList();
    QVariantList hops;
    for (const QVariant &hop : all)
        if (!hop.toMap().value(QString::fromLatin1(Keys::customLinkFilterRegex)).toString().isEmpty())
            hops << hop;
    return hops.mid(0, kMaxIntermediateLinks);
}

HttpRequest pageRequest(const QString &url, const AppSettings &settings)
{
    HttpRequest request;
    request.url = url;
    request.headers = parseRequestHeaders(settings.values().value(QString::fromLatin1(Keys::requestHeader)));
    return request;
}

} // namespace

QList<QPair<QByteArray, QByteArray>> parseRequestHeaders(const QVariant &value)
{
    QStringList lines;
    const QVariantList items = value.type() == QVariant::List ? value.toList() : QVariantList{value};
    for (const QVariant &item : items) {
        const QString text = item.type() == QVariant::Map
                                 ? item.toMap().value(QString::fromLatin1(Keys::requestHeader)).toString()
                                 : item.toString();
        lines << text.split(QLatin1Char('\n'));
    }
    QList<QPair<QByteArray, QByteArray>> headers;
    for (const QString &line : lines) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString name = line.left(colon).trimmed();
        const QString headerValue = line.mid(colon + 1).trimmed();
        if (!name.isEmpty() && !headerValue.isEmpty())
            headers.append(qMakePair(name.toUtf8(), headerValue.toUtf8()));
    }
    return headers;
}

Result<QList<PageLink>> grabLinks(const QString &body, const QString &baseUrl, const AppSettings &options)
{
    QList<PageLink> all;
    for (const HtmlLink &anchor : extractAnchorLinks(body)) {
        if (anchor.url.isEmpty())
            continue;
        all << PageLink{resolveUrl(baseUrl, anchor.url),
                        anchor.text.isEmpty() ? anchor.url.section(QLatin1Char('/'), -1) : anchor.text};
    }
    if (all.isEmpty() || options.getBool(Keys::matchLinksOutsideATags)) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && !doc.isNull()) {
            QStringList strings;
            collectJsonStrings(doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()), strings);
            QStringList urls = extractUrlsInText(strings.join(QLatin1Char('\n')));
            if (urls.isEmpty()) {
                // Relative paths in JSON values.
                QStringList absolute;
                for (const QString &s : strings)
                    absolute << resolveUrl(baseUrl, s);
                urls = extractUrlsInText(absolute.join(QLatin1Char('\n')));
            }
            all = linksFromUrls(urls);
        } else {
            all = linksFromUrls(extractUrlsInText(body));
        }
    }

    const bool byText = options.getBool(Keys::filterByLinkText);
    const QString custom = options.getString(Keys::customLinkFilterRegex);
    const QRegularExpression filter(custom);
    if (!custom.isEmpty() && !filter.isValid())
        return Result<QList<PageLink>>::failure(Error::make(
            Error::InvalidSetting, QStringLiteral("Invalid link filter regex: %1").arg(filter.errorString())));

    QList<PageLink> links;
    for (const PageLink &link : all) {
        const QString subject = byText ? link.text : decodeUrl(link.url);
        const bool keep = custom.isEmpty() ? isInstallableRpm(QUrl(subject.trimmed()).path())
                                           : filter.match(subject).hasMatch();
        if (keep)
            links << link;
    }

    if (!options.getBool(Keys::skipSort)) {
        const bool lastSegment = options.getBool(Keys::sortByLastLinkSegment);
        auto key = [lastSegment](const PageLink &link) {
            if (!lastSegment)
                return link.url;
            const QStringList parts = link.url.split(QLatin1Char('/'));
            for (int i = parts.size() - 1; i >= 0; --i)
                if (!parts.at(i).isEmpty())
                    return parts.at(i);
            return link.url;
        };
        QStringList keys;
        for (const PageLink &link : links)
            keys << stripPackageExtension(key(link));
        const bool versionOrder = versionsHaveConsistentOrder(keys);
        std::stable_sort(links.begin(), links.end(), [&](const PageLink &a, const PageLink &b) {
            const QString ka = key(a);
            const QString kb = key(b);
            return (versionOrder ? compareReleaseNames(ka, kb) : compareAlphaNumeric(ka, kb)) < 0;
        });
    }
    if (options.getBool(Keys::reverseSort))
        std::reverse(links.begin(), links.end());
    return Result<QList<PageLink>>::success(links);
}

bool HtmlSource::matchesUrlShape(const QString &url) const
{
    return standardizeUrl(url).ok();
}

Result<QString> HtmlSource::standardizeUrl(const QString &input) const
{
    const QString url = preStandardizeUrl(input);
    const QUrl parsed(url);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() || parsed.host().isEmpty()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https")))
        return Result<QString>::failure(
            Error::make(Error::InvalidUrl, QStringLiteral("Not a valid %1 URL: %2").arg(displayName(), input)));
    return Result<QString>::success(url);
}

void HtmlSource::fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                               Callback done)
{
    followHops(intermediateHops(settings), 0, standardUrl, settings, transport, done);
}

void HtmlSource::followHops(const QVariantList &hops, int index, const QString &url, const AppSettings &settings,
                            HttpTransport &transport, Callback done)
{
    transport.get(pageRequest(url, settings), [this, hops, index, url, settings, &transport,
                                               done](const HttpResponse &response) {
        FetchResult result;
        if (response.status != 200) {
            result.error = httpErrorFor(response, displayName(), QStringLiteral("Page not found: %1").arg(url));
            done(result);
            return;
        }
        const QString base = response.finalUrl.isEmpty() ? url : response.finalUrl;
        const QString body = QString::fromUtf8(response.body);
        const bool isHop = index < hops.size();
        auto links = grabLinks(body, base, isHop ? AppSettings(hops.at(index).toMap()) : settings);
        if (!links.ok()) {
            result.error = links.error;
            done(result);
            return;
        }

        if (isHop) {
            if (links.value.isEmpty()) {
                result.error = Error::make(
                    Error::NoReleases,
                    QStringLiteral("No link matched intermediate step %1 on %2").arg(index + 1).arg(url));
                done(result);
                return;
            }
            followHops(hops, index + 1, links.value.last().url, settings, transport, done);
            return;
        }

        // ObtainX filters the final links by apkFilterRegEx before choosing
        // the last one; AssetFilter applies the same filter again later.
        const QString assetFilter = settings.getString(Keys::assetFilterRegEx);
        if (!assetFilter.isEmpty()) {
            const QRegularExpression pattern(assetFilter);
            if (!pattern.isValid()) {
                result.error = Error::make(Error::InvalidSetting,
                                           QStringLiteral("Invalid asset filter regex: %1").arg(pattern.errorString()));
                done(result);
                return;
            }
            const bool invert = settings.getBool(Keys::invertAssetFilter);
            QList<PageLink> kept;
            for (const PageLink &link : links.value) {
                const bool hit = pattern.match(assetNameFor(link)).hasMatch() || pattern.match(link.url).hasMatch();
                if (hit != invert)
                    kept << link;
            }
            links.value = kept;
        }
        if (links.value.isEmpty()) {
            result.error = Error::make(Error::NoReleases, QStringLiteral("No matching links found on %1").arg(url));
            done(result);
            return;
        }
        finish(url, body, links.value, settings, transport, done);
    });
}

void HtmlSource::finish(const QString &pageUrl, const QString &pageBody, const QList<PageLink> &links,
                        const AppSettings &settings, HttpTransport &transport, Callback done)
{
    const PageLink selected = links.last();
    const QString regex = settings.getString(Keys::versionExtractionRegEx);
    const QString matchGroup = settings.getString(Keys::matchGroupToUse);
    const bool wholePage = settings.getBool(Keys::versionExtractWholePage) && !pageBody.isNull();

    Release release;
    release.title = selected.text;
    release.pageUrl = pageUrl;

    // Links of the selected version.
    QList<PageLink> group;
    if (!regex.isEmpty() && !wholePage) {
        const auto version = extractVersion(regex, matchGroup, decodeUrl(selected.url));
        if (!version.ok()) {
            FetchResult result;
            result.error = version.error;
            done(result);
            return;
        }
        for (const PageLink &link : links) {
            const auto v = extractVersion(regex, matchGroup, decodeUrl(link.url));
            if (v.ok() && v.value == version.value)
                group << link;
        }
    } else {
        const QString key = archNeutralKey(selected.url);
        for (const PageLink &link : links)
            if (archNeutralKey(link.url) == key)
                group << link;
    }
    for (const PageLink &link : group) {
        Asset asset;
        asset.name = assetNameFor(link);
        asset.url = link.url;
        release.assets << asset;
    }

    if (!regex.isEmpty()) {
        if (!wholePage) {
            release.tag = decodeUrl(selected.url);
        } else {
            QString page = pageBody;
            page.replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1String("\n"), QLatin1String("\\n"));
            const auto full = extractVersion(regex, matchGroup, page);
            if (!full.ok()) {
                FetchResult result;
                result.error = full.error;
                done(result);
                return;
            }
            // Keep the tag short when the regex's own match reproduces the version.
            QRegularExpressionMatch last;
            auto it = QRegularExpression(regex).globalMatch(page);
            while (it.hasNext())
                last = it.next();
            const auto shortened = extractVersion(regex, matchGroup, last.captured(0));
            release.tag = shortened.ok() && shortened.value == full.value ? last.captured(0) : page;
        }
        FetchResult result;
        result.releases << release;
        done(result);
        return;
    }

    // Pseudo-version.
    const QString method = settings.getString(Keys::defaultPseudoVersioningMethod, QStringLiteral("ETag"));
    if (method == QLatin1String("linkHash") || method == QLatin1String("APKLinkHash")) {
        release.tag = shortSha256(selected.url.toUtf8());
        FetchResult result;
        result.releases << release;
        done(result);
        return;
    }
    HttpRequest probe = pageRequest(selected.url, settings);
    probe.setHeader("Range", "bytes=0-0"); // only the headers are needed
    transport.get(probe, [this, release, selected, done](const HttpResponse &response) {
        FetchResult result;
        if (response.status < 200 || response.status >= 300) {
            result.error = httpErrorFor(response, displayName(), QStringLiteral("File not found: %1").arg(selected.url));
            done(result);
            return;
        }
        QByteArray etag = response.header("etag");
        etag.replace('"', QByteArray());
        Release withVersion = release;
        withVersion.tag = shortSha256(etag.isEmpty() ? selected.url.toUtf8() : etag);
        result.releases << withVersion;
        done(result);
    });
}

} // namespace Harpoon

namespace Harpoon {

void HtmlSource::prepareDownload(const Asset &, const AppSettings &settings, DownloadRequest &request) const
{
    request.headers.append(parseRequestHeaders(settings.values().value(QString::fromLatin1(Keys::requestHeader))));
}

} // namespace Harpoon
