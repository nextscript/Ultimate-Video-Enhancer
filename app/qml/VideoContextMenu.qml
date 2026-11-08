import QtQuick
import QtQuick.Controls

// Rechtsklick auf das Videobild: Wiedergabe, Audiospur, Untertitel, Geschwindigkeit, Bild, Effekte, Diagnose
// Die ID darf nicht "menu" heißen: MenuItem hat eine eigene Eigenschaft "menu" (das direkte Eltern-Menü), die in
// Delegates (Instantiator, Repeater) die ID der äußeren Komponente verdeckt – player/effects wären dort undefined.
Menu {
    id: videoMenu
    property var player
    property var effects       // UVE-Controller oder null
    property var act           // Aktionsfunktion aus Main.qml
    property string aspect: "fit"
    readonly property bool hasMedia: player && player.state !== 0

    MenuItem { text: player && player.paused ? qsTr("Wiedergabe") : qsTr("Pause"); enabled: videoMenu.hasMedia; onTriggered: act("play.toggle") }
    MenuItem { text: qsTr("Stopp"); enabled: videoMenu.hasMedia; onTriggered: act("play.stop") }
    MenuItem { text: qsTr("Vorheriger Titel"); onTriggered: act("play.prev") }
    MenuItem { text: qsTr("Nächster Titel"); onTriggered: act("play.next") }
    MenuSeparator {}

    Menu {
        id: audioMenu
        title: qsTr("Audiospur")
        enabled: videoMenu.hasMedia
        Instantiator {
            model: videoMenu.player ? videoMenu.player.audioTracks : []
            delegate: MenuItem {
                required property var modelData
                text: modelData.label
                checkable: true
                checked: modelData.selected === true
                onTriggered: videoMenu.player.setAudioTrack(modelData.id)
            }
            onObjectAdded: (i, o) => audioMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => audioMenu.removeItem(o)
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Audioverzögerung −50 ms"); onTriggered: act("audio.delayMinus") }
        MenuItem { text: qsTr("Audioverzögerung +50 ms"); onTriggered: act("audio.delayPlus") }
        MenuItem {
            text: qsTr("Verzögerung zurücksetzen (") + Math.round((videoMenu.player ? videoMenu.player.audioDelay : 0) * 1000) + qsTr(" ms)")
            enabled: !!videoMenu.player && videoMenu.player.audioDelay !== 0
            onTriggered: videoMenu.player.setAudioDelay(0)
        }
        MenuItem { text: videoMenu.player && videoMenu.player.muted ? qsTr("Ton einschalten") : qsTr("Stumm"); onTriggered: act("mute") }
    }

    Menu {
        id: subMenu
        title: qsTr("Untertitel")
        enabled: videoMenu.hasMedia
        Instantiator {
            model: videoMenu.player ? videoMenu.player.subtitleTracks : []
            delegate: MenuItem {
                required property var modelData
                text: modelData.label
                checkable: true
                checked: modelData.selected === true
                onTriggered: videoMenu.player.setSubtitleTrack(modelData.id)
            }
            onObjectAdded: (i, o) => subMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => subMenu.removeItem(o)
        }
        MenuItem {
            text: qsTr("Aus")
            checkable: true
            checked: videoMenu.player ? !videoMenu.player.subtitleTracks.some(t => t.selected) : true
            onTriggered: videoMenu.player.setSubtitleTrack(0)
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Untertiteldatei hinzufügen…"); onTriggered: act("sub.add") }
        MenuItem { text: qsTr("Verzögerung −50 ms"); onTriggered: act("sub.delayMinus") }
        MenuItem { text: qsTr("Verzögerung +50 ms"); onTriggered: act("sub.delayPlus") }
        MenuItem {
            text: qsTr("Verzögerung zurücksetzen (") + Math.round((videoMenu.player ? videoMenu.player.subDelay : 0) * 1000) + qsTr(" ms)")
            enabled: !!videoMenu.player && videoMenu.player.subDelay !== 0
            onTriggered: videoMenu.player.setSubDelay(0)
        }
    }

    // YouTube/Twitch: verfügbare Auflösungen (über yt-dlp ermittelt)
    Menu {
        id: qualityMenu
        title: qsTr("Auflösung") + (app.streamQuality !== "" ? " (" + app.streamQuality + ")" : "")
        enabled: videoMenu.hasMedia && (app.streamQualities.length > 0 || app.streamQualitiesLoading)
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
        title: qsTr("Geschwindigkeit (") + (videoMenu.player ? videoMenu.player.speed.toFixed(2) : "1.00") + "×)"
        Repeater {
            model: [0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 3.0, 4.0]
            MenuItem {
                required property real modelData
                text: modelData.toFixed(2) + "×" + (modelData === 1 ? qsTr(" (normal)") : "")
                checkable: true
                checked: !!videoMenu.player && Math.abs(videoMenu.player.speed - modelData) < 0.01
                onTriggered: videoMenu.player.setSpeed(modelData)
            }
        }
    }

    Menu {
        title: qsTr("Bild")
        Repeater {
            model: [["fit", qsTr("Einpassen")], ["stretch", qsTr("Strecken")], ["16:9", "16:9"], ["4:3", "4:3"], ["21:9", "21:9"]]
            MenuItem {
                required property var modelData
                text: modelData[1]
                checkable: true
                checked: videoMenu.aspect === modelData[0]
                onTriggered: act("aspect.set", modelData[0])
            }
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Vollbild"); onTriggered: act("fullscreen") }
        MenuItem { text: qsTr("Bildschirmfoto"); enabled: videoMenu.hasMedia; onTriggered: act("snapshot") }
        MenuItem { text: qsTr("Einzelbild vor"); enabled: videoMenu.hasMedia; onTriggered: act("frame.step") }
        MenuItem { text: qsTr("Einzelbild zurück"); enabled: videoMenu.hasMedia; onTriggered: act("frame.back") }
    }

    Menu {
        title: qsTr("Effekte (UVE)")
        enabled: videoMenu.effects !== null && videoMenu.effects !== undefined
        MenuItem { text: qsTr("Effekte an"); checkable: true; checked: videoMenu.effects ? videoMenu.effects.enabled : false; onTriggered: videoMenu.effects.toggleEnabled() }
        MenuItem { text: qsTr("Vorher/Nachher"); checkable: true; checked: videoMenu.effects ? videoMenu.effects.compare : false; onTriggered: videoMenu.effects.toggleCompare() }
        MenuSeparator {}
        Instantiator {
            model: videoMenu.effects ? videoMenu.effects.presets : []
            delegate: MenuItem {
                required property var modelData
                text: modelData ? modelData.name : ""
                checkable: true
                // beim Neuaufbau der Preset-Liste können videoMenu.effects/modelData kurzzeitig null sein
                checked: { var e = videoMenu ? videoMenu.effects : null; return !!e && !!modelData && e.activePreset === modelData.name }
                onTriggered: if (videoMenu && videoMenu.effects && modelData) videoMenu.effects.applyPreset(modelData.name)
            }
            onObjectAdded: (i, o) => presetsHolder.insertItem(i, o)
            onObjectRemoved: (i, o) => presetsHolder.removeItem(o)
        }
        Menu { id: presetsHolder; title: qsTr("Preset") }
        MenuItem { text: qsTr("Effekt-Panel öffnen"); onTriggered: act("effects.panel") }
    }

    MenuSeparator {}
    MenuItem {
        text: videoMenu.player && videoMenu.player.abLoopActive ? qsTr("A-B-Wiederholung: ") + videoMenu.player.abLoopText : qsTr("A-B-Wiederholung: A setzen")
        enabled: videoMenu.hasMedia
        onTriggered: act("abloop")
    }
    MenuItem { text: qsTr("Zu Zeit springen…"); enabled: videoMenu.hasMedia && videoMenu.player.duration > 0; onTriggered: act("goto") }
    MenuItem { text: qsTr("Medieninformationen…"); enabled: videoMenu.hasMedia; onTriggered: act("mediainfo") }
    MenuItem { text: qsTr("Diagnose-Overlay"); onTriggered: act("diagnostics") }
    MenuItem { text: qsTr("Seitenleiste ein/aus"); onTriggered: act("sidebar.toggle") }
}
