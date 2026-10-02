#pragma once

#include <QString>

namespace Harpoon {

// Names the per-user data folders (~/.local/share/<org>/<app> etc.). They
// must match [X-Sailjail] OrganizationName/ApplicationName in harpoon.desktop
// and must never change after release: changing them orphans user data.
inline QString organizationName() { return QStringLiteral("io.github.juhanilehtimaeki"); }
inline QString applicationName() { return QStringLiteral("harpoon"); }

} // namespace Harpoon
