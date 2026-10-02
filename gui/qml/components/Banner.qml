import QtQuick 2.0
import Nemo.Notifications 1.0

// A short-lived system banner for results and errors.
Notification {
    function show(text) {
        previewSummary = text
        publish()
    }

    appName: qsTr("Harpoon")
    isTransient: true
    urgency: Notification.Low
}
