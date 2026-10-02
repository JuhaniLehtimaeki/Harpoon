import QtQuick 2.0
Page { property bool canAccept: true; signal accepted(); signal rejected(); function accept() { if (canAccept) accepted() } }
