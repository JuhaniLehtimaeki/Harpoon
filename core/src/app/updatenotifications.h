#pragma once

#include "app/app.h"
#include "app/updatestatus.h"
#include "notify/notifier.h"

namespace Harpoon {

struct AppWithStatus
{
    App app;
    UpdateStatus status;
};

struct UpdateNotificationPlan
{
    bool shouldNotify = false;      // at least one update not announced yet
    NotificationRequest request;    // one summary notification for all updates
    QList<App> appsToMark;          // records whose notifiedVersion must be saved
};

// Decides whether a background check should notify. Every app with an
// available update is listed, but a notification is only sent when some
// update has not been announced before (notifiedVersion != latestVersion),
// so the same release never notifies twice.
UpdateNotificationPlan planUpdateNotification(const QList<AppWithStatus> &apps);

} // namespace Harpoon
