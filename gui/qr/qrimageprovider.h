#pragma once

#include "qrencoder.h"

#include <QQuickImageProvider>
#include <QUrl>

namespace Harpoon {

// image://harpoonqr/<percent-encoded text>: the text as a QR code.
class QrImageProvider : public QQuickImageProvider
{
public:
    QrImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        const int wanted = requestedSize.width() > 0 ? requestedSize.width() : 512;
        const QImage image = QrEncoder::encode(QUrl::fromPercentEncoding(id.toUtf8()), wanted);
        if (size)
            *size = image.size();
        return image;
    }
};

} // namespace Harpoon
