import QtQuick 2.0
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0
import "../components"

Page {
    id: page

    readonly property var _intervals: [1, 3, 6, 12, 24, 48]

    allowedOrientations: Orientation.All

    Banner { id: banner }

    Component {
        id: backupPicker

        FilePickerPage {
            title: qsTr("Choose a Harpoon backup")
            nameFilters: ["*.json"]
            onSelectedContentPropertiesChanged: {
                if (!selectedContentProperties || !selectedContentProperties.filePath) {
                    return
                }
                var result = harpoon.importBackup(selectedContentProperties.filePath, false)
                if (result.ok) {
                    banner.show(result.skipped.length > 0
                                ? qsTr("Imported %n app(s); %1 already tracked", "", result.added).arg(result.skipped.length)
                                : qsTr("Imported %n app(s)", "", result.added))
                } else {
                    banner.show(result.error)
                }
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("About Harpoon")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
            MenuItem {
                text: qsTr("Import backup")
                onClicked: pageStack.push(backupPicker)
            }
            MenuItem {
                visible: harpoon.apps.count > 0
                text: qsTr("Export backup")
                onClicked: {
                    var result = harpoon.exportBackup(false)
                    banner.show(result.ok ? qsTr("Saved %1").arg(result.path) : result.error)
                }
            }
        }

        Column {
            id: column

            width: parent.width

            PageHeader { title: qsTr("Settings") }

            SectionHeader { text: qsTr("Updates") }

            TextSwitch {
                text: qsTr("Check in the background")
                description: qsTr("Checks for new releases even when Harpoon is closed")
                checked: harpoon.settings.backgroundChecks
                automaticCheck: false
                onClicked: harpoon.settings.backgroundChecks = !checked
            }

            ComboBox {
                width: parent.width
                enabled: harpoon.settings.backgroundChecks
                label: qsTr("Check every")
                currentIndex: Math.max(0, page._intervals.indexOf(harpoon.settings.checkIntervalHours))
                menu: ContextMenu {
                    Repeater {
                        model: page._intervals
                        MenuItem {
                            text: qsTr("%n hour(s)", "", modelData)
                            onClicked: harpoon.settings.checkIntervalHours = modelData
                        }
                    }
                }
            }

            TextSwitch {
                enabled: harpoon.settings.backgroundChecks
                text: qsTr("Notify about updates")
                checked: harpoon.settings.notifyUpdates
                automaticCheck: false
                onClicked: harpoon.settings.notifyUpdates = !checked
            }

            TextSwitch {
                enabled: harpoon.settings.backgroundChecks && harpoon.settings.installBackend === "packagekit"
                text: qsTr("Install updates automatically")
                description: qsTr("Updates apps that Harpoon installed during background checks. Apps can be excluded in their settings.")
                checked: harpoon.settings.autoUpdate
                automaticCheck: false
                onClicked: harpoon.settings.autoUpdate = !checked
            }

            SectionHeader { text: qsTr("Installing") }

            ComboBox {
                width: parent.width
                label: qsTr("Install with")
                description: currentIndex === 0
                             ? qsTr("Installs without asking. Harpoon must run with the privileged group, which the app launcher provides.")
                             : qsTr("The system asks you to confirm each installation. Allow untrusted software must be enabled in Settings.")
                currentIndex: harpoon.settings.installBackend === "handler" ? 1 : 0
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("PackageKit")
                        onClicked: harpoon.settings.installBackend = "packagekit"
                    }
                    MenuItem {
                        text: qsTr("System installer")
                        onClicked: harpoon.settings.installBackend = "handler"
                    }
                }
            }

            SectionHeader { text: qsTr("Access tokens") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Optional. A token raises GitHub's limit of 60 checks per hour and allows private repositories. Tokens are stored unencrypted in Harpoon's private settings file.")
            }

            Repeater {
                model: harpoon.sources

                PasswordField {
                    width: column.width
                    label: qsTr("%1 token").arg(modelData.name)
                    placeholderText: label
                    text: harpoon.settings.token(modelData.id)
                    EnterKey.iconSource: "image://theme/icon-m-enter-close"
                    EnterKey.onClicked: focus = false
                    onActiveFocusChanged: {
                        if (!activeFocus && text !== harpoon.settings.token(modelData.id)) {
                            harpoon.settings.setToken(modelData.id, text)
                        }
                    }
                }
            }

            SectionHeader { text: qsTr("Device") }

            DetailItem {
                label: qsTr("Architecture")
                value: harpoon.deviceArch
            }
            DetailItem {
                label: qsTr("SailfishOS")
                value: harpoon.osVersion.length > 0 ? harpoon.osVersion : qsTr("Unknown")
            }
        }

        VerticalScrollDecorator { }
    }
}
