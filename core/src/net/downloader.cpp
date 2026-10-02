#include "net/downloader.h"

#include "net/networktransport.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace Harpoon {

namespace {

Error downloadError(const QString &message)
{
    return Error::make(Error::Download, message);
}

// Checks a finished file against the request. Returns an error or None.
Error verify(const DownloadRequest &request, const QString &path)
{
    const qint64 size = QFileInfo(path).size();
    if (request.expectedSize >= 0 && size != request.expectedSize)
        return downloadError(QStringLiteral("Incomplete download: got %1 of %2 bytes").arg(size).arg(request.expectedSize));
    if (!request.expectedSha256.isEmpty()) {
        const QString actual = Downloader::sha256OfFile(path);
        if (actual != request.expectedSha256.toLower())
            return Error::make(Error::Checksum, QStringLiteral("Checksum mismatch: expected %1, got %2")
                                                    .arg(request.expectedSha256.toLower(), actual));
    }
    return Error();
}

} // namespace

Downloader::Downloader(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_userAgent(NetworkTransport::defaultUserAgent())
{
}

QString Downloader::sha256OfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
        hash.addData(file.read(1 << 16));
    return QString::fromLatin1(hash.result().toHex());
}

void Downloader::download(const DownloadRequest &request, Progress progress, Done done)
{
    // Reuse a finished download when it verifies.
    if (QFileInfo(request.targetPath).isFile()
        && (request.expectedSize >= 0 || !request.expectedSha256.isEmpty())
        && verify(request, request.targetPath).ok()) {
        done(Result<QString>::success(request.targetPath));
        return;
    }
    QDir().mkpath(QFileInfo(request.targetPath).absolutePath());
    start(request, progress, done, true);
}

void Downloader::start(const DownloadRequest &request, Progress progress, Done done, bool allowRestart)
{
    const QString partPath = request.targetPath + QStringLiteral(".part");
    auto file = std::make_shared<QFile>(partPath);
    const qint64 resumeFrom = file->exists() ? file->size() : 0;

    QNetworkRequest req{QUrl(request.url)};
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#else
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true); // Qt 5.6 (SailfishOS)
#endif
    req.setRawHeader("User-Agent", m_userAgent);
    for (const auto &h : request.headers)
        req.setRawHeader(h.first, h.second);
    if (resumeFrom > 0)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(resumeFrom) + '-');

    QNetworkReply *reply = m_nam->get(req);
    auto *stall = new QTimer(reply);
    stall->setSingleShot(true);
    connect(stall, &QTimer::timeout, reply, &QNetworkReply::abort);
    stall->start(m_stallTimeoutMs);

    // Shared between the readyRead and finished handlers.
    struct State
    {
        bool opened = false;
        bool failed = false;
        qint64 offset = 0;
        qint64 total = -1;
        QString error;
    };
    auto state = std::make_shared<State>();

    auto openTarget = [reply, file, state, resumeFrom]() {
        if (state->opened || state->failed)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200 && status != 206)
            return; // handled in finished()
        bool append = false;
        if (status == 206 && resumeFrom > 0) {
            // Only append when the server resumed exactly where we stopped.
            static const QRegularExpression range(QStringLiteral("^bytes (\\d+)-\\d+/(\\d+|\\*)"));
            const auto m = range.match(QString::fromLatin1(reply->rawHeader("Content-Range")));
            if (m.hasMatch() && m.captured(1).toLongLong() == resumeFrom) {
                append = true;
                if (m.captured(2) != QLatin1String("*"))
                    state->total = m.captured(2).toLongLong();
            }
        }
        if (!append) {
            const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
            state->total = length.isValid() ? length.toLongLong() : -1;
            if (status == 206) {
                state->failed = true;
                state->error = QStringLiteral("Server sent an unexpected partial response");
                reply->abort();
                return;
            }
        }
        state->offset = append ? resumeFrom : 0;
        if (!file->open(append ? (QIODevice::WriteOnly | QIODevice::Append) : (QIODevice::WriteOnly | QIODevice::Truncate))) {
            state->failed = true;
            state->error = QStringLiteral("Cannot write %1: %2").arg(file->fileName(), file->errorString());
            reply->abort();
            return;
        }
        state->opened = true;
    };

    connect(reply, &QNetworkReply::readyRead, this, [reply, file, state, stall, openTarget, progress, this]() {
        openTarget();
        if (!state->opened) {
            reply->readAll(); // an error body; discard
            return;
        }
        const QByteArray chunk = reply->readAll();
        if (file->write(chunk) != chunk.size()) {
            state->failed = true;
            state->error = QStringLiteral("Write failed: %1").arg(file->errorString());
            reply->abort();
            return;
        }
        state->offset += chunk.size();
        stall->start(m_stallTimeoutMs);
        if (progress)
            progress(state->offset, state->total);
    });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, state, openTarget, request, progress, done, allowRestart, partPath, resumeFrom]() {
                reply->deleteLater();
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                // Range not satisfiable: the .part is stale or already complete.
                if (status == 416 && resumeFrom > 0) {
                    QFile::remove(partPath);
                    if (allowRestart) {
                        start(request, progress, done, false);
                        return;
                    }
                }

                openTarget(); // empty bodies never trigger readyRead
                if (state->opened && reply->bytesAvailable() > 0) {
                    const QByteArray rest = reply->readAll();
                    file->write(rest);
                    state->offset += rest.size();
                }
                if (file->isOpen())
                    file->close();

                if (state->failed) {
                    done(Result<QString>::failure(downloadError(state->error)));
                    return;
                }
                if (status != 200 && status != 206) {
                    const QString why = status == 0 ? reply->errorString()
                                                    : QStringLiteral("HTTP %1 %2").arg(status).arg(
                                                          reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString());
                    Error e = downloadError(QStringLiteral("Download failed: %1").arg(why));
                    e.httpStatus = status;
                    done(Result<QString>::failure(e));
                    return;
                }
                if (reply->error() != QNetworkReply::NoError) {
                    // Keep the .part so the next attempt resumes.
                    done(Result<QString>::failure(
                        downloadError(QStringLiteral("Download interrupted: %1").arg(reply->errorString()))));
                    return;
                }
                if (state->total >= 0 && state->offset != state->total) {
                    done(Result<QString>::failure(downloadError(
                        QStringLiteral("Download interrupted at %1 of %2 bytes").arg(state->offset).arg(state->total))));
                    return;
                }

                const Error bad = verify(request, partPath);
                if (!bad.ok()) {
                    QFile::remove(partPath); // corrupt; resuming would not help
                    done(Result<QString>::failure(bad));
                    return;
                }
                QFile::remove(request.targetPath);
                if (!QFile::rename(partPath, request.targetPath)) {
                    done(Result<QString>::failure(
                        downloadError(QStringLiteral("Cannot move download to %1").arg(request.targetPath))));
                    return;
                }
                done(Result<QString>::success(request.targetPath));
            });
}

} // namespace Harpoon
