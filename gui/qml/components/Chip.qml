import QtQuick 2.0
import Sailfish.Silica 1.0

// A small rounded, tappable label.
BackgroundItem {
    id: chip

    property alias text: chipLabel.text
    property string iconSource
    property bool selected

    width: chipRow.width + 2 * Theme.paddingLarge
    // Touched by its whole height, drawn smaller: a chip-sized target would be
    // under the size a finger hits reliably.
    height: Theme.itemSizeSmall
    highlightedColor: "transparent"

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: Theme.itemSizeExtraSmall
        radius: height / 2
        color: chip.selected || chip.highlighted
               ? Theme.rgba(Theme.highlightBackgroundColor, Theme.highlightBackgroundOpacity)
               : "transparent"
        border.width: Math.max(1, Math.round(Theme.paddingSmall / 3))
        border.color: chip.selected || chip.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
    }

    Row {
        id: chipRow

        anchors.centerIn: parent
        spacing: Theme.paddingSmall

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.iconSource.length > 0
            source: chip.iconSource
            color: chipLabel.color
        }

        Label {
            id: chipLabel

            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: Theme.fontSizeSmall
            color: chip.selected || chip.highlighted ? Theme.highlightColor : Theme.primaryColor
        }
    }
}
