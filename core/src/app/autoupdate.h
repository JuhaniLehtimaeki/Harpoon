#pragma once

#include "app/app.h"
#include "app/updatestatus.h"
#include "notify/notifier.h"

namespace Harpoon {

// Whether a background run may install this app's update without asking.
// Only plain updates of packages Harpoon installed itself qualify: not
// track-only or excluded apps, not first installs, not a change of package
// name or of the set of packages. Downgrades and reinstalls are refused by the installer anyway.
// `why` receives the reason when the answer is no.
bool autoUpdateEligible(const App &app, const UpdateStatus &status, QString *why = nullptr);

// One notification summarising a background update run; empty summary when
// there is nothing to report.
NotificationRequest autoUpdateNotification(const QStringList &updated, const QStringList &failed);

} // namespace Harpoon
