#pragma once

#include <QObject>

#include <functional>

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

    // Tells app links (harpoon:// or an address a source recognises) from
    // other web links. Without one, every link is taken as an app link.
    void setAppLinkTest(std::function<bool(const QString &)> isAppLink) { m_isAppLink = std::move(isAppLink); }
    // Where other web links go; by default the Sailfish browser, over D-Bus.
    void setBrowser(std::function<void(const QString &)> openInBrowser) { m_openInBrowser = std::move(openInBrowser); }

public slots:
    // Bring the window to the front.
    void activate();
    // Bring the window to the front and show the list of updates.
    void showUpdates();
    // Bring the window to the front and open one app.
    void showApp(const QString &id);
    // Open harpoon://add?... links and links to apps' repositories (the
    // x-scheme-handler for "harpoon:", "http:" and "https:"). Called by the
    // system when such a link is opened, e.g. from the camera's QR reader or
    // a web page. Other web links go on to the browser, without showing
    // Harpoon, in case the system made Harpoon its handler for all of them.
    void openUrl(const QStringList &urls);

signals:
    void activateRequested();
    void showUpdatesRequested();
    void showAppRequested(const QString &id);
    void addLinkRequested(const QString &link);

private:
    std::function<bool(const QString &)> m_isAppLink;
    std::function<void(const QString &)> m_openInBrowser;
};

} // namespace Harpoon
