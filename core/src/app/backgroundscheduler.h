#pragma once

#include "model/error.h"

#include <QDBusConnection>
#include <QObject>

#include <functional>

namespace Harpoon {

// Turns the background update check on or off. The package ships the systemd
// user units harpoon-check.service and harpoon-check.timer; this class talks
// to the user's systemd (session bus, org.freedesktop.systemd1) to
// enable/start or stop/disable the timer, and sets the interval through a
// drop-in file:
//   <configDir>/systemd/user/harpoon-check.timer.d/50-interval.conf
class BackgroundScheduler : public QObject
{
    Q_OBJECT
public:
    // configDir empty: QStandardPaths::GenericConfigLocation (~/.config).
    explicit BackgroundScheduler(const QDBusConnection &bus = QDBusConnection::sessionBus(),
                                 const QString &configDir = QString(), QObject *parent = nullptr);

    static QString timerUnit() { return QStringLiteral("harpoon-check.timer"); }
    QString dropInPath() const;

    // Applies the settings. Calls are serialized; the callback reports the
    // first failure, if any.
    void apply(bool enabled, int intervalHours, std::function<void(const Error &)> done = nullptr);

    // Drop-in file content for an interval (exposed for tests).
    static QByteArray dropInContent(int intervalHours);

private:
    void call(const QString &method, const QList<QVariant> &args, std::function<void(const Error &)> next);

    QDBusConnection m_bus;
    QString m_configDir;
};

} // namespace Harpoon
