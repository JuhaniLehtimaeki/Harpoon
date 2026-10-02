#pragma once

#include <QObject>
#include <QSettings>
#include <QVariantMap>

#include <memory>

namespace Harpoon {

// Global app settings, stored in <AppConfigLocation>/harpoon.conf. Sailjail
// only persists files inside that folder, so the default QSettings location
// (beside it) must not be used.
class HarpoonSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString installBackend READ installBackend WRITE setInstallBackend NOTIFY changed)
    Q_PROPERTY(bool backgroundChecks READ backgroundChecks WRITE setBackgroundChecks NOTIFY changed)
    Q_PROPERTY(int checkIntervalHours READ checkIntervalHours WRITE setCheckIntervalHours NOTIFY changed)
    Q_PROPERTY(bool notifyUpdates READ notifyUpdates WRITE setNotifyUpdates NOTIFY changed)
    Q_PROPERTY(bool autoUpdate READ autoUpdate WRITE setAutoUpdate NOTIFY changed)

public:
    // filePath empty: the default location.
    explicit HarpoonSettings(const QString &filePath = QString(), QObject *parent = nullptr);

    QString installBackend() const;         // "packagekit" (default) | "handler"
    void setInstallBackend(const QString &backend);
    bool backgroundChecks() const;          // default true
    void setBackgroundChecks(bool enabled);
    int checkIntervalHours() const;         // default 6, clamped to 1..168
    void setCheckIntervalHours(int hours);
    bool notifyUpdates() const;             // default true
    void setNotifyUpdates(bool enabled);
    // Install updates during background checks (PackageKit only). Default off.
    bool autoUpdate() const;
    void setAutoUpdate(bool enabled);

    // API tokens per source id ("GitHub", "Forgejo", ...). Plain text in the
    // app's private config folder; not a secure store.
    Q_INVOKABLE QString token(const QString &sourceId) const;
    Q_INVOKABLE void setToken(const QString &sourceId, const QString &token);
    QVariantMap tokens() const;

    // Settings for a backup; tokens only when includeTokens.
    QVariantMap exportable(bool includeTokens) const;
    // Restores what exportable() produced. Unknown keys are ignored.
    void restore(const QVariantMap &values);

    QString filePath() const;
    static QString defaultFilePath();

signals:
    void changed();
    void tokensChanged();

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace Harpoon
