#include "qrdecoder.h"

#include <ReadBarcode.h>

namespace Harpoon {

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

} // namespace Harpoon
