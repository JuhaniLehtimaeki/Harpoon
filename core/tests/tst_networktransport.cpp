// NetworkTransport against a local HTTP server: incomplete and oversized
// bodies, header-only probes, and which headers follow a redirect.

#include "minihttpserver.h"

#include "net/networktransport.h"

#include <QtTest>

using namespace Harpoon;

namespace {
HttpResponse fetch(NetworkTransport &transport, const HttpRequest &request)
{
    HttpResponse out;
    bool done = false;
    transport.get(request, [&](const HttpResponse &r) {
        out = r;
        done = true;
    });
    if (!QTest::qWaitFor([&]() { return done; }, 10000))
        qFatal("request did not finish");
    return out;
}
} // namespace

class TestNetworkTransport : public QObject
{
    Q_OBJECT
private slots:
    void cutOffBodyIsAFailure()
    {
        // A page cut off half way must not look like a whole page (an HTML
        // source would then pick an older link as the latest).
        MiniHttpServer server;
        MiniHttpServer::Route r;
        r.body = QByteArray(4000, 'x');
        r.dropAfterBytes = 1000;
        server.route(QStringLiteral("/page"), r);
        NetworkTransport transport;
        HttpRequest request;
        request.url = server.url(QStringLiteral("/page"));
        const HttpResponse response = fetch(transport, request);
        QVERIFY(response.isNetworkError());
        QVERIFY(response.body.isEmpty());
        // The next attempt gets the whole page.
        QCOMPARE(fetch(transport, request).body.size(), 4000);
    }

    void bodiesAreCapped()
    {
        MiniHttpServer server;
        server.routeBody(QStringLiteral("/big"), QByteArray(200000, 'x'));
        NetworkTransport transport;
        HttpRequest request;
        request.url = server.url(QStringLiteral("/big"));
        request.maxBodyBytes = 1000;
        const HttpResponse response = fetch(transport, request);
        QVERIFY(response.isNetworkError());
        QVERIFY(response.networkError.contains(QLatin1String("larger")));
    }

    void headersOnlyProbe()
    {
        // A server that ignores Range would send the whole file.
        MiniHttpServer server;
        MiniHttpServer::Route r;
        r.body = QByteArray(500000, 'x');
        r.supportsRange = false;
        r.headers << qMakePair(QByteArray("ETag"), QByteArray("\"abc\""));
        server.route(QStringLiteral("/file.rpm"), r);
        NetworkTransport transport;
        HttpRequest request;
        request.url = server.url(QStringLiteral("/file.rpm"));
        request.headersOnly = true;
        const HttpResponse response = fetch(transport, request);
        QCOMPARE(response.status, 200);
        QCOMPARE(response.header("etag"), QByteArray("\"abc\""));
        QVERIFY(response.body.isEmpty());
    }

    void onlyHarmlessHeadersCrossOrigins()
    {
        // 127.0.0.1 and localhost are different origins.
        MiniHttpServer server;
        const QString target = server.url(QStringLiteral("/target")).replace(QLatin1String("127.0.0.1"),
                                                                             QLatin1String("localhost"));
        MiniHttpServer::Route redirect;
        redirect.status = 302;
        redirect.headers << qMakePair(QByteArray("Location"), target.toUtf8());
        server.route(QStringLiteral("/start"), redirect);
        server.routeBody(QStringLiteral("/target"), "ok");
        NetworkTransport transport;
        HttpRequest request;
        request.url = server.url(QStringLiteral("/start"));
        request.setHeader("Accept", "application/json");
        request.setHeader("Authorization", "Bearer secret");
        request.setHeader("X-Api-Key", "secret");
        request.setHeader("Cookie", "session=secret");
        QCOMPARE(fetch(transport, request).body, QByteArray("ok"));
        const auto seen = server.lastRequestHeaders.value(QStringLiteral("/target"));
        QCOMPARE(seen.value("accept"), QByteArray("application/json"));
        QVERIFY(!seen.contains("authorization"));
        QVERIFY(!seen.contains("x-api-key"));
        QVERIFY(!seen.contains("cookie"));
    }
};

QTEST_GUILESS_MAIN(TestNetworkTransport)
#include "tst_networktransport.moc"
