pragma Singleton
import QtQuick 2.0
QtObject {
    property real paddingSmall: 6; property real paddingMedium: 12; property real paddingLarge: 24
    property real horizontalPageMargin: 24
    property real itemSizeExtraSmall: 60; property real itemSizeSmall: 80; property real itemSizeMedium: 100; property real itemSizeLarge: 120
    property real iconSizeSmall: 32; property real iconSizeMedium: 64; property real iconSizeLarge: 96; property real iconSizeExtraLarge: 128
    property real fontSizeExtraSmall: 20; property real fontSizeSmall: 24; property real fontSizeMedium: 28
    property real fontSizeLarge: 34; property real fontSizeExtraLarge: 40; property real fontSizeHuge: 60
    property color primaryColor: "white"; property color secondaryColor: "#aaaaaa"
    property color highlightColor: "#7fd6ff"; property color secondaryHighlightColor: "#4fa3c7"; property color errorColor: "red"
    property real iconSizeLauncher: 86
    property real buttonWidthSmall: 200; property real buttonWidthMedium: 300; property real buttonWidthLarge: 400
    property color highlightBackgroundColor: "#2a7bb8"; property real highlightBackgroundOpacity: 0.3
    property string fontFamilyHeading: "sans"
    function rgba(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    property real opacityFaint: 0.2; property real opacityLow: 0.4; property real opacityHigh: 0.6; property real opacityOverlay: 0.8
}
