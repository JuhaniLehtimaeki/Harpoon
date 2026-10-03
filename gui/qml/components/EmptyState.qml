import QtQuick 2.0
import Sailfish.Silica 1.0

// The empty list: a harpoon bobbing over the waves, and what to do next.
Column {
    id: root

    property alias text: title.text
    property alias hintText: hint.text
    property bool active: true

    width: parent ? parent.width : 0
    spacing: Theme.paddingLarge

    Item {
        width: parent.width
        height: Theme.iconSizeExtraLarge * 2

        HighlightImage {
            id: mark

            anchors.horizontalCenter: parent.horizontalCenter
            y: Theme.paddingLarge
            width: Theme.iconSizeExtraLarge * 1.3
            height: width
            sourceSize { width: width; height: height }
            source: Qt.resolvedUrl("../images/harpoon-mark.png")
            color: Theme.highlightColor

            SequentialAnimation on y {
                running: root.active && Qt.application.state === Qt.ApplicationActive
                loops: Animation.Infinite
                NumberAnimation { to: Theme.paddingLarge + Theme.paddingMedium; duration: 1800; easing.type: Easing.InOutSine }
                NumberAnimation { to: Theme.paddingLarge; duration: 1800; easing.type: Easing.InOutSine }
            }
        }

        Waves {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.itemSizeMedium
            color: Theme.highlightBackgroundColor
            opacity: Theme.opacityLow
            animated: root.active
        }
    }

    Label {
        id: title

        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        font.family: Theme.fontFamilyHeading
        font.pixelSize: Theme.fontSizeExtraLarge
        color: Theme.highlightColor
    }

    Label {
        id: hint

        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSizeMedium
        color: Theme.secondaryHighlightColor
    }
}
