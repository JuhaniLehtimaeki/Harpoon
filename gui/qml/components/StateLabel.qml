import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.harpoon 1.0

// One-line summary of an app's update state.
Label {
    property int appState
    property string installedVersion
    property string latestVersion
    property bool trackOnly
    property bool highlighted

    font.pixelSize: Theme.fontSizeExtraSmall
    truncationMode: TruncationMode.Fade
    color: appState === AppListModel.UpdateAvailable
           ? Theme.highlightColor
           : (highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor)
    text: {
        switch (appState) {
        case AppListModel.UpdateAvailable:
            return trackOnly ? qsTr("New release %1").arg(latestVersion)
                             : qsTr("Update %1 → %2").arg(installedVersion).arg(latestVersion)
        case AppListModel.UpToDate:
            return trackOnly ? qsTr("Seen %1").arg(latestVersion) : qsTr("Up to date, %1").arg(installedVersion)
        case AppListModel.NotInstalled:
            return qsTr("Not installed, latest %1").arg(latestVersion)
        case AppListModel.Unknown:
            return qsTr("Installed %1, latest %2").arg(installedVersion).arg(latestVersion)
        default:
            return qsTr("Not checked yet")
        }
    }
}
