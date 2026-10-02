#include "qrencoder.h"

#include <BitMatrix.h>
#include <CharacterSet.h>
#include <MultiFormatWriter.h>

#include <QPainter>

#include <exception>

namespace Harpoon {

QImage QrEncoder::encode(const QString &text, int size)
{
    if (text.isEmpty())
        return QImage();
    ZXing::BitMatrix matrix;
    try {
        // Error correction level M: a good balance for codes shown on screens.
        matrix = ZXing::MultiFormatWriter(ZXing::BarcodeFormat::QRCode)
                     .setEncoding(ZXing::CharacterSet::UTF8)
                     .setEccLevel(4)
                     .setMargin(0)
                     .encode(text.toStdString(), 0, 0);
    } catch (const std::exception &) {
        return QImage(); // too long for a QR code
    }
    const int quiet = 4;
    const int modules = matrix.width() + 2 * quiet;
    const int scale = qMax(1, size / modules);
    QImage image(modules * scale, modules * scale, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    for (int y = 0; y < matrix.height(); ++y)
        for (int x = 0; x < matrix.width(); ++x)
            if (matrix.get(x, y))
                painter.fillRect((x + quiet) * scale, (y + quiet) * scale, scale, scale, Qt::black);
    return image;
}

} // namespace Harpoon
