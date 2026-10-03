import QtQuick 2.0
Item {
    property string text; property string description; property bool checked; property bool automaticCheck: true; signal clicked()
    width: parent ? parent.width : 0; height: Math.max(80, d.y + d.height + 16)
    Rectangle { x: 24; y: 22; width: 36; height: 36; radius: 18; color: parent.checked ? "#7fd6ff" : "transparent"; border.color: "#7fd6ff" }
    Text { id: t; x: 84; y: 20; width: parent.width - 108; wrapMode: Text.Wrap; color: "white"; font.pixelSize: 28; text: parent.text }
    Text { id: d; x: 84; y: t.y + t.height + 4; width: parent.width - 108; wrapMode: Text.Wrap; color: "#4fa3c7"; font.pixelSize: 20; text: parent.description; visible: text.length > 0; height: visible ? implicitHeight : 0 }
}
