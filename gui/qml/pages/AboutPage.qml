import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader { title: qsTr("About Harpoon") }

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "/usr/share/icons/hicolor/172x172/apps/harpoon.png"
                width: Theme.iconSizeExtraLarge
                height: width
                sourceSize { width: width; height: height }
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Version %1").arg(Qt.application.version)
                color: Theme.highlightColor
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: qsTr("Harpoon installs and updates SailfishOS apps straight from where their developers publish them: GitHub, Codeberg and other Forgejo or Gitea servers.")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Inspired by Obtainium and ObtainX; parts of the release handling are ported from ObtainX.")
            }

            LinkedLabel {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                font.pixelSize: Theme.fontSizeSmall
                plainText: qsTr("Source code and issues: %1").arg("https://github.com/JuhaniLehtimaeki/Harpoon")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Licensed under the GNU General Public License, version 3 or later.")
            }
        }

        VerticalScrollDecorator { }
    }
}
