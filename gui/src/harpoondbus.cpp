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

void HarpoonDBus::showApp(const QString &id)
{
    emit activateRequested();
    emit showAppRequested(id);
}

} // namespace Harpoon
