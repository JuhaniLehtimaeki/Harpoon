import QtQuick 2.0
TextSwitch {
    readonly property alias icon: _icon
    Image { id: _icon; visible: false }
    Text { x: 24; y: 20; color: "#7fd6ff"; font.pixelSize: 28; text: "◆"; visible: false }
}
