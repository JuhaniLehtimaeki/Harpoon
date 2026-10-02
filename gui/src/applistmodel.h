#pragma once

#include "app/app.h"
#include "app/updatestatus.h"

#include <QAbstractListModel>

namespace Harpoon {

// Tracked apps for the UI, sorted: updates first, then by name.
class AppListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int updatesCount READ updatesCount NOTIFY updatesCountChanged)

public:
    // Mirrors Harpoon::UpdateState for QML.
    enum State { NotChecked, NotInstalled, UpToDate, UpdateAvailable, Unknown };
    Q_ENUM(State)

    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        AuthorRole,
        UrlRole,
        SourceRole,
        InstalledVersionRole,
        LatestVersionRole,
        StateRole,
        LastErrorRole,
        TrackOnlyRole,
        PrereleaseRole,
        ReleaseDateRole,
        BusyRole,
        StageRole,
        ProgressRole,
        HasUpdateRole,
    };

    struct Entry
    {
        App app;
        RpmInfo installed;
        UpdateStatus status;
        bool busy = false;
        QString stage;
        qreal progress = -1; // < 0: indeterminate
    };

    explicit AppListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_entries.size(); }
    int updatesCount() const;

    void setEntries(QList<Entry> entries);
    // Inserts or replaces by id. oldId: the previous id when it changed.
    void upsert(const Entry &entry, const QString &oldId = QString());
    void remove(const QString &id);
    void setBusy(const QString &id, bool busy, const QString &stage = QString(), qreal progress = -1);

    int indexOf(const QString &id) const;
    const Entry *entry(const QString &id) const;
    QList<Entry> entries() const { return m_entries; }

    static State toState(UpdateState state);

signals:
    void countChanged();
    void updatesCountChanged();

private:
    static bool lessThan(const Entry &a, const Entry &b);
    void resort();

    QList<Entry> m_entries;
};

} // namespace Harpoon
