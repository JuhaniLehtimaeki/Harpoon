#pragma once

#include "model/error.h"

#include <QString>

namespace Harpoon {

// Applies the user's versionExtractionRegEx to a raw version string (ObtainX
// VersionService.extractVersion). The last match is used and matchGroup is a
// template such as "$1", "$1.$2" or a bare group number "2"; empty means "$0".
//
// An empty regex returns the input unchanged. A regex that does not compile,
// does not match, or yields an empty string is an error.
Result<QString> extractVersion(const QString &regex, const QString &matchGroup, const QString &input);

} // namespace Harpoon
