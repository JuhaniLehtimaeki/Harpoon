import QtQuick 2.0
import Sailfish.Silica 1.0

// One layer of Waves: a seamless strip of the layer's image drifting left.
Item {
    id: root

    property string image
    property color color
    property bool running
    property int duration

    readonly property real _tileWidth: height * 5 // the images are 5:1 and tile seamlessly

    Row {
        height: parent.height

        Repeater {
            model: Math.ceil(root.width / Math.max(1, root._tileWidth)) + 1

            HighlightImage {
                width: root._tileWidth
                height: root.height
                sourceSize { width: root._tileWidth; height: root.height }
                source: Qt.resolvedUrl("../images/waves-" + root.image + ".png")
                color: root.color
            }
        }

        NumberAnimation on x {
            from: 0
            to: -root._tileWidth
            duration: root.duration
            loops: Animation.Infinite
            running: root.running && root._tileWidth > 0
        }
    }
}
