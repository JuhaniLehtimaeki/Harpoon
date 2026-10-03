#pragma once

#include "app/app.h"

#include <QStringList>

#include <functional>

namespace Harpoon {

// The package name in an RPM file name ("harbour-dwd-1.1.1-1.noarch.rpm" ->
// "harbour-dwd"); empty when the name does not look like name-ver-rel.arch.rpm.
QString rpmNameFromFileName(const QString &fileName);

// An app that was just added still has a temporary id until Harpoon installs
// it. When its latest release's package is already installed (from Chum, by
// hand, ...), use that package's name as the id right away, so the app shows
// as installed and gets updates. Returns true when the id was changed.
//   isInstalled: whether an RPM of that name is installed
//   taken: ids of other tracked apps, never taken over
bool adoptInstalledPackage(App &app, const std::function<bool(const QString &)> &isInstalled,
                           const QStringList &taken);

} // namespace Harpoon
