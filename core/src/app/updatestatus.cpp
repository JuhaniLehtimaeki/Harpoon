#include "app/updatestatus.h"

#include "app/packagebuilds.h"
#include "version/versioncompare.h"

namespace Harpoon {

QString updateStateName(UpdateState state)
{
    switch (state) {
    case UpdateState::NotChecked: return QStringLiteral("not checked");
    case UpdateState::NotInstalled: return QStringLiteral("not installed");
    case UpdateState::UpToDate: return QStringLiteral("up to date");
    case UpdateState::UpdateAvailable: return QStringLiteral("update available");
    case UpdateState::Unknown: return QStringLiteral("unknown");
    }
    return QString();
}

namespace {

// A release can be newer by its tag and still carry the installed package:
// a build made before the version was raised. The package file names say so
// before anything is downloaded. Only files named as the installed package
// count, and every one of them must be no newer.
bool releasePackageNotNewer(const App &app, const RpmInfo &installed, QString *offered)
{
    // File names carry no epoch: compare without it.
    Evr installedEvr = installed.evr;
    installedEvr.epoch = 0;
    bool any = false;
    for (const Asset &asset : app.latestAssets) {
        const RpmFileName file = parseRpmFileName(asset.name);
        if (file.name != installed.name)
            continue;
        if (compareEvr(file.evr, installedEvr) > 0)
            return false;
        any = true;
        *offered = file.evr.toString();
    }
    return any;
}

} // namespace

UpdateStatus updateStatusFor(const App &app, const RpmInfo &installed)
{
    UpdateStatus status = updateStatusByVersion(app, installed);
    QString offered;
    if (status.state == UpdateState::UpdateAvailable && !app.settings.getBool(Keys::trackOnly)
        && releasePackageNotNewer(app, installed, &offered)) {
        status.state = UpdateState::UpToDate;
        status.reason = QStringLiteral("releasePackageNotNewer");
        status.offeredPackageVersion = offered;
    }
    return status;
}

UpdateStatus updateStatusByVersion(const App &app, const RpmInfo &installed)
{
    UpdateStatus status;
    status.latestVersion = app.latestVersion;

    if (app.settings.getBool(Keys::trackOnly)) {
        status.installedVersion = app.acknowledgedVersion;
        if (app.latestVersion.isEmpty()) {
            status.state = UpdateState::NotChecked;
        } else if (app.acknowledgedVersion == app.latestVersion) {
            status.state = UpdateState::UpToDate;
            status.reason = QStringLiteral("acknowledged");
        } else {
            status.state = UpdateState::UpdateAvailable;
            status.reason = QStringLiteral("new release");
        }
        return status;
    }

    if (!installed.name.isEmpty())
        status.installedVersion = installed.evr.toString();
    if (installed.name.isEmpty()) {
        status.state = app.latestVersion.isEmpty() ? UpdateState::NotChecked : UpdateState::NotInstalled;
        return status;
    }
    if (app.latestVersion.isEmpty()) {
        status.state = UpdateState::NotChecked;
        return status;
    }

    VersionDecision d = compareVersionStrings(installed.evr.version, app.latestVersion);
    if (!d.isOrdered() && d.reason == QLatin1String("missingBuildRevision") && !installed.evr.release.isEmpty()) {
        const VersionDecision full = compareVersionStrings(
            installed.evr.version + QLatin1Char('-') + installed.evr.release, app.latestVersion);
        if (full.isOrdered())
            d = full;
    }
    if (d.isOrdered()) {
        status.state = d.relation == VersionRelation::Older ? UpdateState::UpdateAvailable : UpdateState::UpToDate;
        status.reason = d.reason;
        return status;
    }

    if (app.receipt.isValid() && app.receipt.evr == installed.evr.toString()) {
        status.state = app.receipt.version == app.latestVersion ? UpdateState::UpToDate : UpdateState::UpdateAvailable;
        status.reason = QStringLiteral("installReceipt");
        return status;
    }

    status.state = UpdateState::Unknown;
    status.reason = d.reason;
    return status;
}

} // namespace Harpoon
