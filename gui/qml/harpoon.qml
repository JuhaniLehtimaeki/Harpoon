import QtQuick 2.0
import Sailfish.Silica 1.0
import "pages"

ApplicationWindow {
    id: window

    // Opens the details of one app, replacing whatever is above the list.
    function showApp(appId) {
        pageStack.pop(pageStack.find(function(page) { return page.objectName === "appListPage" }),
                      PageStackAction.Immediate)
        pageStack.push(Qt.resolvedUrl("pages/AppPage.qml"), { appId: appId })
    }

    initialPage: Component { AppListPage { } }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

    Connections {
        target: harpoonDBus
        onActivateRequested: window.activate()
        onShowUpdatesRequested: pageStack.pop(pageStack.find(function(page) {
            return page.objectName === "appListPage"
        }), PageStackAction.Immediate)
        onShowAppRequested: window.showApp(id)
    }

    Connections {
        target: harpoon
        // A first install replaces the temporary id with the RPM name; every
        // page showing that app follows, not only the top one.
        onAppIdChanged: {
            pageStack.find(function(page) {
                if (page.appId === oldId) {
                    page.appId = newId
                }
                return false
            })
        }
    }
}
