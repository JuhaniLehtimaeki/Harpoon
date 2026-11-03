#include "net/ratelimit.h"

#include <QDateTime>

#include <cmath>

namespace Harpoon {

Error rateLimitError(const HttpResponse &response, qint64 nowMsecs)
{
    const QString reason = response.reasonPhrase.toLower();
    const QString body = QString::fromUtf8(response.body.left(1000)).toLower();
    const auto mentions = [&](const QString &text) {
        return text.contains(QLatin1String("rate limit")) || text.contains(QLatin1String("too many requests"));
    };
    // A 403 alone is not a rate limit: GitHub also answers 403 for tokens
    // without access, SSO enforcement and blocked repositories.
    const bool isRateLimit = response.header("x-ratelimit-remaining") == "0" || response.status == 429
                             || (response.status == 403 && !response.header("retry-after").isEmpty())
                             || mentions(reason) || mentions(body);
    if (!isRateLimit)
        return Error();

    const qint64 nowSecs = nowMsecs / 1000;
    bool ok = false;
    qint64 resetSecs = response.header("x-ratelimit-reset").toLongLong(&ok);
    if (!ok) {
        const QByteArray header = response.header("retry-after");
        const qint64 retryAfter = header.toLongLong(&ok);
        // Or an HTTP date (RFC 7231), "Wed, 21 Oct 2026 07:28:00 GMT".
        const QDateTime at = ok ? QDateTime() : QDateTime::fromString(QString::fromLatin1(header).trimmed(), Qt::RFC2822Date);
        if (at.isValid())
            resetSecs = at.toMSecsSinceEpoch() / 1000;
        else
            resetSecs = nowSecs + (ok ? retryAfter : 1800);
    }
    const int minutes = qBound<qint64>(1, static_cast<qint64>(std::ceil((resetSecs - nowSecs) / 60.0)), 9999);

    Error e = Error::make(Error::RateLimited,
                          QStringLiteral("Rate limited; try again in %1 minute(s)").arg(minutes));
    e.httpStatus = response.status;
    e.retryAfterMinutes = minutes;
    return e;
}

} // namespace Harpoon
