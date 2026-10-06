#include "app/problemreport.h"

#include <QStringList>

namespace Harpoon {

QString errorKindName(Error::Kind kind)
{
    switch (kind) {
    case Error::None: return QStringLiteral("None");
    case Error::InvalidUrl: return QStringLiteral("InvalidUrl");
    case Error::UnsupportedUrl: return QStringLiteral("UnsupportedUrl");
    case Error::Network: return QStringLiteral("Network");
    case Error::Http: return QStringLiteral("Http");
    case Error::NotFound: return QStringLiteral("NotFound");
    case Error::RateLimited: return QStringLiteral("RateLimited");
    case Error::Parse: return QStringLiteral("Parse");
    case Error::NoReleases: return QStringLiteral("NoReleases");
    case Error::NoAsset: return QStringLiteral("NoAsset");
    case Error::NoVersion: return QStringLiteral("NoVersion");
    case Error::InvalidSetting: return QStringLiteral("InvalidSetting");
    case Error::Download: return QStringLiteral("Download");
    case Error::Checksum: return QStringLiteral("Checksum");
    case Error::Verification: return QStringLiteral("Verification");
    case Error::Package: return QStringLiteral("Package");
    case Error::WrongArch: return QStringLiteral("WrongArch");
    case Error::IdChanged: return QStringLiteral("IdChanged");
    case Error::AlreadyInstalled: return QStringLiteral("AlreadyInstalled");
    case Error::Downgrade: return QStringLiteral("Downgrade");
    case Error::Install: return QStringLiteral("Install");
    case Error::NotAuthorized: return QStringLiteral("NotAuthorized");
    case Error::Cancelled: return QStringLiteral("Cancelled");
    case Error::Busy: return QStringLiteral("Busy");
    case Error::Storage: return QStringLiteral("Storage");
    case Error::System: return QStringLiteral("System");
    }
    return QString::number(int(kind));
}

bool isAppPackageProblem(Error::Kind kind)
{
    switch (kind) {
    case Error::Checksum:
    case Error::Verification:
    case Error::Package:
    case Error::WrongArch:
    case Error::IdChanged:
    case Error::NoAsset:
        return true;
    default:
        return false;
    }
}

QString installAction(const InstallProblem &problem)
{
    return problem.installedEvr.isEmpty() ? QStringLiteral("Install") : QStringLiteral("Update");
}

QString installProblemTitle(const InstallProblem &problem)
{
    return QStringLiteral("%1 of %2 failed: %3")
        .arg(installAction(problem), problem.app.name, errorKindName(problem.error.kind));
}

QString installProblemReport(const InstallProblem &problem)
{
    const App &app = problem.app;
    const auto orUnknown = [](const QString &s) { return s.isEmpty() ? QStringLiteral("unknown") : s; };
    QStringList packages;
    for (const Asset &asset : app.latestAssets)
        packages << asset.name;

    QStringList lines;
    lines << QStringLiteral("### What happened");
    QString what = QStringLiteral("%1 of %2 %3 failed")
                       .arg(installAction(problem), app.name, orUnknown(app.latestVersion));
    if (!problem.stage.isEmpty())
        what += QStringLiteral(" at \"%1\"").arg(problem.stage);
    lines << what + QLatin1Char('.') << QString();
    lines << QStringLiteral("```") << problem.error.message.trimmed() << QStringLiteral("```") << QString();
    lines << QStringLiteral("### Details");
    lines << QStringLiteral("- Harpoon: %1").arg(QStringLiteral(HARPOON_VERSION));
    lines << QStringLiteral("- SailfishOS: %1 (%2)").arg(orUnknown(problem.device.osVersion), orUnknown(problem.device.arch));
    lines << QStringLiteral("- Install backend: %1").arg(orUnknown(problem.backend));
    lines << QStringLiteral("- App: %1 (%2)").arg(app.name, app.id);
    lines << QStringLiteral("- Source: %1%2")
                 .arg(app.url, app.sourceId.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(app.sourceId));
    lines << QStringLiteral("- Installed: %1").arg(problem.installedEvr.isEmpty() ? QStringLiteral("not installed")
                                                                                   : problem.installedEvr);
    QString release = orUnknown(app.latestVersion);
    if (!app.latestTag.isEmpty() && app.latestTag != app.latestVersion)
        release += QStringLiteral(" (tag %1)").arg(app.latestTag);
    if (app.latestPrerelease)
        release += QStringLiteral(", prerelease");
    lines << QStringLiteral("- Release: %1").arg(release);
    if (!app.releasePageUrl.isEmpty())
        lines << QStringLiteral("- Release page: %1").arg(app.releasePageUrl);
    lines << QStringLiteral("- Packages: %1").arg(packages.isEmpty() ? QStringLiteral("none") : packages.join(QStringLiteral(", ")));
    lines << QStringLiteral("- Error kind: %1").arg(errorKindName(problem.error.kind));
    if (problem.error.httpStatus > 0)
        lines << QStringLiteral("- HTTP status: %1").arg(problem.error.httpStatus);
    lines << QStringLiteral("- Time: %1").arg(problem.when.toUTC().toString(Qt::ISODate));
    return lines.join(QLatin1Char('\n'));
}

QUrl newIssueUrl(const QString &repositoryUrl, const QString &title, const QString &body)
{
    return QUrl::fromEncoded(repositoryUrl.toUtf8() + "/issues/new?title=" + QUrl::toPercentEncoding(title)
                             + "&body=" + QUrl::toPercentEncoding(body));
}

} // namespace Harpoon
