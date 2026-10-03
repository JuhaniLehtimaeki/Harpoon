#include "app/appstore.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace Harpoon {

AppStore::AppStore(const QString &directory) : m_dir(directory) {}

QString AppStore::defaultDirectory()
{
    const QByteArray env = qgetenv("HARPOON_DATA_DIR");
    const QString base = !env.isEmpty() ? QString::fromLocal8Bit(env)
                                        : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/apps");
}

QString AppStore::pathFor(const QString &id) const
{
    // RPM names are [A-Za-z0-9._+-]; anything else must not reach the path.
    QString safe;
    for (const QChar c : id)
        safe += (c.isLetterOrNumber() && c.unicode() < 128) || c == QLatin1Char('.') || c == QLatin1Char('_')
                        || c == QLatin1Char('+') || c == QLatin1Char('-')
                    ? c
                    : QLatin1Char('_');
    if (safe.startsWith(QLatin1Char('.')))
        safe[0] = QLatin1Char('_');
    return m_dir + QLatin1Char('/') + safe + QStringLiteral(".json");
}

QList<App> AppStore::loadAll(QStringList *errors) const
{
    QList<App> apps;
    const QDir dir(m_dir);
    for (const QString &file : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile f(dir.filePath(file));
        if (!f.open(QIODevice::ReadOnly)) {
            if (errors)
                *errors << QStringLiteral("%1: %2").arg(file, f.errorString());
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
        const auto app = doc.isObject() ? App::fromJson(doc.object())
                                        : Result<App>::failure(Error::make(Error::Storage, parseError.errorString()));
        if (!app.ok()) {
            if (errors)
                *errors << QStringLiteral("%1: %2").arg(file, app.error.message);
            continue;
        }
        apps << app.value;
    }
    // replace() writes the new record before removing the old one; after a
    // crash in between, the temporary record is the stale one.
    QList<App> kept;
    for (const App &app : apps) {
        const bool superseded = app.temporaryId && std::any_of(apps.begin(), apps.end(), [&app](const App &other) {
            return !other.temporaryId && other.url.compare(app.url, Qt::CaseInsensitive) == 0;
        });
        if (superseded)
            QFile::remove(pathFor(app.id));
        else
            kept << app;
    }
    return kept;
}

Result<App> AppStore::load(const QString &id) const
{
    QFile f(pathFor(id));
    if (!f.open(QIODevice::ReadOnly))
        return Result<App>::failure(Error::make(Error::Storage, QStringLiteral("No app with id %1").arg(id)));
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return Result<App>::failure(Error::make(Error::Storage, QStringLiteral("Corrupt app record %1").arg(id)));
    return App::fromJson(doc.object());
}

bool AppStore::contains(const QString &id) const
{
    return QFile::exists(pathFor(id));
}

Error AppStore::save(const App &app) const
{
    if (!App::isValidId(app.id, app.temporaryId))
        return Error::make(Error::Storage, QStringLiteral("Invalid app id: %1").arg(app.id.left(80)));
    if (!QDir().mkpath(m_dir))
        return Error::make(Error::Storage, QStringLiteral("Cannot create %1").arg(m_dir));
    QSaveFile f(pathFor(app.id));
    if (!f.open(QIODevice::WriteOnly))
        return Error::make(Error::Storage, QStringLiteral("Cannot write %1: %2").arg(f.fileName(), f.errorString()));
    f.write(QJsonDocument(app.toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit())
        return Error::make(Error::Storage, QStringLiteral("Cannot write %1: %2").arg(f.fileName(), f.errorString()));
    return Error();
}

Error AppStore::remove(const QString &id) const
{
    const QString path = pathFor(id);
    if (QFile::exists(path) && !QFile::remove(path))
        return Error::make(Error::Storage, QStringLiteral("Cannot delete %1").arg(path));
    return Error();
}

Error AppStore::replace(const QString &oldId, const App &app) const
{
    const Error saved = save(app);
    if (!saved.ok() || oldId == app.id)
        return saved;
    return remove(oldId);
}

} // namespace Harpoon
