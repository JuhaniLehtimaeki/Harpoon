import QtQuick 2.0
import Sailfish.Silica 1.0

// A per-app setting chosen from a list of {value, text} options. The first
// option is the default and is stored as "no value".
ComboBox {
    id: box

    property string appId
    property string key
    property var values
    property var options: []

    function _indexOf(value) {
        for (var i = 0; i < options.length; ++i) {
            if (options[i].value === value) { return i }
        }
        return 0
    }

    function _sync() {
        currentIndex = _indexOf(values && values[key] !== undefined ? values[key] : options.length > 0 ? options[0].value : "")
    }

    width: parent.width
    // Picking an item assigns currentIndex inside ComboBox, which would break
    // a binding; re-sync explicitly instead.
    onValuesChanged: _sync()
    onOptionsChanged: _sync()
    Component.onCompleted: _sync()
    menu: ContextMenu {
        Repeater {
            model: box.options
            MenuItem {
                text: modelData.text
                onClicked: harpoon.setAppSetting(box.appId, box.key, index === 0 ? null : modelData.value)
            }
        }
    }
}
