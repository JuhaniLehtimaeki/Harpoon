import QtQuick 2.0
import Sailfish.Silica 1.0

// Explanatory text under a setting or section, aligned with the page margin.
Label {
    x: Theme.horizontalPageMargin
    width: (parent ? parent.width : 0) - 2 * Theme.horizontalPageMargin
    wrapMode: Text.Wrap
    font.pixelSize: Theme.fontSizeExtraSmall
    color: Theme.secondaryHighlightColor
}
