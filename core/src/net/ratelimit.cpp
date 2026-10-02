#include "net/ratelimit.h"

#include <cmath>

namespace Harpoon {

Error rateLimitError(const HttpResponse &response, qint64 nowMsecs)
{
    const QString reason = response.reasonPhrase.toLower();
    const QString body = QString::fromUtf8(response.body.left(1000)).toLower();
    const auto mentions = [&](const QString &text) {
        return text.contains(QLatin1String("rate limit")) || text.contains(QLatin1String("too many requests"));
    };
    const bool isRateLimit = response.header("x-ratelimit-remaining") == "0" || response.status == 429
                             || response.status == 403 || mentions(reason) || mentions(body);
    if (!isRateLimit)
        return Error();

    const qint64 nowSecs = nowMsecs / 1000;
    bool ok = false;
    qint64 resetSecs = response.header("x-ratelimit-reset").toLongLong(&ok);
    if (!ok) {
        const qint64 retryAfter = response.header("retry-after").toLongLong(&ok);
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
