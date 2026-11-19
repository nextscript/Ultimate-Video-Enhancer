// Wird als Tab-Seite sofort geladen und bekommt "host" (= app): darüber erreicht der Test das echte Plugin.
import QtQuick

Item {
    id: root
    property var host
    property int step: 0
    readonly property string target: "org.uveplayer.glsl-manager"

    function info() {
        var panels = host.plugins.panels
        for (var i = 0; i < panels.length; ++i) if (panels[i].key === target + "/main") return panels[i]
        return null
    }
    function shot(name) { host.saveWindowShot(host.selftestPath("glsl-" + name + ".png")) }
    function note(text) { console.warn("GLSLTEST " + text) }

    Timer {
        interval: 1500
        repeat: true
        running: root.host !== undefined && root.host !== null
        onTriggered: {
            var p = root.info()
            if (!p) {
                // Szenario "install": Paket über denselben Weg wie "Plugin installieren" vormerken
                var r = root.host.plugins.installPackageInfo(root.host.selftestPath("GLSLManager.zip"))
                root.note("install=" + JSON.stringify(r))
                stop()
                return
            }
            var c = p.controller, b = p.plugin
            switch (root.step++) {
            case 0: root.host.plugins.triggerAction("plugin:" + root.target + ":open"); break
            case 1:
                root.shot("tab")
                root.note("entries=" + JSON.stringify(c.entries.map(e => ({ l: e.label, e: e.error }))))
                root.note("shaders=" + root.host.shaders.activeShaders.length)
                c.entries.forEach(e => {
                    var full = c.entry(e.id)
                    root.note("validate " + e.label + ": " + JSON.stringify(c.validate(full)))
                })
                break
            case 2:
                if (c.entries.length) b.call("modal.open", { panel: "editor", data: { token: 1, id: c.entries[0].id } })
                root.note("modal.open err=" + b.lastError())
                break
            case 3: root.shot("modal-tab"); break
            case 4:
                if (c.entries.length) b.call("modal.open", { panel: "editor", scope: "video", mode: "movable", width: 760, height: 620, data: { token: 1, id: c.entries[0].id } })
                break
            case 5: root.shot("modal-large"); b.call("modal.close", { panel: "editor" }); break
            case 6:
                root.note("final entries=" + JSON.stringify(c.entries.map(e => ({ l: e.label, e: e.error }))))
                root.note("final shaders=" + root.host.shaders.activeShaders.length)
                root.shot("final")
                stop()
                break
            }
        }
    }
}
