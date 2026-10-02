import QtQuick 2.0
import Sailfish.Silica 1.0

// A boolean per-app setting. Stores nothing while it equals the default.
TextSwitch {
    property string appId
    property string key
    property var values
    property bool defaultValue

    checked: values && values[key] !== undefined ? values[key] === true || values[key] === "true" : defaultValue
    automaticCheck: false
    onClicked: harpoon.setAppSetting(appId, key, (!checked) === defaultValue ? null : !checked)
}
