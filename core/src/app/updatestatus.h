#pragma once

#include "app/app.h"
#include "pkg/rpminspector.h"

namespace Harpoon {

enum class UpdateState {
    NotChecked,      // no successful check yet
    NotInstalled,
    UpToDate,
    UpdateAvailable,
    Unknown,         // installed and latest versions cannot be ordered
};

struct UpdateStatus
{
    UpdateState state = UpdateState::NotChecked;
    QString installedVersion; // RPM EVR, or the acknowledged version for track-only apps
    QString latestVersion;
    QString reason;
};

// Decides whether `app` has an update, given what rpm reports as installed
// (`installed.name` empty when not installed).
//
// Order of evidence:
//  1. track-only apps compare the latest version with the acknowledged one
//  2. not installed -> NotInstalled
//  3. installed RPM VERSION vs latest forge version (ObtainX comparator);
//     when only the revision is missing, VERSION-RELEASE is tried too
//  4. if those cannot be ordered, the install receipt: when the installed EVR
//     is the one Harpoon installed from the latest release, it is up to date
UpdateStatus updateStatusFor(const App &app, const RpmInfo &installed);

QString updateStateName(UpdateState state);

} // namespace Harpoon
