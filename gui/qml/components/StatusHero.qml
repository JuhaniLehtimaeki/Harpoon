import QtQuick 2.0
import Sailfish.Silica 1.0

// Top of the app list: a harpoon over the sea and, in one friendly line,
// where things stand. With updates waiting, tapping it installs them all.
BackgroundItem {
    id: root

    property bool active: true

    readonly property int _apps: harpoon.apps.count
    readonly property int _updates: harpoon.apps.updatesCount
    readonly property int _failed: harpoon.apps.failedCount
    readonly property bool _checking: harpoon.checking

    function _lastChecked() {
        var when = harpoon.apps.lastChecked
        return when && !isNaN(when.getTime()) ? Format.formatDate(when, Formatter.DurationElapsed) : ""
    }

    height: Math.max(Theme.iconSizeExtraLarge, textColumn.height) + Theme.itemSizeSmall + Theme.paddingLarge
    enabled: _updates > 0 && !_checking
    onClicked: harpoon.updateAll()

    Waves {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.itemSizeSmall
        color: Theme.highlightBackgroundColor
        opacity: Theme.opacityLow
        animated: root.active
    }

    // Bobs while Harpoon is out fishing for updates.
    HighlightImage {
        id: mark

        x: Theme.horizontalPageMargin
        y: Theme.paddingLarge
        width: Theme.iconSizeExtraLarge
        height: width
        sourceSize { width: width; height: height }
        source: Qt.resolvedUrl("../images/harpoon-mark.png")
        color: Theme.highlightColor
        highlighted: root.highlighted

        SequentialAnimation on rotation {
            running: root._checking && root.active
            loops: Animation.Infinite
            NumberAnimation { to: -8; duration: 600; easing.type: Easing.InOutSine }
            NumberAnimation { to: 6; duration: 600; easing.type: Easing.InOutSine }
            onRunningChanged: if (!running) mark.rotation = 0
        }
    }

    Column {
        id: textColumn

        anchors {
            left: mark.right
            leftMargin: Theme.paddingLarge
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            verticalCenter: mark.verticalCenter
        }
        spacing: Theme.paddingSmall

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            font.family: Theme.fontFamilyHeading
            font.pixelSize: Theme.fontSizeLarge
            color: Theme.highlightColor
            text: root._checking ? qsTr("Checking for updates…")
                : root._updates > 0 ? qsTr("%n update(s) ready", "", root._updates)
                : root._failed > 0 ? qsTr("%n check(s) need attention", "", root._failed)
                : qsTr("All caught up")
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryHighlightColor
            text: {
                if (root._updates > 0 && !root._checking)
                    return qsTr("Tap to install them all")
                var parts = [qsTr("%n app(s)", "", root._apps)]
                if (harpoon.apps.installedCount > 0)
                    parts.push(qsTr("%n installed", "", harpoon.apps.installedCount))
                var checked = root._lastChecked()
                if (checked.length > 0 && !root._checking)
                    parts.push(qsTr("checked %1").arg(checked))
                return parts.join(" · ")
            }
        }
    }
}
