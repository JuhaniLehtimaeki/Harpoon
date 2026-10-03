import QtQuick 2.0
Item {
    property string label; property string value
    width: parent ? parent.width : 0; height: Math.max(l.height, v.height) + 12
    Text { id: l; anchors.right: parent.horizontalCenter; anchors.rightMargin: 6; width: parent.width / 2 - 30; horizontalAlignment: Text.AlignRight; wrapMode: Text.Wrap; color: "#4fa3c7"; font.pixelSize: 24; text: parent.label }
    Text { id: v; anchors.left: parent.horizontalCenter; anchors.leftMargin: 6; width: parent.width / 2 - 30; wrapMode: Text.Wrap; color: "#7fd6ff"; font.pixelSize: 24; text: parent.value }
}
