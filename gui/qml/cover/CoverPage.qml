import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    readonly property int _updates: harpoon.apps.updatesCount

    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingSmall
        visible: harpoon.apps.count > 0

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: _updates > 0 ? _updates : "✓"
            font.pixelSize: Theme.fontSizeHuge
            color: Theme.primaryColor
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeSmall
            text: harpoon.checking ? qsTr("Checking…")
                                   : _updates > 0 ? qsTr("%n update(s)", "", _updates)
                                                  : qsTr("Up to date")
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
