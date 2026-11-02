import QtQuick
import QtQuick.Controls

// Rechtsklick auf das Videobild: Wiedergabe, Audiospur, Untertitel, Geschwindigkeit, Bild, Effekte, Diagnose
Menu {
    id: menu
    property var player
    property var effects       // UVE-Controller oder null
    property var act           // Aktionsfunktion aus Main.qml
    property string aspect: "fit"
    readonly property bool hasMedia: player && player.state !== 0

    MenuItem { text: player && player.paused ? qsTr("Wiedergabe") : qsTr("Pause"); enabled: menu.hasMedia; onTriggered: act("play.toggle") }
    MenuItem { text: qsTr("Stopp"); enabled: menu.hasMedia; onTriggered: act("play.stop") }
    MenuItem { text: qsTr("Vorheriger Titel"); onTriggered: act("play.prev") }
    MenuItem { text: qsTr("Nächster Titel"); onTriggered: act("play.next") }
    MenuSeparator {}

    Menu {
        id: audioMenu
        title: qsTr("Audiospur")
        enabled: menu.hasMedia
        Instantiator {
            model: menu.player ? menu.player.audioTracks : []
            delegate: MenuItem {
                required property var modelData
                text: modelData.label
                checkable: true
                checked: modelData.selected === true
                onTriggered: menu.player.setAudioTrack(modelData.id)
            }
            onObjectAdded: (i, o) => audioMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => audioMenu.removeItem(o)
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Audioverzögerung −50 ms"); onTriggered: act("audio.delayMinus") }
        MenuItem { text: qsTr("Audioverzögerung +50 ms"); onTriggered: act("audio.delayPlus") }
        MenuItem {
            text: qsTr("Verzögerung zurücksetzen (") + Math.round((menu.player ? menu.player.audioDelay : 0) * 1000) + qsTr(" ms)")
            enabled: !!menu.player && menu.player.audioDelay !== 0
            onTriggered: menu.player.setAudioDelay(0)
        }
        MenuItem { text: menu.player && menu.player.muted ? qsTr("Ton einschalten") : qsTr("Stumm"); onTriggered: act("mute") }
    }

    Menu {
        id: subMenu
        title: qsTr("Untertitel")
        enabled: menu.hasMedia
        Instantiator {
            model: menu.player ? menu.player.subtitleTracks : []
            delegate: MenuItem {
                required property var modelData
                text: modelData.label
                checkable: true
                checked: modelData.selected === true
                onTriggered: menu.player.setSubtitleTrack(modelData.id)
            }
            onObjectAdded: (i, o) => subMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => subMenu.removeItem(o)
        }
        MenuItem {
            text: qsTr("Aus")
            checkable: true
            checked: menu.player ? !menu.player.subtitleTracks.some(t => t.selected) : true
            onTriggered: menu.player.setSubtitleTrack(0)
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Untertiteldatei hinzufügen…"); onTriggered: act("sub.add") }
        MenuItem { text: qsTr("Verzögerung −50 ms"); onTriggered: act("sub.delayMinus") }
        MenuItem { text: qsTr("Verzögerung +50 ms"); onTriggered: act("sub.delayPlus") }
        MenuItem {
            text: qsTr("Verzögerung zurücksetzen (") + Math.round((menu.player ? menu.player.subDelay : 0) * 1000) + qsTr(" ms)")
            enabled: !!menu.player && menu.player.subDelay !== 0
            onTriggered: menu.player.setSubDelay(0)
        }
    }

    // YouTube/Twitch: verfügbare Auflösungen (über yt-dlp ermittelt)
    Menu {
        id: qualityMenu
        title: qsTr("Auflösung") + (app.streamQuality !== "" ? " (" + app.streamQuality + ")" : "")
        enabled: menu.hasMedia && (app.streamQualities.length > 0 || app.streamQualitiesLoading)
        MenuItem {
            text: qsTr("Beste (automatisch)")
            checkable: true
            checked: app.streamQuality === ""
            onTriggered: app.setStreamQuality("")
        }
        MenuSeparator {}
        Instantiator {
            model: app.streamQualities
            delegate: MenuItem {
                required property var modelData
                text: modelData.label
                checkable: true
                checked: app.streamQuality === modelData.id
                onTriggered: app.setStreamQuality(modelData.id)
            }
            onObjectAdded: (i, o) => qualityMenu.insertItem(i + 2, o)
            onObjectRemoved: (i, o) => qualityMenu.removeItem(o)
        }
        MenuItem { text: qsTr("Auflösungen werden ermittelt…"); enabled: false; visible: app.streamQualitiesLoading; height: visible ? implicitHeight : 0 }
    }

    Menu {
        id: speedMenu
        title: qsTr("Geschwindigkeit (") + (menu.player ? menu.player.speed.toFixed(2) : "1.00") + "×)"
        Repeater {
            model: [0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 3.0, 4.0]
            MenuItem {
                required property real modelData
                text: modelData.toFixed(2) + "×" + (modelData === 1 ? qsTr(" (normal)") : "")
                checkable: true
                checked: !!menu.player && Math.abs(menu.player.speed - modelData) < 0.01
                onTriggered: menu.player.setSpeed(modelData)
            }
        }
    }

    Menu {
        title: qsTr("Bild")
        Repeater {
            model: [["fit", qsTr("Einpassen")], ["fill", qsTr("Füllen (zuschneiden)")], ["stretch", qsTr("Strecken")], ["16:9", "16:9"], ["4:3", "4:3"], ["21:9", "21:9"]]
            MenuItem {
                required property var modelData
                text: modelData[1]
                checkable: true
                checked: menu.aspect === modelData[0]
                onTriggered: act("aspect.set", modelData[0])
            }
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Vollbild"); onTriggered: act("fullscreen") }
        MenuItem { text: qsTr("Bildschirmfoto"); enabled: menu.hasMedia; onTriggered: act("snapshot") }
        MenuItem { text: qsTr("Einzelbild vor"); enabled: menu.hasMedia; onTriggered: act("frame.step") }
        MenuItem { text: qsTr("Einzelbild zurück"); enabled: menu.hasMedia; onTriggered: act("frame.back") }
    }

    Menu {
        title: qsTr("Effekte (UVE)")
        enabled: menu.effects !== null && menu.effects !== undefined
        MenuItem { text: qsTr("Effekte an"); checkable: true; checked: menu.effects ? menu.effects.enabled : false; onTriggered: menu.effects.toggleEnabled() }
        MenuItem { text: qsTr("Vorher/Nachher"); checkable: true; checked: menu.effects ? menu.effects.compare : false; onTriggered: menu.effects.toggleCompare() }
        MenuSeparator {}
        Instantiator {
            model: menu.effects ? menu.effects.presets : []
            delegate: MenuItem {
                required property var modelData
                text: modelData ? modelData.name : ""
                checkable: true
                // beim Neuaufbau der Preset-Liste können menu.effects/modelData kurzzeitig null sein
                checked: { var e = menu ? menu.effects : null; return !!e && !!modelData && e.activePreset === modelData.name }
                onTriggered: if (menu && menu.effects && modelData) menu.effects.applyPreset(modelData.name)
            }
            onObjectAdded: (i, o) => presetsHolder.insertItem(i, o)
            onObjectRemoved: (i, o) => presetsHolder.removeItem(o)
        }
        Menu { id: presetsHolder; title: qsTr("Preset") }
        MenuItem { text: qsTr("Effekt-Panel öffnen"); onTriggered: act("effects.panel") }
    }

    MenuSeparator {}
    MenuItem {
        text: menu.player && menu.player.abLoopActive ? qsTr("A-B-Wiederholung: ") + menu.player.abLoopText : qsTr("A-B-Wiederholung: A setzen")
        enabled: menu.hasMedia
        onTriggered: act("abloop")
    }
    MenuItem { text: qsTr("Zu Zeit springen…"); enabled: menu.hasMedia && menu.player.duration > 0; onTriggered: act("goto") }
    MenuItem { text: qsTr("Medieninformationen…"); enabled: menu.hasMedia; onTriggered: act("mediainfo") }
    MenuItem { text: qsTr("Diagnose-Overlay"); onTriggered: act("diagnostics") }
    MenuItem { text: qsTr("Seitenleiste ein/aus"); onTriggered: act("sidebar.toggle") }
}
