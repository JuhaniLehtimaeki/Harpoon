#include "sources/githubsource.h"

#include "net/ratelimit.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace Harpoon {

namespace {

QDateTime parseDate(const QJsonValue &value)
{
    if (!value.isString())
        return QDateTime();
    const QDateTime dt = QDateTime::fromString(value.toString(), Qt::ISODate);
    return dt.isValid() ? dt.toUTC() : QDateTime();
}

QDateTime firstDate(const QJsonObject &obj, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QDateTime dt = parseDate(obj.value(QLatin1String(key)));
        if (dt.isValid())
            return dt;
    }
    return QDateTime();
}

Result<QJsonArray> parseArray(const QByteArray &json)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return Result<QJsonArray>::failure(Error::make(
            Error::Parse, QStringLiteral("Unexpected API response: %1")
                              .arg(err.error != QJsonParseError::NoError ? err.errorString()
                                                                         : QStringLiteral("not a JSON array"))));
    return Result<QJsonArray>::success(doc.array());
}

} // namespace

Result<QList<Release>> parseGitHubStyleReleases(const QByteArray &json)
{
    const auto array = parseArray(json);
    if (!array.ok())
        return Result<QList<Release>>::failure(array.error);

    QList<Release> releases;
    for (const QJsonValue &value : array.value) {
        const QJsonObject r = value.toObject();
        Release release;
        release.tag = r.value(QLatin1String("tag_name")).toString();
        release.title = r.value(QLatin1String("name")).toString();
        release.changelog = r.value(QLatin1String("body")).toString();
        release.pageUrl = r.value(QLatin1String("html_url")).toString();
        release.prerelease = r.value(QLatin1String("prerelease")).toBool();
        release.draft = r.value(QLatin1String("draft")).toBool();
        release.date = firstDate(r, {"published_at", "created_at"});

        for (const QJsonValue &assetValue : r.value(QLatin1String("assets")).toArray()) {
            const QJsonObject a = assetValue.toObject();
            Asset asset;
            asset.name = a.value(QLatin1String("name")).toString();
            asset.url = a.value(QLatin1String("browser_download_url")).toString();
            asset.apiUrl = a.value(QLatin1String("url")).toString(); // GitHub only
            if (asset.url.isEmpty())
                asset.url = asset.apiUrl;
            asset.size = static_cast<qint64>(a.value(QLatin1String("size")).toDouble(-1));
            const QString digest = a.value(QLatin1String("digest")).toString();
            if (digest.startsWith(QLatin1String("sha256:")))
                asset.sha256 = digest.mid(7).toLower();
            // Forgejo assets only have created_at.
            asset.updatedAt = firstDate(a, {"updated_at", "created_at"});
            if (!asset.name.isEmpty() && !asset.url.isEmpty())
                release.assets << asset;
        }
        releases << release;
    }
    return Result<QList<Release>>::success(releases);
}

Result<QList<Release>> parseGitHubStyleTags(const QByteArray &json)
{
    const auto array = parseArray(json);
    if (!array.ok())
        return Result<QList<Release>>::failure(array.error);

    QList<Release> releases;
    for (const QJsonValue &value : array.value) {
        const QJsonObject t = value.toObject();
        Release release;
        release.tag = t.value(QLatin1String("name")).toString();
        // Forgejo includes the commit date; GitHub does not.
        release.date = parseDate(t.value(QLatin1String("commit")).toObject().value(QLatin1String("created")));
        if (!release.tag.isEmpty())
            releases << release;
    }
    return Result<QList<Release>>::success(releases);
}

Result<QString> GitHubSource::standardizeUrl(const QString &url) const
{
    auto result = standardizeWithRegex(preStandardizeUrl(url), QStringLiteral("/[^/?#]+/[^/?#]+"));
    if (result.ok() && result.value.endsWith(QLatin1String(".git"), Qt::CaseInsensitive))
        result.value.chop(4);
    return result;
}

QString GitHubSource::apiBaseUrl(const QString &standardUrl) const
{
    const QUrl url(standardUrl);
    const QString host = url.host().toLower();
    if (host == QLatin1String("github.com") || host == QLatin1String("www.github.com"))
        return QStringLiteral("https://api.github.com/repos") + url.path();
    return QStringLiteral("%1://%2/api/v3/repos%3").arg(url.scheme(), url.authority(), url.path());
}

void GitHubSource::prepareDownload(const Asset &asset, const AppSettings &, const QString &standardUrl,
                                   DownloadRequest &request) const
{
    const QString token = config().value(QStringLiteral("token")).toString().trimmed();
    if (token.isEmpty())
        return;
    const QString api = apiBaseUrl(standardUrl);
    auto trusted = [&](const QString &url) { return isOwnOrigin(url, standardUrl) || isOwnOrigin(url, api); };
    if (!asset.apiUrl.isEmpty() && trusted(asset.apiUrl)) {
        request.url = asset.apiUrl;
        request.headers.append(qMakePair(QByteArray("Accept"), QByteArray("application/octet-stream")));
    }
    if (trusted(request.url))
        request.headers.append(qMakePair(QByteArray("Authorization"), authorizationHeader(token)));
}

QByteArray GitHubSource::authorizationHeader(const QString &token) const
{
    return "Bearer " + token.toUtf8();
}

Error GitHubSource::errorForResponse(const HttpResponse &response) const
{
    if (response.isNetworkError())
        return Error::make(Error::Network, response.networkError);
    Error rateLimited = rateLimitError(response, QDateTime::currentMSecsSinceEpoch());
    if (!rateLimited.ok())
        return rateLimited;
    Error e = response.status == 404
                  ? Error::make(Error::NotFound, QStringLiteral("Repository not found"))
                  : Error::make(Error::Http, QStringLiteral("%1 API returned HTTP %2 %3")
                                                 .arg(displayName())
                                                 .arg(response.status)
                                                 .arg(response.reasonPhrase));
    e.httpStatus = response.status;
    return e;
}

void GitHubSource::getJson(const QString &url, HttpTransport &transport, bool withToken,
                           std::function<void(const Result<QByteArray> &)> done)
{
    const QString token = config().value(QStringLiteral("token")).toString().trimmed();
    const bool sendToken = withToken && !token.isEmpty();

    HttpRequest request;
    request.url = url;
    request.setHeader("Accept", "application/vnd.github+json, application/json");
    if (sendToken)
        request.setHeader("Authorization", authorizationHeader(token));

    transport.get(request, [this, url, &transport, sendToken, done](const HttpResponse &response) {
        if (response.status == 200) {
            done(Result<QByteArray>::success(response.body));
            return;
        }
        // An expired, wrong or under-privileged token should not block
        // public repositories.
        if ((response.status == 401 || (response.status == 403 && rateLimitError(response, 0).ok())) && sendToken) {
            getJson(url, transport, false, done);
            return;
        }
        done(Result<QByteArray>::failure(errorForResponse(response)));
    });
}

void GitHubSource::fetchReleases(const QString &standardUrl, const AppSettings &settings,
                                 HttpTransport &transport, Callback done)
{
    const QString api = apiBaseUrl(standardUrl);
    const bool trackOnly = settings.getBool(Keys::trackOnly);

    getJson(api + QStringLiteral("/releases?per_page=100"), transport, true,
            [this, api, trackOnly, &transport, done](const Result<QByteArray> &body) {
                FetchResult result;
                if (!body.ok()) {
                    result.error = body.error;
                    done(result);
                    return;
                }
                const auto parsed = parseGitHubStyleReleases(body.value);
                if (!parsed.ok() || !parsed.value.isEmpty() || !trackOnly) {
                    result.releases = parsed.value;
                    result.error = parsed.error;
                    if (result.error.ok() && result.releases.isEmpty())
                        result.error = Error::make(Error::NoReleases, QStringLiteral("The repository has no releases"));
                    done(result);
                    return;
                }
                // Track-only apps can follow plain tags.
                getJson(api + QStringLiteral("/tags?per_page=100"), transport, true,
                        [done](const Result<QByteArray> &tagsBody) {
                            FetchResult tagsResult;
                            if (!tagsBody.ok()) {
                                tagsResult.error = tagsBody.error;
                            } else {
                                const auto tags = parseGitHubStyleTags(tagsBody.value);
                                tagsResult.releases = tags.value;
                                tagsResult.error = tags.error;
                                if (tagsResult.error.ok() && tagsResult.releases.isEmpty())
                                    tagsResult.error = Error::make(Error::NoReleases,
                                                                   QStringLiteral("The repository has no releases or tags"));
                            }
                            done(tagsResult);
                        });
            });
}

} // namespace Harpoon
