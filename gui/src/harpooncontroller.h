#pragma once

#include "applistmodel.h"
#include "app/harpoonsettings.h"

#include "app/appchecker.h"
#include "app/appstore.h"
#include "app/backgroundscheduler.h"
#include "net/downloader.h"
#include "net/httptransport.h"
#include "pipeline/deviceinfo.h"
#include "pkg/packagebackend.h"
#include "pkg/rpminspector.h"
#include "sources/sourceregistry.h"

#include <QObject>
#include <QVariantMap>

#include <memory>

namespace Harpoon {

class AppInstaller;

// Everything the controller needs from the outside world. Null pointers are
// replaced by the real implementations; tests inject fakes.
struct ControllerEnvironment
{
    QString dataDir;                     // app records; empty: AppStore default
    QString cacheDir;                    // downloads; empty: CacheLocation/downloads
    DeviceInfo device;                   // arch empty: DeviceInfo::detect()
    HttpTransport *transport = nullptr;
    ProcessRunner *runner = nullptr;
    PackageBackend *backend = nullptr;   // forces a backend regardless of settings
    HarpoonSettings *settings = nullptr;
    BackgroundScheduler *scheduler = nullptr;
    QString backupDir;                   // exports; empty: DocumentsLocation
};

// The QML-facing API. Owns the core objects and keeps AppListModel in sync
// with the app store, update checks and installs. All operations are
// asynchronous; results arrive as model changes and signals.
class HarpoonController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Harpoon::AppListModel *apps READ apps CONSTANT)
    Q_PROPERTY(Harpoon::HarpoonSettings *settings READ settings CONSTANT)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(QString deviceArch READ deviceArch CONSTANT)
    Q_PROPERTY(QString osVersion READ osVersion CONSTANT)
    Q_PROPERTY(QVariantList sources READ sources CONSTANT)
    Q_PROPERTY(QDateTime lastCheckAll READ lastCheckAll NOTIFY checkingChanged)

public:
    explicit HarpoonController(ControllerEnvironment env = ControllerEnvironment(), QObject *parent = nullptr);
    ~HarpoonController() override;

    AppListModel *apps() { return &m_model; }
    HarpoonSettings *settings() { return m_settings; }
    bool checking() const { return m_checksRunning > 0; }
    QString deviceArch() const { return m_device.arch; }
    QString osVersion() const { return m_device.osVersion; }
    QVariantList sources() const;
    QDateTime lastCheckAll() const { return m_lastCheckAll; }

    // Reloads records from disk and re-reads installed versions.
    Q_INVOKABLE void reload();

    // Checks the URL without saving: {ok, standardUrl, sourceId, sourceName, error}.
    Q_INVOKABLE QVariantMap inspectUrl(const QString &url, const QString &sourceId = QString()) const;
    // Tracks a new app after a first successful check (or always with force).
    // Emits addFinished.
    Q_INVOKABLE void addApp(const QString &url, const QString &sourceId, const QVariantMap &settings,
                            bool force = false);

    Q_INVOKABLE void checkAll();
    // Checks apps whose last check is older than maxAgeMinutes (or never).
    Q_INVOKABLE void checkStale(int maxAgeMinutes);
    Q_INVOKABLE void check(const QString &id);
    Q_INVOKABLE void install(const QString &id, bool reinstall = false, bool downgrade = false);
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE void uninstall(const QString &id);
    Q_INVOKABLE void removeApp(const QString &id);
    Q_INVOKABLE void acknowledge(const QString &id);

    // Changes one per-app setting; an empty string or null removes it.
    Q_INVOKABLE void setAppSetting(const QString &id, const QString &key, const QVariant &value);
    Q_INVOKABLE void setAppName(const QString &id, const QString &name);
    // All details of one app for the details page.
    Q_INVOKABLE QVariantMap appDetails(const QString &id) const;

    // Writes a backup to the documents folder: {ok, path, error}.
    Q_INVOKABLE QVariantMap exportBackup(bool includeTokens);
    // Adds apps from a backup file (path or file:// URL): {ok, added, skipped, error}.
    Q_INVOKABLE QVariantMap importBackup(const QString &path, bool withSettings);

    // Enables or disables the systemd timer to match the settings.
    Q_INVOKABLE void syncBackgroundSchedule();

signals:
    void checkingChanged();
    void addFinished(bool ok, const QString &idOrError);
    // An install, uninstall or check finished. message is user-presentable.
    void operationFinished(const QString &id, bool ok, const QString &message);
    // Emitted after an id changed (temporary id -> RPM name).
    void appIdChanged(const QString &oldId, const QString &newId);
    // Changing the background schedule failed.
    void backgroundError(const QString &message);

private:
    AppListModel::Entry makeEntry(const App &app) const;
    void storeAndShow(const App &app, const QString &oldId = QString());
    PackageBackend &backend();
    QHash<QString, QVariantMap> tokenConfigs() const;
    AppChecker &checker();
    void beginCheck();
    void endCheck();

    QString m_cacheDir;
    DeviceInfo m_device;
    AppStore m_store;
    SourceRegistry m_registry;
    AppListModel m_model;

    std::unique_ptr<HttpTransport> m_ownTransport;
    std::unique_ptr<ProcessRunner> m_ownRunner;
    std::unique_ptr<PackageBackend> m_packageKit;
    std::unique_ptr<PackageBackend> m_handler;
    HttpTransport *m_transport;
    ProcessRunner *m_runner;
    PackageBackend *m_forcedBackend;
    HarpoonSettings *m_settings;
    BackgroundScheduler *m_scheduler;
    QString m_backupDir;
    std::unique_ptr<RpmInspector> m_inspector;
    Downloader m_downloader;
    std::unique_ptr<AppChecker> m_checker;
    int m_checksRunning = 0;
    QDateTime m_lastCheckAll;
};

} // namespace Harpoon
