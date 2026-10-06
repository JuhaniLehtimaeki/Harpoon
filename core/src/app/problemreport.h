#pragma once

#include "app/app.h"
#include "model/error.h"
#include "pipeline/deviceinfo.h"

#include <QDateTime>
#include <QString>
#include <QUrl>

namespace Harpoon {

// What was known when an install or update failed.
struct InstallProblem
{
    App app;
    QString installedEvr;   // empty: not installed
    DeviceInfo device;
    QString backend;        // PackageBackend::name(): "PackageKit", "Installation handler"
    QString stage;          // the installer's last stage, e.g. "Installing x.rpm"
    Error error;
    QDateTime when;
};

// "Install", "Package", ...: the error kind as in the code.
QString errorKindName(Error::Kind kind);

// Whether the error most likely lies in the app's release or package rather
// than in Harpoon, the network or the phone: its developer can fix it.
bool isAppPackageProblem(Error::Kind kind);

// "Update" when a version is installed, else "Install".
QString installAction(const InstallProblem &problem);

// A plain-text report of the failure for whoever can fix it, in English on
// purpose: maintainers read it. Formatted to paste into an issue.
QString installProblemReport(const InstallProblem &problem);

// One-line title for the report.
QString installProblemTitle(const InstallProblem &problem);

// A "new issue" page of a GitHub repository with title and body filled in.
QUrl newIssueUrl(const QString &repositoryUrl, const QString &title, const QString &body);

} // namespace Harpoon
