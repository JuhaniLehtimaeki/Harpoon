#include "app/autoupdate.h"

#include <QCoreApplication>

namespace Harpoon {

bool autoUpdateEligible(const App &app, const UpdateStatus &status, QString *why)
{
    QString reason;
    if (status.state != UpdateState::UpdateAvailable)
        reason = QStringLiteral("no update");
    else if (app.settings.getBool(Keys::trackOnly))
        reason = QStringLiteral("track only");
    else if (app.settings.getBool(Keys::excludeFromAutoUpdate))
        reason = QStringLiteral("excluded from automatic updates");
    else if (app.temporaryId || app.receipt.evr.isEmpty())
        reason = QStringLiteral("not installed by Harpoon");
    else if (app.settings.getBool(Keys::allowIdChange))
        reason = QStringLiteral("may change its package name");
    else if (app.latestAssets.isEmpty())
        reason = QStringLiteral("no package in the latest release");
    if (why)
        *why = reason;
    return reason.isEmpty();
}

NotificationRequest autoUpdateNotification(const QStringList &updated, const QStringList &failed)
{
    NotificationRequest r;
    r.appName = QStringLiteral("Harpoon");
    r.appIcon = QStringLiteral("/usr/share/icons/hicolor/86x86/apps/harpoon.png");
    r.category = QStringLiteral("x-nemo.software-update");
    if (!updated.isEmpty()) {
        r.summary = updated.size() == 1
                        ? QCoreApplication::translate("Harpoon", "%1 was updated").arg(updated.first())
                        : QCoreApplication::translate("Harpoon", "%n apps were updated", "", updated.size());
        r.body = updated.join(QStringLiteral(", "));
    }
    if (!failed.isEmpty()) {
        const QString line = QCoreApplication::translate("Harpoon", "Could not update %1")
                                 .arg(failed.join(QStringLiteral(", ")));
        if (r.summary.isEmpty())
            r.summary = line;
        else
            r.body += QLatin1Char('\n') + line;
    }
    r.previewSummary = r.summary;
    r.previewBody = r.body;
    r.itemCount = updated.size() + failed.size();
    r.remoteService = QStringLiteral("io.github.juhanilehtimaeki.harpoon");
    r.remotePath = QStringLiteral("/harpoon");
    r.remoteInterface = QStringLiteral("io.github.juhanilehtimaeki.harpoon");
    r.remoteMethod = QStringLiteral("showUpdates");
    return r;
}

} // namespace Harpoon
