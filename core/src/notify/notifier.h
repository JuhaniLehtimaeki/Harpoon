#pragma once

#include "model/error.h"

#include <QDBusConnection>
#include <QVariantList>
#include <QVariantMap>

namespace Harpoon {

// A desktop notification with the SailfishOS (Nemo/Lipstick) extensions.
struct NotificationRequest
{
    QString appName;
    QString appIcon;
    QString summary;
    QString body;
    QString previewSummary;   // banner shown on arrival
    QString previewBody;
    QString category;
    int itemCount = 0;
    uint replacesId = 0;      // update an earlier notification in place
    int urgency = 1;          // 0 low, 1 normal, 2 critical
    bool transient = false;   // not kept in the Events view
    int expireTimeout = -1;

    // D-Bus call made when the notification is tapped ("default" action).
    QString remoteService;
    QString remotePath;
    QString remoteInterface;
    QString remoteMethod;
    QVariantList remoteArguments;
};

// Sends notifications through org.freedesktop.Notifications, which Lipstick
// implements on SailfishOS. Synchronous: used by the CLI and background job.
class Notifier
{
public:
    explicit Notifier(const QDBusConnection &bus = QDBusConnection::sessionBus(),
                      const QString &service = QStringLiteral("org.freedesktop.Notifications"));

    // Returns the notification id.
    Result<uint> notify(const NotificationRequest &request);
    void close(uint id);

    // The hint map sent with a request (exposed for tests).
    static QVariantMap hintsFor(const NotificationRequest &request);
    // "service path interface method [base64 QDataStream args...]", the
    // encoding nemo-qml-plugin-notifications uses for remote actions.
    static QString encodeDBusCall(const QString &service, const QString &path, const QString &iface,
                                  const QString &method, const QVariantList &arguments);

private:
    QDBusConnection m_bus;
    QString m_service;
};

} // namespace Harpoon
