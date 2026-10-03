import QtQuick 2.0
Item {
    property string acceptText; property string title; property Item dialog
    width: parent ? parent.width : 0; height: 110
    Text { anchors { right: parent.right; rightMargin: 24; top: parent.top; topMargin: 32 } color: "#7fd6ff"; font.pixelSize: 34; text: parent.acceptText }
}
