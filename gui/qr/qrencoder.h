#pragma once

#include <QImage>
#include <QString>

namespace Harpoon {

// Draws text as a QR code with zxing-cpp: black modules on white, with the
// standard four-module quiet zone.
class QrEncoder
{
public:
    // A square image of about `size` pixels (rounded down to whole modules,
    // at least one pixel per module). Null when the text does not fit.
    static QImage encode(const QString &text, int size);
};

} // namespace Harpoon
