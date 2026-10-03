import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.harpoon 1.0
import "../components"

Page {
    id: page

    // The add dialog's last request, until harpoon reports how it went.
    property var pendingAdd: null

    objectName: "appListPage"
    allowedOrientations: Orientation.All

    Banner { id: banner }

    Connections {
        target: harpoon
        onOperationFinished: {
            if (message.length > 0) {
                banner.show(message)
            }
        }
        onAddFinished: {
            var request = page.pendingAdd
            page.pendingAdd = null
            if (ok) {
                return
            }
            if (!request) {
                banner.show(qsTr("Could not add app: %1").arg(idOrError))
                return
            }
            // Back to the form as it was, to fix the address or options.
            pageStack.push(Qt.resolvedUrl("AddAppDialog.qml"), {
                               initialUrl: request.url,
                               initialSourceId: request.sourceId,
                               initialPackage: request.packageName,
                               initialSettings: request.settings,
                               errorText: idOrError
                           })
        }
        onBackgroundError: banner.show(qsTr("Background checks: %1").arg(message))
    }

    SilicaListView {
        id: listView

        anchors.fill: parent
        model: harpoon.apps

        header: Column {
            width: listView.width

            PageHeader {
                title: qsTr("Harpoon")
            }

            StatusHero {
                width: parent.width
                visible: harpoon.apps.count > 0
                active: page.status === PageStatus.Active
            }
        }

        // Under a short list: a way to add more, a tip, and the sea filling
        // the rest of the screen.
        footer: Item {
            width: listView.width
            visible: listView.count > 0
            height: visible ? Math.max(footerColumn.height + Theme.itemSizeLarge,
                                       listView.height - (y - listView.originY))
                            : 0

            Waves {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.itemSizeMedium
                color: Theme.highlightBackgroundColor
                opacity: Theme.opacityLow
                animated: page.status === PageStatus.Active
            }

        Column {
            id: footerColumn

            width: parent.width

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            BackgroundItem {
                id: addItem

                width: parent.width
                height: Theme.itemSizeMedium
                onClicked: pageStack.push(Qt.resolvedUrl("AddAppDialog.qml"))

                Rectangle {
                    id: addCircle

                    anchors {
                        left: parent.left
                        leftMargin: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    width: Theme.iconSizeMedium
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: Math.max(1, Math.round(Theme.paddingSmall / 3))
                    border.color: addItem.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor

                    Icon {
                        anchors.centerIn: parent
                        source: "image://theme/icon-m-add"
                        color: addItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                }

                Column {
                    anchors {
                        left: addCircle.right
                        leftMargin: Theme.paddingLarge
                        right: parent.right
                        rightMargin: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }

                    Label {
                        width: parent.width
                        truncationMode: TruncationMode.Fade
                        color: addItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                        text: qsTr("Add another app")
                    }
                    Label {
                        width: parent.width
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: addItem.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                        text: qsTr("From a link or a QR code")
                    }
                }
            }

            TipCard {
                width: parent.width
            }
        }
        }

        PullDownMenu {
            busy: harpoon.checking

            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("Add app")
                onClicked: pageStack.push(Qt.resolvedUrl("AddAppDialog.qml"))
            }
            MenuItem {
                visible: harpoon.apps.updatesCount > 0
                text: qsTr("Update all")
                onClicked: harpoon.updateAll()
            }
            MenuItem {
                visible: harpoon.apps.count > 0
                enabled: !harpoon.checking
                text: qsTr("Check for updates")
                onClicked: harpoon.checkAll()
            }
        }

        section {
            property: "hasUpdate"
            delegate: SectionHeader {
                text: section === "true" ? qsTr("Updates") : qsTr("Apps")
            }
            criteria: ViewSection.FullString
        }

        delegate: ListItem {
            id: item

            contentHeight: Theme.itemSizeMedium
            menu: ContextMenu {
                MenuItem {
                    visible: !model.trackOnly && !model.busy
                             && (model.state === AppListModel.UpdateAvailable
                                 || model.state === AppListModel.NotInstalled)
                    text: model.state === AppListModel.UpdateAvailable ? qsTr("Update") : qsTr("Install")
                    onClicked: harpoon.install(model.appId)
                }
                MenuItem {
                    visible: model.trackOnly && model.state === AppListModel.UpdateAvailable
                    text: qsTr("Mark as seen")
                    onClicked: harpoon.acknowledge(model.appId)
                }
                MenuItem {
                    visible: !model.busy
                    text: qsTr("Check now")
                    onClicked: harpoon.check(model.appId)
                }
                MenuItem {
                    visible: !model.busy
                    text: qsTr("Stop tracking")
                    onClicked: {
                        var appId = model.appId
                        item.remorseAction(qsTr("Stopping tracking"), function() { harpoon.removeApp(appId) })
                    }
                }
            }

            onClicked: pageStack.push(Qt.resolvedUrl("AppPage.qml"), { appId: model.appId })

            // The installed app's own launcher icon, else its initial.
            AppIcon {
                id: icon

                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                width: Theme.iconSizeMedium
                height: Theme.iconSizeMedium
                source: model.icon
                name: model.name
                highlighted: item.highlighted
            }

            Column {
                anchors {
                    left: icon.right
                    right: statusIcon.visible ? statusIcon.left : parent.right
                    leftMargin: Theme.paddingLarge
                    rightMargin: statusIcon.visible ? Theme.paddingMedium : Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    text: model.name
                    truncationMode: TruncationMode.Fade
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                }

                StateLabel {
                    width: parent.width
                    visible: !model.busy && model.lastError.length === 0
                    appState: model.state
                    installedVersion: model.installedVersion
                    latestVersion: model.latestVersion
                    trackOnly: model.trackOnly
                    highlighted: item.highlighted
                }

                Label {
                    width: parent.width
                    visible: !model.busy && model.lastError.length > 0
                    text: model.lastError
                    font.pixelSize: Theme.fontSizeExtraSmall
                    truncationMode: TruncationMode.Fade
                    color: Theme.errorColor
                }

                Label {
                    width: parent.width
                    visible: model.busy
                    text: model.stage
                    font.pixelSize: Theme.fontSizeExtraSmall
                    truncationMode: TruncationMode.Fade
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
            }

            // Right edge: busy, an update waiting, or a failed check.
            Item {
                id: statusIcon

                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                width: Theme.iconSizeSmall
                height: Theme.iconSizeSmall
                visible: model.busy || model.hasUpdate === true || model.lastError.length > 0

                BusyIndicator {
                    anchors.centerIn: parent
                    size: BusyIndicatorSize.Small
                    running: model.busy
                    visible: running
                }
                Icon {
                    anchors.centerIn: parent
                    visible: !model.busy
                    source: model.lastError.length > 0 ? "image://theme/icon-s-filled-warning"
                                                        : "image://theme/icon-s-update"
                    color: model.lastError.length > 0 ? Theme.errorColor : Theme.highlightColor
                }
            }
        }

        EmptyState {
            y: Math.max(listView.headerItem ? listView.headerItem.height : 0,
                        (listView.height - height) / 2 - Theme.itemSizeLarge)
            visible: listView.count === 0 && harpoon.loaded
            active: visible && page.status === PageStatus.Active
            text: qsTr("Nothing on the line yet")
            hintText: qsTr("Pull down to add an app by its link or QR code, from GitHub, Codeberg or another forge")
        }

        VerticalScrollDecorator { }
    }
}
