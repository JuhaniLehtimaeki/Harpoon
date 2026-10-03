import QtQuick 2.0
TextInput {
    property string label; property string placeholderText
    height: 96; color: "white"; font.pixelSize: 28;
    Rectangle { x: 24; width: parent.width - 48; y: 60; height: 2; color: "#7fd6ff" }
    Text { x: 24; y: 68; color: "#4fa3c7"; font.pixelSize: 20; text: parent.text.length > 0 ? parent.label : parent.placeholderText }
}
