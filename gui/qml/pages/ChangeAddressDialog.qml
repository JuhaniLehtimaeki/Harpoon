import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Points a tracked app at another address, for example after its repository
// moved. Settings, the name and the installed package stay as they are.
Dialog {
    id: dialog

    property string appId
    property string initialUrl
    property string initialSourceId
    // After a failed change: why the new address did not work.
    property string errorText

    readonly property var _details: harpoon.appDetails(appId)
    readonly property string _sourceId: sourceBox.currentIndex > 0
                                        ? harpoon.sources[sourceBox.currentIndex - 1].id : ""
    readonly property var _inspected: urlField.text.trim().length > 0
                                      ? harpoon.inspectUrl(urlField.text.trim(), _sourceId) : ({})
    // inspectUrl() reports the app's own address as already tracked.
    readonly property bool _usable: _inspected.ok === true || _inspected.existingId === appId
    readonly property bool _unchanged: _inspected.standardUrl === _details.url
                                       && _sourceId === (_details.sourceId || "")

    allowedOrientations: Orientation.All
    canAccept: _usable && !_unchanged

    Component.onCompleted: {
        urlField.text = initialUrl
        var index = 0
        for (var i = 0; i < harpoon.sources.length; ++i) {
            if (harpoon.sources[i].id === initialSourceId) {
                index = i + 1
            }
        }
        sourceBox.currentIndex = index
        urlField.cursorPosition = urlField.text.length
    }

    onAccepted: {
        var settingsPage = pageStack.find(function(p) {
            return p.objectName === "appSettingsPage" && p.appId === dialog.appId
        })
        if (settingsPage) {
            settingsPage._pendingAddress = { url: urlField.text.trim(), sourceId: _sourceId }
        }
        harpoon.setAppAddress(appId, urlField.text.trim(), _sourceId)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column

            width: parent.width

            DialogHeader {
                dialog: dialog
                acceptText: qsTr("Change")
                title: qsTr("Change address")
            }

            // Why the last try failed; the form below is as it was.
            Item {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: errorLabel.height + Theme.paddingLarge
                visible: dialog.errorText.length > 0

                Icon {
                    id: errorIcon

                    source: "image://theme/icon-s-filled-warning"
                    color: Theme.errorColor
                }

                LinkedLabel {
                    id: errorLabel

                    anchors {
                        left: errorIcon.right
                        leftMargin: Theme.paddingSmall
                        right: parent.right
                    }
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.errorColor
                    plainText: qsTr("The address was not changed: %1").arg(dialog.errorText)
                }
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

            // What the address was recognised as, or why not.
            Item {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: Math.max(detectIcon.visible ? detectIcon.height : 0, detectLabel.height)

                Icon {
                    id: detectIcon

                    visible: urlField.text.trim().length > 0
                    source: _usable ? "image://theme/icon-s-accept" : "image://theme/icon-s-warning"
                    color: _usable ? Theme.highlightColor : Theme.secondaryHighlightColor
                }

                Label {
                    id: detectLabel

                    anchors {
                        left: detectIcon.visible ? detectIcon.right : parent.left
                        leftMargin: detectIcon.visible ? Theme.paddingSmall : 0
                        right: parent.right
                    }
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: _usable ? Theme.highlightColor : Theme.secondaryHighlightColor
                    text: urlField.text.trim().length === 0
                          ? qsTr("The address of the app's repository or releases page")
                          : _unchanged ? qsTr("This is the app's current address")
                          : _usable ? qsTr("%1: %2").arg(_inspected.sourceName).arg(_inspected.standardUrl)
                                    : _inspected.error
                }
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            HostShortcuts {
                field: urlField
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            ComboBox {
                id: sourceBox

                width: parent.width
                label: qsTr("Source type")
                description: qsTr("Choose a type for self-hosted servers (your own Forgejo or Gitea), Jenkins jobs and RPM repositories")
                menu: ContextMenu {
                    MenuItem { text: qsTr("Detect from URL") }
                    Repeater {
                        model: harpoon.sources
                        MenuItem { text: modelData.name }
                    }
                }
            }

            HintLabel {
                text: qsTr("Harpoon checks the new address before switching to it. The app's settings and installed version are kept.")
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }
        }

        VerticalScrollDecorator { }
    }
}
