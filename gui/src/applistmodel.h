#pragma once

#include "app/app.h"
#include "app/updatestatus.h"

#include <QAbstractListModel>
#include <QVariantMap>

namespace Harpoon {

// Tracked apps for the UI, sorted: updates first, then by displayed name.
class AppListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int updatesCount READ updatesCount NOTIFY updatesCountChanged)
    // Apps whose last check failed (not those only waiting for builds).
    // Shares updatesCount's change signal.
    Q_PROPERTY(int failedCount READ failedCount NOTIFY updatesCountChanged)
    // For the list's summary: apps that are installed, and the most recent
    // check of any app (invalid: never checked).
    Q_PROPERTY(int installedCount READ installedCount NOTIFY summaryChanged)
    Q_PROPERTY(QDateTime lastChecked READ lastChecked NOTIFY summaryChanged)

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
        IconRole,
        WaitingForBuildsRole,
        InstallFailedRole,
    };

    struct Entry
    {
        App app;
        QString displayName; // what the UI shows; app.name when empty
        QString iconPath;    // the installed app's launcher icon; empty: none
        RpmInfo installed;
        UpdateStatus status;
        bool busy = false;
        QString stage;
        qreal progress = -1; // < 0: indeterminate
        // The last failed install or update (HarpoonController::installProblem());
        // empty once one succeeds. Kept in memory only.
        QVariantMap installProblem;
    };

    explicit AppListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_entries.size(); }
    int updatesCount() const;
    int failedCount() const;
    int installedCount() const;
    QDateTime lastChecked() const;

    void setEntries(QList<Entry> entries);
    // Inserts or replaces by id. oldId: the previous id when it changed.
    void upsert(const Entry &entry, const QString &oldId = QString());
    void remove(const QString &id);
    void setBusy(const QString &id, bool busy, const QString &stage = QString(), qreal progress = -1);
    void setInstallProblem(const QString &id, const QVariantMap &problem);

    int indexOf(const QString &id) const;
    const Entry *entry(const QString &id) const;
    QList<Entry> entries() const { return m_entries; }

    static State toState(UpdateState state);

signals:
    // installedCount or lastChecked may have changed.
    void summaryChanged();
    void countChanged();
    void updatesCountChanged();
    // One app's record or busy state changed (pages showing one app listen
    // to this instead of every row's dataChanged). busyOnly: only the
    // busy state, stage or progress.
    void appChanged(const QString &id, bool busyOnly);

private:
    static bool lessThan(const Entry &a, const Entry &b);
    void resort();

    QList<Entry> m_entries;
};

} // namespace Harpoon
