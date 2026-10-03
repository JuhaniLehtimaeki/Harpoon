import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Dialog {
    id: dialog

    // Prefilled from a scanned QR code or a harpoon://add link.
    property string initialUrl
    property string initialSourceId
    property string initialPackage
    // After a failed add: what went wrong, and the options chosen then
    // ({includePrereleases, trackOnly, assetFilterRegEx}), so only the
    // mistake needs fixing.
    property string errorText
    property var initialSettings: ({})

    // Source forced for self-hosted forges; empty means "by host".
    readonly property string _sourceId: sourceBox.currentIndex > 0
                                        ? harpoon.sources[sourceBox.currentIndex - 1].id : ""
    readonly property var _inspected: urlField.text.trim().length > 0
                                      ? harpoon.inspectUrl(urlField.text.trim(), _sourceId) : ({})

    readonly property bool _needsPackageName: _inspected.sourceId === "RpmMdRepo"
                                              && urlField.text.indexOf("package=") < 0

    // Fills the form from harpoon.parseAddLink() output.
    function applyLink(link) {
        urlField.text = link.url
        var index = 0
        for (var i = 0; i < harpoon.sources.length; ++i) {
            if (harpoon.sources[i].id === link.sourceId) {
                index = i + 1
            }
        }
        sourceBox.currentIndex = index
        packageField.text = link.packageName || ""
    }

    function _scan() {
        var scanPage = pageStack.push(Qt.resolvedUrl("ScanPage.qml"))
        if (!scanPage) {
            return // the page failed to load; Silica shows why
        }
        scanPage.linkFound.connect(function(link) {
            dialog.applyLink(link)
            pageStack.pop(dialog)
        })
    }

    Component.onCompleted: {
        if (initialUrl.length > 0) {
            applyLink({ url: initialUrl, sourceId: initialSourceId, packageName: initialPackage })
        }
        prereleaseSwitch.checked = initialSettings.includePrereleases === true
        trackOnlySwitch.checked = initialSettings.trackOnly === true
        filterField.text = initialSettings.assetFilterRegEx || ""
        if (errorText.length > 0) {
            urlField.cursorPosition = urlField.text.length
        }
    }

    allowedOrientations: Orientation.All
    canAccept: _inspected.ok === true && (!_needsPackageName || packageField.text.trim().length > 0)

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
        if (_needsPackageName) {
            settings.packageName = packageField.text.trim()
        }
        // Kept by the list, which brings this dialog back filled in if the
        // first check fails.
        var list = pageStack.find(function(p) { return p.objectName === "appListPage" })
        if (list) {
            list.pendingAdd = {
                url: urlField.text.trim(),
                sourceId: _sourceId,
                packageName: packageField.text.trim(),
                settings: settings
            }
        }
        harpoon.addApp(urlField.text.trim(), _sourceId, settings, forceSwitch.visible && forceSwitch.checked)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column

            width: parent.width

            DialogHeader {
                dialog: dialog
                acceptText: qsTr("Add")
                title: qsTr("Add app")
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

                // Explanations can name the app's website: keep it tappable.
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
                    plainText: qsTr("Could not add the app: %1").arg(dialog.errorText)
                }
            }

            TextField {
                id: urlField

                width: parent.width
                // No keyboard over a form already filled from a QR code or
                // link, but straight back to typing after a failed add.
                focus: dialog.initialUrl.length === 0 || dialog.errorText.length > 0
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
                    source: _inspected.ok ? "image://theme/icon-s-accept" : "image://theme/icon-s-warning"
                    color: _inspected.ok ? Theme.highlightColor : Theme.secondaryHighlightColor
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
                    color: _inspected.ok ? Theme.highlightColor : Theme.secondaryHighlightColor
                    text: urlField.text.trim().length === 0
                          ? qsTr("The address of the app's repository or releases page, for example on GitHub or Codeberg")
                          : _inspected.ok ? qsTr("%1: %2").arg(_inspected.sourceName).arg(_inspected.standardUrl)
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

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                preferredWidth: Theme.buttonWidthMedium
                icon.source: "image://theme/icon-m-qr"
                text: qsTr("Scan QR code")
                onClicked: dialog._scan()
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

            TextField {
                id: packageField

                width: parent.width
                visible: dialog._needsPackageName
                label: qsTr("Package name in the repository")
                placeholderText: label
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
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

            TextSwitch {
                id: forceSwitch

                visible: dialog.errorText.length > 0
                text: qsTr("Add it anyway")
                description: qsTr("Track the app even though the check failed, for example when it has no release yet")
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

            HintLabel {
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
