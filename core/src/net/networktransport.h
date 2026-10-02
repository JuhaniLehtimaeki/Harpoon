#pragma once

#include "net/httptransport.h"

#include <QObject>

class QNetworkAccessManager;

namespace Harpoon {

// HttpTransport backed by QNetworkAccessManager. Follows redirects and aborts
// requests that take longer than the timeout.
class NetworkTransport : public QObject, public HttpTransport
{
    Q_OBJECT
public:
    explicit NetworkTransport(QObject *parent = nullptr);

    void get(const HttpRequest &request, Callback done) override;

    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    void setUserAgent(const QByteArray &userAgent) { m_userAgent = userAgent; }

    static QByteArray defaultUserAgent();

private:
    void get(const HttpRequest &request, Callback done, int redirects);

    QNetworkAccessManager *m_nam;
    int m_timeoutMs = 30000;
    QByteArray m_userAgent;
};

} // namespace Harpoon
