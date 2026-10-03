#pragma once

#include "model/appsettings.h"
#include "model/error.h"
#include "model/release.h"

#include <QDateTime>
#include <QJsonObject>
#include <QStringList>

namespace Harpoon {

// What Harpoon last installed for an app. Lets pseudo-versions (dates,
// commit hashes) be compared: if the installed EVR is still the one we put
// there, the installed "version" is the release we installed it from.
struct InstallReceipt
{
    QString version;       // Harpoon version string of the release
    QString tag;
    QString evr;           // RPM EVR of the main package
    QStringList assetNames;
    QStringList packageNames; // every RPM installed with the app, main first
    QStringList sha256s;
    QString verification;  // e.g. "attestation:verified"; empty when not checked
    QDateTime installedAt;

    bool isValid() const { return !evr.isEmpty(); }
};

// One tracked app. Persisted as one JSON file per app by AppStore.
struct App
{
    static const int kSchemaVersion = 1;

    QString id;                 // RPM %{NAME} once known, else a temporary id
    bool temporaryId = false;
    QString url;                // standardized source URL
    QString sourceId;           // forced source (self-hosted forges); empty = by host
    QString name;               // display name
    QString author;
    AppSettings settings;
    QDateTime addedAt;

    // Result of the last successful check.
    QString latestVersion;
    QString latestTag;
    QString latestTitle;
    QDateTime latestDate;
    QString changelog;
    QString releasePageUrl;
    bool latestPrerelease = false;
    QList<Asset> latestAssets;  // installable assets for this device

    QDateTime lastCheck;
    QString lastError;          // empty when the last check succeeded

    InstallReceipt receipt;
    QString acknowledgedVersion; // track-only apps: latest version the user has seen
    QString notifiedVersion;     // latest version a background notification announced

    QJsonObject toJson() const;
    static Result<App> fromJson(const QJsonObject &json);

    // "tmp-" + 12 hex digits derived from the URL.
    static QString temporaryIdFor(const QString &url);
    // A temporary id is "tmp-" + 12 hex digits; any other id is an RPM
    // package name. Ids are file names in the store and arguments to rpm.
    static bool isValidId(const QString &id, bool temporary);
    // Default name/author from a forge URL: https://host/owner/repo.
    static App fromUrl(const QString &standardUrl, const QString &sourceId = QString());
};

} // namespace Harpoon
