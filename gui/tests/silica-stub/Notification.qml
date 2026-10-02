import QtQuick 2.0
QtObject {
    enum Urgency { Low, Normal, Critical }
    property string appName; property bool isTransient; property int urgency
    property string previewSummary; property string previewBody; property string summary; property string body
    property int publishCount
    function publish() { publishCount++ }
}
