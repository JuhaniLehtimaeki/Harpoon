#include "applistmodel.h"

#include <algorithm>

namespace Harpoon {

AppListModel::AppListModel(QObject *parent) : QAbstractListModel(parent) {}

int AppListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant AppListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return QVariant();
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case IdRole: return e.app.id;
    case NameRole: return e.app.name;
    case AuthorRole: return e.app.author;
    case UrlRole: return e.app.url;
    case SourceRole: return e.app.sourceId;
    case InstalledVersionRole: return e.status.installedVersion;
    case LatestVersionRole: return e.status.latestVersion;
    case StateRole: return int(toState(e.status.state));
    case LastErrorRole: return e.app.lastError;
    case TrackOnlyRole: return e.app.settings.getBool(Keys::trackOnly);
    case PrereleaseRole: return e.app.latestPrerelease;
    case ReleaseDateRole: return e.app.latestDate;
    case BusyRole: return e.busy;
    case StageRole: return e.stage;
    case ProgressRole: return e.progress;
    case HasUpdateRole: return e.status.state == UpdateState::UpdateAvailable;
    default: return QVariant();
    }
}

QHash<int, QByteArray> AppListModel::roleNames() const
{
    return {
        {IdRole, "appId"},
        {NameRole, "name"},
        {AuthorRole, "author"},
        {UrlRole, "url"},
        {SourceRole, "sourceId"},
        {InstalledVersionRole, "installedVersion"},
        {LatestVersionRole, "latestVersion"},
        {StateRole, "state"},
        {LastErrorRole, "lastError"},
        {TrackOnlyRole, "trackOnly"},
        {PrereleaseRole, "prerelease"},
        {ReleaseDateRole, "releaseDate"},
        {BusyRole, "busy"},
        {StageRole, "stage"},
        {ProgressRole, "progress"},
        {HasUpdateRole, "hasUpdate"},
    };
}

AppListModel::State AppListModel::toState(UpdateState state)
{
    switch (state) {
    case UpdateState::NotChecked: return NotChecked;
    case UpdateState::NotInstalled: return NotInstalled;
    case UpdateState::UpToDate: return UpToDate;
    case UpdateState::UpdateAvailable: return UpdateAvailable;
    case UpdateState::Unknown: return Unknown;
    }
    return Unknown;
}

int AppListModel::updatesCount() const
{
    int n = 0;
    for (const Entry &e : m_entries)
        if (e.status.state == UpdateState::UpdateAvailable)
            ++n;
    return n;
}

bool AppListModel::lessThan(const Entry &a, const Entry &b)
{
    const bool aUpdate = a.status.state == UpdateState::UpdateAvailable;
    const bool bUpdate = b.status.state == UpdateState::UpdateAvailable;
    if (aUpdate != bUpdate)
        return aUpdate;
    const int byName = a.app.name.compare(b.app.name, Qt::CaseInsensitive);
    return byName != 0 ? byName < 0 : a.app.id < b.app.id;
}

void AppListModel::setEntries(QList<Entry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    std::stable_sort(m_entries.begin(), m_entries.end(), &AppListModel::lessThan);
    endResetModel();
    emit countChanged();
    emit updatesCountChanged();
}

int AppListModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).app.id == id)
            return i;
    return -1;
}

const AppListModel::Entry *AppListModel::entry(const QString &id) const
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : &m_entries.at(i);
}

void AppListModel::resort()
{
    // Rows move rarely; a layout change keeps delegates (and their state).
    QList<Entry> sorted = m_entries;
    std::stable_sort(sorted.begin(), sorted.end(), &AppListModel::lessThan);
    bool same = true;
    for (int i = 0; i < sorted.size() && same; ++i)
        same = sorted.at(i).app.id == m_entries.at(i).app.id;
    if (same)
        return;
    emit layoutAboutToBeChanged();
    const QModelIndexList before = persistentIndexList();
    QList<QString> beforeIds;
    for (const QModelIndex &idx : before)
        beforeIds << m_entries.at(idx.row()).app.id;
    m_entries = sorted;
    QModelIndexList after;
    for (const QString &id : beforeIds)
        after << index(indexOf(id));
    changePersistentIndexList(before, after);
    emit layoutChanged();
}

void AppListModel::upsert(const Entry &entry, const QString &oldId)
{
    const int previousUpdates = updatesCount();
    int row = indexOf(oldId.isEmpty() ? entry.app.id : oldId);
    if (row < 0)
        row = indexOf(entry.app.id);
    if (row < 0) {
        auto pos = std::lower_bound(m_entries.begin(), m_entries.end(), entry, &AppListModel::lessThan);
        const int at = int(pos - m_entries.begin());
        beginInsertRows(QModelIndex(), at, at);
        m_entries.insert(at, entry);
        endInsertRows();
        emit countChanged();
    } else {
        // Busy state is owned by setBusy(); a record update must not clear it.
        Entry merged = entry;
        merged.busy = m_entries.at(row).busy;
        merged.stage = m_entries.at(row).stage;
        merged.progress = m_entries.at(row).progress;
        m_entries[row] = merged;
        emit dataChanged(index(row), index(row));
        resort();
    }
    if (updatesCount() != previousUpdates)
        emit updatesCountChanged();
}

void AppListModel::remove(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0)
        return;
    const int previousUpdates = updatesCount();
    beginRemoveRows(QModelIndex(), row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    emit countChanged();
    if (updatesCount() != previousUpdates)
        emit updatesCountChanged();
}

void AppListModel::setBusy(const QString &id, bool busy, const QString &stage, qreal progress)
{
    const int row = indexOf(id);
    if (row < 0)
        return;
    Entry &e = m_entries[row];
    e.busy = busy;
    e.stage = busy ? stage : QString();
    e.progress = busy ? progress : -1;
    emit dataChanged(index(row), index(row), {BusyRole, StageRole, ProgressRole});
}

} // namespace Harpoon
