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

signals:
    void activateRequested();
    void showUpdatesRequested();
    void showAppRequested(const QString &id);
};

} // namespace Harpoon
