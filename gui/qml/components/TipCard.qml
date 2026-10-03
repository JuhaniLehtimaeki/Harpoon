import QtQuick 2.0
import Sailfish.Silica 1.0

// A small, rotating tip under the list. Tap for the next one.
BackgroundItem {
    id: root

    readonly property var _tips: {
        var tips = [
            qsTr("Long-press an app for quick actions such as Check now or Stop tracking."),
            qsTr("Share an app with a friend as a QR code: pull down on its page."),
            qsTr("Developers can add a QR code to their README, so people add their app in one scan."),
            qsTr("In an app's settings, Harpoon can check that its packages were built by the app's own repository.")
        ]
        // Not tracking itself yet: say it can.
        if (harpoon.appDetails("harpoon").appId === undefined) {
            tips.push(qsTr("Harpoon keeps itself up to date too: add github.com/JuhaniLehtimaeki/Harpoon."))
        }
        if (harpoon.settings.backgroundChecks) {
            tips.push(qsTr("Harpoon looks for new releases every %n hour(s), even when it is closed.", "",
                           harpoon.settings.checkIntervalHours))
        } else {
            tips.push(qsTr("Turn on background checks in Settings to hear about new releases without opening Harpoon."))
        }
        return tips
    }
    property int _index: Math.floor(Math.random() * 7)

    height: content.height + 2 * Theme.paddingLarge
    onClicked: {
        fade.start()
    }

    SequentialAnimation {
        id: fade

        NumberAnimation { target: content; property: "opacity"; to: 0; duration: 120 }
        ScriptAction { script: root._index = (root._index + 1) % root._tips.length }
        NumberAnimation { target: content; property: "opacity"; to: 1; duration: 180 }
    }

    Rectangle {
        x: Theme.horizontalPageMargin
        y: Theme.paddingMedium
        width: parent.width - 2 * Theme.horizontalPageMargin
        height: parent.height - 2 * Theme.paddingMedium
        radius: Theme.paddingMedium
        color: Theme.rgba(Theme.highlightBackgroundColor, Theme.opacityFaint)
    }

    Item {
        id: content

        x: Theme.horizontalPageMargin + Theme.paddingLarge
        y: Theme.paddingLarge
        width: parent.width - 2 * x
        height: Math.max(tipIcon.height, tipText.height)

        Icon {
            id: tipIcon

            source: "image://theme/icon-m-about"
            color: Theme.highlightColor
        }

        Label {
            id: tipText

            anchors {
                left: tipIcon.right
                leftMargin: Theme.paddingMedium
                right: parent.right
                verticalCenter: tipIcon.verticalCenter
            }
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: root.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
            text: root._tips[root._index % root._tips.length]
        }
    }
}
