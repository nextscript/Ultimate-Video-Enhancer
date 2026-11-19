import QtQuick

// Der Player weist nur die HIER deklarierten Properties zu (theme, controller, plugin) –
// und zwar erst nach dem Laden. Deshalb werden sie per Qt.binding an KickChat.qml
// weitergereicht.
Item {
    id: root
    property var controller: null
    property var plugin: null
    property var theme: null
    implicitWidth: 360
    implicitHeight: 600

    Loader {
        id: chatLoader
        objectName: "chatLoader"
        anchors.fill: parent
        source: "KickChat.qml"
        onLoaded: {
            if (!item) return
            item.controller = Qt.binding(function () { return root.controller })
            item.plugin     = Qt.binding(function () { return root.plugin })
            item.theme      = Qt.binding(function () { return root.theme })
        }
    }
}
