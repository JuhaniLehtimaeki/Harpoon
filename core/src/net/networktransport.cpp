#include "net/networktransport.h"

#include "net/redirects.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <memory>

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
    get(request, std::move(done), 0);
}

void NetworkTransport::get(const HttpRequest &request, Callback done, int redirects)
{
    QNetworkRequest req{QUrl(request.url)};
    // Redirects are followed below, without credentials across origins.
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
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

    // Why the reply was aborted on purpose, if it was.
    enum class Stop { None, HeadersDone, TooLarge };
    auto stop = std::make_shared<Stop>(Stop::None);
    if (request.headersOnly) {
        connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, stop]() {
            const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const int code = status.toInt();
            if (status.isValid() && !(code >= 300 && code < 400) && *stop == Stop::None) {
                *stop = Stop::HeadersDone;
                reply->abort();
            }
        });
    }
    const qint64 maxBody = request.maxBodyBytes;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, stop, maxBody](qint64 received, qint64) {
        if (maxBody > 0 && received > maxBody && *stop == Stop::None) {
            *stop = Stop::TooLarge;
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, request, done, redirects, stop]() {
        reply->deleteLater();
        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int code = status.isValid() ? status.toInt() : 0;

        if (*stop == Stop::HeadersDone) {
            HttpResponse response;
            response.status = code;
            response.reasonPhrase = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
            response.finalUrl = reply->url().toString();
            for (const auto &pair : reply->rawHeaderPairs())
                response.headers.insert(pair.first.toLower(), pair.second);
            done(response);
            return;
        }
        if (*stop == Stop::TooLarge) {
            HttpResponse failed;
            failed.finalUrl = reply->url().toString();
            failed.networkError = QStringLiteral("The response is larger than %1 MB").arg(request.maxBodyBytes >> 20);
            done(failed);
            return;
        }

        if (code >= 300 && code < 400 && reply->hasRawHeader("Location")) {
            const QUrl from = reply->url();
            const QUrl to = from.resolved(QUrl::fromEncoded(reply->rawHeader("Location")));
            HttpResponse failed;
            failed.finalUrl = from.toString();
            if (redirects >= Redirects::kMax) {
                failed.networkError = QStringLiteral("Too many redirects");
                done(failed);
                return;
            }
            if (Redirects::isDowngrade(from, to)) {
                failed.networkError = QStringLiteral("Refusing redirect from https to http");
                done(failed);
                return;
            }
            HttpRequest next = request;
            next.url = to.toString();
            next.headers = Redirects::headersFor(from, to, request.headers);
            get(next, done, redirects + 1);
            return;
        }

        HttpResponse response;
        response.status = code;
        response.reasonPhrase = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        response.finalUrl = reply->url().toString();
        response.body = reply->readAll();
        for (const auto &pair : reply->rawHeaderPairs())
            response.headers.insert(pair.first.toLower(), pair.second);
        // Errors below 200 are transport failures (timeout, reset, TLS...):
        // the status line may have arrived, but the body is incomplete and
        // must not be used as if it were whole.
        const QNetworkReply::NetworkError error = reply->error();
        if (error != QNetworkReply::NoError && int(error) < 200) {
            response.status = 0;
            response.body.clear();
        }
        if (response.status == 0)
            response.networkError = reply->errorString();
        done(response);
    });
}

} // namespace Harpoon
