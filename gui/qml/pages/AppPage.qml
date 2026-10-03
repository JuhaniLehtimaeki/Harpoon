import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.harpoon 1.0
import "../components"

Page {
    id: page

    property string appId
    // Refreshed whenever the model changes, so the page follows checks and installs.
    property var details: harpoon.appDetails(appId)
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
            spacing: Theme.paddingMedium

            PageHeader {
                title: details.name || ""
                description: details.author || ""
            }

            StateLabel {
                // A failed first check has nothing to summarise; the error says it.
                visible: !(appState === AppListModel.NotChecked && details.lastError !== undefined
                           && details.lastError.length > 0)
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                font.pixelSize: Theme.fontSizeSmall
                appState: details.state !== undefined ? details.state : AppListModel.NotChecked
                installedVersion: details.installedVersion || ""
                latestVersion: details.latestVersion || ""
                trackOnly: details.trackOnly === true
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: details.lastError !== undefined && details.lastError.length > 0
                wrapMode: Text.Wrap
                text: details.lastError || ""
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.errorColor
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
                visible: !details.busy && !details.trackOnly
                         && (details.state === AppListModel.UpdateAvailable
                             || details.state === AppListModel.NotInstalled)
                text: details.state === AppListModel.UpdateAvailable ? qsTr("Update") : qsTr("Install")
                onClicked: harpoon.install(appId)
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !details.busy && details.trackOnly === true && details.state === AppListModel.UpdateAvailable
                text: qsTr("Mark as seen")
                onClicked: harpoon.acknowledge(appId)
            }

            SectionHeader { text: qsTr("Latest release") }

            DetailItem {
                label: qsTr("Version")
                value: details.latestVersion || "-"
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

            SectionHeader {
                visible: changelogLabel.text.length > 0
                text: qsTr("Release notes")
            }

            Label {
                id: changelogLabel

                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: (details.changelog || "").trim()
            }
        }

        VerticalScrollDecorator { }
    }
}
