import QtQuick 2.0
// QtMultimedia VideoOutput stand-in: shows testImage instead of camera frames,
// so grabToImage() returns something to decode.
Item {
    enum FillMode { Stretch, PreserveAspectFit, PreserveAspectCrop }
    property var source
    property int fillMode
    property bool autoOrientation
    property int orientation
    property url testImage
    Rectangle { anchors.fill: parent; color: "white" }
    Image { anchors.fill: parent; source: parent.testImage; fillMode: Image.PreserveAspectFit }
}
