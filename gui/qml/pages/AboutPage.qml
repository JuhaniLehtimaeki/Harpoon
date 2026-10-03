import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Page {
    id: page

    readonly property string _repository: "https://github.com/JuhaniLehtimaeki/Harpoon"

    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column

            width: parent.width

            PageHeader { title: qsTr("About") }

            // The harpoon over a slowly moving sea.
            Item {
                width: parent.width
                height: Theme.iconSizeExtraLarge + Theme.itemSizeMedium

                Waves {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.itemSizeMedium
                    color: Theme.highlightBackgroundColor
                    opacity: Theme.opacityLow
                    animated: page.status === PageStatus.Active
                }

                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    source: "/usr/share/icons/hicolor/172x172/apps/harpoon.png"
                    width: Theme.iconSizeExtraLarge
                    height: width
                    sourceSize { width: width; height: height }
                }
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                font.family: Theme.fontFamilyHeading
                font.pixelSize: Theme.fontSizeHuge
                color: Theme.highlightColor
                text: qsTr("Harpoon")
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Version %1").arg(Qt.application.version)
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: qsTr("Apps straight from where their developers publish them: GitHub, Codeberg, Forgejo and Gitea servers, GitLab, SourceHut, SourceForge, Jenkins, web pages and RPM repositories.")
            }

            Item {
                width: 1
                height: Theme.paddingLarge * 2
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                preferredWidth: Theme.buttonWidthLarge
                icon.source: "image://theme/icon-m-website"
                text: qsTr("Source code")
                onClicked: Qt.openUrlExternally(page._repository)
            }

            Item {
                width: 1
                height: Theme.paddingMedium
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                preferredWidth: Theme.buttonWidthLarge
                icon.source: "image://theme/icon-m-question"
                text: qsTr("Report a problem")
                onClicked: Qt.openUrlExternally(page._repository + "/issues")
            }

            SectionHeader { text: qsTr("Thanks") }

            HintLabel {
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Inspired by Obtainium and ObtainX; parts of the release handling are ported from ObtainX. QR codes are read and drawn with zxing-cpp.")
            }

            SectionHeader { text: qsTr("Licence") }

            HintLabel {
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Harpoon is free software under the GNU General Public License, version 3 or later. zxing-cpp is under the Apache License 2.0.")
            }
        }

        VerticalScrollDecorator { }
    }
}
