#pragma once

#include <QImage>
#include <QObject>
#include <QThreadPool>
#include <QVariant>

namespace Harpoon {

// Finds a QR code in an image (a camera frame) with zxing-cpp.
class QrDecoder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit QrDecoder(QObject *parent = nullptr);
    ~QrDecoder() override;

    // The QR code's text, or an empty string when none is found.
    static QString decode(const QImage &image);

    // For QML: the image from Item.grabToImage() (ItemGrabResult.image).
    Q_INVOKABLE QString decodeImage(const QVariant &image) const;
    Q_INVOKABLE QString decodeFile(const QString &path) const;

    // Decodes in a worker thread so the viewfinder keeps running, then emits
    // scanned(). Returns false (and does nothing) while a scan is running.
    Q_INVOKABLE bool scan(const QVariant &image);
    bool busy() const { return m_busy; }

signals:
    void scanned(const QString &text);
    void busyChanged();

private slots:
    void finishScan(const QString &text);

private:
    QThreadPool m_pool;
    bool m_busy = false;
};

} // namespace Harpoon
