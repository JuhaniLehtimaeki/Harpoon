import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Page {
    id: page

    property string appId
    property var details: harpoon.appDetails(appId)
    readonly property var _values: details.settings || ({})
    readonly property bool _isWebPage: details.effectiveSourceId === "HTML" || details.effectiveSourceId === "DirectLink"
    readonly property bool _isRepo: details.effectiveSourceId === "RpmMdRepo"
    property bool _changed

    allowedOrientations: Orientation.All

    // Settings change which release and package are chosen: check again
    // when leaving the page.
    onStatusChanged: {
        if (status === PageStatus.Deactivating && _changed) {
            harpoon.check(appId)
        }
    }

    Connections {
        target: harpoon.apps
        onDataChanged: {
            var before = JSON.stringify(page._values)
            page.details = harpoon.appDetails(page.appId)
            if (JSON.stringify(page._values) !== before) {
                page._changed = true
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column

            width: parent.width

            PageHeader {
                title: qsTr("App settings")
                description: details.name || ""
            }

            TextField {
                width: parent.width
                label: qsTr("Name")
                // Empty means automatic: the installed app's own name, or one
                // made from the repository name.
                placeholderText: details.name || qsTr("Name")
                text: details.customName || ""
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
                onActiveFocusChanged: {
                    if (!activeFocus && text.trim() !== (details.customName || "")) {
                        harpoon.setAppName(appId, text)
                    }
                }
            }

            SectionHeader {
                visible: page._isWebPage || page._isRepo
                text: page._isRepo ? qsTr("Repository") : qsTr("Web page")
            }

            SettingText {
                visible: page._isRepo
                appId: page.appId; key: "packageName"; values: page._values
                label: qsTr("Package name")
            }

            Column {
                width: parent.width
                visible: page._isWebPage

                SettingText {
                    appId: page.appId; key: "customLinkFilterRegex"; values: page._values
                    label: qsTr("Link filter (regular expression)")
                }
                SettingSwitch {
                    appId: page.appId; key: "filterByLinkText"; values: page._values
                    text: qsTr("Match the link text instead of the address")
                }
                SettingSwitch {
                    appId: page.appId; key: "matchLinksOutsideATags"; values: page._values
                    text: qsTr("Also find addresses outside links")
                }
                SettingSwitch {
                    appId: page.appId; key: "skipSort"; values: page._values
                    text: qsTr("Keep the page's order of links")
                }
                SettingSwitch {
                    appId: page.appId; key: "reverseSort"; values: page._values
                    text: qsTr("Take the first link instead of the last")
                }
                SettingSwitch {
                    appId: page.appId; key: "sortByLastLinkSegment"; values: page._values
                    text: qsTr("Sort by file name only")
                }
                SettingSwitch {
                    appId: page.appId; key: "versionExtractWholePage"; values: page._values
                    text: qsTr("Extract the version from the whole page")
                }
                SettingChoice {
                    appId: page.appId; key: "defaultPseudoVersioningMethod"; values: page._values
                    label: qsTr("Without a version, detect changes by")
                    options: [
                        { value: "ETag", text: qsTr("Server ETag") },
                        { value: "linkHash", text: qsTr("Link address") }
                    ]
                }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                    text: qsTr("Multi-page link chains and request headers can be set with harpoon-cli.")
                }
            }

            SectionHeader { text: qsTr("Releases") }

            SettingSwitch {
                appId: page.appId; key: "includePrereleases"; values: page._values
                text: qsTr("Include prereleases")
            }
            SettingSwitch {
                appId: page.appId; key: "fallbackToOlderReleases"; values: page._values; defaultValue: true
                text: qsTr("Fall back to older releases")
                description: qsTr("Use the newest release that has a package for this device")
            }
            SettingSwitch {
                appId: page.appId; key: "trackOnly"; values: page._values
                text: qsTr("Track only")
                description: qsTr("Only notify about new releases; nothing is installed")
            }
            SettingChoice {
                appId: page.appId; key: "sortMethodChoice"; values: page._values
                label: qsTr("Order releases by")
                options: [
                    { value: "date", text: qsTr("Publish date") },
                    { value: "smartname-datefallback", text: qsTr("Version, else date") },
                    { value: "smartname", text: qsTr("Version, else name") },
                    { value: "name", text: qsTr("Name") },
                    { value: "none", text: qsTr("As listed by the forge") }
                ]
            }
            SettingSwitch {
                appId: page.appId; key: "useLatestAssetDateAsReleaseDate"; values: page._values
                text: qsTr("Date releases by their newest package")
            }
            SettingText {
                appId: page.appId; key: "filterReleaseTitlesByRegEx"; values: page._values
                label: qsTr("Release title filter (regular expression)")
            }
            SettingText {
                appId: page.appId; key: "filterReleaseNotesByRegEx"; values: page._values
                label: qsTr("Release notes filter (regular expression)")
            }

            SectionHeader { text: qsTr("Packages") }

            SettingText {
                appId: page.appId; key: "assetFilterRegEx"; values: page._values
                label: qsTr("Package filter (regular expression)")
            }
            SettingSwitch {
                appId: page.appId; key: "invertAssetFilter"; values: page._values
                text: qsTr("Invert package filter")
            }
            SettingSwitch {
                appId: page.appId; key: "autoAssetFilterByArch"; values: page._values; defaultValue: true
                text: qsTr("Pick packages for this device")
                description: qsTr("Keep only %1 packages, or noarch if there are none").arg(harpoon.deviceArch)
            }
            SettingSwitch {
                appId: page.appId; key: "preferSfosVersionTag"; values: page._values; defaultValue: true
                text: qsTr("Prefer packages for this SailfishOS version")
                description: qsTr("Uses tags such as sfos5.0 in package names")
            }
            SettingSwitch {
                visible: harpoon.settings.autoUpdate && details.trackOnly !== true
                appId: page.appId; key: "excludeFromAutoUpdate"; values: page._values
                text: qsTr("Exclude from automatic updates")
                description: qsTr("Always ask before updating this app")
            }
            SettingSwitch {
                appId: page.appId; key: "allowIdChange"; values: page._values
                text: qsTr("Allow a different package name")
                description: details.temporaryId === true
                             ? qsTr("Accept a release whose RPM has a different name from the first one installed")
                             : qsTr("Accept a release whose RPM is not named %1").arg(page.appId)
            }

            SectionHeader {
                visible: details.effectiveSourceId === "GitHub"
                text: qsTr("Security")
            }

            SettingChoice {
                visible: details.effectiveSourceId === "GitHub"
                appId: page.appId; key: "githubBuildVerificationMode"; values: page._values
                label: qsTr("Check build provenance")
                description: qsTr("Uses GitHub artifact attestations to confirm the package was built by the repository's own workflow")
                options: [
                    { value: "off", text: qsTr("Off") },
                    { value: "audit", text: qsTr("Warn if missing") },
                    { value: "enforce", text: qsTr("Refuse to install if missing") }
                ]
            }

            SectionHeader { text: qsTr("Version") }

            SettingChoice {
                appId: page.appId; key: "versionSource"; values: page._values
                label: qsTr("Version comes from")
                options: [
                    { value: "tag", text: qsTr("Release tag") },
                    { value: "title", text: qsTr("Release title") },
                    { value: "assetName", text: qsTr("Package file name") },
                    { value: "date", text: qsTr("Release date") }
                ]
            }
            SettingText {
                appId: page.appId; key: "versionExtractionRegEx"; values: page._values
                label: qsTr("Version extraction (regular expression)")
            }
            SettingText {
                appId: page.appId; key: "matchGroupToUse"; values: page._values
                label: qsTr("Match group, for example $1")
            }
        }

        VerticalScrollDecorator { }
    }
}
