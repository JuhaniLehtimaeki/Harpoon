import QtQuick 2.0
import Sailfish.Silica 1.0

// The sea: three layers of waves. When animated, each layer drifts at its own
// slow pace, the farthest slowest, so the sea moves without drawing the eye.
Item {
    id: root

    property color color: Theme.highlightColor
    property bool animated

    readonly property bool _running: animated && visible
                                     && Qt.application.state === Qt.ApplicationActive

    clip: true

    WaveLayer {
        anchors.fill: parent
        image: "back"
        color: root.color
        running: root._running
        duration: 140000
    }

    WaveLayer {
        anchors.fill: parent
        image: "middle"
        color: root.color
        running: root._running
        duration: 95000
    }

    WaveLayer {
        anchors.fill: parent
        image: "front"
        color: root.color
        running: root._running
        duration: 65000
    }
}
