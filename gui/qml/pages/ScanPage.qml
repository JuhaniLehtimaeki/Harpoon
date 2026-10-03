import QtQuick 2.0
import QtMultimedia 5.6
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0

// Reads an app link from a QR code: a repository URL or a
// harpoon://add?url=... link (see docs/add-to-harpoon.md). Emits linkFound
// with the parsed link (harpoon.parseAddLink) once, then stops the camera.
Page {
    id: page

    signal linkFound(var link)

    property bool _found: false
    property string _message: ""
    readonly property bool _scanning: status === PageStatus.Active && !_found
                                      && Qt.application.state === Qt.ApplicationActive

    // Returns true when the text is a usable link.
    function handleDecoded(text) {
        if (_found || text.length === 0) {
            return false
        }
        var link = harpoon.parseAddLink(text)
        if (!link.ok) {
            _message = qsTr("This QR code is not an app link: %1").arg(link.error)
            return false
        }
        _found = true
        _message = ""
        linkFound(link)
        return true
    }

    function _grab() {
        if (qrDecoder.busy) {
            return
        }
        viewfinder.grabToImage(function(result) {
            if (page._scanning) {
                qrDecoder.scan(result.image)
            }
        })
    }

    objectName: "scanPage"
    allowedOrientations: Orientation.Portrait

    Camera {
        id: camera

        captureMode: Camera.CaptureViewfinder
        cameraState: page._scanning ? Camera.ActiveState : Camera.UnloadedState
        focus.focusMode: Camera.FocusContinuous
    }

    Connections {
        target: qrDecoder
        onScanned: page.handleDecoded(text)
    }

    Timer {
        interval: 400
        repeat: true
        running: page._scanning && camera.cameraStatus === Camera.ActiveStatus
        onTriggered: page._grab()
    }

    Component {
        id: imagePicker

        ImagePickerPage {
            onSelectedContentPropertiesChanged: {
                var path = selectedContentProperties.filePath
                if (!path) {
                    return
                }
                var text = qrDecoder.decodeFile(path)
                if (text.length === 0) {
                    page._message = qsTr("No QR code found in the image")
                } else {
                    page.handleDecoded(text)
                }
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        PullDownMenu {
            MenuItem {
                text: qsTr("Read from image")
                onClicked: pageStack.push(imagePicker)
            }
        }

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader { title: qsTr("Scan QR code") }

            // Camera images go edge to edge, like other graphics.
            Item {
                width: parent.width
                height: width
                clip: true

                VideoOutput {
                    id: viewfinder

                    anchors.fill: parent
                    source: camera
                    fillMode: VideoOutput.PreserveAspectCrop
                    // Sailfish's camera backend already turns the image
                    // upright for the device, so only the page's own
                    // rotation would be added here, and the page is
                    // portrait only. autoOrientation would add the sensor's
                    // rotation a second time: a sideways preview.
                    orientation: 0
                    visible: camera.availability === Camera.Available
                }

                InfoLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    visible: !viewfinder.visible
                    text: camera.errorString.length > 0 ? camera.errorString : qsTr("Camera is not available")
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: page._message.length > 0 ? Theme.highlightColor : Theme.secondaryHighlightColor
                text: page._message.length > 0
                      ? page._message
                      : qsTr("Point the camera at the QR code on the app's web page or README.")
            }
        }
    }
}
