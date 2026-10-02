import QtQuick 2.0
Item {
    property real contentHeight: 80
    property var menu
    property bool highlighted
    signal clicked()
    function remorseAction(text, action) { action() }
    function openMenu() { }
    width: ListView.view ? ListView.view.width : 540
    height: contentHeight
}
