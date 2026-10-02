#pragma once

#include <QString>

namespace Harpoon {

struct DeviceInfo
{
    QString arch;      // RPM arch: aarch64, armv7hl, i486, x86_64
    QString osVersion; // SailfishOS VERSION_ID, e.g. "5.0.0.62"; empty if unknown

    // Reads `rpm --eval %{_arch}` (falling back to the CPU architecture) and
    // VERSION_ID from /etc/sailfish-release.
    static DeviceInfo detect();

    // Maps uname/Qt names to RPM arch names: arm64 -> aarch64,
    // armv7l/arm -> armv7hl, i386/i686 -> i486.
    static QString normalizeArch(const QString &arch);

    // Parses VERSION_ID out of an os-release style file body.
    static QString parseVersionId(const QString &releaseFile);
};

} // namespace Harpoon
