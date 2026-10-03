import QtQuick 2.0
Item {
    property string text; property real preferredWidth: 200
    readonly property alias icon: _icon
    signal clicked()
    width: preferredWidth; height: 72
    Rectangle { anchors.fill: parent; radius: 36; color: "#33ffffff" }
    Image { id: _icon; visible: false }
    Text { anchors.centerIn: parent; color: "white"; font.pixelSize: 26; text: (parent.icon.source.toString().length ? "◆ " : "") + parent.text }
}
