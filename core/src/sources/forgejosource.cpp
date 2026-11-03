#include "sources/forgejosource.h"

#include "net/ratelimit.h"

#include "sources/sourceutil.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace Harpoon {

QString ForgejoSource::apiBaseUrl(const QString &standardUrl) const
{
    const QUrl url(standardUrl);
    return QStringLiteral("%1://%2/api/v1/repos%3").arg(url.scheme(), url.authority(), url.path());
}

QByteArray ForgejoSource::authorizationHeader(const QString &token) const
{
    return "token " + token.toUtf8();
}

Error ForgejoSource::errorForResponse(const HttpResponse &response) const
{
    // Forgejo answers 403 for private repositories, so only 429 means
    // rate limiting here.
    if (response.status == 429)
        return rateLimitError(response, QDateTime::currentMSecsSinceEpoch());
    if (response.isNetworkError())
        return Error::make(Error::Network, response.networkError);
    // Private repositories answer 404 (and Forgejo 403) without a token.
    const bool hasToken = !config().value(QStringLiteral("token")).toString().trimmed().isEmpty();
    const QString notFound = hasToken
                                 ? QStringLiteral("Repository not found, or the access token cannot see it")
                                 : QStringLiteral("Repository not found. If it is private, add a %1 access token "
                                                  "in Harpoon's settings.")
                                       .arg(displayName());
    const bool hidden = response.status == 404 || (response.status == 403 && id() == QLatin1String("Forgejo"));
    Error e = hidden
                  ? Error::make(Error::NotFound, notFound)
                  : Error::make(Error::Http, QStringLiteral("%1 API returned HTTP %2 %3")
                                                 .arg(displayName())
                                                 .arg(response.status)
                                                 .arg(response.reasonPhrase));
    e.httpStatus = response.status;
    return e;
}

void ForgejoSource::explainReleasesNotFound(const QString &api, HttpTransport &transport, const Error &error,
                                            std::function<void(const Error &)> done)
{
    getJson(api, transport, true, [error, done](const Result<QByteArray> &repo) {
        const QJsonObject info = repo.ok() ? QJsonDocument::fromJson(repo.value).object() : QJsonObject();
        // Older Gitea has no has_releases: then the 404 stays unexplained.
        if (info.value(QStringLiteral("has_releases")).isBool() && !info.value(QStringLiteral("has_releases")).toBool()) {
            const QString website = info.value(QStringLiteral("website")).toString().trimmed();
            const bool webLink = website.startsWith(QLatin1String("https://")) || website.startsWith(QLatin1String("http://"));
            done(Error::make(Error::NoReleases, releasesTurnedOffMessage(webLink ? website : QString())));
            return;
        }
        done(error);
    });
}

} // namespace Harpoon
