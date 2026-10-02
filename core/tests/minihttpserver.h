#pragma once

#include <QHash>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>

// Tiny HTTP/1.1 server for tests: fixed routes, Range support, and a way to
// cut a response short to simulate a dropped connection.
class MiniHttpServer : public QObject
{
    Q_OBJECT
public:
    struct Route
    {
        int status = 200;
        QByteArray body;
        QList<QPair<QByteArray, QByteArray>> headers;
        bool supportsRange = true;
        int dropAfterBytes = -1; // > 0: send only this many body bytes once, then close
    };

    MiniHttpServer()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, &MiniHttpServer::onConnection);
    }

    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    QString url(const QString &path) const { return baseUrl() + path; }

    void route(const QString &path, const Route &r) { m_routes.insert(path, r); }
    void routeBody(const QString &path, const QByteArray &body, const QByteArray &contentType = "application/octet-stream")
    {
        Route r;
        r.body = body;
        r.headers << qMakePair(QByteArray("Content-Type"), contentType);
        route(path, r);
    }

    int hits(const QString &path) const { return m_hits.value(path); }
    QList<QByteArray> rangeHeaders; // every Range header received, in order
    // Lowercased header name -> value, from the last request to each path.
    QHash<QString, QHash<QByteArray, QByteArray>> lastRequestHeaders;

private slots:
    void onConnection()
    {
        while (QTcpSocket *s = m_server.nextPendingConnection()) {
            connect(s, &QTcpSocket::readyRead, this, [this, s]() { onReadyRead(s); });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    }

private:
    void onReadyRead(QTcpSocket *s)
    {
        QByteArray &buf = m_buffers[s];
        buf += s->readAll();
        const int end = buf.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QList<QByteArray> lines = buf.left(end).split('\n');
        m_buffers.remove(s);
        const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
        const QString path = QString::fromLatin1(requestLine.value(1));
        QByteArray range;
        QHash<QByteArray, QByteArray> received;
        for (const QByteArray &line : lines.mid(1)) {
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            const QByteArray name = line.left(colon).trimmed().toLower();
            received.insert(name, line.mid(colon + 1).trimmed());
            if (name == "range")
                range = line.mid(colon + 1).trimmed();
        }
        lastRequestHeaders.insert(path, received);
        if (!range.isEmpty())
            rangeHeaders << range;
        m_hits[path] += 1;

        if (!m_routes.contains(path)) {
            send(s, 404, "Not Found", {}, "not found");
            return;
        }
        Route &r = m_routes[path];
        QList<QPair<QByteArray, QByteArray>> headers = r.headers;
        if (r.supportsRange)
            headers << qMakePair(QByteArray("Accept-Ranges"), QByteArray("bytes"));

        if (r.status == 200 && r.supportsRange && range.startsWith("bytes=")) {
            const qint64 from = range.mid(6, range.indexOf('-') - 6).toLongLong();
            if (from >= r.body.size()) {
                headers << qMakePair(QByteArray("Content-Range"), "bytes */" + QByteArray::number(r.body.size()));
                send(s, 416, "Range Not Satisfiable", headers, QByteArray());
                return;
            }
            headers << qMakePair(QByteArray("Content-Range"),
                                 "bytes " + QByteArray::number(from) + '-' + QByteArray::number(r.body.size() - 1) + '/'
                                     + QByteArray::number(r.body.size()));
            send(s, 206, "Partial Content", headers, r.body.mid(from));
            return;
        }
        if (r.dropAfterBytes > 0) {
            const int n = r.dropAfterBytes;
            r.dropAfterBytes = -1; // only once
            send(s, r.status, "OK", headers, r.body, n);
            return;
        }
        send(s, r.status, r.status == 200 ? "OK" : "Error", headers, r.body);
    }

    void send(QTcpSocket *s, int status, const QByteArray &reason, const QList<QPair<QByteArray, QByteArray>> &headers,
              const QByteArray &body, int truncateTo = -1)
    {
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
        for (const auto &h : headers)
            response += h.first + ": " + h.second + "\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
        response += truncateTo >= 0 ? body.left(truncateTo) : body;
        s->write(response);
        s->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QString, Route> m_routes;
    QHash<QString, int> m_hits;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};
