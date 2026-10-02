import QtQuick 2.0
// QtMultimedia Camera stand-in: goes "active" as soon as it is asked to.
QtObject {
    enum CaptureMode { CaptureViewfinder, CaptureStillImage, CaptureVideo }
    enum State { UnloadedState, LoadedState, ActiveState }
    enum Status { UnavailableStatus, UnloadedStatus, LoadedStatus, ActiveStatus }
    enum Availability { Available, Busy, Unavailable, ResourceMissing }
    enum FocusMode { FocusManual, FocusAuto, FocusContinuous, FocusMacro }
    property int captureMode
    property int cameraState
    readonly property int cameraStatus: cameraState === 2 ? 3 : 1
    property int availability: 0
    property string errorString
    property CameraFocus focus: CameraFocus { }
}
