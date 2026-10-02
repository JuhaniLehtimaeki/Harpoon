#include "minihttpserver.h"
#include "net/downloader.h"
#include "net/networktransport.h"

#include <QCryptographicHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace Harpoon;

namespace {
QByteArray payload(int size)
{
    QByteArray b;
    b.reserve(size);
    for (int i = 0; i < size; ++i)
        b.append(char('a' + (i * 7) % 26));
    return b;
}

QString sha(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}

Result<QString> run(Downloader &d, const DownloadRequest &req, QList<qint64> *progress = nullptr)
{
    Result<QString> result;
    bool finished = false;
    d.download(
        req,
        [progress](qint64 received, qint64) {
            if (progress)
                *progress << received;
        },
        [&](const Result<QString> &r) {
            result = r;
            finished = true;
        });
    if (!QTest::qWaitFor([&]() { return finished; }, 10000))
        result = Result<QString>::failure(Error::make(Error::Download, QStringLiteral("test timeout")));
    return result;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}
} // namespace

class TestDownloader : public QObject
{
    Q_OBJECT
private slots:
    void simpleDownloadWithChecksum()
    {
        MiniHttpServer server;
        const QByteArray body = payload(300000);
        server.routeBody(QStringLiteral("/a.rpm"), body);
        QTemporaryDir dir;
        Downloader d;
        DownloadRequest req;
        req.url = server.url(QStringLiteral("/a.rpm"));
        req.targetPath = dir.filePath(QStringLiteral("sub/a.rpm"));
        req.expectedSize = body.size();
        req.expectedSha256 = sha(body).toUpper(); // case-insensitive
        QList<qint64> progress;
        const auto r = run(d, req, &progress);
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(readAll(r.value), body);
        QVERIFY(!QFile::exists(req.targetPath + QStringLiteral(".part")));
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last(), qint64(body.size()));

        // A verified existing file is reused without a request.
        const auto again = run(d, req);
        QVERIFY(again.ok());
        QCOMPARE(server.hits(QStringLiteral("/a.rpm")), 1);
    }

    void checksumMismatchDeletesFile()
    {
        MiniHttpServer server;
        server.routeBody(QStringLiteral("/a.rpm"), payload(1000));
        QTemporaryDir dir;
        Downloader d;
        DownloadRequest req;
        req.url = server.url(QStringLiteral("/a.rpm"));
        req.targetPath = dir.filePath(QStringLiteral("a.rpm"));
        req.expectedSha256 = sha("something else");
        const auto r = run(d, req);
        QCOMPARE(int(r.error.kind), int(Error::Checksum));
        QVERIFY(!QFile::exists(req.targetPath));
        QVERIFY(!QFile::exists(req.targetPath + QStringLiteral(".part")));
    }

    void resumesAfterDroppedConnection()
    {
        MiniHttpServer server;
        const QByteArray body = payload(200000);
        MiniHttpServer::Route route;
        route.body = body;
        route.dropAfterBytes = 50000;
        server.route(QStringLiteral("/big.rpm"), route);
        QTemporaryDir dir;
        Downloader d;
        DownloadRequest req;
        req.url = server.url(QStringLiteral("/big.rpm"));
        req.targetPath = dir.filePath(QStringLiteral("big.rpm"));
        req.expectedSha256 = sha(body);

        const auto first = run(d, req);
        QCOMPARE(int(first.error.kind), int(Error::Download));
        const QString part = req.targetPath + QStringLiteral(".part");
        QVERIFY(QFile::exists(part));
        QCOMPARE(QFileInfo(part).size(), qint64(50000));

        const auto second = run(d, req);
        QVERIFY2(second.ok(), qPrintable(second.error.message));
        QCOMPARE(readAll(second.value), body);
        QCOMPARE(server.rangeHeaders, QList<QByteArray>{"bytes=50000-"});
    }

    void serverWithoutRangeRestarts()
    {
        MiniHttpServer server;
        const QByteArray body = payload(5000);
        MiniHttpServer::Route route;
        route.body = body;
        route.supportsRange = false;
        server.route(QStringLiteral("/x.rpm"), route);
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("x.rpm"));
        {
            QFile part(target + QStringLiteral(".part"));
            part.open(QIODevice::WriteOnly);
            part.write("garbage-from-an-old-attempt");
        }
        Downloader d;
        DownloadRequest req;
        req.url = server.url(QStringLiteral("/x.rpm"));
        req.targetPath = target;
        req.expectedSize = body.size();
        const auto r = run(d, req);
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(readAll(target), body);
    }

    void stalePartGets416AndRestarts()
    {
        MiniHttpServer server;
        const QByteArray body = payload(100);
        server.routeBody(QStringLiteral("/s.rpm"), body);
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("s.rpm"));
        {
            QFile part(target + QStringLiteral(".part"));
            part.open(QIODevice::WriteOnly);
            part.write(payload(500)); // longer than the file
        }
        Downloader d;
        DownloadRequest req;
        req.url = server.url(QStringLiteral("/s.rpm"));
        req.targetPath = target;
        const auto r = run(d, req);
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(readAll(target), body);
    }

    void redirectsKeepCredentialsOnlyWithinOrigin()
    {
        MiniHttpServer api;
        MiniHttpServer storage;
        const QByteArray body = payload(4000);
        storage.routeBody(QStringLiteral("/blob/a.rpm"), body);
        MiniHttpServer::Route toStorage;
        toStorage.status = 302;
        toStorage.headers << qMakePair(QByteArray("Location"), storage.url(QStringLiteral("/blob/a.rpm")).toUtf8());
        api.route(QStringLiteral("/assets/1"), toStorage);
        MiniHttpServer::Route sameOrigin;
        sameOrigin.status = 301;
        sameOrigin.headers << qMakePair(QByteArray("Location"), QByteArray("/assets/1"));
        api.route(QStringLiteral("/old/1"), sameOrigin);

        QTemporaryDir dir;
        Downloader d;
        DownloadRequest req;
        req.url = api.url(QStringLiteral("/old/1"));
        req.targetPath = dir.filePath(QStringLiteral("a.rpm"));
        req.expectedSha256 = sha(body);
        req.headers << qMakePair(QByteArray("Authorization"), QByteArray("Bearer secret"))
                    << qMakePair(QByteArray("Accept"), QByteArray("application/octet-stream"));
        const auto r = run(d, req);
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(readAll(r.value), body);
        // Same origin: the token goes along...
        QCOMPARE(api.lastRequestHeaders.value(QStringLiteral("/assets/1")).value("authorization"), QByteArray("Bearer secret"));
        // ...another origin: it does not, other headers do.
        const auto storageHeaders = storage.lastRequestHeaders.value(QStringLiteral("/blob/a.rpm"));
        QVERIFY(!storageHeaders.contains("authorization"));
        QCOMPARE(storageHeaders.value("accept"), QByteArray("application/octet-stream"));
    }

    void redirectLoopStops()
    {
        MiniHttpServer server;
        MiniHttpServer::Route loop;
        loop.status = 302;
        loop.headers << qMakePair(QByteArray("Location"), QByteArray("/loop"));
        server.route(QStringLiteral("/loop"), loop);
        QTemporaryDir dir;
        Downloader d;
        DownloadRequest req;
        req.url = server.url(QStringLiteral("/loop"));
        req.targetPath = dir.filePath(QStringLiteral("x.rpm"));
        const auto r = run(d, req);
        QCOMPARE(int(r.error.kind), int(Error::Download));
        QVERIFY(r.error.message.contains(QLatin1String("redirects")));
        QCOMPARE(server.hits(QStringLiteral("/loop")), 11);
    }

    void unverifiablePartIsNotResumed()
    {
        MiniHttpServer server;
        const QByteArray body = payload(3000);
        server.routeBody(QStringLiteral("/latest.rpm"), body);
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("latest.rpm"));
        {
            QFile part(target + QStringLiteral(".part"));
            QVERIFY(part.open(QIODevice::WriteOnly));
            part.write(payload(1000).replace('a', 'z')); // an older version's first bytes
        }
        Downloader d;
        DownloadRequest req; // no size, no sha256: nothing to verify a resume against
        req.url = server.url(QStringLiteral("/latest.rpm"));
        req.targetPath = target;
        const auto r = run(d, req);
        QVERIFY2(r.ok(), qPrintable(r.error.message));
        QCOMPARE(readAll(target), body);
        QVERIFY(server.rangeHeaders.isEmpty());
    }

    void transportRedirectsDropCredentialsAcrossOrigins()
    {
        MiniHttpServer api;
        MiniHttpServer other;
        other.routeBody(QStringLiteral("/data"), "{\"ok\":true}", "application/json");
        MiniHttpServer::Route away;
        away.status = 302;
        away.headers << qMakePair(QByteArray("Location"), other.url(QStringLiteral("/data")).toUtf8());
        api.route(QStringLiteral("/moved"), away);
        MiniHttpServer::Route local;
        local.status = 307;
        local.headers << qMakePair(QByteArray("Location"), QByteArray("/moved"));
        api.route(QStringLiteral("/start"), local);

        NetworkTransport transport;
        HttpRequest request;
        request.url = api.url(QStringLiteral("/start"));
        request.setHeader("Authorization", "Bearer secret");
        request.setHeader("Accept", "application/json");
        HttpResponse response;
        bool finished = false;
        transport.get(request, [&](const HttpResponse &r) {
            response = r;
            finished = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
        QCOMPARE(response.status, 200);
        QCOMPARE(response.body, QByteArray("{\"ok\":true}"));
        QCOMPARE(api.lastRequestHeaders.value(QStringLiteral("/moved")).value("authorization"), QByteArray("Bearer secret"));
        QVERIFY(!other.lastRequestHeaders.value(QStringLiteral("/data")).contains("authorization"));
        QCOMPARE(other.lastRequestHeaders.value(QStringLiteral("/data")).value("accept"), QByteArray("application/json"));
    }

    void httpError()
    {
        MiniHttpServer server;
        QTemporaryDir dir;
        Downloader d;
        DownloadRequest req;
        req.url = server.url(QStringLiteral("/missing.rpm"));
        req.targetPath = dir.filePath(QStringLiteral("m.rpm"));
        const auto r = run(d, req);
        QCOMPARE(int(r.error.kind), int(Error::Download));
        QCOMPARE(r.error.httpStatus, 404);
        QVERIFY(!QFile::exists(req.targetPath));
    }
};

QTEST_GUILESS_MAIN(TestDownloader)
#include "tst_downloader.moc"
