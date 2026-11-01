#include "harpoondbus.h"

#include <QDBusConnection>
#include <QDBusMessage>

namespace Harpoon {

HarpoonDBus::HarpoonDBus(QObject *parent) : QObject(parent)
{
    m_openInBrowser = [](const QString &url) {
        QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.sailfishos.browser.ui"), QStringLiteral("/ui"), QStringLiteral("org.sailfishos.browser.ui"),
            QStringLiteral("openUrl"));
        call << QStringList{url};
        QDBusConnection::sessionBus().send(call);
    };
}

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
    QStringList appLinks;
    for (const QString &url : urls) {
        const bool web = url.startsWith(QLatin1String("http:"), Qt::CaseInsensitive)
                         || url.startsWith(QLatin1String("https:"), Qt::CaseInsensitive);
        if (web && m_isAppLink && !m_isAppLink(url))
            m_openInBrowser(url);
        else
            appLinks << url;
    }
    if (appLinks.isEmpty())
        return;
    emit activateRequested();
    for (const QString &url : appLinks)
        emit addLinkRequested(url);
}

void HarpoonDBus::showApp(const QString &id)
{
    emit activateRequested();
    emit showAppRequested(id);
}

} // namespace Harpoon
