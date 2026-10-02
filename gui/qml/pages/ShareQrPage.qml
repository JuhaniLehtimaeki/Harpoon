import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Shows a tracked app's link as a QR code, so Harpoon on another phone can
// add the same app by scanning it.
Page {
    id: page

    property string appId
    readonly property var _details: harpoon.appDetails(appId)
    readonly property string link: harpoon.shareLink(appId)

    objectName: "shareQrPage"
    allowedOrientations: Orientation.All

    Banner { id: banner }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            visible: page.link.length > 0

            MenuItem {
                text: qsTr("Copy link")
                onClicked: {
                    Clipboard.text = page.link
                    banner.show(qsTr("Link copied"))
                }
            }
        }

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                title: qsTr("Share app")
                description: page._details.name || ""
            }

            // The code keeps its own white background and quiet zone so it
            // scans on any ambience.
            Image {
                id: code

                readonly property int side: Math.min(page.width, page.height) - 2 * Theme.horizontalPageMargin

                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.link.length > 0
                width: side
                height: side
                sourceSize { width: side; height: side }
                fillMode: Image.PreserveAspectFit
                smooth: false
                source: page.link.length > 0 ? "image://harpoonqr/" + encodeURIComponent(page.link) : ""
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: page.link.length > 0
                      ? qsTr("Scan this code with Harpoon on another phone to add %1.").arg(page._details.name || "")
                      : qsTr("This app's address cannot be shared.")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: page.link.length > 0
                wrapMode: Text.WrapAnywhere
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: page.link
            }
        }

        VerticalScrollDecorator { }
    }
}
