import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    id: dialog

    // Source forced for self-hosted forges; empty means "by host".
    readonly property string _sourceId: sourceBox.currentIndex > 0
                                        ? harpoon.sources[sourceBox.currentIndex - 1].id : ""
    readonly property var _inspected: urlField.text.trim().length > 0
                                      ? harpoon.inspectUrl(urlField.text.trim(), _sourceId) : ({})

    allowedOrientations: Orientation.All
    canAccept: _inspected.ok === true

    onAccepted: {
        var settings = {}
        if (prereleaseSwitch.checked) {
            settings.includePrereleases = true
        }
        if (trackOnlySwitch.checked) {
            settings.trackOnly = true
        }
        if (filterField.text.length > 0) {
            settings.assetFilterRegEx = filterField.text
        }
        harpoon.addApp(urlField.text.trim(), _sourceId, settings)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column

            width: parent.width

            DialogHeader {
                acceptText: qsTr("Add")
                title: qsTr("Add app")
            }

            TextField {
                id: urlField

                width: parent.width
                focus: true
                label: qsTr("Repository URL")
                placeholderText: qsTr("Repository URL")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.enabled: dialog.canAccept
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: dialog.accept()
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: _inspected.ok ? Theme.highlightColor : Theme.secondaryHighlightColor
                text: urlField.text.trim().length === 0
                      ? qsTr("For example https://github.com/owner/repo or https://codeberg.org/owner/repo")
                      : _inspected.ok ? qsTr("%1: %2").arg(_inspected.sourceName).arg(_inspected.standardUrl)
                                      : _inspected.error
            }

            ComboBox {
                id: sourceBox

                width: parent.width
                label: qsTr("Source type")
                description: qsTr("Choose a type only for self-hosted servers, such as your own Forgejo or Gitea")
                menu: ContextMenu {
                    MenuItem { text: qsTr("Detect from URL") }
                    Repeater {
                        model: harpoon.sources
                        MenuItem { text: modelData.name }
                    }
                }
            }

            SectionHeader { text: qsTr("Options") }

            TextSwitch {
                id: prereleaseSwitch

                text: qsTr("Include prereleases")
                description: qsTr("Also offer releases the developer marked as prerelease")
            }

            TextSwitch {
                id: trackOnlySwitch

                text: qsTr("Track only")
                description: qsTr("Only notify about new releases; nothing is installed")
            }

            TextField {
                id: filterField

                width: parent.width
                label: qsTr("Package filter (regular expression)")
                placeholderText: qsTr("Package filter (regular expression)")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Harpoon picks the RPM for this device (%1) automatically. A filter is only needed when a release contains several packages.").arg(harpoon.deviceArch)
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }
        }

        VerticalScrollDecorator { }
    }
}
