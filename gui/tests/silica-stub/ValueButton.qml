import QtQuick 2.0
MouseArea {
    property string label; property string value; property string description
    width: parent ? parent.width : 0; height: Math.max(80, d.y + d.height + 16)
    Text { id: t; x: 24; y: 20; width: parent.width - 48; elide: Text.ElideRight; color: "white"; font.pixelSize: 28; text: parent.label + ":  " + parent.value }
    Text { id: d; x: 24; y: t.y + t.height + 4; width: parent.width - 48; wrapMode: Text.Wrap; color: "#4fa3c7"; font.pixelSize: 20; text: parent.description; visible: text.length > 0; height: visible ? implicitHeight : 0 }
}
