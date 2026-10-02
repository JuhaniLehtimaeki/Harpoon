#include "harpoondbus.h"

#include <QDBusConnection>

namespace Harpoon {

HarpoonDBus::HarpoonDBus(QObject *parent) : QObject(parent) {}

bool HarpoonDBus::registerOnSessionBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(objectPath(), this, QDBusConnection::ExportAllSlots))
        return false;
    return bus.registerService(serviceName());
}

void HarpoonDBus::activate()
{
    emit activateRequested();
}

void HarpoonDBus::showUpdates()
{
    emit activateRequested();
    emit showUpdatesRequested();
}

void HarpoonDBus::openUrl(const QStringList &urls)
{
    emit activateRequested();
    for (const QString &url : urls)
        emit addLinkRequested(url);
}

void HarpoonDBus::showApp(const QString &id)
{
    emit activateRequested();
    emit showAppRequested(id);
}

} // namespace Harpoon
