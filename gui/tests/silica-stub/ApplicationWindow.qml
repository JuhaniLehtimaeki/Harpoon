import QtQuick 2.0
Item {
    id: window
    property var initialPage
    property var cover
    property int allowedOrientations
    property int defaultAllowedOrientations: 15
    property alias pageStack: stack
    function activate() { }
    width: 540; height: 960
    Rectangle { anchors.fill: parent; color: "#14202c" }
    Item {
        id: stack
        property var pages: []
        property var currentPage: pages.length > 0 ? pages[pages.length - 1] : null
        function push(page, props) {
            var component = (typeof page === "string" || String(page).indexOf("file:") === 0)
                    ? Qt.createComponent(page) : page
            if (component.status === Component.Error) { console.error(component.errorString()); return null }
            var obj = component.createObject(window, props || {})
            if (!obj) { console.error("Could not create page " + page); return null }
            for (var i = 0; i < pages.length; ++i) { pages[i].visible = false }
            var list = pages.slice(); list.push(obj); pages = list
            return obj
        }
        function pop(page) {
            var list = pages.slice()
            while (list.length > 1 && list[list.length - 1] !== page) { list.pop().destroy() }
            pages = list
            if (list.length > 0) { list[list.length - 1].visible = true }
        }
        function find(fn) {
            for (var i = pages.length - 1; i >= 0; --i) { if (fn(pages[i])) { return pages[i] } }
            return null
        }
    }
    Loader { id: coverLoader; source: typeof window.cover === "string" ? window.cover : "" }
    property alias coverItem: coverLoader.item
    Component.onCompleted: stack.push(initialPage)
}
