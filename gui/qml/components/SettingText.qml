import QtQuick 2.0
import Sailfish.Silica 1.0

// A text per-app setting, saved when editing finishes.
TextField {
    property string appId
    property string key
    property var values

    function _save() {
        var stored = values && values[key] !== undefined ? String(values[key]) : ""
        if (text !== stored) {
            harpoon.setAppSetting(appId, key, text)
        }
    }

    // The stored value, unless the user is typing: re-syncing then would
    // throw away the edit.
    function _sync() {
        if (!activeFocus) {
            text = values && values[key] !== undefined ? String(values[key]) : ""
        }
    }

    width: parent.width
    placeholderText: label
    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
    onValuesChanged: _sync()
    Component.onCompleted: _sync()
    onActiveFocusChanged: if (!activeFocus) _save()
    EnterKey.iconSource: "image://theme/icon-m-enter-close"
    EnterKey.onClicked: focus = false
}
