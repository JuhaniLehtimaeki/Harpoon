#include "sources/gitlabsource.h"

#include "sources/sourceutil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

namespace Harpoon {

namespace {

QDateTime parseIsoDate(const QJsonValue &value)
{
    if (!value.isString())
        return QDateTime();
    // Drop fractional seconds ("12:00:00.123Z"): not every Qt 5 parses them.
    static const QRegularExpression fraction(QStringLiteral("(T\\d{2}:\\d{2}:\\d{2})\\.\\d+"));
    QString text = value.toString();
    text.replace(fraction, QStringLiteral("\\1"));
    const QDateTime dt = QDateTime::fromString(text, Qt::ISODate);
    return dt.isValid() ? dt.toUTC() : QDateTime();
}

QString origin(const QString &standardUrl)
{
    const QUrl url(standardUrl);
    return QStringLiteral("%1://%2").arg(url.scheme(), url.authority());
}

void addOrReplace(QList<Asset> &assets, const Asset &asset)
{
    for (Asset &existing : assets) {
        if (existing.name == asset.name) {
            existing = asset;
            return;
        }
    }
    assets << asset;
}

} // namespace

Result<QString> GitLabSource::standardizeUrl(const QString &input) const
{
    // Everything after the "/-/" separator is a sub-page of the project.
    QStringList segments = preStandardizeUrl(input).split(QLatin1Char('/'));
    const int cut = segments.indexOf(QStringLiteral("-"));
    if (cut > 0)
        segments = segments.mid(0, cut);
    auto result = standardizeWithRegex(segments.join(QLatin1Char('/')), QStringLiteral("/[^/?#]+(/[^/?#]+){1,20}"));
    if (result.ok() && result.value.endsWith(QLatin1String(".git"), Qt::CaseInsensitive))
        result.value.chop(4);
    return result;
}

QString GitLabSource::apiBaseUrl(const QString &standardUrl)
{
    const QString path = QUrl(standardUrl).path().mid(1);
    return origin(standardUrl) + QStringLiteral("/api/v4/projects/")
           + QString::fromLatin1(QUrl::toPercentEncoding(path));
}

QString GitLabSource::withToken(const QString &url) const
{
    const QString token = configToken();
    if (token.isEmpty())
        return url;
    return url + (url.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?')) + QStringLiteral("private_token=")
           + QString::fromLatin1(QUrl::toPercentEncoding(token));
}

QString GitLabSource::authorizedAssetUrl(const QString &assetUrl) const
{
    return withToken(assetUrl);
}

HttpRequest GitLabSource::request(const QString &url, const QString &standardUrl) const
{
    HttpRequest req;
    req.url = withToken(url);
    req.setHeader("Accept", "application/json");
    // Cloudflare in front of some instances rejects requests without one.
    req.setHeader("Referer", origin(standardUrl).toUtf8());
    return req;
}

void GitLabSource::fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                                 Callback done)
{
    const QString api = apiBaseUrl(standardUrl);
    const bool trackOnly = settings.getBool(Keys::trackOnly);
    const QString name = displayName();

    transport.get(request(api, standardUrl), [this, api, standardUrl, trackOnly, name, &transport,
                                              done](const HttpResponse &projectResponse) {
        FetchResult result;
        if (projectResponse.status != 200) {
            result.error = httpErrorFor(projectResponse, name, QStringLiteral("Project not found"));
            done(result);
            return;
        }
        const QJsonObject project = QJsonDocument::fromJson(projectResponse.body).object();
        const qint64 projectId = static_cast<qint64>(project.value(QLatin1String("id")).toDouble(-1));
        if (projectId < 0) {
            result.error = Error::make(Error::Parse, QStringLiteral("Unexpected API response: no project id"));
            done(result);
            return;
        }

        // Track-only apps follow tags, as upstream (tags carry their release notes).
        const QString path = trackOnly ? QStringLiteral("/repository/tags") : QStringLiteral("/releases");
        transport.get(request(api + path + QStringLiteral("?per_page=100"), standardUrl),
                      [standardUrl, projectId, trackOnly, name, done](const HttpResponse &response) {
                          FetchResult releases;
                          if (response.status != 200) {
                              releases.error = httpErrorFor(response, name, QStringLiteral("Project not found"));
                              done(releases);
                              return;
                          }
                          const auto parsed = parseGitLabReleases(response.body, standardUrl, projectId, trackOnly);
                          releases.releases = parsed.value;
                          releases.error = parsed.error;
                          if (releases.error.ok() && releases.releases.isEmpty())
                              releases.error = Error::make(Error::NoReleases,
                                                           trackOnly ? QStringLiteral("The project has no tags")
                                                                     : noReleasesMessage());
                          done(releases);
                      });
    });
}

Result<QList<Release>> parseGitLabReleases(const QByteArray &json, const QString &standardUrl, qint64 projectId,
                                           bool fromTags)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return Result<QList<Release>>::failure(Error::make(
            Error::Parse, QStringLiteral("Unexpected API response: %1")
                              .arg(err.error != QJsonParseError::NoError ? err.errorString()
                                                                         : QStringLiteral("not a JSON array"))));

    const QString host = origin(standardUrl);
    static const QRegularExpression uploads(QStringLiteral("\\]\\((/uploads/[^)\\s]+\\.rpm)\\)"),
                                            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression artifact(QStringLiteral("^%1/-/jobs/[0-9]+/artifacts/file/[^/]+")
                                          .arg(QRegularExpression::escape(standardUrl)));

    QList<Release> releases;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject r = value.toObject();
        const QJsonObject commit = r.value(QLatin1String("commit")).toObject();
        const QJsonObject tagRelease = r.value(QLatin1String("release")).toObject();

        Release release;
        release.tag = r.value(QLatin1String(fromTags ? "name" : "tag_name")).toString();
        if (release.tag.isEmpty())
            release.tag = r.value(QLatin1String("name")).toString();
        if (!fromTags)
            release.title = r.value(QLatin1String("name")).toString();
        if (release.tag.isEmpty() && release.title.isEmpty())
            continue;

        for (const QJsonValue &v : {r.value(QLatin1String("description")), tagRelease.value(QLatin1String("description")),
                                    r.value(QLatin1String("message")), commit.value(QLatin1String("message"))}) {
            if (v.isString() && !v.toString().trimmed().isEmpty()) {
                release.changelog = v.toString().trimmed();
                break;
            }
        }
        for (const QJsonValue &v : {r.value(QLatin1String("released_at")), r.value(QLatin1String("created_at")),
                                    commit.value(QLatin1String("created_at"))}) {
            release.date = parseIsoDate(v);
            if (release.date.isValid())
                break;
        }
        release.prerelease = r.value(QLatin1String("upcoming_release")).toBool();
        release.pageUrl = r.value(QLatin1String("_links")).toObject().value(QLatin1String("self")).toString();
        if (release.pageUrl.isEmpty())
            release.pageUrl = standardUrl + (fromTags ? QStringLiteral("/-/tags/") : QStringLiteral("/-/releases/"))
                              + QString::fromLatin1(QUrl::toPercentEncoding(release.tag));

        const QJsonArray links = r.value(QLatin1String("assets")).toObject().value(QLatin1String("links")).toArray();
        for (const QJsonValue &linkValue : links) {
            const QJsonObject link = linkValue.toObject();
            Asset asset;
            asset.url = link.value(QLatin1String("direct_asset_url")).toString();
            if (asset.url.isEmpty())
                asset.url = link.value(QLatin1String("url")).toString();
            if (asset.url.isEmpty())
                continue;
            asset.name = link.value(QLatin1String("name")).toString().trimmed();
            const QString fileName = lastPathSegment(asset.url);
            if (asset.name.isEmpty()
                || (!asset.name.endsWith(QLatin1String(".rpm"), Qt::CaseInsensitive)
                    && fileName.endsWith(QLatin1String(".rpm"), Qt::CaseInsensitive)))
                asset.name = fileName;
            if (asset.name.isEmpty())
                continue;
            addOrReplace(release.assets, asset);
        }

        auto it = uploads.globalMatch(release.changelog);
        while (it.hasNext()) {
            Asset asset;
            asset.url = QStringLiteral("%1/-/project/%2%3").arg(host).arg(projectId).arg(it.next().captured(1));
            asset.name = lastPathSegment(asset.url);
            addOrReplace(release.assets, asset);
        }

        // CI job artifacts: the /file/ page is HTML, /raw/ is the file itself.
        for (Asset &asset : release.assets)
            if (artifact.match(asset.url).hasMatch())
                asset.url.replace(asset.url.indexOf(QLatin1String("/file/"), standardUrl.size()), 6,
                                  QStringLiteral("/raw/"));

        releases << release;
    }
    return Result<QList<Release>>::success(releases);
}

} // namespace Harpoon

namespace Harpoon {

void GitLabSource::prepareDownload(const Asset &asset, const AppSettings &, const QString &standardUrl,
                                   DownloadRequest &request) const
{
    if (isOwnOrigin(asset.url, standardUrl))
        request.url = authorizedAssetUrl(asset.url);
}

} // namespace Harpoon
