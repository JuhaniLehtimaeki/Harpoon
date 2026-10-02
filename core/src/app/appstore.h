#pragma once

#include "app/app.h"

namespace Harpoon {

// One JSON file per app in a directory, written atomically (QSaveFile).
class AppStore
{
public:
    explicit AppStore(const QString &directory);

    QString directory() const { return m_dir; }

    // Loads every record. Unreadable files are skipped and reported in errors.
    QList<App> loadAll(QStringList *errors = nullptr) const;
    Result<App> load(const QString &id) const;
    bool contains(const QString &id) const;

    Error save(const App &app) const;
    Error remove(const QString &id) const;
    // Saves `app` and deletes the record stored under oldId (id change).
    Error replace(const QString &oldId, const App &app) const;

    // Default location: $HARPOON_DATA_DIR/apps, else <AppDataLocation>/apps.
    static QString defaultDirectory();

private:
    QString pathFor(const QString &id) const;

    QString m_dir;
};

} // namespace Harpoon
