import QtQuick

// Panel v4 (ab 2.2.0): NUR noch der native Chat (ChatNative.qml) – der
// eingebettete Browser-Chat (ChatWeb.qml) ist auf Nutzerwunsch entfernt und
// liegt nicht mehr im Paket; die DLL kennt keinen Embed-Modus mehr.
//
// Wichtig: Der Player weist nur die HIER deklarierten Properties zu (theme,
// controller, plugin) – und zwar erst nach dem Laden des Unterpaneels. Deshalb
// werden sie per Qt.binding weitergereicht, damit ChatNative.qml sie trotzdem
// bekommt.
Item {
    id: root
    property var controller: null
    property var plugin: null
    property var theme: null

    Loader {
        id: chatLoader
        objectName: "chatLoader"
        anchors.fill: parent
        source: "ChatNative.qml"
        onLoaded: {
            if (!item) return
            if ("controller" in item) item.controller = Qt.binding(function () { return root.controller })
            if ("plugin" in item)     item.plugin     = Qt.binding(function () { return root.plugin })
            if ("theme" in item)      item.theme      = Qt.binding(function () { return root.theme })
        }
    }
}
