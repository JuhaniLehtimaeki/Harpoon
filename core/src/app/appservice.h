#pragma once

#include "app/app.h"
#include "app/appinstaller.h"

#include <QHash>
#include <QStringList>
#include <QVariantMap>

namespace Harpoon {

class HarpoonSettings;

// Logic the app (HarpoonController) and harpoon-cli share, so the two
// front ends cannot drift apart.

// <CacheLocation>/downloads, or $HARPOON_CACHE_DIR/downloads.
QString defaultDownloadDirectory();

// Per-source configuration for AppChecker: the stored tokens, keyed
// "GitHub" or "Forgejo@git.example.org". With environment, a
// HARPOON_TOKEN_<SOURCE> variable overrides the token of a source's default
// host (for the command line).
QHash<QString, QVariantMap> sourceConfigs(const HarpoonSettings &settings, const QStringList &sourceIds,
                                          bool environment);

// The record to store after an install: `current` (the record as it is now,
// settings may have changed during the install) with the new receipt, under
// the installed package's name. When that name is already another tracked
// app (`idTaken`), the record keeps its id: one package must never end up as
// two records, and the other one must not be overwritten.
App recordAfterInstall(const App &current, const InstallResult &result, bool idTaken);

} // namespace Harpoon
