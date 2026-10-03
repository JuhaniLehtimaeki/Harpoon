import QtQuick 2.0
import Sailfish.Silica 1.0

// One-tap starts for an app's address: the big forges, and the clipboard
// when it holds a link. Picking a forge keeps an owner/repo already typed.
Flow {
    id: root

    property Item field
    property var hosts: ["github.com", "codeberg.org", "codefloe.com"]

    readonly property string _clip: Clipboard.text ? String(Clipboard.text).trim() : ""
    readonly property bool _clipIsLink: /^https?:\/\/[^\s\/]+\.[^\s\/]+\/\S+$/i.test(_clip)
                                        && _clip !== (field ? field.text.trim() : "")

    function _hostOf(text) {
        var m = /^(?:[a-z]+:\/\/)?([^\/\s]+)/i.exec(text.trim())
        return m ? m[1].toLowerCase().replace(/^www\./, "") : ""
    }

    function useHost(host) {
        var rest = field.text.trim().replace(/^[a-z]+:\/\//i, "")
        var slash = rest.indexOf("/")
        var first = slash >= 0 ? rest.substring(0, slash) : rest
        // Keep what follows a typed host, or a bare "owner/repo".
        var path = first.indexOf(".") >= 0 ? (slash >= 0 ? rest.substring(slash + 1) : "") : rest
        field.text = "https://" + host + "/" + path
        field.forceActiveFocus()
        field.cursorPosition = field.text.length
    }

    function _paste() {
        field.text = _clip
        field.cursorPosition = field.text.length
    }

    x: Theme.horizontalPageMargin
    width: (parent ? parent.width : 0) - 2 * Theme.horizontalPageMargin
    spacing: Theme.paddingMedium

    Repeater {
        model: root.hosts

        Chip {
            text: modelData
            selected: root.field && root._hostOf(root.field.text) === modelData
            onClicked: root.useHost(modelData)
        }
    }

    Chip {
        visible: root._clipIsLink
        iconSource: "image://theme/icon-s-clipboard"
        text: qsTr("Paste link")
        onClicked: root._paste()
    }
}
