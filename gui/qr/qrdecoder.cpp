#include "qrdecoder.h"

#include <ReadBarcode.h>

#include <QRunnable>

namespace Harpoon {

namespace {

class ScanJob : public QRunnable
{
public:
    ScanJob(QrDecoder *decoder, const QImage &image) : m_decoder(decoder), m_image(image) {}
    void run() override
    {
        // The decoder waits for this job before it is destroyed.
        QMetaObject::invokeMethod(m_decoder, "finishScan", Qt::QueuedConnection,
                                  Q_ARG(QString, QrDecoder::decode(m_image)));
    }

private:
    QrDecoder *m_decoder;
    QImage m_image;
};

} // namespace

QrDecoder::QrDecoder(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

QrDecoder::~QrDecoder()
{
    m_pool.waitForDone();
}

QString QrDecoder::decode(const QImage &image)
{
    if (image.isNull())
        return QString();
    // Camera frames are large; QR codes decode well from ~1000 px.
    QImage gray = image;
    if (qMax(gray.width(), gray.height()) > 1280)
        gray = gray.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    gray = gray.convertToFormat(QImage::Format_Grayscale8);

    const ZXing::ImageView view(gray.constBits(), gray.width(), gray.height(), ZXing::ImageFormat::Lum,
                                gray.bytesPerLine());
    ZXing::ReaderOptions options;
    options.setFormats(ZXing::BarcodeFormat::QRCode);
    options.setTryHarder(true);
    options.setTryRotate(true);
    options.setTryInvert(true);
    const ZXing::Result result = ZXing::ReadBarcode(view, options);
    if (!result.isValid())
        return QString();
    return QString::fromStdString(result.text());
}

QString QrDecoder::decodeImage(const QVariant &image) const
{
    return decode(image.value<QImage>());
}

QString QrDecoder::decodeFile(const QString &path) const
{
    return decode(QImage(path));
}

bool QrDecoder::scan(const QVariant &image)
{
    const QImage frame = image.value<QImage>();
    if (m_busy || frame.isNull())
        return false;
    m_busy = true;
    emit busyChanged();
    m_pool.start(new ScanJob(this, frame));
    return true;
}

void QrDecoder::finishScan(const QString &text)
{
    m_busy = false;
    emit busyChanged();
    emit scanned(text);
}

} // namespace Harpoon
