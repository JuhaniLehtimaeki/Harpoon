// Harpoon: install and update SailfishOS apps from GitHub, Codeberg and
// other forges.

#include "applistmodel.h"
#include "harpooncontroller.h"
#include "harpoondbus.h"
#include "app/harpoonsettings.h"
#include "model/identity.h"
#include "qrdecoder.h"
#include "qrimageprovider.h"

#include <sailfishapp.h>

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>
#include <QTimer>
#include <QtQml>

#include <sys/stat.h>

using namespace Harpoon;

int main(int argc, char *argv[])
{
    // The app launcher (booster) starts apps with umask 0000, which would
    // make every file Harpoon writes world-writable.
    umask(022);
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    app->setOrganizationName(organizationName());
    app->setApplicationName(applicationName());
    app->setApplicationVersion(QStringLiteral(HARPOON_VERSION));

    const char *uri = "harbour.harpoon";
    qmlRegisterUncreatableType<AppListModel>(uri, 1, 0, "AppListModel", QStringLiteral("Provided by harpoon.apps"));
    qmlRegisterUncreatableType<HarpoonSettings>(uri, 1, 0, "HarpoonSettings",
                                                QStringLiteral("Provided by harpoon.settings"));
    qmlRegisterUncreatableType<HarpoonController>(uri, 1, 0, "HarpoonController",
                                                  QStringLiteral("Provided as the harpoon context property"));

    HarpoonController controller;
    controller.reload();
    // Make the systemd timer match the settings (also enables it on first run).
    controller.syncBackgroundSchedule();

    HarpoonDBus dbus;
    if (!dbus.registerOnSessionBus())
        qWarning("Harpoon: could not register %s on the session bus", qPrintable(HarpoonDBus::serviceName()));

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->rootContext()->setContextProperty(QStringLiteral("harpoon"), &controller);
    view->rootContext()->setContextProperty(QStringLiteral("harpoonDBus"), &dbus);
    QrDecoder qrDecoder;
    view->rootContext()->setContextProperty(QStringLiteral("qrDecoder"), &qrDecoder);
    view->engine()->addImageProvider(QStringLiteral("harpoonqr"), new Harpoon::QrImageProvider);
    view->setSource(SailfishApp::pathToMainQml());
    view->show();

    // Started to open a link (harpoon:// passed on the command line).
    QStringList links;
    for (const QString &arg : app->arguments().mid(1))
        if (arg.startsWith(QLatin1String("harpoon:")))
            links << arg;
    if (!links.isEmpty())
        QTimer::singleShot(0, &dbus, [&dbus, links]() { dbus.openUrl(links); });

    return app->exec();
}
