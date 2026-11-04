import QtQuick 2.0
import Nemo.Notifications 1.0

// A short-lived system banner for results and errors.
Notification {
    // A banner shows one line well: the first line is the summary, the rest
    // (warnings, "also tracked as …") the body, so it is not cut off.
    function show(text) {
        var newline = text.indexOf("\n")
        previewSummary = newline < 0 ? text : text.substring(0, newline)
        previewBody = newline < 0 ? "" : text.substring(newline + 1)
        publish()
    }

    appName: qsTr("Harpoon")
    isTransient: true
    urgency: Notification.Low
}
