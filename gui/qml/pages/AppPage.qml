import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.harpoon 1.0
import "../components"

Page {
    id: page

    property string appId
    // Refreshed whenever the model changes, so the page follows checks and installs.
    property var details: harpoon.appDetails(appId)
    readonly property real _downloadSize: {
        var total = 0
        var assets = details.assets || []
        for (var i = 0; i < assets.length; ++i) {
            total += assets[i].size > 0 ? assets[i].size : 0
        }
        return total
    }
    readonly property bool _installed: details.installedVersion !== undefined && details.installedVersion.length > 0

    function refresh() {
        details = harpoon.appDetails(appId)
    }

    // Invalid QDateTimes arrive as truthy "Invalid Date" objects.
    function formatDate(date, format) {
        return date && !isNaN(date.getTime()) ? Format.formatDate(date, format) : ""
    }

    allowedOrientations: Orientation.All
    onAppIdChanged: refresh()

    Connections {
        target: harpoon.apps
        onAppChanged: {
            if (id === page.appId) {
                page.refresh()
            }
        }
        onModelReset: page.refresh()
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            visible: !details.busy

            MenuItem {
                visible: page._installed && !details.trackOnly && !details.temporaryId
                text: qsTr("Uninstall")
                onClicked: Remorse.popupAction(page, qsTr("Uninstalling"), function() { harpoon.uninstall(appId) })
            }
            MenuItem {
                text: qsTr("Share as QR code")
                onClicked: pageStack.push(Qt.resolvedUrl("ShareQrPage.qml"), { appId: appId })
            }
            MenuItem {
                text: qsTr("App settings")
                onClicked: pageStack.push(Qt.resolvedUrl("AppSettingsPage.qml"), { appId: appId })
            }
            MenuItem {
                text: qsTr("Check now")
                onClicked: harpoon.check(appId)
            }
        }

        PushUpMenu {
            visible: !details.busy && (releaseItem.visible || reinstallItem.visible)

            MenuItem {
                id: releaseItem

                visible: details.releasePageUrl !== undefined && details.releasePageUrl.length > 0
                text: qsTr("Open release page")
                onClicked: Qt.openUrlExternally(details.releasePageUrl)
            }
            MenuItem {
                id: reinstallItem

                visible: page._installed && !details.trackOnly
                         && (details.state === AppListModel.UpToDate || details.state === AppListModel.Unknown)
                text: qsTr("Reinstall")
                onClicked: harpoon.install(appId, true, true)
            }
        }

        Column {
            id: column

            width: parent.width

            // Hero: the app's icon and name over a calm sea, and where it stands.
            Item {
                width: parent.width
                height: heroColumn.height + Theme.itemSizeSmall

                Waves {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.itemSizeSmall
                    color: Theme.highlightBackgroundColor
                    opacity: Theme.opacityLow
                }

                Column {
                    id: heroColumn

                    width: parent.width
                    spacing: Theme.paddingMedium

                    Item {
                        width: 1
                        height: Theme.paddingLarge * 2
                    }

                    AppIcon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Theme.iconSizeExtraLarge
                        height: Theme.iconSizeExtraLarge
                        source: details.icon || ""
                        name: details.name || ""
                    }

                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.family: Theme.fontFamilyHeading
                        font.pixelSize: Theme.fontSizeExtraLarge
                        color: Theme.highlightColor
                        text: details.name || ""
                    }

                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        visible: text.length > 0
                        horizontalAlignment: Text.AlignHCenter
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                        text: details.author ? qsTr("by %1").arg(details.author) : ""
                    }

                    StateLabel {
                        // A failed first check has nothing to summarise; the error says it.
                        visible: !(appState === AppListModel.NotChecked && details.lastError !== undefined
                                   && details.lastError.length > 0)
                                 && details.waitingForBuilds !== true
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: Theme.fontSizeSmall
                        appState: details.state !== undefined ? details.state : AppListModel.NotChecked
                        installedVersion: details.installedVersion || ""
                        latestVersion: details.latestVersion || ""
                        trackOnly: details.trackOnly === true
                    }

                    LinkedLabel {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        visible: details.lastError !== undefined && details.lastError.length > 0
                                 && details.waitingForBuilds !== true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        plainText: details.lastError || ""
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.errorColor
                    }
                }
            }

            // Tracked, but nothing to install yet: say why, and that it is fine.
            Item {
                id: waitingCard

                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: waitingColumn.height + 2 * Theme.paddingLarge
                visible: details.waitingForBuilds === true

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.paddingMedium
                    color: Theme.rgba(Theme.highlightBackgroundColor, Theme.opacityFaint)
                }

                Column {
                    id: waitingColumn

                    x: Theme.paddingLarge
                    y: Theme.paddingLarge
                    width: parent.width - 2 * Theme.paddingLarge
                    spacing: Theme.paddingMedium

                    Row {
                        spacing: Theme.paddingMedium

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            source: "image://theme/icon-m-time"
                            color: Theme.highlightColor
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            font.family: Theme.fontFamilyHeading
                            color: Theme.highlightColor
                            text: qsTr("No builds yet")
                        }
                    }

                    LinkedLabel {
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.highlightColor
                        plainText: details.lastError || ""
                    }

                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryHighlightColor
                        text: qsTr("Harpoon keeps checking this app and offers it as soon as a build is published. You don't need to do anything.")
                    }
                }
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            ProgressBar {
                width: parent.width
                visible: details.busy === true
                indeterminate: !(details.progress >= 0)
                value: Math.max(0, details.progress || 0)
                label: details.stage || ""
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                preferredWidth: Theme.buttonWidthMedium
                icon.source: "image://theme/icon-m-device-download"
                visible: !details.busy && !details.trackOnly
                         && (details.state === AppListModel.UpdateAvailable
                             || details.state === AppListModel.NotInstalled)
                text: details.state === AppListModel.UpdateAvailable ? qsTr("Update") : qsTr("Install")
                onClicked: harpoon.install(appId)
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                preferredWidth: Theme.buttonWidthMedium
                icon.source: "image://theme/icon-m-acknowledge"
                visible: !details.busy && details.trackOnly === true && details.state === AppListModel.UpdateAvailable
                text: qsTr("Mark as seen")
                onClicked: harpoon.acknowledge(appId)
            }

            Item {
                width: 1
                height: Theme.paddingLarge
                visible: notesCard.visible
            }

            // What changed, first: that is what decides an update. Markdown
            // from the forge, as styled text with working links.
            Item {
                id: notesCard

                property bool expanded

                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: notesColumn.height + 2 * Theme.paddingLarge
                visible: changelogLabel.text.length > 0

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.paddingMedium
                    color: Theme.rgba(Theme.highlightBackgroundColor, Theme.opacityFaint)
                }

                Column {
                    id: notesColumn

                    x: Theme.paddingLarge
                    y: Theme.paddingLarge
                    width: parent.width - 2 * Theme.paddingLarge
                    spacing: Theme.paddingMedium

                    Label {
                        width: parent.width
                        truncationMode: TruncationMode.Fade
                        font.family: Theme.fontFamilyHeading
                        color: Theme.highlightColor
                        text: details.latestVersion ? qsTr("What's new in %1").arg(details.latestVersion)
                                                    : qsTr("What's new")
                    }

                    Label {
                        id: changelogLabel

                        width: parent.width
                        wrapMode: Text.Wrap
                        textFormat: Text.StyledText
                        maximumLineCount: notesCard.expanded ? 100000 : 8
                        elide: Text.ElideRight
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.highlightColor
                        linkColor: Theme.primaryColor
                        text: details.changelogText || ""
                        onLinkActivated: Qt.openUrlExternally(link)
                    }

                    Label {
                        visible: changelogLabel.truncated || notesCard.expanded
                        anchors.right: parent.right
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: moreArea.pressed ? Theme.highlightColor : Theme.primaryColor
                        text: notesCard.expanded ? qsTr("Show less") : qsTr("Show all")

                        MouseArea {
                            id: moreArea

                            anchors.fill: parent
                            anchors.margins: -Theme.paddingMedium
                            onClicked: notesCard.expanded = !notesCard.expanded
                        }
                    }
                }
            }

            SectionHeader {
                visible: (details.latestVersion || "").length > 0
                text: qsTr("Latest release")
            }

            DetailItem {
                visible: (details.latestVersion || "").length > 0
                label: qsTr("Version")
                value: details.latestVersion || ""
            }
            DetailItem {
                visible: details.latestTitle !== undefined && details.latestTitle.length > 0
                         && details.latestTitle !== details.latestTag
                label: qsTr("Title")
                value: details.latestTitle || ""
            }
            DetailItem {
                visible: value.length > 0
                label: qsTr("Published")
                value: page.formatDate(details.latestDate, Formatter.DateMedium)
            }
            DetailItem {
                visible: details.prerelease === true
                label: qsTr("Channel")
                value: qsTr("Prerelease")
            }
            DetailItem {
                visible: page._downloadSize > 0
                label: qsTr("Download size")
                value: Format.formatFileSize(page._downloadSize)
            }
            Repeater {
                model: details.assets || []
                DetailItem {
                    label: index === 0 ? qsTr("Package") : ""
                    // Let long file names wrap after "-" and "." rather than
                    // anywhere ("…aarch64.rp" / "m").
                    value: modelData.name.replace(/([-.])/g, "$1\u200b")
                }
            }

            SectionHeader { text: qsTr("Installed") }

            DetailItem {
                label: qsTr("Version")
                value: details.installedVersion && details.installedVersion.length > 0
                       ? details.installedVersion : qsTr("Not installed")
            }
            DetailItem {
                visible: details.installedVendor !== undefined && details.installedVendor.length > 0
                label: qsTr("Vendor")
                value: details.installedVendor || ""
            }
            DetailItem {
                visible: details.receiptEvr !== undefined && details.receiptEvr.length > 0
                label: qsTr("Installed by Harpoon")
                value: page.formatDate(details.receiptInstalledAt, Formatter.DateMedium)
            }

            DetailItem {
                visible: details.receiptVerification !== undefined && details.receiptVerification.length > 0
                label: qsTr("Build provenance")
                value: details.receiptVerification === "attestation:verified" ? qsTr("Signature verified")
                       : details.receiptVerification === "attestation:github" ? qsTr("Reported by GitHub")
                       : details.receiptVerification === "attestation:missing" ? qsTr("No attestation")
                       : qsTr("Could not check")
            }

            SectionHeader { text: qsTr("Source") }

            DetailItem {
                label: qsTr("Type")
                value: details.sourceName || ""
            }
            DetailItem {
                label: qsTr("Last check")
                value: page.formatDate(details.lastCheck, Formatter.TimepointRelative) || qsTr("Never")
            }
            LinkedLabel {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                font.pixelSize: Theme.fontSizeSmall
                plainText: details.url || ""
            }

        }

        VerticalScrollDecorator { }
    }
}
