import QtQuick 2.0
import Sailfish.Silica 1.0

// An app's launcher icon; until it is installed, a round tile with the first
// letter of its name.
Item {
    id: root

    property string source
    property string name
    property bool highlighted

    readonly property bool _hasImage: source.length > 0 && image.status === Image.Ready

    Image {
        id: image

        anchors.fill: parent
        sourceSize { width: width; height: height }
        source: root.source
        asynchronous: true
        visible: root._hasImage
    }

    Rectangle {
        anchors.fill: parent
        visible: !root._hasImage
        radius: width / 2
        color: Theme.rgba(Theme.highlightBackgroundColor, Theme.highlightBackgroundOpacity)

        Label {
            anchors.centerIn: parent
            text: root.name.length > 0 ? root.name.charAt(0).toUpperCase() : "?"
            font.family: Theme.fontFamilyHeading
            font.pixelSize: Math.round(parent.height * 0.45)
            color: root.highlighted ? Theme.highlightColor : Theme.primaryColor
        }
    }
}
