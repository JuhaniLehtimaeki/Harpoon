#include "notify/notifier.h"

#include <QBuffer>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDataStream>
#include <QDateTime>

namespace Harpoon {

namespace {
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");
const QString kInterface = QStringLiteral("org.freedesktop.Notifications");
} // namespace

Notifier::Notifier(const QDBusConnection &bus, const QString &service) : m_bus(bus), m_service(service) {}

QString Notifier::encodeDBusCall(const QString &service, const QString &path, const QString &iface,
                                 const QString &method, const QVariantList &arguments)
{
    QString s = service + QLatin1Char(' ') + path + QLatin1Char(' ') + iface + QLatin1Char(' ') + method;
    for (const QVariant &arg : arguments) {
        QByteArray buffer;
        QDataStream stream(&buffer, QIODevice::WriteOnly);
        stream << arg;
        s += QLatin1Char(' ') + QString::fromLatin1(buffer.toBase64());
    }
    return s;
}

QVariantMap Notifier::hintsFor(const NotificationRequest &r)
{
    QVariantMap hints;
    hints.insert(QStringLiteral("urgency"), r.urgency);
    hints.insert(QStringLiteral("x-nemo-timestamp"), QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!r.category.isEmpty())
        hints.insert(QStringLiteral("category"), r.category);
    if (r.transient)
        hints.insert(QStringLiteral("transient"), true);
    if (r.itemCount > 0)
        hints.insert(QStringLiteral("x-nemo-item-count"), r.itemCount);
    if (!r.previewSummary.isEmpty())
        hints.insert(QStringLiteral("x-nemo-preview-summary"), r.previewSummary);
    if (!r.previewBody.isEmpty())
        hints.insert(QStringLiteral("x-nemo-preview-body"), r.previewBody);
    if (!r.remoteService.isEmpty() && !r.remotePath.isEmpty() && !r.remoteInterface.isEmpty()
        && !r.remoteMethod.isEmpty())
        hints.insert(QStringLiteral("x-nemo-remote-action-default"),
                     encodeDBusCall(r.remoteService, r.remotePath, r.remoteInterface, r.remoteMethod,
                                    r.remoteArguments));
    return hints;
}

Result<uint> Notifier::notify(const NotificationRequest &r)
{
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, kPath, kInterface, QStringLiteral("Notify"));
    QStringList actions;
    if (!r.remoteMethod.isEmpty())
        actions << QStringLiteral("default") << QString();
    call << r.appName << r.replacesId << r.appIcon << r.summary << r.body << actions << hintsFor(r)
         << r.expireTimeout;
    const QDBusReply<uint> reply = m_bus.call(call, QDBus::BlockWithGui, 10000);
    if (!reply.isValid())
        return Result<uint>::failure(
            Error::make(Error::System, QStringLiteral("Notification failed: %1").arg(reply.error().message())));
    return Result<uint>::success(reply.value());
}

void Notifier::close(uint id)
{
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, kPath, kInterface,
                                                       QStringLiteral("CloseNotification"));
    call << id;
    m_bus.call(call, QDBus::BlockWithGui, 5000);
}

} // namespace Harpoon
