#pragma once

#include <QImage>
#include <QObject>
#include <QVariant>

namespace Harpoon {

// Finds a QR code in an image (a camera frame) with zxing-cpp.
class QrDecoder : public QObject
{
    Q_OBJECT
public:
    explicit QrDecoder(QObject *parent = nullptr) : QObject(parent) {}

    // The QR code's text, or an empty string when none is found.
    static QString decode(const QImage &image);

    // For QML: the image from Item.grabToImage() (ItemGrabResult.image).
    Q_INVOKABLE QString decodeImage(const QVariant &image) const;
    Q_INVOKABLE QString decodeFile(const QString &path) const;
};

} // namespace Harpoon
