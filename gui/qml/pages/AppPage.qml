import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.harpoon 1.0
import "../components"

Page {
    id: page

    property string appId
    // Refreshed whenever the model changes, so the page follows checks and installs.
    property var details: harpoon.appDetails(appId)
    // Busy, stage and progress alone: updated on every progress step, which
    // must not rebuild the whole page (details).
    property var progress: harpoon.appProgress(appId)
    readonly property real _downloadSize: {
        var total = 0
        var assets = details.assets || []
        for (var i = 0; i < assets.length; ++i) {
            total += assets[i].size > 0 ? assets[i].size : 0
        }
        return total
    }
    // Several builds of the app in the release: the user picks one first.
    readonly property bool _needsBuild: details.trackOnly !== true && (details.buildChoices || []).length > 1
    readonly property bool _installed: details.installedVersion !== undefined && details.installedVersion.length > 0

    function refresh() {
        details = harpoon.appDetails(appId)
        progress = harpoon.appProgress(appId)
        _leaveIfGone()
    }

    // Stopped tracking here or elsewhere (the list, harpoon-cli): a page about
    // nothing would show an empty tile and "Not installed".
    function _leaveIfGone() {
        if (details.appId === undefined && harpoon.loaded && status === PageStatus.Active
                && pageStack.currentPage === page) {
            pageStack.pop()
        }
    }

    onStatusChanged: _leaveIfGone()

    // Invalid QDateTimes arrive as truthy "Invalid Date" objects.
    function formatDate(date, format) {
        return date && !isNaN(date.getTime()) ? Format.formatDate(date, format) : ""
    }

    allowedOrientations: Orientation.All
    onAppIdChanged: refresh()

    Connections {
        target: harpoon.apps
        onAppChanged: {
            if (id !== page.appId) {
                return
            }
            if (busyOnly) {
                page.progress = harpoon.appProgress(page.appId)
            } else {
                page.refresh()
            }
        }
        onModelReset: page.refresh()
        // Removing a row says nothing about which app: look whether it was ours.
        onRowsRemoved: {
            if (harpoon.appDetails(page.appId).appId === undefined) {
                page.refresh()
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            visible: !page.progress.busy

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
            visible: !page.progress.busy

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
            // Here too, not only in the list's long-press menu: from the page of
            // a repository that is gone there was no way out.
            MenuItem {
                text: qsTr("Stop tracking")
                onClicked: {
                    var id = appId
                    Remorse.popupAction(page, qsTr("Stopping tracking"), function() { harpoon.removeApp(id) })
                }
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

            // The last install or update failed: say so, with the way to the report.
            Item {
                width: 1
                height: Theme.paddingLarge
                visible: problemCard.visible
            }

            BackgroundItem {
                id: problemCard

                readonly property var _problem: details.installProblem || ({})

                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: problemColumn.height + 2 * Theme.paddingLarge
                visible: page.progress.busy !== true && _problem.report !== undefined
                onClicked: pageStack.push(Qt.resolvedUrl("InstallErrorPage.qml"), { appId: page.appId })

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.paddingMedium
                    color: Theme.rgba(Theme.errorColor, Theme.opacityFaint)
                }

                Column {
                    id: problemColumn

                    x: Theme.paddingLarge
                    y: Theme.paddingLarge
                    width: parent.width - 2 * Theme.paddingLarge
                    spacing: Theme.paddingSmall

                    Row {
                        spacing: Theme.paddingMedium

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            source: "image://theme/icon-s-filled-warning"
                            color: Theme.errorColor
                        }

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.errorColor
                            text: problemCard._problem.updating ? qsTr("The update failed")
                                                                : qsTr("The installation failed")
                        }
                    }

                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.highlightColor
                        text: problemCard._problem.message || ""
                    }

                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryHighlightColor
                        text: qsTr("Tap for what to try and a report to send")
                    }
                }
            }

            // Keeps the card below off the hero's waves.
            Item {
                width: 1
                height: Theme.paddingLarge
                visible: waitingCard.visible
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

            // Several builds of the same package, such as "openrepos" and
            // "store": installing needs a choice, which is kept as a filter.
            Item {
                width: 1
                height: Theme.paddingLarge
                visible: buildCard.visible
            }

            Item {
                id: buildCard

                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: buildColumn.height + 2 * Theme.paddingLarge
                visible: page._needsBuild && page.progress.busy !== true

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.paddingMedium
                    color: Theme.rgba(Theme.highlightBackgroundColor, Theme.opacityFaint)
                }

                Column {
                    id: buildColumn

                    y: Theme.paddingLarge
                    width: parent.width
                    spacing: Theme.paddingSmall

                    Label {
                        x: Theme.paddingLarge
                        width: parent.width - 2 * Theme.paddingLarge
                        wrapMode: Text.Wrap
                        color: Theme.highlightColor
                        text: qsTr("Choose a build")
                    }

                    Label {
                        x: Theme.paddingLarge
                        width: parent.width - 2 * Theme.paddingLarge
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                        text: qsTr("This release has %n builds of %1. Choose the one to install; Harpoon keeps to it in later releases.", "",
                                   (details.buildChoices || []).length).arg(details.buildPackage || "")
                    }

                    Repeater {
                        model: details.buildChoices || []

                        BackgroundItem {
                            id: choiceItem

                            width: parent.width
                            height: choiceColumn.height + 2 * Theme.paddingMedium
                            onClicked: harpoon.chooseBuild(page.appId, modelData.key)

                            Column {
                                id: choiceColumn

                                x: Theme.paddingLarge
                                y: Theme.paddingMedium
                                width: parent.width - 2 * Theme.paddingLarge

                                Label {
                                    width: parent.width
                                    truncationMode: TruncationMode.Fade
                                    color: choiceItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                                    text: modelData.label.length > 0 ? modelData.label : qsTr("Standard build")
                                }

                                Label {
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    font.pixelSize: Theme.fontSizeExtraSmall
                                    color: choiceItem.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                                    text: modelData.assetName
                                }
                            }
                        }
                    }
                }
            }

            // Newer by its tag, but the package inside is the installed one.
            Item {
                width: 1
                height: Theme.paddingLarge
                visible: notNewerLabel.visible
            }

            Label {
                id: notNewerLabel

                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: details.stateReason === "releasePackageNotNewer" && page.progress.busy !== true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Release %1 contains version %2 of the package, which is not newer than the installed one, so it is not offered as an update.")
                      .arg(details.latestTag || details.latestVersion || "").arg(details.offeredPackageVersion || "")
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            ProgressBar {
                width: parent.width
                visible: page.progress.busy === true
                indeterminate: !(page.progress.progress >= 0)
                value: Math.max(0, page.progress.progress || 0)
                label: page.progress.stage || ""
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                preferredWidth: Theme.buttonWidthMedium
                icon.source: "image://theme/icon-m-device-download"
                visible: !page.progress.busy && !details.trackOnly && !page._needsBuild
                         && (details.state === AppListModel.UpdateAvailable
                             || details.state === AppListModel.NotInstalled)
                text: details.state === AppListModel.UpdateAvailable ? qsTr("Update") : qsTr("Install")
                onClicked: harpoon.install(appId)
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                preferredWidth: Theme.buttonWidthMedium
                icon.source: "image://theme/icon-m-acknowledge"
                visible: !page.progress.busy && details.trackOnly === true && details.state === AppListModel.UpdateAvailable
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

                        // A finger-sized target around the small label.
                        MouseArea {
                            id: moreArea

                            anchors.centerIn: parent
                            width: parent.width + 2 * Theme.paddingLarge
                            height: Theme.itemSizeSmall
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

            // Track-only apps are not installed by Harpoon: what counts is the
            // release last marked as seen (which the status calls "installed").
            SectionHeader { text: details.trackOnly === true ? qsTr("Tracking") : qsTr("Installed") }

            DetailItem {
                label: details.trackOnly === true ? qsTr("Last seen") : qsTr("Version")
                value: details.trackOnly === true
                       ? ((details.acknowledgedVersion || "").length > 0 ? details.acknowledgedVersion : qsTr("Nothing yet"))
                       : details.installedVersion && details.installedVersion.length > 0
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
