#include "app/backup.h"

#include <QFileDevice>
#include <QJsonArray>
#include <QSaveFile>
#include <QJsonDocument>
#include <QSet>

namespace Harpoon {

QByteArray Backup::toJson() const
{
    QJsonArray appArray;
    for (const App &app : apps)
        appArray.append(app.toJson());
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("harpoon-backup"));
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    root.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("appVersion"), QStringLiteral(HARPOON_VERSION));
    root.insert(QStringLiteral("apps"), appArray);
    root.insert(QStringLiteral("settings"), QJsonObject::fromVariantMap(settings));
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

Backup Backup::create(const QList<App> &apps, const QVariantMap &settings, bool includeSecrets)
{
    Backup backup;
    backup.settings = settings;
    for (App app : apps) {
        if (!includeSecrets) {
            // Request headers carry cookies and tokens, also those of each
            // intermediate page.
            QVariantMap values = app.settings.values();
            values.remove(QString::fromLatin1(Keys::requestHeader));
            const QString hopsKey = QString::fromLatin1(Keys::intermediateLink);
            if (values.contains(hopsKey)) {
                QVariantList hops = values.value(hopsKey).toList();
                for (QVariant &hop : hops) {
                    QVariantMap map = hop.toMap();
                    map.remove(QString::fromLatin1(Keys::requestHeader));
                    hop = map;
                }
                values.insert(hopsKey, hops);
            }
            app.settings = AppSettings(values);
        }
        backup.apps << app;
    }
    return backup;
}

Error Backup::writeTo(const QString &path, bool containsSecrets) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return Error::make(Error::Storage, QStringLiteral("Cannot write %1: %2").arg(path, file.errorString()));
    if (containsSecrets)
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(toJson());
    if (!file.commit())
        return Error::make(Error::Storage, QStringLiteral("Cannot write %1: %2").arg(path, file.errorString()));
    return Error();
}

Result<Backup> Backup::fromJson(const QByteArray &json)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return Result<Backup>::failure(
            Error::make(Error::Storage, QStringLiteral("Not a Harpoon backup: %1").arg(err.errorString())));
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("harpoon-backup"))
        return Result<Backup>::failure(Error::make(Error::Storage, QStringLiteral("Not a Harpoon backup file")));
    const int schema = root.value(QStringLiteral("schemaVersion")).toInt();
    if (schema < 1 || schema > kSchemaVersion)
        return Result<Backup>::failure(Error::make(
            Error::Storage, QStringLiteral("This backup was made by a newer Harpoon (format %1)").arg(schema)));

    Backup backup;
    for (const QJsonValue &v : root.value(QStringLiteral("apps")).toArray()) {
        const auto app = App::fromJson(v.toObject());
        if (!app.ok())
            return Result<Backup>::failure(app.error);
        backup.apps << app.value;
    }
    backup.settings = root.value(QStringLiteral("settings")).toObject().toVariantMap();
    return Result<Backup>::success(backup);
}

ImportResult mergeBackupApps(const QList<App> &existing, const QList<App> &fromBackup)
{
    ImportResult result;
    QSet<QString> urls;
    QSet<QString> ids;
    for (const App &a : existing) {
        urls.insert(a.url.toLower());
        ids.insert(a.id);
    }
    for (App app : fromBackup) {
        // A backup's id is not trusted: it would let a shared file make
        // Harpoon install over, or uninstall, any package under any name.
        // The real package name is found again by checking or installing.
        app.id = App::temporaryIdFor(app.url);
        app.temporaryId = true;
        if (urls.contains(app.url.toLower()) || ids.contains(app.id)) {
            result.skipped << app.name;
            continue;
        }
        app.receipt = InstallReceipt();
        app.notifiedVersion.clear();
        app.lastError.clear();
        // Release data is re-fetched from the source before any install.
        app.latestVersion.clear();
        app.latestTag.clear();
        app.latestTitle.clear();
        app.latestDate = QDateTime();
        app.changelog.clear();
        app.releasePageUrl.clear();
        app.latestPrerelease = false;
        app.latestAssets.clear();
        app.lastCheck = QDateTime();
        urls.insert(app.url.toLower());
        ids.insert(app.id);
        result.added << app;
    }
    return result;
}

} // namespace Harpoon
