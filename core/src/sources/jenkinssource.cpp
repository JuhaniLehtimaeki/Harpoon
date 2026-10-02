#include "sources/jenkinssource.h"

#include "sources/sourceutil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

namespace Harpoon {

Result<QString> JenkinsSource::standardizeUrl(const QString &url) const
{
    // An optional path prefix (not "job"), then one or more /job/{name} pairs.
    static const QRegularExpression pattern(
        QStringLiteral("^https?://[^/?#]+(/(?!job/)[^/?#]+)*?(/job/[^/?#]+)+"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(preStandardizeUrl(url));
    if (!match.hasMatch())
        return Result<QString>::failure(
            Error::make(Error::InvalidUrl, QStringLiteral("Not a Jenkins job URL: %1").arg(url)));
    return Result<QString>::success(match.captured(0));
}

void JenkinsSource::fetchReleases(const QString &standardUrl, const AppSettings &, HttpTransport &transport,
                                  Callback done)
{
    HttpRequest request;
    request.url = standardUrl + QStringLiteral("/lastSuccessfulBuild/api/json");
    request.setHeader("Accept", "application/json");
    transport.get(request, [this, standardUrl, done](const HttpResponse &response) {
        FetchResult result;
        if (response.status != 200) {
            result.error = httpErrorFor(response, displayName(),
                                        QStringLiteral("The job does not exist or has no successful build"));
        } else {
            const auto build = parseJenkinsBuild(response.body, standardUrl);
            if (build.ok())
                result.releases << build.value;
            result.error = build.error;
        }
        done(result);
    });
}

Result<Release> parseJenkinsBuild(const QByteArray &json, const QString &jobUrl)
{
    QJsonParseError err;
    const QJsonObject build = QJsonDocument::fromJson(json, &err).object();
    if (err.error != QJsonParseError::NoError)
        return Result<Release>::failure(
            Error::make(Error::Parse, QStringLiteral("Unexpected API response: %1").arg(err.errorString())));
    const QJsonValue number = build.value(QLatin1String("number"));
    if (!number.isDouble())
        return Result<Release>::failure(Error::make(Error::NoVersion, QStringLiteral("The build has no number")));

    Release release;
    release.tag = QString::number(static_cast<qint64>(number.toDouble()));
    release.title = build.value(QLatin1String("fullDisplayName")).toString();
    release.pageUrl = build.value(QLatin1String("url")).toString();
    const QJsonValue timestamp = build.value(QLatin1String("timestamp"));
    if (timestamp.isDouble())
        release.date = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestamp.toDouble()), Qt::UTC);

    // Freestyle jobs report "changeSet", pipelines "changeSets".
    QJsonArray changeSets = build.value(QLatin1String("changeSets")).toArray();
    if (build.value(QLatin1String("changeSet")).isObject())
        changeSets.append(build.value(QLatin1String("changeSet")));
    QStringList messages;
    for (const QJsonValue &set : changeSets)
        for (const QJsonValue &item : set.toObject().value(QLatin1String("items")).toArray()) {
            const QString msg = item.toObject().value(QLatin1String("msg")).toString().trimmed();
            if (!msg.isEmpty())
                messages << QStringLiteral("- ") + msg;
        }
    release.changelog = messages.join(QLatin1Char('\n'));

    const QString buildBase = jobUrl + QLatin1Char('/') + release.tag + QStringLiteral("/artifact/");
    for (const QJsonValue &value : build.value(QLatin1String("artifacts")).toArray()) {
        const QJsonObject artifact = value.toObject();
        const QString relativePath = artifact.value(QLatin1String("relativePath")).toString();
        if (relativePath.isEmpty())
            continue;
        QStringList encoded;
        for (const QString &segment : relativePath.split(QLatin1Char('/')))
            encoded << QString::fromLatin1(QUrl::toPercentEncoding(segment));
        Asset asset;
        asset.name = artifact.value(QLatin1String("fileName")).toString();
        if (asset.name.isEmpty())
            asset.name = relativePath.section(QLatin1Char('/'), -1);
        asset.url = buildBase + encoded.join(QLatin1Char('/'));
        asset.updatedAt = release.date;
        release.assets << asset;
    }
    return Result<Release>::success(release);
}

} // namespace Harpoon
