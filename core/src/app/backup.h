#pragma once

#include "app/app.h"

#include <QJsonObject>
#include <QVariantMap>

namespace Harpoon {

// Harpoon's own backup format:
//   {"format": "harpoon-backup", "schemaVersion": 1, "exportedAt": ISO-8601,
//    "appVersion": "...", "apps": [App.toJson()...], "settings": {...}}
// Settings are HarpoonSettings::exportable(); tokens only when asked for.
struct Backup
{
    static const int kSchemaVersion = 1;

    QList<App> apps;
    QVariantMap settings;

    QByteArray toJson() const;
    static Result<Backup> fromJson(const QByteArray &json);

    // A backup of apps and settings. Without includeSecrets, per-app request
    // headers (often cookies or credentials) are removed; settings must
    // already have been exported accordingly (HarpoonSettings::exportable).
    static Backup create(const QList<App> &apps, const QVariantMap &settings, bool includeSecrets);

    // Writes atomically. A backup with secrets is created owner-only before
    // any data is written.
    Error writeTo(const QString &path, bool containsSecrets) const;
};

struct ImportResult
{
    QList<App> added;    // new apps, ready to save
    QStringList skipped; // names of apps already tracked (same URL)
};

// Merges backup apps into the existing list: apps whose URL is already
// tracked are skipped. Device-specific state (install receipt, notification
// state, errors) and the last check's release data are dropped, so nothing
// from a backup file can be installed before a fresh check.
ImportResult mergeBackupApps(const QList<App> &existing, const QList<App> &fromBackup);

} // namespace Harpoon
