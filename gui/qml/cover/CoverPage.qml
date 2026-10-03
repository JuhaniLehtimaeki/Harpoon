import QtQuick 2.0
import Sailfish.Silica 1.0

// Summary for the Home screen: how many updates there are and for which apps.
CoverBackground {
    readonly property int _updates: harpoon.apps.updatesCount

    // A faint harpoon in the corner, as Sailfish covers usually carry.
    Image {
        visible: harpoon.apps.count > 0
        anchors {
            right: parent.right
            bottom: parent.bottom
            rightMargin: -Theme.paddingLarge
            bottomMargin: Theme.itemSizeLarge
        }
        width: parent.width * 0.6
        height: width
        sourceSize { width: width; height: height }
        source: "/usr/share/icons/hicolor/172x172/apps/harpoon.png"
        opacity: Theme.opacityFaint
    }

    Column {
        anchors {
            top: parent.top
            topMargin: Theme.paddingLarge
            left: parent.left
            leftMargin: Theme.paddingLarge
            right: parent.right
            rightMargin: Theme.paddingLarge
        }
        visible: harpoon.apps.count > 0

        Label {
            text: _updates > 0 ? _updates : harpoon.apps.count
            font.pixelSize: Theme.fontSizeHuge
            color: Theme.primaryColor
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryColor
            text: harpoon.checking ? qsTr("Checking…")
                                   : _updates > 0 ? qsTr("%n update(s)", "", _updates)
                                   : harpoon.apps.failedCount > 0 ? qsTr("%n app(s)", "", harpoon.apps.count)
                                                  : qsTr("%n app(s), all up to date", "", harpoon.apps.count)
        }

        Label {
            width: parent.width
            visible: !harpoon.checking && harpoon.apps.failedCount > 0
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            text: qsTr("%n check(s) failed", "", harpoon.apps.failedCount)
        }

        Item {
            width: 1
            height: Theme.paddingMedium
        }

        // Updates are sorted first in the model.
        Repeater {
            model: harpoon.apps

            Label {
                width: parent.width
                visible: model.hasUpdate === true && index < 4
                text: model.name
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.primaryColor
            }
        }
    }

    CoverPlaceholder {
        enabled: harpoon.apps.count === 0
        icon.source: "/usr/share/icons/hicolor/86x86/apps/harpoon.png"
        text: qsTr("No apps tracked")
    }

    CoverActionList {
        enabled: harpoon.apps.count > 0

        CoverAction {
            iconSource: "image://theme/icon-cover-refresh"
            onTriggered: harpoon.checkAll()
        }
    }
}
