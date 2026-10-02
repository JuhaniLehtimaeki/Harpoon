#include "app/updatenotifications.h"


#include <QCoreApplication>

namespace Harpoon {

UpdateNotificationPlan planUpdateNotification(const QList<AppWithStatus> &apps)
{
    UpdateNotificationPlan plan;
    QStringList names;
    for (const AppWithStatus &a : apps) {
        if (a.status.state != UpdateState::UpdateAvailable)
            continue;
        names << a.app.name;
        if (a.app.notifiedVersion != a.app.latestVersion) {
            plan.shouldNotify = true;
            App marked = a.app;
            marked.notifiedVersion = a.app.latestVersion;
            plan.appsToMark << marked;
        }
    }
    if (!plan.shouldNotify)
        return plan;

    NotificationRequest &r = plan.request;
    r.appName = QStringLiteral("Harpoon");
    r.appIcon = QStringLiteral("/usr/share/icons/hicolor/86x86/apps/harpoon.png");
    r.category = QStringLiteral("x-nemo.software-update");
    r.itemCount = names.size();
    r.summary = names.size() == 1 ? QCoreApplication::translate("Harpoon", "Update available")
                                  : QCoreApplication::translate("Harpoon", "%n updates available", "", names.size());
    r.body = names.join(QStringLiteral(", "));
    r.previewSummary = r.summary;
    r.previewBody = r.body;
    r.remoteService = QStringLiteral("io.github.juhanilehtimaeki.harpoon");
    r.remotePath = QStringLiteral("/harpoon");
    r.remoteInterface = QStringLiteral("io.github.juhanilehtimaeki.harpoon");
    r.remoteMethod = QStringLiteral("showUpdates");
    return plan;
}

} // namespace Harpoon
