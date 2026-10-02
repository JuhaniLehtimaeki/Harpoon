#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QUrl>

namespace Harpoon {

// Redirect handling shared by NetworkTransport and Downloader. Both follow
// redirects themselves so credentials never travel to another origin; Qt
// 5.6 copies every raw header onto the redirected request.
namespace Redirects {

const int kMax = 10;

inline bool sameOrigin(const QUrl &a, const QUrl &b)
{
    const int defaultA = a.scheme() == QLatin1String("http") ? 80 : 443;
    const int defaultB = b.scheme() == QLatin1String("http") ? 80 : 443;
    return a.scheme() == b.scheme() && a.host().compare(b.host(), Qt::CaseInsensitive) == 0
           && a.port(defaultA) == b.port(defaultB);
}

inline bool isCredential(const QByteArray &headerName)
{
    const QByteArray name = headerName.toLower();
    return name == "authorization" || name == "private-token" || name == "cookie";
}

// Headers for the redirected request: credentials are dropped when the
// redirect leaves the origin.
inline QList<QPair<QByteArray, QByteArray>> headersFor(const QUrl &from, const QUrl &to,
                                                       const QList<QPair<QByteArray, QByteArray>> &headers)
{
    if (sameOrigin(from, to))
        return headers;
    QList<QPair<QByteArray, QByteArray>> kept;
    for (const auto &h : headers)
        if (!isCredential(h.first))
            kept << h;
    return kept;
}

// https -> http is refused (it could leak the request and the response).
inline bool isDowngrade(const QUrl &from, const QUrl &to)
{
    return from.scheme() == QLatin1String("https") && to.scheme() == QLatin1String("http");
}

} // namespace Redirects
} // namespace Harpoon
