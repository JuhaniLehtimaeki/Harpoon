#pragma once

#include "model/error.h"

#include <QString>

namespace Harpoon {

// What a QR code or link needs to add an app.
//
// Accepted forms:
//   harpoon://add?url=<percent-encoded repo URL>[&source=<source id>][&package=<rpm name>]
//   https://github.com/owner/repo  (any plain http(s) URL, e.g. a QR code of the repo link)
struct AddLink
{
    QString url;         // http(s) URL of the project
    QString sourceId;    // optional forced source (self-hosted forges, Jenkins, RpmMdRepo)
    QString packageName; // optional; required by RpmMdRepo

    // harpoon://add?... for this link.
    QString toString() const;
};

// Parses scanned or opened text. Rejects anything that is not one of the
// forms above (other schemes such as javascript: or file:, malformed values).
Result<AddLink> parseAddLink(const QString &text);

} // namespace Harpoon
