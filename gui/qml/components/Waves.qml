import QtQuick 2.0
import Sailfish.Silica 1.0

// Layered waves, tinted with an ambience colour. When animated they drift
// slowly sideways, only while the app is in front.
Item {
    id: root

    property color color: Theme.highlightColor
    property bool animated
    property int duration: 30000

    readonly property real _tileWidth: height * 5 // waves.png is 5:1 and tiles seamlessly

    clip: true

    Row {
        id: tiles

        height: parent.height

        Repeater {
            model: Math.ceil(root.width / Math.max(1, root._tileWidth)) + 1

            HighlightImage {
                width: root._tileWidth
                height: root.height
                sourceSize { width: root._tileWidth; height: root.height }
                source: Qt.resolvedUrl("../images/waves.png")
                color: root.color
            }
        }

        NumberAnimation on x {
            from: 0
            to: -root._tileWidth
            duration: root.duration
            loops: Animation.Infinite
            running: root.animated && root.visible && root._tileWidth > 0
                     && Qt.application.state === Qt.ApplicationActive
        }
    }
}
