#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

#include <functional>

namespace Harpoon {

struct HttpRequest
{
    QString url;
    QList<QPair<QByteArray, QByteArray>> headers;
    // Larger bodies fail the request: API responses and pages are small, and
    // the phone has little memory.
    qint64 maxBodyBytes = 32 * 1024 * 1024;
    // Stop once the headers have arrived (for ETag probes); body stays empty.
    bool headersOnly = false;

    void setHeader(const QByteArray &name, const QByteArray &value) { headers.append(qMakePair(name, value)); }
    QByteArray header(const QByteArray &name) const
    {
        for (const auto &h : headers)
            if (h.first.toLower() == name.toLower())
                return h.second;
        return QByteArray();
    }
};

struct HttpResponse
{
    int status = 0;                        // 0 when no HTTP response was received
    QByteArray body;
    QHash<QByteArray, QByteArray> headers; // names lowercased
    QString reasonPhrase;
    QString networkError;                  // set when status == 0
    QString finalUrl;                      // after redirects

    bool isNetworkError() const { return status == 0; }
    QByteArray header(const QByteArray &name) const { return headers.value(name.toLower()); }
};

// Minimal async HTTP GET abstraction so sources can be unit-tested with canned
// responses. The callback may be invoked synchronously (fakes) or later from
// the event loop (NetworkTransport).
class HttpTransport
{
public:
    using Callback = std::function<void(const HttpResponse &)>;

    virtual ~HttpTransport() = default;
    virtual void get(const HttpRequest &request, Callback done) = 0;
};

} // namespace Harpoon
