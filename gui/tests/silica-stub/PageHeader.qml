import QtQuick 2.0
Item {
    property string title; property string description
    width: parent ? parent.width : 0; height: description.length > 0 ? 130 : 110
    Text { anchors { right: parent.right; rightMargin: 24; top: parent.top; topMargin: 32 } color: "#7fd6ff"; font.pixelSize: 34; text: parent.title }
    Text { anchors { right: parent.right; rightMargin: 24; top: parent.top; topMargin: 76 } color: "#4fa3c7"; font.pixelSize: 20; text: parent.description }
}
