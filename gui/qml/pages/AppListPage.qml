import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.harpoon 1.0
import "../components"

Page {
    id: page

    objectName: "appListPage"
    allowedOrientations: Orientation.All

    // Refresh apps that have not been checked within the last hour.
    Component.onCompleted: harpoon.checkStale(60)

    Banner { id: banner }

    Connections {
        target: harpoon
        onOperationFinished: {
            if (message.length > 0) {
                banner.show(message)
            }
        }
        onAddFinished: {
            if (!ok) {
                banner.show(qsTr("Could not add app: %1").arg(idOrError))
            }
        }
        onBackgroundError: banner.show(qsTr("Background checks: %1").arg(message))
    }

    SilicaListView {
        id: listView

        anchors.fill: parent
        model: harpoon.apps

        header: PageHeader {
            title: qsTr("Harpoon")
            description: harpoon.checking ? qsTr("Checking for updates…")
                                          : harpoon.apps.updatesCount > 0
                                            ? qsTr("%n update(s) available", "", harpoon.apps.updatesCount)
                                            : ""
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

            Column {
                anchors {
                    left: parent.left
                    right: busyIndicator.visible ? busyIndicator.left : parent.right
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: busyIndicator.visible ? Theme.paddingMedium : Theme.horizontalPageMargin
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

            BusyIndicator {
                id: busyIndicator

                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                size: BusyIndicatorSize.Small
                running: model.busy
                visible: running
            }
        }

        ViewPlaceholder {
            enabled: listView.count === 0
            text: qsTr("No apps yet")
            hintText: qsTr("Pull down to add an app from GitHub, Codeberg or another forge")
        }

        VerticalScrollDecorator { }
    }
}
