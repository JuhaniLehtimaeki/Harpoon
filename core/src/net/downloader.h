#pragma once

#include "model/error.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>

#include <functional>

class QNetworkAccessManager;

namespace Harpoon {

struct DownloadRequest
{
    QString url;
    QList<QPair<QByteArray, QByteArray>> headers;
    QString targetPath;       // final file; data is written to targetPath + ".part"
    qint64 expectedSize = -1; // verified when >= 0
    QString expectedSha256;   // lowercase hex; verified when set
};

// Downloads one file at a time per call, streaming to disk.
//  - An existing complete target (matching sha256, else size) is reused.
//  - An existing .part is resumed with a Range request; a server that answers
//    200 instead of 206 restarts the file from scratch.
//  - The result is verified (size, sha256) before it is renamed into place.
class Downloader : public QObject
{
    Q_OBJECT
public:
    using Progress = std::function<void(qint64 received, qint64 total)>;
    using Done = std::function<void(const Result<QString> &path)>;

    explicit Downloader(QObject *parent = nullptr);

    void download(const DownloadRequest &request, Progress progress, Done done);

    void setUserAgent(const QByteArray &userAgent) { m_userAgent = userAgent; }
    // Abort when no data arrives for this long.
    void setStallTimeoutMs(int ms) { m_stallTimeoutMs = ms; }

    static QString sha256OfFile(const QString &path);

private:
    void start(const DownloadRequest &request, Progress progress, Done done, bool allowRestart);

    QNetworkAccessManager *m_nam;
    QByteArray m_userAgent;
    int m_stallTimeoutMs = 60000;
};

} // namespace Harpoon
