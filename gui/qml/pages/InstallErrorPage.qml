import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Why an install or update failed, what to try, and a report to send to
// whoever can fix it.
Page {
    id: page

    property string appId
    property var problem: harpoon.installProblem(appId)
    property bool _copied

    objectName: "installErrorPage"
    allowedOrientations: Orientation.All

    Connections {
        target: harpoon.apps
        // A later attempt replaces the report; a success clears it.
        onAppChanged: {
            if (id === page.appId) {
                var next = harpoon.installProblem(page.appId)
                if (next.report !== undefined) {
                    page.problem = next
                }
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
                title: problem.updating ? qsTr("Update failed") : qsTr("Installation failed")
                description: problem.appName || ""
            }

            // The error as the system put it.
            Item {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: messageLabel.height

                Icon {
                    id: errorIcon

                    y: Theme.paddingSmall
                    source: "image://theme/icon-s-filled-warning"
                    color: Theme.errorColor
                }

                Label {
                    id: messageLabel

                    anchors {
                        left: errorIcon.right
                        leftMargin: Theme.paddingMedium
                        right: parent.right
                    }
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    color: Theme.errorColor
                    text: problem.message || ""
                }
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: problem.advice || ""
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                preferredWidth: Theme.buttonWidthMedium
                icon.source: "image://theme/icon-m-sync"
                text: qsTr("Try again")
                onClicked: {
                    harpoon.install(page.appId)
                    pageStack.pop()
                }
            }

            SectionHeader { text: qsTr("Report") }

            HintLabel {
                text: problem.appProblem
                      ? qsTr("Send this report to the app's developer, or to Harpoon's maintainer if you are not sure. It has no personal data: only versions, the app and the error.")
                      : qsTr("Send this report to Harpoon's maintainer. It has no personal data: only versions, the app and the error.")
            }

            Item {
                width: 1
                height: Theme.paddingMedium
            }

            // The report, as it will be pasted.
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: reportLabel.height + 2 * Theme.paddingMedium
                radius: Theme.paddingSmall
                color: Theme.rgba(Theme.highlightBackgroundColor, Theme.opacityFaint)

                Label {
                    id: reportLabel

                    x: Theme.paddingMedium
                    y: Theme.paddingMedium
                    width: parent.width - 2 * Theme.paddingMedium
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    font.family: "monospace"
                    font.pixelSize: Theme.fontSizeTiny
                    color: Theme.secondaryHighlightColor
                    text: problem.report || ""
                }
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                preferredWidth: Theme.buttonWidthLarge
                icon.source: "image://theme/icon-s-clipboard"
                text: page._copied ? qsTr("Copied") : qsTr("Copy report")
                onClicked: {
                    Clipboard.text = problem.report
                    page._copied = true
                }
            }

            Item {
                width: 1
                height: Theme.paddingMedium
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                preferredWidth: Theme.buttonWidthLarge
                icon.source: "image://theme/icon-m-website"
                text: qsTr("Report to Harpoon")
                onClicked: Qt.openUrlExternally(problem.issueUrl)
            }

            Item {
                width: 1
                height: Theme.paddingMedium
                visible: appButton.visible
            }

            Button {
                id: appButton

                anchors.horizontalCenter: parent.horizontalCenter
                visible: problem.appProblem === true && (problem.appUrl || "").length > 0
                preferredWidth: Theme.buttonWidthLarge
                icon.source: "image://theme/icon-m-website"
                text: qsTr("Open the app's page")
                onClicked: Qt.openUrlExternally(problem.appUrl)
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            HintLabel {
                text: qsTr("\"Report to Harpoon\" opens a new issue on GitHub with the report filled in; you need a GitHub account to send it. Without one, copy the report and send it another way.")
            }
        }

        VerticalScrollDecorator { }
    }
}
