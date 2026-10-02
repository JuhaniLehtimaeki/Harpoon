#include "app/backgroundscheduler.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace Harpoon {

namespace {
const QString kService = QStringLiteral("org.freedesktop.systemd1");
const QString kPath = QStringLiteral("/org/freedesktop/systemd1");
const QString kManager = QStringLiteral("org.freedesktop.systemd1.Manager");
} // namespace

BackgroundScheduler::BackgroundScheduler(const QDBusConnection &bus, const QString &configDir, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_configDir(configDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                      : configDir)
{
}

QString BackgroundScheduler::dropInPath() const
{
    return m_configDir + QStringLiteral("/systemd/user/") + timerUnit() + QStringLiteral(".d/50-interval.conf");
}

QByteArray BackgroundScheduler::dropInContent(int intervalHours)
{
    // An empty assignment clears the packaged default before setting ours.
    return "# Written by Harpoon from its settings; edits are overwritten.\n"
           "[Timer]\n"
           "OnUnitActiveSec=\n"
           "OnUnitActiveSec="
           + QByteArray::number(qBound(1, intervalHours, 168)) + "h\n";
}

void BackgroundScheduler::call(const QString &method, const QList<QVariant> &args,
                               std::function<void(const Error &)> next)
{
    if (!m_bus.isConnected()) {
        // Without a bus a pending call never reports back.
        next(Error::make(Error::System, QStringLiteral("No D-Bus session bus: %1").arg(m_bus.lastError().message())));
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kManager, method);
    message.setArguments(args);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [method, next](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError()) {
            // Stopping a unit that is not loaded is not a failure.
            if (method == QLatin1String("StopUnit")
                && w->error().name() == QLatin1String("org.freedesktop.systemd1.NoSuchUnit")) {
                next(Error());
                return;
            }
            next(Error::make(Error::System, QStringLiteral("systemd %1 failed: %2").arg(method, w->error().message())));
            return;
        }
        next(Error());
    });
}

void BackgroundScheduler::apply(bool enabled, int intervalHours, std::function<void(const Error &)> done)
{
    auto finish = [done](const Error &e) {
        if (done)
            done(e);
    };

    const QString path = dropInPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(dropInContent(intervalHours)) < 0 || !file.commit()) {
        finish(Error::make(Error::System, QStringLiteral("Cannot write %1").arg(path)));
        return;
    }

    const QStringList units{timerUnit()};
    call(QStringLiteral("Reload"), {}, [this, enabled, units, finish](const Error &reloaded) {
        if (!reloaded.ok()) {
            finish(reloaded);
            return;
        }
        if (enabled) {
            call(QStringLiteral("EnableUnitFiles"), {units, false, true}, [this, finish](const Error &e) {
                if (!e.ok()) {
                    finish(e);
                    return;
                }
                // Restart so a changed interval takes effect now.
                call(QStringLiteral("RestartUnit"), {timerUnit(), QStringLiteral("replace")}, finish);
            });
        } else {
            call(QStringLiteral("StopUnit"), {timerUnit(), QStringLiteral("replace")}, [this, units, finish](const Error &e) {
                if (!e.ok()) {
                    finish(e);
                    return;
                }
                call(QStringLiteral("DisableUnitFiles"), {units, false}, finish);
            });
        }
    });
}

} // namespace Harpoon
