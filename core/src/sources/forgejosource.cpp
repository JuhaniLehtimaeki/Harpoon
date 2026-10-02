#include "sources/forgejosource.h"

#include "net/ratelimit.h"

#include <QDateTime>
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
    Error e = response.status == 404
                  ? Error::make(Error::NotFound, QStringLiteral("Repository not found"))
                  : Error::make(Error::Http, QStringLiteral("%1 API returned HTTP %2 %3")
                                                 .arg(displayName())
                                                 .arg(response.status)
                                                 .arg(response.reasonPhrase));
    e.httpStatus = response.status;
    return e;
}

} // namespace Harpoon
