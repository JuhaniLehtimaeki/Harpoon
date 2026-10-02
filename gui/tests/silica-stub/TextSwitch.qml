import QtQuick 2.0
Item { property string text; property string description; property bool checked; property bool automaticCheck: true; signal clicked(); width: parent ? parent.width : 0; height: 60 }
