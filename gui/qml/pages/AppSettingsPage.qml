import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Page {
    id: page

    property string appId
    property var details: harpoon.appDetails(appId)
    readonly property var _values: details.settings || ({})
    property bool _changed

    allowedOrientations: Orientation.All

    // Settings change which release and package are chosen: check again
    // when leaving the page.
    onStatusChanged: {
        if (status === PageStatus.Deactivating && _changed) {
            harpoon.check(appId)
        }
    }

    Connections {
        target: harpoon.apps
        onDataChanged: {
            page.details = harpoon.appDetails(page.appId)
            page._changed = true
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column

            width: parent.width

            PageHeader {
                title: qsTr("App settings")
                description: details.name || ""
            }

            TextField {
                width: parent.width
                label: qsTr("Name")
                placeholderText: qsTr("Name")
                text: details.name || ""
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
                onActiveFocusChanged: if (!activeFocus && text !== details.name) harpoon.setAppName(appId, text)
            }

            SectionHeader { text: qsTr("Releases") }

            SettingSwitch {
                appId: page.appId; key: "includePrereleases"; values: page._values
                text: qsTr("Include prereleases")
            }
            SettingSwitch {
                appId: page.appId; key: "fallbackToOlderReleases"; values: page._values; defaultValue: true
                text: qsTr("Fall back to older releases")
                description: qsTr("Use the newest release that has a package for this device")
            }
            SettingSwitch {
                appId: page.appId; key: "trackOnly"; values: page._values
                text: qsTr("Track only")
                description: qsTr("Only notify about new releases; nothing is installed")
            }
            SettingChoice {
                appId: page.appId; key: "sortMethodChoice"; values: page._values
                label: qsTr("Order releases by")
                options: [
                    { value: "date", text: qsTr("Publish date") },
                    { value: "smartname-datefallback", text: qsTr("Version, else date") },
                    { value: "smartname", text: qsTr("Version, else name") },
                    { value: "name", text: qsTr("Name") },
                    { value: "none", text: qsTr("As listed by the forge") }
                ]
            }
            SettingSwitch {
                appId: page.appId; key: "useLatestAssetDateAsReleaseDate"; values: page._values
                text: qsTr("Date releases by their newest package")
            }
            SettingText {
                appId: page.appId; key: "filterReleaseTitlesByRegEx"; values: page._values
                label: qsTr("Release title filter (regular expression)")
            }
            SettingText {
                appId: page.appId; key: "filterReleaseNotesByRegEx"; values: page._values
                label: qsTr("Release notes filter (regular expression)")
            }

            SectionHeader { text: qsTr("Packages") }

            SettingText {
                appId: page.appId; key: "assetFilterRegEx"; values: page._values
                label: qsTr("Package filter (regular expression)")
            }
            SettingSwitch {
                appId: page.appId; key: "invertAssetFilter"; values: page._values
                text: qsTr("Invert package filter")
            }
            SettingSwitch {
                appId: page.appId; key: "autoAssetFilterByArch"; values: page._values; defaultValue: true
                text: qsTr("Pick packages for this device")
                description: qsTr("Keep only %1 packages, or noarch if there are none").arg(harpoon.deviceArch)
            }
            SettingSwitch {
                appId: page.appId; key: "preferSfosVersionTag"; values: page._values; defaultValue: true
                text: qsTr("Prefer packages for this SailfishOS version")
                description: qsTr("Uses tags such as sfos5.0 in package names")
            }
            SettingSwitch {
                appId: page.appId; key: "allowIdChange"; values: page._values
                text: qsTr("Allow a different package name")
                description: qsTr("Accept a release whose RPM is not named %1").arg(page.appId)
            }

            SectionHeader { text: qsTr("Version") }

            SettingChoice {
                appId: page.appId; key: "versionSource"; values: page._values
                label: qsTr("Version comes from")
                options: [
                    { value: "tag", text: qsTr("Release tag") },
                    { value: "title", text: qsTr("Release title") },
                    { value: "assetName", text: qsTr("Package file name") },
                    { value: "date", text: qsTr("Release date") }
                ]
            }
            SettingText {
                appId: page.appId; key: "versionExtractionRegEx"; values: page._values
                label: qsTr("Version extraction (regular expression)")
            }
            SettingText {
                appId: page.appId; key: "matchGroupToUse"; values: page._values
                label: qsTr("Match group, for example $1")
            }
        }

        VerticalScrollDecorator { }
    }
}
