#include "harpooncontroller.h"

#include "app/addlink.h"
#include "app/appinstaller.h"
#include "app/backup.h"
#include "app/installedmatch.h"
#include "app/appidentity.h"
#include "app/appservice.h"
#include "net/networktransport.h"
#include "pkg/installhandlerbackend.h"
#include "pkg/packagekitbackend.h"

#include <QDir>
#include <QSet>
#include <QFile>
#include <QJsonObject>
#include <QUrl>
#include <QStandardPaths>
#include <QVariantList>

namespace Harpoon {

namespace {

QVariantMap assetToVariant(const Asset &a)
{
    return {{QStringLiteral("name"), a.name},
            {QStringLiteral("url"), a.url},
            {QStringLiteral("size"), a.size},
            {QStringLiteral("sha256"), a.sha256}};
}

} // namespace

HarpoonController::HarpoonController(ControllerEnvironment env, QObject *parent)
    : QObject(parent)
    , m_cacheDir(env.cacheDir.isEmpty() ? defaultDownloadDirectory() : env.cacheDir)
    , m_applicationsDir(env.applicationsDir.isEmpty() ? QStringLiteral("/usr/share/applications") : env.applicationsDir)
    , m_iconsDir(env.iconsDir.isEmpty() ? QStringLiteral("/usr/share/icons/hicolor") : env.iconsDir)
    , m_device(env.device.arch.isEmpty() ? DeviceInfo::detect() : env.device)
    , m_store(env.dataDir.isEmpty() ? AppStore::defaultDirectory() : env.dataDir)
    , m_transport(env.transport)
    , m_runner(env.runner)
    , m_forcedBackend(env.backend)
    , m_settings(env.settings)
    , m_scheduler(env.scheduler)
    , m_backupDir(env.backupDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                          : env.backupDir)
{
    if (!m_transport) {
        m_ownTransport.reset(new NetworkTransport());
        m_transport = m_ownTransport.get();
    }
    if (!m_runner) {
        m_ownRunner.reset(new SystemProcessRunner());
        m_runner = m_ownRunner.get();
    }
    if (!m_settings)
        m_settings = new HarpoonSettings(QString(), this);
    if (!m_scheduler)
        m_scheduler = new BackgroundScheduler(QDBusConnection::sessionBus(), QString(), this);
    m_inspector.reset(new RpmInspector(*m_runner));
    // Update tokens in place: destroying the checker would orphan running checks.
    connect(m_settings, &HarpoonSettings::tokensChanged, this, [this]() {
        if (m_checker)
            m_checker->setSourceConfigs(tokenConfigs());
    });

    // Self-hosted servers come and go with the apps that use them.
    connect(&m_model, &QAbstractItemModel::rowsInserted, this, &HarpoonController::tokenTargetsChanged);
    connect(&m_model, &QAbstractItemModel::rowsRemoved, this, &HarpoonController::tokenTargetsChanged);
    connect(&m_model, &QAbstractItemModel::modelReset, this, &HarpoonController::tokenTargetsChanged);

    // Re-apply the schedule only when its inputs change.
    auto schedule = std::make_shared<QPair<bool, int>>(m_settings->backgroundChecks(), m_settings->checkIntervalHours());
    connect(m_settings, &HarpoonSettings::changed, this, [this, schedule]() {
        const QPair<bool, int> now(m_settings->backgroundChecks(), m_settings->checkIntervalHours());
        if (now != *schedule) {
            *schedule = now;
            syncBackgroundSchedule();
        }
    });
}

HarpoonController::~HarpoonController() = default;

QVariantList HarpoonController::sources() const
{
    QVariantList out;
    for (const QString &id : m_registry.ids()) {
        const auto source = m_registry.create(id);
        out << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), source->displayName()}};
    }
    return out;
}

QVariantList HarpoonController::tokenTargets() const
{
    QVariantList out;
    QStringList seen;
    for (const QString &id : m_registry.ids()) {
        const auto source = m_registry.create(id);
        if (!source->usesToken())
            continue;
        seen << id;
        out << QVariantMap{{QStringLiteral("key"), id}, {QStringLiteral("name"), source->displayName()}};
    }
    for (const AppListModel::Entry &e : m_model.entries()) {
        const auto match = m_registry.match(e.app.url, e.app.sourceId);
        if (!match.ok() || !match.value.source->usesToken())
            continue;
        const QString key = match.value.source->tokenKey();
        if (seen.contains(key))
            continue;
        seen << key;
        out << QVariantMap{{QStringLiteral("key"), key},
                           {QStringLiteral("name"), QStringLiteral("%1 (%2)").arg(match.value.source->displayName(),
                                                                                  match.value.source->customHost())}};
    }
    return out;
}

AppChecker &HarpoonController::checker()
{
    if (!m_checker) {
        m_checker.reset(new AppChecker(m_registry, *m_transport, m_device));
        m_checker->setSourceConfigs(tokenConfigs());
    }
    return *m_checker;
}

QHash<QString, QVariantMap> HarpoonController::tokenConfigs() const
{
    return sourceConfigs(*m_settings, m_registry.ids(), false);
}

PackageBackend &HarpoonController::backend()
{
    if (m_forcedBackend)
        return *m_forcedBackend;
    if (m_settings->installBackend() == QLatin1String("handler")) {
        if (!m_handler)
            m_handler.reset(new InstallHandlerBackend());
        return *m_handler;
    }
    if (!m_packageKit)
        m_packageKit.reset(new PackageKitBackend());
    return *m_packageKit;
}

AppListModel::Entry HarpoonController::makeEntry(const App &app, const RpmInfo *installed) const
{
    AppListModel::Entry e;
    e.app = app;
    if (!app.temporaryId) {
        if (installed) {
            e.installed = *installed;
        } else {
            const auto info = m_inspector->installedPackage(app.id);
            if (info.ok())
                e.installed = info.value;
        }
    }
    e.status = updateStatusFor(app, e.installed);
    // A name the user chose wins; otherwise the installed app's own name,
    // else a tidied repository name.
    const bool renamed = app.name != App::fromUrl(app.url).name;
    const DesktopEntry desktop = e.installed.name.isEmpty()
                                     ? DesktopEntry()
                                     : findDesktopEntry(app.id, m_applicationsDir, m_iconsDir);
    e.displayName = renamed ? app.name : desktop.isValid() ? desktop.name : prettyAppName(app.name);
    e.iconPath = desktop.iconPath;
    return e;
}

void HarpoonController::reload()
{
    const QList<App> apps = m_store.loadAll();
    QStringList ids;
    for (const App &app : apps)
        if (!app.temporaryId)
            ids << app.id;
    // One rpm call for all apps; launching one per app is slow on a phone.
    const auto installed = m_inspector->installedPackages(ids);
    const QHash<QString, RpmInfo> known = installed.ok() ? installed.value : QHash<QString, RpmInfo>();
    QList<AppListModel::Entry> entries;
    for (const App &app : apps) {
        const RpmInfo info = known.value(app.id); // empty when not installed
        entries << makeEntry(app, installed.ok() ? &info : nullptr);
    }
    m_model.setEntries(entries);
    if (!m_loaded) {
        m_loaded = true;
        emit loadedChanged();
    }
}

void HarpoonController::refresh()
{
    QSet<QString> onDisk;
    for (const App &app : m_store.loadAll()) {
        onDisk.insert(app.id);
        const AppListModel::Entry *e = m_model.entry(app.id);
        if (e && e->busy)
            continue; // its own operation writes it when it ends
        if (e) {
            if (e->app.toJson() != app.toJson())
                m_model.upsert(makeEntry(app));
            continue;
        }
        // New, or renamed from a temporary id by the background job.
        QString oldId;
        for (const AppListModel::Entry &other : m_model.entries())
            if (other.app.temporaryId && !other.busy && other.app.url.compare(app.url, Qt::CaseInsensitive) == 0)
                oldId = other.app.id;
        m_model.upsert(makeEntry(app), oldId);
        if (!oldId.isEmpty()) {
            onDisk.insert(oldId); // now this entry; nothing to remove
            emit appIdChanged(oldId, app.id);
        }
    }
    QStringList gone;
    for (const AppListModel::Entry &e : m_model.entries())
        if (!onDisk.contains(e.app.id) && !e.busy)
            gone << e.app.id;
    for (const QString &id : gone)
        m_model.remove(id);
}

void HarpoonController::storeAndShow(const App &app, const QString &oldId)
{
    const QString diskId = oldId.isEmpty() ? app.id : oldId;
    if (m_model.entry(diskId) && !m_store.contains(diskId)) {
        // Renamed or removed by the background job since we loaded it:
        // writing our copy back would recreate a stale record.
        refresh();
        emit operationFinished(app.id, false, tr("The app was changed in the background; try again."));
        return;
    }
    const Error saved = oldId.isEmpty() || oldId == app.id ? m_store.save(app) : m_store.replace(oldId, app);
    if (!saved.ok()) {
        // Do not show what was not stored.
        emit operationFinished(app.id, false, saved.message);
        return;
    }
    m_model.upsert(makeEntry(app), oldId);
    if (!oldId.isEmpty() && oldId != app.id)
        emit appIdChanged(oldId, app.id);
}

QVariantMap HarpoonController::parseAddLink(const QString &text) const
{
    const auto link = Harpoon::parseAddLink(text);
    if (!link.ok())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), link.error.message}};
    // A source the registry does not know would only fail later.
    if (!link.value.sourceId.isEmpty() && !m_registry.ids().contains(link.value.sourceId))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), tr("Unknown source type: %1").arg(link.value.sourceId)}};
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("url"), link.value.url},
            {QStringLiteral("sourceId"), link.value.sourceId},
            {QStringLiteral("packageName"), link.value.packageName}};
}

QString HarpoonController::shareLink(const QString &id) const
{
    const AppListModel::Entry *e = m_model.entry(id);
    if (!e)
        return QString();
    const App &a = e->app;
    const auto match = m_registry.match(a.url, a.sourceId);
    if (!match.ok())
        return QString();
    AddLink link;
    link.url = match.value.standardUrl;
    const QString sourceId = match.value.source->id();
    if (sourceId == QLatin1String("RpmMdRepo"))
        link.packageName = a.settings.getString(Keys::packageName);
    // Name the source unless the URL alone leads to the same one.
    const auto detected = m_registry.match(link.url, QString());
    if (!detected.ok() || detected.value.source->id() != sourceId || !link.packageName.isEmpty())
        link.sourceId = sourceId;
    return link.sourceId.isEmpty() ? link.url : link.toString();
}

QVariantMap HarpoonController::inspectUrl(const QString &url, const QString &sourceId) const
{
    QVariantMap out;
    const auto match = m_registry.match(url, sourceId);
    out.insert(QStringLiteral("ok"), match.ok());
    if (!match.ok()) {
        out.insert(QStringLiteral("error"), match.error.message);
        return out;
    }
    out.insert(QStringLiteral("standardUrl"), match.value.standardUrl);
    out.insert(QStringLiteral("sourceId"), match.value.source->id());
    out.insert(QStringLiteral("sourceName"), match.value.source->displayName());
    for (const AppListModel::Entry &e : m_model.entries())
        if (e.app.url.compare(match.value.standardUrl, Qt::CaseInsensitive) == 0) {
            out.insert(QStringLiteral("ok"), false);
            out.insert(QStringLiteral("error"), tr("Already tracked as %1").arg(e.app.name));
            out.insert(QStringLiteral("existingId"), e.app.id);
        }
    return out;
}

void HarpoonController::beginCheck()
{
    if (m_checksRunning++ == 0)
        emit checkingChanged();
}

void HarpoonController::endCheck()
{
    if (--m_checksRunning == 0)
        emit checkingChanged();
}

void HarpoonController::addApp(const QString &url, const QString &sourceId, const QVariantMap &settings, bool force)
{
    const QVariantMap inspected = inspectUrl(url, sourceId);
    if (!inspected.value(QStringLiteral("ok")).toBool()) {
        emit addFinished(false, inspected.value(QStringLiteral("error")).toString());
        return;
    }
    App app = App::fromUrl(inspected.value(QStringLiteral("standardUrl")).toString(), sourceId);
    QVariantMap values;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it)
        if (it.value().isValid() && !(it.value().type() == QVariant::String && it.value().toString().isEmpty()))
            values.insert(it.key(), it.value());
    app.settings = AppSettings(values);

    beginCheck();
    checker().check(app, [this, force](const App &checked, const Error &error) {
        endCheck();
        if (!error.ok() && !force) {
            emit addFinished(false, error.message);
            return;
        }
        App added = checked;
        adoptInstalled(added);
        storeAndShow(added);
        emit addFinished(true, added.id);
    });
}

bool HarpoonController::adoptInstalled(App &app) const
{
    QStringList taken;
    for (const AppListModel::Entry &e : m_model.entries())
        if (e.app.id != app.id)
            taken << e.app.id;
    return adoptInstalledPackage(
        app,
        [this](const QString &name) {
            const auto installed = m_inspector->installedPackage(name);
            return installed.ok() && !installed.value.name.isEmpty();
        },
        taken);
}

void HarpoonController::check(const QString &id)
{
    const AppListModel::Entry *e = m_model.entry(id);
    if (!e || e->busy)
        return;
    m_model.setBusy(id, true, tr("Checking"));
    beginCheck();
    checker().check(e->app, [this, id](const App &checked, const Error &error) {
        endCheck();
        // The app may have been removed while the check ran.
        const AppListModel::Entry *current = m_model.entry(id);
        if (!current)
            return;
        // Take only what the check owns; settings or the name may have been
        // edited meanwhile.
        App merged = applyCheckResult(current->app, checked);
        // Installed some other way since it was added: follow that package.
        adoptInstalled(merged);
        storeAndShow(merged, id);
        m_model.setBusy(merged.id, false);
        // No banner: the list, the app page and the cover show a failed check.
        emit operationFinished(merged.id, error.ok(), QString());
    });
}

void HarpoonController::checkAll()
{
    for (const AppListModel::Entry &e : m_model.entries())
        if (!e.busy)
            check(e.app.id);
    m_lastCheckAll = QDateTime::currentDateTimeUtc();
    emit checkingChanged();
}

void HarpoonController::checkStale(int maxAgeMinutes)
{
    const QDateTime limit = QDateTime::currentDateTimeUtc().addSecs(-qint64(maxAgeMinutes) * 60);
    for (const AppListModel::Entry &e : m_model.entries())
        if (!e.busy && (!e.app.lastCheck.isValid() || e.app.lastCheck < limit))
            check(e.app.id);
}

void HarpoonController::install(const QString &id, bool reinstall, bool downgrade)
{
    const AppListModel::Entry *e = m_model.entry(id);
    if (!e)
        return;
    if (e->busy) {
        emit operationFinished(id, false, tr("%1 is busy: %2").arg(e->app.name, e->stage));
        return;
    }
    const App app = e->app;
    m_model.setBusy(id, true, tr("Preparing"));

    auto *installer = new AppInstaller(m_downloader, *m_inspector, backend(), m_cacheDir, m_device, this);
    installer->setDownloadPreparer(checker().downloadPreparer(app));
    installer->setVerifier(checker().verifier(app));
    InstallOptions options;
    options.allowReinstall = reinstall;
    options.allowDowngrade = downgrade;
    installer->install(
        app, options,
        [this, id](const QString &stage, qint64 done, qint64 total) {
            m_model.setBusy(id, true, localizedStage(stage), total > 0 ? qreal(done) / qreal(total) : -1);
        },
        [this, id, installer](const Result<InstallResult> &result) {
            installer->deleteLater();
            m_model.setBusy(id, false);
            if (!result.ok()) {
                emit operationFinished(id, false, result.error.message);
                return;
            }
            const AppListModel::Entry *current = m_model.entry(id);
            if (!current) {
                // Stopped tracking while installing: the install itself stands.
                emit operationFinished(result.value.app.id, true,
                                       tr("Installed %1").arg(result.value.installed.evr.toString()));
                return;
            }
            QString message = tr("Installed %1").arg(result.value.installed.evr.toString());
            for (const QString &w : result.value.warnings)
                message += QLatin1Char('\n') + w;

            const QString newId = result.value.app.id;
            const AppListModel::Entry *other = newId != id ? m_model.entry(newId) : nullptr;
            const bool idTaken = other || (newId != id && m_store.contains(newId));
            const App merged = recordAfterInstall(current->app, result.value, idTaken);
            if (idTaken) {
                message += QLatin1Char('\n')
                           + tr("This package is also tracked as \"%1\"; remove one of the two.")
                                 .arg(other ? other->app.name : newId);
                storeAndShow(merged);
                emit operationFinished(id, true, message);
                return;
            }
            storeAndShow(merged, id);
            emit operationFinished(newId, true, message);
        });
}

QString HarpoonController::localizedStage(const QString &stage) const
{
    // AppInstaller reports its stages in English (it is also used by the CLI).
    const auto rest = [&stage](const char *prefix) { return stage.mid(int(qstrlen(prefix))); };
    if (stage.startsWith(QLatin1String("Downloading ")))
        return tr("Downloading %1").arg(rest("Downloading "));
    if (stage.startsWith(QLatin1String("Verifying ")))
        return tr("Verifying %1").arg(rest("Verifying "));
    if (stage.startsWith(QLatin1String("Installing ")))
        return tr("Installing %1").arg(rest("Installing "));
    if (stage == QLatin1String("Checking packages"))
        return tr("Checking packages");
    return stage;
}

void HarpoonController::updateAll()
{
    for (const AppListModel::Entry &e : m_model.entries())
        if (e.status.state == UpdateState::UpdateAvailable && !e.app.settings.getBool(Keys::trackOnly) && !e.busy)
            install(e.app.id);
}

void HarpoonController::uninstall(const QString &id)
{
    const AppListModel::Entry *e = m_model.entry(id);
    if (!e)
        return;
    if (e->busy) {
        emit operationFinished(id, false, tr("%1 is busy: %2").arg(e->app.name, e->stage));
        return;
    }
    const App app = e->app;
    m_model.setBusy(id, true, tr("Uninstalling"));
    auto *installer = new AppInstaller(m_downloader, *m_inspector, backend(), m_cacheDir, m_device, this);
    installer->uninstall(app, [this, id, installer](const Error &error) {
        installer->deleteLater();
        m_model.setBusy(id, false);
        if (const AppListModel::Entry *current = m_model.entry(id))
            m_model.upsert(makeEntry(current->app));
        emit operationFinished(id, error.ok(), error.ok() ? tr("Uninstalled") : error.message);
    });
}

void HarpoonController::removeApp(const QString &id)
{
    const Error removed = m_store.remove(id);
    if (!removed.ok()) {
        emit operationFinished(id, false, removed.message);
        return;
    }
    m_model.remove(id);
}

void HarpoonController::acknowledge(const QString &id)
{
    const AppListModel::Entry *e = m_model.entry(id);
    if (!e)
        return;
    App app = e->app;
    app.acknowledgedVersion = app.latestVersion;
    storeAndShow(app);
}

void HarpoonController::setAppSetting(const QString &id, const QString &key, const QVariant &value)
{
    const AppListModel::Entry *e = m_model.entry(id);
    if (!e || key.isEmpty())
        return;
    App app = e->app;
    QVariantMap values = app.settings.values();
    if (!value.isValid() || value.isNull() || (value.type() == QVariant::String && value.toString().isEmpty()))
        values.remove(key);
    else
        values.insert(key, value);
    app.settings = AppSettings(values);
    storeAndShow(app);
}

void HarpoonController::setAppName(const QString &id, const QString &name)
{
    const AppListModel::Entry *e = m_model.entry(id);
    if (!e)
        return;
    App app = e->app;
    // Empty: back to the automatic name.
    app.name = name.trimmed().isEmpty() ? App::fromUrl(app.url).name : name.trimmed();
    storeAndShow(app);
}

QVariantMap HarpoonController::appDetails(const QString &id) const
{
    const AppListModel::Entry *e = m_model.entry(id);
    if (!e)
        return QVariantMap();
    const App &a = e->app;
    QVariantList assets;
    for (const Asset &asset : a.latestAssets)
        assets << assetToVariant(asset);
    QString sourceName = a.sourceId;
    QString effectiveSourceId = a.sourceId;
    const auto match = m_registry.match(a.url, a.sourceId);
    if (match.ok()) {
        sourceName = match.value.source->displayName();
        effectiveSourceId = match.value.source->id();
    }
    return {
        {QStringLiteral("appId"), a.id},
        {QStringLiteral("temporaryId"), a.temporaryId},
        {QStringLiteral("name"), e->displayName.isEmpty() ? a.name : e->displayName},
        // The stored name: the automatic one unless the user renamed the app.
        {QStringLiteral("customName"), a.name != App::fromUrl(a.url).name ? a.name : QString()},
        {QStringLiteral("icon"), e->iconPath},
        {QStringLiteral("author"), a.author},
        {QStringLiteral("url"), a.url},
        {QStringLiteral("sourceId"), a.sourceId},
        {QStringLiteral("sourceName"), sourceName},
        {QStringLiteral("effectiveSourceId"), effectiveSourceId},
        {QStringLiteral("state"), int(AppListModel::toState(e->status.state))},
        {QStringLiteral("installedVersion"), e->status.installedVersion},
        {QStringLiteral("installedVendor"), e->installed.vendor},
        {QStringLiteral("latestVersion"), a.latestVersion},
        {QStringLiteral("latestTag"), a.latestTag},
        {QStringLiteral("latestTitle"), a.latestTitle},
        {QStringLiteral("latestDate"), a.latestDate},
        {QStringLiteral("prerelease"), a.latestPrerelease},
        {QStringLiteral("changelog"), a.changelog},
        {QStringLiteral("releasePageUrl"), a.releasePageUrl},
        {QStringLiteral("assets"), assets},
        {QStringLiteral("lastCheck"), a.lastCheck},
        {QStringLiteral("lastError"), a.lastError},
        {QStringLiteral("trackOnly"), a.settings.getBool(Keys::trackOnly)},
        {QStringLiteral("acknowledgedVersion"), a.acknowledgedVersion},
        {QStringLiteral("receiptEvr"), a.receipt.evr},
        {QStringLiteral("receiptInstalledAt"), a.receipt.installedAt},
        {QStringLiteral("receiptVerification"), a.receipt.verification},
        {QStringLiteral("settings"), a.settings.values()},
        {QStringLiteral("busy"), e->busy},
        {QStringLiteral("stage"), e->stage},
        {QStringLiteral("progress"), e->progress},
    };
}

} // namespace Harpoon

namespace Harpoon {

void HarpoonController::syncBackgroundSchedule()
{
    m_scheduler->apply(m_settings->backgroundChecks(), m_settings->checkIntervalHours(), [this](const Error &e) {
        if (!e.ok())
            emit backgroundError(e.message);
    });
}

QVariantMap HarpoonController::exportBackup(bool includeTokens)
{
    QList<App> apps;
    for (const AppListModel::Entry &e : m_model.entries())
        apps << e.app;
    const Backup backup = Backup::create(apps, m_settings->exportable(includeTokens), includeTokens);

    QDir().mkpath(m_backupDir);
    const QString path = m_backupDir + QStringLiteral("/harpoon-backup-")
                         + QDate::currentDate().toString(Qt::ISODate) + QStringLiteral(".json");
    const Error written = backup.writeTo(path, includeTokens);
    if (!written.ok())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), written.message}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), path}, {QStringLiteral("count"), backup.apps.size()}};
}

QVariantMap HarpoonController::importBackup(const QString &pathOrUrl, bool withSettings)
{
    const QString path = pathOrUrl.startsWith(QLatin1String("file:")) ? QUrl(pathOrUrl).toLocalFile() : pathOrUrl;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), tr("Cannot read %1: %2").arg(path, file.errorString())}};
    const auto backup = Backup::fromJson(file.readAll());
    if (!backup.ok())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), backup.error.message}};

    QList<App> existing;
    for (const AppListModel::Entry &e : m_model.entries())
        existing << e.app;
    const ImportResult merged = mergeBackupApps(existing, backup.value.apps);
    for (const App &app : merged.added)
        storeAndShow(app);
    if (withSettings)
        m_settings->restore(backup.value.settings);
    // Imported apps carry no release data; fetch it.
    for (const App &app : merged.added)
        check(app.id);
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("added"), merged.added.size()},
            {QStringLiteral("skipped"), merged.skipped}};
}

} // namespace Harpoon
