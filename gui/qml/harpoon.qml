import QtQuick 2.0
import Sailfish.Silica 1.0
import "components"
import "pages"

ApplicationWindow {
    id: window

    // Opens the details of one app, replacing whatever is above the list.
    function showApp(appId) {
        if (pageStack.busy) {
            pageStack.completeAnimation()
        }
        pageStack.pop(pageStack.find(function(page) { return page.objectName === "appListPage" }),
                      PageStackAction.Immediate)
        // A notification can name an app by an id it has since changed (its
        // package name after the first install) or that is gone: the list then.
        if (harpoon.appDetails(appId).appId !== undefined) {
            pageStack.push(Qt.resolvedUrl("pages/AppPage.qml"), { appId: appId })
        }
    }

    // Opens the add dialog for a harpoon://add link or repository URL.
    function openAddLink(text) {
        var link = harpoon.parseAddLink(text)
        if (!link.ok) {
            linkBanner.show(qsTr("Could not add the app: %1").arg(link.error))
            return
        }
        if (pageStack.busy) {
            pageStack.completeAnimation()
        }
        pageStack.pop(pageStack.find(function(page) { return page.objectName === "appListPage" }),
                      PageStackAction.Immediate)
        pageStack.push(Qt.resolvedUrl("pages/AddAppDialog.qml"), {
                           initialUrl: link.url,
                           initialSourceId: link.sourceId,
                           initialPackage: link.packageName
                       })
    }

    initialPage: Component { AppListPage { } }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

    // The background job may have checked, updated or renamed apps while
    // Harpoon was in the background.
    Connections {
        target: Qt.application
        onStateChanged: {
            if (Qt.application.state === Qt.ApplicationActive) {
                harpoon.refresh()
            }
        }
    }

    Connections {
        target: harpoonDBus
        onActivateRequested: window.activate()
        onShowUpdatesRequested: pageStack.pop(pageStack.find(function(page) {
            return page.objectName === "appListPage"
        }), PageStackAction.Immediate)
        onShowAppRequested: window.showApp(id)
        onAddLinkRequested: window.openAddLink(link)
    }

    Banner { id: linkBanner }

    Connections {
        target: harpoon
        // A failed install or update gets its own page with a report, unless
        // one is already showing (Update all can fail several times) or
        // Harpoon is in the background: then the list and the app's page
        // point to it.
        onInstallFailed: {
            if (Qt.application.state !== Qt.ApplicationActive) {
                return
            }
            var top = pageStack.currentPage
            if (top && top.objectName === "installErrorPage") {
                return
            }
            if (pageStack.busy) {
                pageStack.completeAnimation()
            }
            pageStack.push(Qt.resolvedUrl("pages/InstallErrorPage.qml"), { appId: id })
        }
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
