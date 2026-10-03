pragma Singleton
import QtQuick 2.0
QtObject {
    property real paddingSmall: 6; property real paddingMedium: 12; property real paddingLarge: 24
    property real horizontalPageMargin: 24
    property real itemSizeSmall: 80; property real itemSizeMedium: 100; property real itemSizeLarge: 120
    property real iconSizeSmall: 32; property real iconSizeMedium: 64; property real iconSizeLarge: 96; property real iconSizeExtraLarge: 128
    property real fontSizeExtraSmall: 20; property real fontSizeSmall: 24; property real fontSizeMedium: 28
    property real fontSizeLarge: 34; property real fontSizeExtraLarge: 40; property real fontSizeHuge: 60
    property color primaryColor: "white"; property color secondaryColor: "gray"
    property color highlightColor: "cyan"; property color secondaryHighlightColor: "teal"; property color errorColor: "red"
    property real opacityFaint: 0.2; property real opacityLow: 0.4; property real opacityHigh: 0.6; property real opacityOverlay: 0.8
}
