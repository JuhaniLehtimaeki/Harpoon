#pragma once

#include <QObject>

namespace Harpoon {

// Session bus service io.github.juhanilehtimaeki.harpoon at /harpoon, so
// notifications (and the D-Bus activation file) can open the app.
class HarpoonDBus : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.juhanilehtimaeki.harpoon")

public:
    explicit HarpoonDBus(QObject *parent = nullptr);

    // Registers the service and object; false if another instance owns it.
    bool registerOnSessionBus();

    static QString serviceName() { return QStringLiteral("io.github.juhanilehtimaeki.harpoon"); }
    static QString objectPath() { return QStringLiteral("/harpoon"); }

public slots:
    // Bring the window to the front.
    void activate();
    // Bring the window to the front and show the list of updates.
    void showUpdates();
    // Bring the window to the front and open one app.
    void showApp(const QString &id);
    // Open harpoon://add?... links (the x-scheme-handler for "harpoon:").
    // Called by the system when such a link is opened, e.g. from the camera's
    // QR reader or a web page.
    void openUrl(const QStringList &urls);

signals:
    void activateRequested();
    void showUpdatesRequested();
    void showAppRequested(const QString &id);
    void addLinkRequested(const QString &link);
};

} // namespace Harpoon
