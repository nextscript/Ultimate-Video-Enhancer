import QtQml

QtObject {
    id: root
    property var plugin

    readonly property var variants: ({
        "open-player": { scope: "player", mode: "movable" },
        "open-video": { scope: "video", mode: "movable" },
        "open-sidebar": { scope: "sidebar", mode: "movable", width: 280, height: 220 },
        "open-fixed": { scope: "player", mode: "fixed" },
        "open-fill": { scope: "player", mode: "fill" }
    })

    property Connections events: Connections {
        target: root.plugin
        function onEvent(name, data) {
            if (name === "action.triggered" && root.variants[data.id]) {
                var args = Object.assign({ panel: "dialog", data: { variant: data.id } }, root.variants[data.id])
                root.plugin.call("modal.open", args)
            } else if (name === "modal.closed") {
                var s = root.plugin.settings()
                s.closedCount = (s.closedCount || 0) + 1
                root.plugin.saveSettings(s)
            }
        }
    }
}
