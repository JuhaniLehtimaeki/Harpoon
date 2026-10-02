#include "net/networktransport.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace Harpoon {

NetworkTransport::NetworkTransport(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_userAgent(defaultUserAgent())
{
}

QByteArray NetworkTransport::defaultUserAgent()
{
    return QByteArrayLiteral("Harpoon/" HARPOON_VERSION " (SailfishOS)");
}

void NetworkTransport::get(const HttpRequest &request, Callback done)
{
    QNetworkRequest req{QUrl(request.url)};
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#else
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true); // Qt 5.6 (SailfishOS)
#endif
    req.setRawHeader("User-Agent", m_userAgent);
    for (const auto &h : request.headers)
        req.setRawHeader(h.first, h.second);

    QNetworkReply *reply = m_nam->get(req);

    // Qt 5.6 has no per-request transfer timeout.
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(m_timeoutMs);

    connect(reply, &QNetworkReply::finished, this, [reply, done]() {
        HttpResponse response;
        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        response.status = status.isValid() ? status.toInt() : 0;
        response.reasonPhrase = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        response.finalUrl = reply->url().toString();
        response.body = reply->readAll();
        for (const auto &pair : reply->rawHeaderPairs())
            response.headers.insert(pair.first.toLower(), pair.second);
        if (response.status == 0)
            response.networkError = reply->errorString();
        reply->deleteLater();
        done(response);
    });
}

} // namespace Harpoon
