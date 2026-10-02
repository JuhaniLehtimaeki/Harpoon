#pragma once

#include "net/httptransport.h"

#include <QFile>
#include <QHash>
#include <QList>

// Answers requests from a URL -> response table and records every request.
// Unknown URLs get a 404. Callbacks run synchronously.
class FakeTransport : public Harpoon::HttpTransport
{
public:
    void respond(const QString &url, const Harpoon::HttpResponse &response) { m_responses.insert(url, response); }

    void respondJson(const QString &url, const QByteArray &body, int status = 200)
    {
        Harpoon::HttpResponse r;
        r.status = status;
        r.body = body;
        respond(url, r);
    }

    void respondFixture(const QString &url, const QString &fixture)
    {
        QFile f(QStringLiteral(HARPOON_FIXTURE_DIR "/") + fixture);
        if (!f.open(QIODevice::ReadOnly))
            qFatal("missing fixture %s", qPrintable(fixture));
        respondJson(url, f.readAll());
    }

    // Lets a test return different answers depending on the request.
    std::function<bool(const Harpoon::HttpRequest &, Harpoon::HttpResponse *)> handler;

    void get(const Harpoon::HttpRequest &request, Callback done) override
    {
        requests << request;
        Harpoon::HttpResponse response;
        if (handler && handler(request, &response)) {
            done(response);
            return;
        }
        if (m_responses.contains(request.url)) {
            done(m_responses.value(request.url));
            return;
        }
        response.status = 404;
        response.reasonPhrase = QStringLiteral("Not Found");
        done(response);
    }

    QList<Harpoon::HttpRequest> requests;

private:
    QHash<QString, Harpoon::HttpResponse> m_responses;
};
