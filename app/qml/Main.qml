import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import UvePlayer.Native

ApplicationWindow {
    id: win
    width: 1280
    height: 780
    minimumWidth: 640
    minimumHeight: 400
    visible: false   // wird von PlayerApp::setWindow() mit gespeicherter Position/Größe angezeigt
    title: (player.mediaTitle ? player.mediaTitle + " – " : "") + qsTr("UVE Player")
    color: theme.bg

    readonly property var player: app.player
    readonly property var playlist: app.playlist
    readonly property bool fullscreen: visibility === Window.FullScreen
    property bool minimal: false
    property bool sidebarVisible: app.setting("showSidebar", true)
    property bool controlsVisible: true
    property bool diagnosticsVisible: false
    property string aspect: app.setting("keepAspect", "fit")
    property int modalCount: 0
    property int wasVisibility: Window.Windowed
    readonly property bool overlayControls: fullscreen || minimal
    readonly property bool textEditing: activeFocusItem !== null && activeFocusItem.cursorVisible !== undefined
    // Vergleichsansicht (Strg+Alt+U): UVE-Shader liefert ein doppelt breites Bild, splitComposite setzt es zusammen
    // Umschalten erst, wenn das Plugin den doppelt breiten Shader wirklich gesetzt hat (compareActive), nicht schon beim Wunsch
    readonly property bool splitView: effects !== null && effects.compareActive && effects.enabled && player.hasVideo
    onSplitViewChanged: { player.setSplitView(splitView); transitionFreeze.settled() }
    // Wunsch (Strg+Alt+U, Menü, Panel): sofort Standbild halten, bevor sich irgendetwas am Bild ändert
    readonly property bool splitWanted: effects !== null && effects.compare && effects.enabled && player.hasVideo
    onSplitWantedChanged: transitionFreeze.begin()
    readonly property var effects: {
        var ps = app.plugins ? app.plugins.plugins : []
        for (var i = 0; i < ps.length; ++i)
            if (ps[i].id === "org.nextscript.ultimate-video-enhancer" && ps[i].controller) return ps[i].controller
        return null
    }

    Theme { id: theme; mode: app.theme }
    palette {
        window: theme.panel; windowText: theme.text; base: theme.panel2; alternateBase: theme.panel
        text: theme.text; button: theme.panel2; buttonText: theme.text; brightText: "white"
        highlight: theme.accent; highlightedText: "#111111"; toolTipBase: theme.panel2; toolTipText: theme.text
        placeholderText: theme.muted; mid: theme.line; dark: theme.line; light: theme.panel2
    }

    // Menütitel "A&udio" → "A<u>u</u>dio"; "&&" ist ein echtes "&"
    function menuTitleMarkup(title) {
        var out = ""
        for (var i = 0; i < title.length; ++i) {
            var c = title[i]
            var mnemonic = c === "&" && i + 1 < title.length && title[i + 1] !== "&"
            if (c === "&" && i + 1 < title.length) c = title[++i]
            var escaped = c === "&" ? "&amp;" : c === "<" ? "&lt;" : c === ">" ? "&gt;" : c
            out += mnemonic ? "<u>" + escaped + "</u>" : escaped
        }
        return out
    }

    // ------------------------------------------------------------- Aktionen
    function osd(text, ms) { osdLabel.text = text; osdBox.opacity = 1; osdTimer.interval = ms || 1500; osdTimer.restart() }
    function seekBy(s) { if (player.state !== 0) { player.seekRelative(s); osd((s < 0 ? "◀◀ " : "▶▶ ") + app.formatTime(Math.max(0, player.position + s))) } }
    function setVolume(v) { player.setVolume(v); if (player.muted && v > 0) player.setMuted(false); osd(qsTr("Lautstärke ") + Math.round(Math.max(0, Math.min(150, v))) + " %") }
    function setSpeed(s) { player.setSpeed(Math.round(s * 100) / 100); osd(qsTr("Geschwindigkeit ") + (Math.round(s * 100) / 100).toFixed(2) + "×") }
    function toggleFullscreen() {
        if (fullscreen) visibility = wasVisibility === Window.Maximized ? Window.Maximized : Window.Windowed
        else { wasVisibility = visibility; visibility = Window.FullScreen }
    }
    // Seite eines Plugins zeigen, dort wo der Benutzer sie hingelegt hat. Ausgeblendete Seiten blendet nur eine Aktion des Benutzers wieder ein.
    function showPanel(key, userAction) {
        var p = (app.plugins ? app.plugins.panels : []).find(x => x.key === key)
        if (!p) return false
        if (p.location === "settings") return settingsDialog.openPluginPage(key)
        if (p.location === "hidden") {
            if (!userAction) { osd(qsTr("Diese Seite ist ausgeblendet. Einblenden unter Einstellungen → Plugins."), 3000); return false }
            app.plugins.setPanelPlacement(key, "sidebar")
        } else if (p.location !== "sidebar") {
            return false
        }
        sidebarVisible = true
        app.setSetting("showSidebar", true)
        return tabs.selectPanel(key)
    }
    function effectsPanel() {
        return (app.plugins ? app.plugins.panels : []).find(p => p.movable && p.pluginId === "org.nextscript.ultimate-video-enhancer")
    }
    function showEffectsPanel() {
        var p = effectsPanel()
        if (p) { showPanel(p.key, true); return }
        sidebarVisible = true
        app.setSetting("showSidebar", true)
        tabs.selectTab("Effekte")
    }
    function act(id, arg) {
        switch (id) {
        case "play.toggle":
            if (player.state === 0 && playlist.count > 0) app.playIndex(Math.max(0, playlist.currentIndex))
            else if (player.state === 0) openFileDialog.open()
            else player.togglePause()
            break
        case "play.stop": app.stop(); osd(qsTr("Stopp")); break
        case "play.next": app.next(); break
        case "play.prev": app.previous(); break
        case "seek.back": seekBy(-app.setting("seekStep", 10)); break
        case "seek.fwd": seekBy(app.setting("seekStep", 10)); break
        case "seek.backShort": seekBy(-3); break
        case "seek.fwdShort": seekBy(3); break
        case "seek.backLong": seekBy(-60); break
        case "seek.fwdLong": seekBy(60); break
        case "frame.step": player.frameStep(); osd(qsTr("Einzelbild ▶")); break
        case "frame.back": player.frameBackStep(); osd(qsTr("◀ Einzelbild")); break
        case "speed.up": setSpeed(player.speed + (player.speed >= 2 ? 0.25 : 0.1)); break
        case "speed.down": setSpeed(Math.max(0.25, player.speed - (player.speed > 2 ? 0.25 : 0.1))); break
        case "speed.reset": setSpeed(1); break
        case "abloop": player.cycleAbLoop(); Qt.callLater(() => osd(player.abLoopActive ? qsTr("A-B: ") + player.abLoopText : qsTr("A-B-Wiederholung aus"))); break
        case "repeat.cycle": var r = playlist.cycleRepeat(); osd(r === "none" ? qsTr("Wiederholen: aus") : (r === "all" ? qsTr("Wiederholen: alle") : qsTr("Wiederholen: eins"))); break
        case "shuffle.toggle": playlist.shuffle = !playlist.shuffle; osd(playlist.shuffle ? qsTr("Zufall: an") : qsTr("Zufall: aus")); break
        case "goto": if (player.duration > 0) gotoDialog.open(); break
        case "volume.up": setVolume(player.volume + 5); break
        case "volume.down": setVolume(player.volume - 5); break
        case "mute": player.setMuted(!player.muted); osd(player.muted ? qsTr("Ton an") : qsTr("Stumm")); break
        case "audio.cycle": player.cycleAudio(); Qt.callLater(() => { var t = player.audioTracks.find(x => x.selected); osd(t ? qsTr("Audio: ") + t.label : qsTr("Audio aus")) }); break
        case "audio.delayMinus": player.setAudioDelay(player.audioDelay - 0.05); osd(qsTr("Audioverzögerung ") + Math.round((player.audioDelay - 0.05) * 1000) + qsTr(" ms")); break
        case "audio.delayPlus": player.setAudioDelay(player.audioDelay + 0.05); osd(qsTr("Audioverzögerung ") + Math.round((player.audioDelay + 0.05) * 1000) + qsTr(" ms")); break
        case "sub.cycle": player.cycleSubtitle(); Qt.callLater(() => { var t = player.subtitleTracks.find(x => x.selected); osd(t ? qsTr("Untertitel: ") + t.label : qsTr("Untertitel aus")) }); break
        case "sub.delayMinus": player.setSubDelay(player.subDelay - 0.05); osd(qsTr("Untertitelverzögerung ") + Math.round((player.subDelay - 0.05) * 1000) + qsTr(" ms")); break
        case "sub.delayPlus": player.setSubDelay(player.subDelay + 0.05); osd(qsTr("Untertitelverzögerung ") + Math.round((player.subDelay + 0.05) * 1000) + qsTr(" ms")); break
        case "sub.add": if (player.state !== 0) subtitleDialog.open(); break
        case "fullscreen": toggleFullscreen(); break
        case "aspect.cycle":
            var modes = ["fit", "stretch", "16:9", "4:3", "21:9"]
            act("aspect.set", modes[(modes.indexOf(aspect) + 1) % modes.length]); break
        case "aspect.set":
            aspect = arg; player.setAspect(arg); app.setSetting("keepAspect", arg)
            osd(qsTr("Bild: ") + ({ "fit": qsTr("Einpassen"), "stretch": qsTr("Strecken") }[arg] || arg)); break
        case "snapshot": if (player.state !== 0) app.snapshot(); break
        case "diagnostics": diagnosticsVisible = !diagnosticsVisible; break
        case "effects.toggle": if (effects) effects.toggleEnabled(); else osd(qsTr("Effekt-Plugin nicht aktiv")); break
        case "effects.compare": if (effects) effects.toggleCompare(); else osd(qsTr("Effekt-Plugin nicht aktiv")); break
        case "effects.auto": if (effects) effects.toggleAuto(); else osd(qsTr("Effekt-Plugin nicht aktiv")); break
        case "effects.panel": win.showEffectsPanel(); break
        case "open.file": openFileDialog.open(); break
        case "open.folder": openFolderDialog.open(); break
        case "open.url": urlDialog.playNow = true; urlDialog.open(); break
        case "open.disc": discDialog.openTab(0); break
        case "open.discImage": discDialog.openTab(1); break
        case "disc.menu": if (!app.disc.navigate("menu")) osd(app.disc.active ? qsTr("Disc-Menü nicht verfügbar") : qsTr("Keine DVD oder Blu-ray aktiv")); break
        case "playlist.save": if (playlist.count > 0) savePlaylistDialog.open(); break
        case "sidebar.toggle": sidebarVisible = !sidebarVisible; app.setSetting("showSidebar", sidebarVisible); break
        case "mediainfo": if (player.state !== 0) mediaInfoDialog.open(); break
        case "settings": settingsDialog.open(); break
        case "quit": Qt.quit(); break
        case "minimal": minimal = !minimal; break
        default:
            // Menüeinträge und Tastenkürzel von Plugins: "plugin:<plugin-id>:<eintrag>"
            if (String(id).indexOf("plugin:") === 0 && app.plugins) app.plugins.triggerAction(id)
        }
    }

    // Tastenkürzel (editierbar, siehe Einstellungen → Tastenkürzel)
    Repeater {
        model: app.hotkeys.items
        delegate: Item {
            required property var modelData
            Shortcut {
                sequence: modelData.shortcut
                enabled: modelData.shortcut !== "" && win.modalCount === 0
                         && !(win.textEditing && modelData.shortcut.indexOf("Ctrl") < 0 && modelData.shortcut.indexOf("Alt") < 0)
                         && !(app.disc.menuActive && win.discMenuKeys.indexOf(modelData.shortcut) >= 0)
                context: Qt.ApplicationShortcut
                onActivated: win.act(modelData.id)
            }
        }
    }
    // Disc-Menü aktiv (DVD/Blu-ray): Pfeiltasten, Enter und Rücktaste steuern das Menü statt Spulen/Lautstärke.
    // Danach gelten wieder die normalen (frei belegbaren) Tastenkürzel.
    readonly property var discMenuKeys: app.disc.menuKeys()
    Repeater {
        model: win.discMenuKeys
        delegate: Item {
            required property string modelData
            Shortcut {
                sequence: modelData
                enabled: app.disc.menuActive && win.modalCount === 0 && !win.textEditing
                context: Qt.ApplicationShortcut
                onActivated: app.disc.navigate(app.disc.menuKeyAction(modelData))
            }
        }
    }
    Shortcut { sequence: "Esc"; enabled: win.modalCount === 0; onActivated: { if (win.fullscreen) win.toggleFullscreen(); else if (win.minimal) win.minimal = false } }
    Shortcut { sequence: "F1"; enabled: win.modalCount === 0; onActivated: { settingsDialog.open(); settingsDialog.page = 7 } }

    // ------------------------------------------------------------- Menüleiste
    menuBar: MenuBar {
        id: mainMenuBar
        visible: !win.overlayControls
        height: visible ? implicitHeight : 0
        // Eigene Beschriftung: Qts Titel-Label zeichnet nach einem Sprachwechsel nichts mehr, wenn der sichtbare Text gleich bleibt
        // und nur das Tastenkürzel wandert ("&Audio" ↔ "A&udio", "&Plugins" ↔ "Pl&ugins"). Alt+Buchstabe hängt an barItem.text.
        delegate: MenuBarItem {
            id: barItem
            contentItem: Text {
                text: win.menuTitleMarkup(barItem.text)
                textFormat: Text.StyledText
                font: barItem.font
                color: barItem.down || barItem.highlighted ? barItem.palette.highlightedText : barItem.palette.text
                verticalAlignment: Text.AlignVCenter
            }
        }
        Menu {
            title: qsTr("&Datei")
            MenuItem { text: qsTr("Datei öffnen…"); onTriggered: act("open.file") }
            MenuItem { text: qsTr("Ordner öffnen…"); onTriggered: act("open.folder") }
            MenuItem { text: qsTr("Netzwerkstream öffnen…"); onTriggered: act("open.url") }
            MenuItem { text: qsTr("Disc öffnen…"); onTriggered: act("open.disc") }
            MenuItem { text: qsTr("Disc-Image/Disc-Ordner öffnen…"); onTriggered: act("open.discImage") }
            Menu {
                id: recentMenu
                title: qsTr("Zuletzt geöffnet")
                enabled: app.recentFiles.length > 0
                Instantiator {
                    model: app.recentFiles
                    delegate: MenuItem { required property string modelData; text: modelData.split(/[\\/]/).pop() || modelData; onTriggered: app.openPaths([modelData], true) }
                    onObjectAdded: (i, o) => recentMenu.insertItem(i, o)
                    onObjectRemoved: (i, o) => recentMenu.removeItem(o)
                }
                MenuSeparator {}
                MenuItem { text: qsTr("Liste leeren"); onTriggered: app.clearRecentFiles() }
            }
            MenuSeparator {}
            MenuItem { text: qsTr("Wiedergabeliste speichern…"); enabled: playlist.count > 0; onTriggered: act("playlist.save") }
            MenuSeparator {}
            MenuItem { text: qsTr("Beenden"); onTriggered: act("quit") }
        }
        Menu {
            title: qsTr("&Wiedergabe")
            MenuItem { text: player.paused || player.state === 0 ? qsTr("Wiedergabe") : qsTr("Pause"); onTriggered: act("play.toggle") }
            MenuItem { text: qsTr("Stopp"); onTriggered: act("play.stop") }
            MenuItem { text: qsTr("Vorheriger Titel"); onTriggered: act("play.prev") }
            MenuItem { text: qsTr("Nächster Titel"); onTriggered: act("play.next") }
            MenuSeparator {}
            MenuItem { text: qsTr("Zu Zeit springen…"); enabled: player.duration > 0; onTriggered: act("goto") }
            MenuItem { text: qsTr("Einzelbild vor"); onTriggered: act("frame.step") }
            MenuItem { text: qsTr("Einzelbild zurück"); onTriggered: act("frame.back") }
            MenuItem { text: qsTr("Schneller"); onTriggered: act("speed.up") }
            MenuItem { text: qsTr("Langsamer"); onTriggered: act("speed.down") }
            MenuItem { text: qsTr("Normale Geschwindigkeit"); onTriggered: act("speed.reset") }
            MenuSeparator {}
            MenuItem { text: qsTr("A-B-Wiederholung"); onTriggered: act("abloop") }
            MenuItem { text: playlist.repeat === "none" ? qsTr("Wiederholen: aus") : (playlist.repeat === "all" ? qsTr("Wiederholen: alle") : qsTr("Wiederholen: eins")); onTriggered: act("repeat.cycle") }
            MenuItem { text: qsTr("Zufallswiedergabe"); checkable: true; checked: playlist.shuffle; onTriggered: act("shuffle.toggle") }
            MenuSeparator {}
            DiscNavigationMenu { theme: theme }
        }
        Menu {
            title: qsTr("&Video")
            MenuItem { text: qsTr("Vollbild"); onTriggered: act("fullscreen") }
            MenuItem { text: qsTr("Seitenverhältnis wechseln"); onTriggered: act("aspect.cycle") }
            MenuItem { text: qsTr("Bildschirmfoto"); onTriggered: act("snapshot") }
            MenuItem { text: qsTr("Diagnose-Overlay"); checkable: true; checked: win.diagnosticsVisible; onTriggered: act("diagnostics") }
        }
        Menu {
            id: audioMenu
            title: qsTr("&Audio")
            Instantiator {
                model: player.audioTracks
                delegate: MenuItem { required property var modelData; text: modelData.label; checkable: true; checked: modelData.selected === true; onTriggered: player.setAudioTrack(modelData.id) }
                onObjectAdded: (i, o) => audioMenu.insertItem(i, o)
                onObjectRemoved: (i, o) => audioMenu.removeItem(o)
            }
            MenuSeparator {}
            MenuItem { text: qsTr("Lauter"); onTriggered: act("volume.up") }
            MenuItem { text: qsTr("Leiser"); onTriggered: act("volume.down") }
            MenuItem { text: qsTr("Stumm"); checkable: true; checked: player.muted; onTriggered: act("mute") }
            MenuItem { text: qsTr("Audioverzögerung −50 ms"); onTriggered: act("audio.delayMinus") }
            MenuItem { text: qsTr("Audioverzögerung +50 ms"); onTriggered: act("audio.delayPlus") }
        }
        Menu {
            id: subsMenu
            title: qsTr("&Untertitel")
            Instantiator {
                model: player.subtitleTracks
                delegate: MenuItem { required property var modelData; text: modelData.label; checkable: true; checked: modelData.selected === true; onTriggered: player.setSubtitleTrack(modelData.id) }
                onObjectAdded: (i, o) => subsMenu.insertItem(i, o)
                onObjectRemoved: (i, o) => subsMenu.removeItem(o)
            }
            MenuItem { text: qsTr("Aus"); onTriggered: player.setSubtitleTrack(0) }
            MenuSeparator {}
            MenuItem { text: qsTr("Untertiteldatei hinzufügen…"); onTriggered: act("sub.add") }
            MenuItem { text: qsTr("Verzögerung −50 ms"); onTriggered: act("sub.delayMinus") }
            MenuItem { text: qsTr("Verzögerung +50 ms"); onTriggered: act("sub.delayPlus") }
        }
        Menu {
            title: qsTr("&Effekte")
            MenuItem { text: qsTr("Effekte an"); enabled: win.effects !== null; checkable: true; checked: win.effects ? win.effects.enabled : false; onTriggered: act("effects.toggle") }
            MenuItem { text: qsTr("Vorher/Nachher-Vergleich"); enabled: win.effects !== null; checkable: true; checked: win.effects ? win.effects.compare : false; onTriggered: act("effects.compare") }
            MenuItem { text: qsTr("Auto-Scene-Match"); enabled: win.effects !== null; checkable: true; checked: win.effects ? win.effects.autoOn : false; onTriggered: act("effects.auto") }
            MenuItem { text: qsTr("Effekt-Panel öffnen"); enabled: win.effects !== null; onTriggered: act("effects.panel") }
            MenuItem { text: qsTr("LUT-Galerie…"); enabled: win.effects !== null; onTriggered: { act("effects.panel"); Qt.callLater(() => win.effects.requestGallery()) } }
            MenuItem { text: qsTr("Beispielprofil von GitHub laden (My_Profile.json)"); enabled: win.effects !== null; onTriggered: win.effects.loadExampleProfile() }
            MenuItem { text: qsTr("LUT-Paket von GitHub laden"); enabled: win.effects !== null; onTriggered: win.effects.downloadLutPackage() }
            MenuItem { text: win.effects ? qsTr("Ultimate Video Enhancer aktiv") : qsTr("Ultimate Video Enhancer nicht aktiv"); enabled: false }
        }
        Menu {
            id: pluginsMenu
            title: qsTr("&Plugins")
            MenuItem { text: qsTr("Plugins verwalten…"); onTriggered: settingsDialog.openPlugins() }
            MenuSeparator { visible: pluginMenuItems.count > 0; height: visible ? implicitHeight : 0 }
            Instantiator {
                id: pluginMenuItems
                model: app.plugins ? app.plugins.menuItems : []
                delegate: MenuItem { required property var modelData; text: modelData.title; onTriggered: app.plugins.triggerAction(modelData.actionId) }
                onObjectAdded: (i, o) => pluginsMenu.insertItem(2 + i, o)
                onObjectRemoved: (i, o) => pluginsMenu.removeItem(o)
            }
            MenuItem { text: qsTr("Safe Mode beenden"); visible: app.safeMode; height: visible ? implicitHeight : 0; onTriggered: app.resetSafeMode() }
        }
        Menu {
            title: qsTr("A&nsicht")
            MenuItem { text: qsTr("Seitenleiste"); checkable: true; checked: win.sidebarVisible; onTriggered: act("sidebar.toggle") }
            // Einzelne Plugin-Seiten ein- und ausblenden, ohne die ganze Leiste umzuschalten
            Menu {
                id: panelsMenu
                title: qsTr("Plugin-Seiten")
                enabled: count > 0
                Instantiator {
                    model: (app.plugins ? app.plugins.panels : []).filter(p => p.movable)
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.title
                        checkable: true
                        checked: modelData.location === "sidebar"
                        onTriggered: {
                            var key = modelData.key, placement = modelData.location === "sidebar" ? "hidden" : "sidebar"
                            // Später ausführen: Die Änderung baut dieses Menü neu auf
                            Qt.callLater(() => app.plugins.setPanelPlacement(key, placement))
                        }
                    }
                    onObjectAdded: (i, o) => panelsMenu.insertItem(i, o)
                    onObjectRemoved: (i, o) => panelsMenu.removeItem(o)
                }
            }
            MenuItem { text: qsTr("Minimale Oberfläche"); checkable: true; checked: win.minimal; onTriggered: act("minimal") }
            MenuItem { text: qsTr("Medieninformationen…"); onTriggered: act("mediainfo") }
            MenuItem { text: qsTr("Helles Farbschema"); checkable: true; checked: app.theme === "light"; onTriggered: app.theme = (app.theme === "light" ? "dark" : "light") }
            MenuSeparator {}
            MenuItem { text: qsTr("Einstellungen…"); onTriggered: act("settings") }
        }
        Menu {
            title: qsTr("&Hilfe")
            MenuItem { text: qsTr("Tastenkürzel"); onTriggered: { settingsDialog.open(); settingsDialog.page = 7 } }
            MenuItem { text: qsTr("Über UVE Player"); onTriggered: aboutDialog.open() }
        }
    }

    // ------------------------------------------------------------- Layout
    Item {
        anchors.fill: parent

        // Safe-Mode-Hinweis
        Rectangle {
            id: safeBanner
            visible: app.safeMode
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: visible ? 34 : 0
            color: "#3a2a12"
            z: 5
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 6
                Label { text: qsTr("Safe Mode – Plugins nicht geladen. ") + app.safeModeReason; color: "#ffd9a0"; Layout.fillWidth: true; elide: Text.ElideRight }
                Button { text: qsTr("Plugins wieder aktivieren"); onClicked: app.resetSafeMode() }
            }
        }

        Item {
            id: stage
            anchors { left: parent.left; top: safeBanner.bottom; bottom: parent.bottom
                      right: sidebar.visible ? sidebar.left : parent.right }
            Rectangle { anchors.fill: parent; color: "black" }

            // Bereich für Plugin-Fenster mit scope "video": das Bild ohne die Bedienleiste
            Item {
                id: videoScope
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: parent.height - (win.overlayControls ? 0 : controls.height)
            }

            // Sichtbare Videofläche (Bezug für Maus, Startbildschirm, Overlays)
            Item {
                id: videoArea
                anchors { left: parent.left; right: parent.right; top: parent.top; bottom: parent.bottom
                          bottomMargin: win.overlayControls ? 0 : controls.height }
            }

            // Videoebene: mpv-Bild + Vorher/Nachher-Zusammensetzung (Quelle für das Übergangs-Standbild)
            Item {
                id: videoLayer
                anchors.fill: videoArea

            // Nur das Videobild zuschneiden (im Vergleichsmodus ist die mpv-Ausgabe doppelt so breit) –
            // nicht die ganze Bühne, sonst wird die überstehende Steuerleiste (Play-Knopf) im Vollbild abgeschnitten
            Item {
                id: videoClip
                anchors.fill: parent
                clip: true
                MpvVideo {
                    id: video
                    controller: app.player
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    // Vergleich (Strg+Alt+U): mpv rendert links Original | rechts Effekte nebeneinander
                    width: win.splitView ? videoArea.width * 2 : videoArea.width
                }
            }

            // Vorher/Nachher zusammensetzen: je Bildpunkt die passende Hälfte – der Schieber ist nur ein Uniform,
            // mpv bleibt beim Ziehen unverändert (sonst baut mpv bei jeder Änderung seine Render-Kette neu auf → Ruckeln)
            ShaderEffect {
                id: splitComposite
                anchors.fill: parent
                visible: win.splitView
                readonly property var dims: player.osdDimensions
                property variant source: video
                property real split: compareOverlay.shown
                property real flipY: 0
                readonly property var veq: player.videoEq
                property vector4d eq: Qt.vector4d((veq.brightness || 0) / 100, (veq.contrast || 0) / 100,
                                                  (veq.saturation || 0) / 100, (veq.hue || 0) / 100)
                // doppelt breites Bild in der mpv-Ausgabe (normiert)
                property rect combRect: dims && dims.w > 0 && dims.h > 0
                    ? Qt.rect((dims.ml || 0) / dims.w, (dims.mt || 0) / dims.h,
                              (dims.w - (dims.ml || 0) - (dims.mr || 0)) / dims.w, (dims.h - (dims.mt || 0) - (dims.mb || 0)) / dims.h)
                    : Qt.rect(0, 0, 1, 1)
                // Videofläche in der Anzeige (normiert): eine Hälfte, horizontal zentriert
                property rect normRect: Qt.rect((1 - combRect.width) / 2, combRect.y, combRect.width, combRect.height)
                fragmentShader: "qrc:/uve/shaders/splitview.frag.qsb"
            }
            }

            // Übergang Vergleich an/aus: Beim Umschalten ändern sich Shader (mpv baut neu auf), Seitenverhältnis und
            // Layout nicht im selben Frame – dazwischen wären kurz zwei Bilder zu sehen. Daher das letzte Bild als
            // Standbild halten, bis mpv das neue Bild liefert, und dann sanft ausblenden.
            // Eine ständig mitlaufende Kopie kostet bei jedem Videobild einen Durchgang über die ganze Fläche (GPU-Last),
            // daher: Außerhalb des Vergleichs ist die Quelle abgeschaltet und wird erst beim Wunsch einmalig aufgenommen –
            // das Plugin setzt den Shader frühestens 40 ms später (UveController::scheduleShader), das Bild liegt also davor.
            // Nur im Vergleich läuft sie mit: Effekte-aus schaltet die Ansicht dort ohne Verzögerung um.
            // Einfrieren und im selben Frame voll zeigen – kein Einblenden, durch das der Zwischenzustand durchscheinen
            // würde. Ausblenden erst, wenn mpv das neue Bild liefert.
            ShaderEffectSource {
                id: transitionFreeze
                anchors.fill: videoArea
                sourceItem: videoLayer
                live: false
                visible: false
                property bool frozen: false
                property bool capturing: false
                opacity: 0.002
                NumberAnimation { id: freezeFade; target: transitionFreeze; property: "opacity"; to: 0.002; duration: 160
                                  onFinished: transitionFreeze.finish() }
                function hold() {
                    capturing = false
                    frozen = true
                    opacity = 1
                }
                function begin() {
                    if (frozen) { freezeHold.restart(); return }   // schon eingefroren: nur verlängern
                    if (capturing) return                          // Aufnahme läuft bereits
                    freezeFade.stop()
                    if (live) {                                    // im Vergleich: letztes gezeigtes Bild festhalten
                        live = false
                        hold()
                    } else {                                       // sonst jetzt einmalig aufnehmen
                        capturing = true
                        visible = true
                        scheduleUpdate()
                    }
                    freezeHold.interval = 1000                     // Sicherheitsgrenze, falls nichts umschaltet
                    freezeHold.restart()
                }
                onScheduledUpdateCompleted: if (capturing) hold()
                function finish() {
                    frozen = false
                    capturing = false
                    live = win.splitWanted
                    visible = win.splitWanted
                }
                function settled() {
                    if (!frozen) return
                    freezeHold.interval = 400                      // mpv-Neuaufbau + neues Bild abwarten
                    freezeHold.restart()
                }
                Timer { id: freezeHold; onTriggered: freezeFade.restart() }
            }

            // Audio ohne Bild
            Column {
                anchors.centerIn: videoArea
                visible: player.state !== 0 && !player.hasVideo
                spacing: 14
                Label { text: "♪"; font.pixelSize: 96; color: theme.accent; anchors.horizontalCenter: parent.horizontalCenter }
                Label { text: player.mediaTitle; color: "#bbb"; font.pixelSize: 18; anchors.horizontalCenter: parent.horizontalCenter }
            }

            // Startbildschirm
            Rectangle {
                anchors.fill: videoArea
                visible: player.state === 0
                gradient: Gradient {
                    GradientStop { position: 0; color: theme.dark ? "#1a1f27" : "#e9ebef" }
                    GradientStop { position: 1; color: theme.bg }
                }
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 8
                    Image { source: "qrc:/qt/qml/UvePlayer/resources/icon.png"; sourceSize.width: 96; sourceSize.height: 96; Layout.alignment: Qt.AlignHCenter }
                    Label { text: qsTr("UVE Player"); font.pixelSize: 26; font.bold: true; color: theme.text; Layout.alignment: Qt.AlignHCenter }
                    Label { text: qsTr("Datei hierher ziehen oder über das Menü „Datei“ öffnen"); color: theme.muted; Layout.alignment: Qt.AlignHCenter }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        color: theme.muted; font.pixelSize: 12
                        text: win.effects ? qsTr("Ultimate Video Enhancer aktiv · Strg+Alt+E Effekte an/aus") : (app.safeMode ? qsTr("Safe Mode") : qsTr("Ultimate Video Enhancer nicht aktiv"))
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        visible: player.state === 6 || player.errorString.length > 0
                        color: theme.error
                        text: player.errorString ? qsTr("Letzter Fehler: ") + player.errorString : ""
                    }
                }
            }

            // Maus: Klick = Pause, Doppelklick = Vollbild, Rechtsklick = Kontextmenü, Rad = Lautstärke
            MouseArea {
                id: stageMouse
                anchors.fill: videoArea
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true
                cursorShape: win.overlayControls && !win.controlsVisible ? Qt.BlankCursor : Qt.ArrowCursor
                onPositionChanged: win.poke()
                onClicked: (m) => {
                    if (m.button === Qt.RightButton) { clickTimer.stop(); videoMenu.popup(); return }
                    if (player.state !== 0) clickTimer.restart()
                }
                onDoubleClicked: (m) => { if (m.button === Qt.LeftButton) { clickTimer.stop(); win.toggleFullscreen() } }
                onWheel: (w) => win.setVolume(player.volume + (w.angleDelta.y > 0 ? 5 : -5))
                Timer {
                    id: clickTimer
                    interval: 230
                    onTriggered: { player.togglePause(); Qt.callLater(() => win.osd(player.paused ? qsTr("❚❚ Pause") : qsTr("▶ Wiedergabe"), 900)) }
                }
            }

            // Vorher/Nachher-Trenner (Strg+Alt+U) wie im UVE-Userscript: ziehbarer Knopf ◀▶ über dem Video,
            // links Original, rechts mit Effekten. Die Position steuert nur die Zusammensetzung (splitComposite).
            Item {
                id: compareOverlay
                readonly property var dims: player.osdDimensions
                readonly property real dw: dims && dims.w > 0 ? dims.w : 0
                readonly property real dh: dims && dims.h > 0 ? dims.h : 0
                // Videofläche ohne schwarze Ränder (osd-dimensions in Ausgabepixeln → Item-Koordinaten);
                // im Vergleichsmodus die zusammengesetzte Fläche (eine Hälfte, zentriert)
                x: win.splitView ? videoArea.x + splitComposite.normRect.x * videoArea.width
                                 : videoArea.x + (dw > 0 ? (dims.ml || 0) / dw * video.width : 0)
                y: win.splitView ? videoArea.y + splitComposite.normRect.y * videoArea.height
                                 : videoArea.y + (dh > 0 ? (dims.mt || 0) / dh * video.height : 0)
                width: win.splitView ? splitComposite.normRect.width * videoArea.width
                                     : (dw > 0 ? video.width * (dw - (dims.ml || 0) - (dims.mr || 0)) / dw : video.width)
                height: win.splitView ? splitComposite.normRect.height * videoArea.height
                                      : (dh > 0 ? video.height * (dh - (dims.mt || 0) - (dims.mb || 0)) / dh : video.height)
                visible: win.effects !== null && win.effects.compare && player.state !== 0 && player.hasVideo
                property real split: win.effects ? win.effects.values.compareSplit : 0.5
                property real dragSplit: -1
                readonly property real shown: dragSplit >= 0 ? dragSplit : split

                Rectangle {
                    x: Math.round(compareOverlay.shown * compareOverlay.width) - 1
                    width: 2; height: parent.height
                    color: "white"
                    layer.enabled: false
                }
                Repeater {
                    model: [{ t: qsTr("VORHER"), left: true }, { t: qsTr("NACHHER"), left: false }]
                    delegate: Rectangle {
                        required property var modelData
                        x: modelData.left ? 10 : compareOverlay.width - width - 10
                        y: 10
                        width: lbl.implicitWidth + 16; height: lbl.implicitHeight + 6
                        radius: 4; color: "#a6000000"
                        Label { id: lbl; anchors.centerIn: parent; text: parent.modelData.t; color: "white"; font.bold: true; font.pixelSize: 12 }
                    }
                }
                Rectangle {
                    id: compareKnob
                    width: 36; height: 36; radius: 18
                    x: compareOverlay.shown * compareOverlay.width - width / 2
                    y: compareOverlay.height / 2 - height / 2
                    color: "white"
                    border.color: "#55000000"; border.width: 1
                    Label { anchors.centerIn: parent; text: "◀▶"; color: "#222"; font.bold: true; font.pixelSize: 10 }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        cursorShape: Qt.SizeHorCursor
                        preventStealing: true
                        onPressed: (m) => { m.accepted = true; compareOverlay.dragSplit = compareOverlay.split }
                        onPositionChanged: (m) => {
                            if (!pressed || compareOverlay.width <= 0) return
                            var p = mapToItem(compareOverlay, m.x, m.y)
                            compareOverlay.dragSplit = Math.max(0, Math.min(1, p.x / compareOverlay.width))
                            win.effects.setCompareSplit(compareOverlay.dragSplit)
                        }
                        onReleased: compareOverlay.dragSplit = -1
                        onCanceled: compareOverlay.dragSplit = -1
                        onDoubleClicked: win.effects.setCompareSplit(0.5)
                    }
                }
            }

            DropArea {
                anchors.fill: parent
                onEntered: (d) => dropHint.visible = true
                onExited: dropHint.visible = false
                onDropped: (d) => {
                    dropHint.visible = false
                    if (d.hasUrls) app.openUrls(d.urls, true)
                    else if (d.hasText && app.validateUrl(d.text.split(/\r?\n/)[0]) === "") app.openPaths([d.text.split(/\r?\n/)[0]], true)
                }
            }
            Rectangle {
                id: dropHint
                visible: false
                anchors.fill: parent; anchors.margins: 12
                color: "#14ff8a1f"; radius: 12
                border.color: theme.accent; border.width: 2
                Label { anchors.centerIn: parent; text: qsTr("Zum Abspielen loslassen"); font.pixelSize: 20; color: theme.text }
            }

            // OSD
            Rectangle {
                id: osdBox
                anchors { top: parent.top; right: parent.right; margins: 14 }
                width: osdLabel.implicitWidth + 24; height: osdLabel.implicitHeight + 12
                radius: 6; color: "#a6000000"
                opacity: 0
                Behavior on opacity { NumberAnimation { duration: 180 } }
                Label { id: osdLabel; anchors.centerIn: parent; color: "white"; font.pixelSize: 15 }
                Timer { id: osdTimer; onTriggered: osdBox.opacity = 0 }
            }

            // DVD/Blu-ray: Navigation (sichtbar im Disc-Menü oder mit der Bedienleiste)
            DiscNavigationControls {
                theme: theme
                visible: app.disc.navigationAvailable && (app.disc.menuActive || !win.overlayControls || win.controlsVisible)
                anchors { left: parent.left; bottom: videoArea.bottom; margins: 14 }
                onUsed: win.poke()
            }

            DiagnosticsOverlay {
                visible: win.diagnosticsVisible
                anchors { top: parent.top; left: parent.left; margins: 12 }
                player: app.player
                shaders: app.shaders
            }

            ControlBar {
                id: controls
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                player: app.player; playlist: app.playlist; theme: theme
                overlay: win.overlayControls
                opacity: !win.overlayControls || win.controlsVisible ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 220 } }
                effectsAvailable: win.effects !== null
                effectsOn: win.effects ? win.effects.enabled : false
                aspectText: ({ "fit": qsTr("Einpassen"), "stretch": qsTr("Strecken") })[win.aspect] || win.aspect
                onAction: (id) => win.act(id)
                onOpenSpeedMenu: (a) => speedPopup.popup(a, 0, -speedPopup.implicitHeight)
                onOpenSubtitleMenu: (a) => subPopup.popup(a, 0, -subPopup.implicitHeight)
                onOpenAudioMenu: (a) => audioPopup.popup(a, 0, -audioPopup.implicitHeight)
                HoverHandler { onHoveredChanged: if (hovered) win.poke() }
            }
        }

        // Seitenleiste: Wiedergabeliste + Plugin-Panels (verschwinden mit dem Plugin)
        Rectangle {
            id: sidebar
            visible: win.sidebarVisible && !win.overlayControls
            width: visible ? 330 : 0
            anchors { right: parent.right; top: safeBanner.bottom; bottom: parent.bottom }
            color: theme.panel
            Rectangle { width: 1; height: parent.height; color: theme.line }
            readonly property var panels: (app.plugins ? app.plugins.panels : []).filter(p => p.location === "sidebar")
            // Gewählte Seite: Die Tabs werden bei jeder Änderung neu aufgebaut, sie soll trotzdem offen bleiben
            property string activeKey: ""
            onPanelsChanged: Qt.callLater(tabs.restoreActive)

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 1
                spacing: 0
                TabBar {
                    id: tabs
                    Layout.fillWidth: true
                    function selectTab(name) {
                        if (name === "Effekte") {
                            for (var i = 0; i < sidebar.panels.length; ++i)
                                if (sidebar.panels[i].pluginId === "org.nextscript.ultimate-video-enhancer") { sidebar.activeKey = sidebar.panels[i].key; currentIndex = i + 1; return }
                        }
                        sidebar.activeKey = ""
                        currentIndex = 0
                    }
                    function restoreActive() {
                        currentIndex = sidebar.panels.findIndex(p => p.key === sidebar.activeKey) + 1
                    }
                    // Plugin-Befehl "panel.open": Schlüssel "<plugin-id>/<panel-id>"
                    function selectPanel(key) {
                        for (var i = 0; i < sidebar.panels.length; ++i)
                            if (sidebar.panels[i].key === key) { sidebar.activeKey = key; currentIndex = i + 1; return true }
                        return false
                    }
                    TabButton { text: qsTr("Wiedergabeliste"); onClicked: sidebar.activeKey = "" }
                    Repeater {
                        model: sidebar.panels
                        TabButton { required property var modelData; text: modelData.title; onClicked: sidebar.activeKey = modelData.key }
                    }
                }
                StackLayout {
                    id: sidebarStack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: tabs.currentIndex
                    PlaylistPanel {
                        id: playlistPanel
                        playlist: app.playlist; theme: theme
                        onRequestOpenFiles: addFilesDialog.open()
                        onRequestOpenFolder: addFolderDialog.open()
                        onRequestOpenUrl: { urlDialog.playNow = false; urlDialog.open() }
                        onRequestSave: savePlaylistDialog.open()
                        onRequestMediaInfo: mediaInfoDialog.open()
                    }
                    Repeater {
                        model: sidebar.panels
                        delegate: Loader {
                            required property var modelData
                            // Feste Größe: StackLayout vergrößert neu aufgebaute Seiten sonst nicht (0×0, die Leiste bliebe leer)
                            width: sidebarStack.width
                            height: sidebarStack.height
                            source: modelData.url
                            onLoaded: {
                                // Seiten bekommen theme, host (= app), controller und – falls deklariert – plugin (Befehle/Ereignisse)
                                if ("theme" in item) item.theme = theme
                                if ("host" in item) item.host = app
                                if ("controller" in item) item.controller = modelData.controller
                                if ("plugin" in item) item.plugin = modelData.plugin
                            }
                            Label {
                                visible: parent.status === Loader.Error
                                anchors.centerIn: parent
                                width: parent.width - 30
                                wrapMode: Text.Wrap
                                color: theme.error
                                text: qsTr("Das Panel des Plugins konnte nicht geladen werden (siehe Plugin-Fehlerprotokoll). Die Wiedergabe ist davon nicht betroffen.")
                            }
                        }
                    }
                }
            }
        }
    }

    // Steuerelemente im Vollbild/Minimalmodus ausblenden
    function poke() {
        controlsVisible = true
        idleTimer.restart()
    }
    Timer {
        id: idleTimer
        interval: 2500
        onTriggered: if (win.overlayControls && player.state === 2 && !videoMenu.visible && !speedPopup.visible) win.controlsVisible = false
    }
    onOverlayControlsChanged: poke()

    // ------------------------------------------------------------- Menüs & Dialoge
    VideoContextMenu {
        id: videoMenu
        player: app.player; effects: win.effects
        act: win.act
        aspect: win.aspect
        onAboutToShow: win.modalCount++
        onAboutToHide: win.modalCount = Math.max(0, win.modalCount - 1)
    }
    Menu {
        id: subPopup
        Instantiator {
            model: player.subtitleTracks
            delegate: MenuItem { required property var modelData; text: modelData.label; checkable: true; checked: modelData.selected === true; onTriggered: player.setSubtitleTrack(modelData.id) }
            onObjectAdded: (i, o) => subPopup.insertItem(i, o)
            onObjectRemoved: (i, o) => subPopup.removeItem(o)
        }
        MenuItem { text: qsTr("Aus"); checkable: true; checked: !player.subtitleTracks.some(t => t.selected); onTriggered: player.setSubtitleTrack(0) }
        MenuSeparator {}
        MenuItem { text: qsTr("Untertiteldatei hinzufügen…"); onTriggered: win.act("sub.add") }
    }
    Menu {
        id: audioPopup
        Instantiator {
            model: player.audioTracks
            delegate: MenuItem { required property var modelData; text: modelData.label; checkable: true; checked: modelData.selected === true; onTriggered: player.setAudioTrack(modelData.id) }
            onObjectAdded: (i, o) => audioPopup.insertItem(i, o)
            onObjectRemoved: (i, o) => audioPopup.removeItem(o)
        }
        MenuItem { text: qsTr("Keine Audiospuren"); enabled: false; visible: player.audioTracks.length === 0; height: visible ? implicitHeight : 0 }
    }
    Menu {
        id: speedPopup
        Repeater {
            model: [0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 3.0, 4.0]
            MenuItem { required property real modelData; text: modelData.toFixed(2) + "×"; checkable: true; checked: Math.abs(player.speed - modelData) < 0.01; onTriggered: win.setSpeed(modelData) }
        }
    }

    component TrackedDialog: Dialog {
        onOpened: win.modalCount++
        onClosed: win.modalCount = Math.max(0, win.modalCount - 1)
    }

    FileDialog {
        id: openFileDialog
        title: qsTr("Medien öffnen")
        fileMode: FileDialog.OpenFiles
        nameFilters: app.fileDialogFilter.split(";;")
        onAccepted: app.openUrls(selectedFiles, true)
    }
    FileDialog {
        id: addFilesDialog
        title: qsTr("Zur Wiedergabeliste hinzufügen")
        fileMode: FileDialog.OpenFiles
        nameFilters: app.fileDialogFilter.split(";;")
        onAccepted: app.openUrls(selectedFiles, false)
    }
    FolderDialog { id: openFolderDialog; title: qsTr("Ordner öffnen"); onAccepted: app.openUrls([selectedFolder], true) }
    FolderDialog { id: addFolderDialog; title: qsTr("Ordner hinzufügen"); onAccepted: app.openUrls([selectedFolder], false) }
    FileDialog {
        id: subtitleDialog
        title: qsTr("Untertiteldatei hinzufügen")
        nameFilters: ["Untertitel (*.srt *.ass *.ssa *.vtt *.sub *.idx *.sup)", qsTr("Alle Dateien (*)")]
        onAccepted: app.addSubtitle(selectedFile)
    }
    FileDialog {
        id: savePlaylistDialog
        title: qsTr("Wiedergabeliste speichern")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "m3u"
        nameFilters: ["M3U-Wiedergabeliste (*.m3u)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.MusicLocation)
        onAccepted: app.savePlaylist(selectedFile)
    }

    OpenDiscDialog { id: discDialog; theme: theme; onOpened: win.modalCount++; onClosed: win.modalCount = Math.max(0, win.modalCount - 1) }
    OpenUrlDialog { id: urlDialog; theme: theme; onOpened: win.modalCount++; onClosed: win.modalCount = Math.max(0, win.modalCount - 1) }
    MediaInfoDialog { id: mediaInfoDialog; theme: theme; onOpened: win.modalCount++; onClosed: win.modalCount = Math.max(0, win.modalCount - 1) }
    AboutDialog {
        id: aboutDialog; theme: theme
        onOpened: win.modalCount++
        onClosed: win.modalCount = Math.max(0, win.modalCount - 1)
        onToolUpdatesRequested: (updates) => updateDialog.show(updates)
        onCommitsRequested: (result) => appUpdateDialog.show(result)
    }
    UpdateDialog { id: updateDialog; theme: theme; onOpened: win.modalCount++; onClosed: { win.modalCount = Math.max(0, win.modalCount - 1); updateCheck.showNext() } }
    AppUpdateDialog { id: appUpdateDialog; theme: theme; onOpened: win.modalCount++; onClosed: { win.modalCount = Math.max(0, win.modalCount - 1); updateCheck.showNext() } }
    SettingsDialog {
        id: settingsDialog
        theme: theme
        onCheckUpdatesRequested: win.checkForUpdates()
        effects: win.effects
        onOpened: win.modalCount++
        onClosed: win.modalCount = Math.max(0, win.modalCount - 1)
        onOpenEffects: {
            var p = win.effectsPanel()
            if (!p || p.location !== "settings") close()
            win.act("effects.panel")
        }
    }

    TrackedDialog {
        id: gotoDialog
        title: qsTr("Zu Zeit springen")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Component.onCompleted: { var c = standardButton(Dialog.Cancel); if (c) c.text = Qt.binding(() => qsTr("Abbrechen")) }
        onAboutToShow: gotoField.text = app.formatTime(player.position)
        onAccepted: {
            var parts = gotoField.text.trim().split(":").map(Number)
            if (parts.some(isNaN)) { win.osd(qsTr("Ungültige Zeit")); return }
            var s = 0; for (var i = 0; i < parts.length; ++i) s = s * 60 + parts[i]
            player.seek(s, true)
        }
        RowLayout {
            Label { text: qsTr("Zeit (h:mm:ss)"); color: theme.text }
            TextField { id: gotoField; Layout.preferredWidth: 140; onAccepted: gotoDialog.accept() }
        }
    }

    // Update-Prüfung von Player und Werkzeugen. Manuell: Ergebnis immer melden, auch wenn alles aktuell ist.
    // Es ist immer nur ein Update-Fenster offen; das nächste folgt, wenn das vorige geschlossen wurde.
    QtObject {
        id: updateCheck
        property int pendingManual: 0
        property var messages: []
        property int found: 0
        property var queue: []
        function enqueue(fn) {
            queue = queue.concat([fn])
            if (!updateDialog.visible && !appUpdateDialog.visible) showNext()
        }
        function showNext() {
            if (queue.length === 0) return
            var fn = queue[0]
            queue = queue.slice(1)
            fn()
        }
        function finishedOne(count, errorText) {
            if (pendingManual === 0) return
            pendingManual--
            found += count
            if (errorText) messages = messages.concat([errorText])
            if (pendingManual === 0 && found === 0)
                win.osd(messages.length ? qsTr("Update-Prüfung fehlgeschlagen: ") + messages.join("; ") : qsTr("UVE Player, yt-dlp und libmpv sind aktuell"), 3500)
        }
    }
    function checkForUpdates() {
        if (app.updater.busy || app.appUpdater.busy) return
        updateCheck.pendingManual = 2
        updateCheck.messages = []
        updateCheck.found = 0
        osd(qsTr("Suche nach Updates …"), 2500)
        app.updater.check()
        app.appUpdater.check()
    }
    Connections {
        target: app.updater
        function onCheckFinished(updates, errors) { updateCheck.finishedOne(updates.length, errors.join("; ")) }
    }
    Connections {
        target: app.appUpdater
        function onCheckFinished(result, error) {
            var manual = updateCheck.pendingManual > 0
            if (aboutDialog.visible && !manual) return   // „Über UVE Player“ zeigt das Ergebnis selbst
            var relevant = result.updateAvailable === true || result.newerCommits === true
            if (manual) {
                if (relevant) updateCheck.enqueue(() => appUpdateDialog.show(result))
            } else if (result.updateAvailable === true) {
                // Automatische Prüfung: übersprungene Version nicht erneut anbieten
                if (app.setting("skippedPlayerVersion", "") !== result.latestVersion) updateCheck.enqueue(() => appUpdateDialog.show(result))
            } else if (result.newerCommits === true && app.setting("seenPlayerCommit", "") !== result.branchCommit) {
                // Ohne installierbares Paket kein Fenster beim Start, nur einmal je Commit ein Hinweis
                app.setSetting("seenPlayerCommit", result.branchCommit)
                win.osd(qsTr("Neue Commits von UVE Player auf GitHub (noch kein Release) – siehe Hilfe → Über UVE Player"), 5000)
            }
            updateCheck.finishedOne(relevant ? 1 : 0, error ? "UVE Player: " + error : "")
        }
    }
    Connections {
        target: app
        function onOsd(text, ms) { win.osd(text, ms) }
        function onToolUpdatesAvailable(updates) { if (!aboutDialog.visible) updateCheck.enqueue(() => updateDialog.show(updates)) }
        function onRequestRaise() { win.raise(); win.requestActivate() }
        function onPluginPanelRequested(key) {
            win.showPanel(key, false)
        }
    }

    // ------------------------------------------------------------- Fenster von Plugins (Befehl "modal.open")
    property var pluginModals: ({})   // Schlüssel "<plugin-id>/<panel-id>" → PluginModal
    Component {
        id: pluginModalComponent
        PluginModal {
            id: pluginModal
            area: scope === "player" ? overlayItem : (scope === "sidebar" && sidebar.visible ? sidebar : videoScope)
            onOpened: if (blocking) win.modalCount++
            onFinished: (key, result) => {
                if (blocking) win.modalCount = Math.max(0, win.modalCount - 1)
                delete win.pluginModals[key]
                if (app.plugins) app.plugins.notifyModalClosed(key, result)
                pluginModal.destroy()
            }
        }
    }
    Connections {
        target: app.plugins
        function onModalRequested(request) {
            var open = win.pluginModals[request.key]
            if (open) { open.request = request; return }
            if (request.scope === "sidebar" && !win.overlayControls) { win.sidebarVisible = true; app.setSetting("showSidebar", true) }
            var modal = pluginModalComponent.createObject(win, { request: request, theme: theme })
            win.pluginModals[request.key] = modal
            modal.open()
        }
        function onModalCloseRequested(pluginId, key) {
            for (var k of Object.keys(win.pluginModals))
                if (k === key || (key === "" && win.pluginModals[k].request.pluginId === pluginId)) win.pluginModals[k].closeWith(undefined)
        }
        function onPluginsChanged() {
            // Fenster verschwinden mit dem Plugin (deaktiviert oder entfernt)
            var panels = app.plugins.panels
            for (var k of Object.keys(win.pluginModals))
                if (!panels.some(p => p.key === k)) win.pluginModals[k].closeWith(undefined)
        }
    }
    Component.onCompleted: {
        player.setAspect(aspect)
        if (app.selftest) selftestUi.start()
    }

    // Selbsttest der Oberfläche. Im UI-Modus (UVE_SELFTEST_UI=1) werden echte Mausereignisse an das Fenster
    // gesendet: Doppelklick in der Wiedergabeliste, Klick/Doppelklick/Rechtsklick auf das Bild, Effekt-Vergleich.
    property var stUi: ({})
    property var stLeftRect: ({})
    property int stStep: 0
    function videoCenter() { return stageMouse.mapToItem(null, stageMouse.width / 2, stageMouse.height / 2) }
    function videoRect() { var p = videoArea.mapToItem(null, videoArea.width * 0.2, videoArea.height * 0.25); return { x: p.x, y: p.y, w: videoArea.width * 0.6, h: videoArea.height * 0.5 } }
    // Selbsttest Live-Denoising (UVE_SELFTEST_UI=1, UVE_SELFTEST_ONLY=denoise): alle Modi während der Wiedergabe umschalten.
    // Am Ende bleibt NLMeans gewählt, damit ein zweiter Lauf die Wiederherstellung nach dem Neustart prüfen kann.
    property var stDenoiseModes: ["bilateral", "adaptive", "temporal", "nlmeans", "off"]
    property int stDenoiseIndex: 0
    property var stDenoiseLog: []
    property real stDenoiseWall: 0
    property real stDenoisePos: 0
    function denoiseSelftestRecord(name) {
        var d = win.effects.denoise
        var e = { mode: d.mode, status: d.status, problem: d.statusIsProblem, state: player.state, paused: player.paused,
                  position: player.position, wallSeconds: (Date.now() - win.stDenoiseWall) / 1000,
                  shaders: app.shaders.activeShaders.length, vf: (player.getProperty("vf") || []).length,
                  hwdec: player.getProperty("hwdec-current"), uveError: win.effects.lastError, pipeline: win.effects.pipelineInfo }
        var l = win.stDenoiseLog; l.push(e); win.stDenoiseLog = l
        app.saveWindowShot(app.selftestPath("denoise-" + name + ".png"))
        return e
    }
    // Selbsttest Plugin-Fenster, -Einstellungen und Entfernen (UVE_SELFTEST_ONLY=plugins, plugins/examples/modaldemo im Benutzerordner)
    property int stPluginStep: 0
    function pluginsSelftestStep(ui) {
        selftestUi.interval = 300
        if (ui.failures === undefined) ui.failures = []
        var demo = "org.uveplayer.example.modal-demo"
        var key = demo + "/dialog"
        var m = win.pluginModals[key]
        var check = (name, ok) => { if (!ok) ui.failures.push(win.stPluginStep + ": " + name) }
        var shot = (name) => app.saveWindowShot(app.selftestPath("plugins-" + name + ".png"))
        var trigger = (id) => app.plugins.triggerAction("plugin:" + demo + ":" + id)
        var root = win.contentItem.parent
        var dragTitle = (dx, dy) => {
            var p = m.titleBar.mapToItem(root, m.titleBar.width / 3, m.titleBar.height / 2)
            app.simulateDrag(p.x, p.y, p.x + dx, p.y + dy)
        }
        var closeByButton = () => {
            var p = m.closeButton.mapToItem(root, m.closeButton.width / 2, m.closeButton.height / 2)
            app.simulateClick(p.x, p.y)
        }
        var inside = (area) => {
            var a = area.mapToItem(root, 0, 0), c = m.card.mapToItem(root, 0, 0)
            return c.x >= a.x - 1 && c.y >= a.y - 1 && c.x + m.card.width <= a.x + area.width + 1 && c.y + m.card.height <= a.y + area.height + 1
        }
        var demoInfo = () => app.plugins.plugins.find(p => p.id === demo)
        // Die Seite muss wirklich sichtbar sein, nicht nur ihr Tab
        var sidebarShowsPage = () => {
            var r = sidebar.mapToItem(root, 0, 0)
            return app.selftestInkShare(r.x + 4, r.y + tabs.height + 4, sidebar.width - 8, sidebar.height - tabs.height - 8) > 0.01
        }
        var uvePanel = app.plugins.panels.find(p => p.movable && p.pluginId === "org.nextscript.ultimate-video-enhancer")
        var timerPanel = app.plugins.panels.find(p => p.movable && p.pluginId === "org.uveplayer.example.sleep-timer")
        switch (win.stPluginStep) {
        case 0:
            check("demo active", demoInfo() && demoInfo().state === "aktiv")
            check("demo removable", demoInfo() && demoInfo().removable)
            check("uve bundled", app.plugins.plugins.find(p => p.id === "org.nextscript.ultimate-video-enhancer").removable === false)
            trigger("open-player")
            break
        case 1:
            check("player opened", m && m.opened)
            check("player blocking", m && m.modal && win.modalCount > 0)
            check("player over menu bar", m && m.area === m.overlayItem)
            check("page loaded", m && m.page.status === Loader.Ready)
            shot("player")
            ui.x0 = m.card.x; ui.y0 = m.card.y
            dragTitle(150, 90)
            break
        case 2:
            check("moved", Math.abs(m.card.x - ui.x0 - 150) <= 2 && Math.abs(m.card.y - ui.y0 - 90) <= 2)
            shot("player-moved")
            dragTitle(-5000, -5000)
            break
        case 3:
            check("clamped top left", m.card.x === 0 && m.card.y === 0)
            closeByButton()
            break
        case 4:
            check("closed", win.pluginModals[key] === undefined)
            check("hotkeys free again", win.modalCount === 0)
            trigger("open-video")
            break
        case 5:
            check("video opened", m && m.opened && m.area === videoScope && !m.modal)
            shot("video")
            dragTitle(5000, 5000)
            break
        case 6:
            check("video clamped", inside(videoScope))
            shot("video-moved")
            // Der Rest des Players bleibt bedienbar
            var tab = tabs.itemAt(1)
            var tp = tab.mapToItem(root, tab.width / 2, tab.height / 2)
            app.simulateClick(tp.x, tp.y)
            break
        case 7:
            check("player usable next to video modal", tabs.currentIndex === 1 && m.opened)
            tabs.currentIndex = 0
            m.closeWith("x")
            trigger("open-sidebar")
            break
        case 8:
            check("sidebar opened", m && m.opened && m.area === sidebar)
            check("sidebar size", m && inside(sidebar) && m.card.width <= 280)
            shot("sidebar")
            dragTitle(-5000, 5000)
            break
        case 9:
            check("sidebar clamped", inside(sidebar))
            m.closeWith("x")
            trigger("open-fixed")
            break
        case 10:
            ui.x0 = m.card.x; ui.y0 = m.card.y
            dragTitle(100, 100)
            break
        case 11:
            check("fixed does not move", m.card.x === ui.x0 && m.card.y === ui.y0)
            m.closeWith("x")
            trigger("open-fill")
            break
        case 12:
            check("fill covers player", m.card.width === m.overlayItem.width && m.card.height === m.overlayItem.height)
            shot("fill")
            m.closeWith("x")
            break
        case 13:
            check("modal.closed delivered", demoInfo().plugin.settings().closedCount === 5)
            check("settings panel listed", settingsDialog.openPluginPage(demo + "/settings"))
            break
        case 14:
            check("settings entry", settingsDialog.pageCount === settingsDialog.builtinPages + 1)
            check("settings page", settingsDialog.page === settingsDialog.builtinPages && settingsDialog.pluginPage.status === Loader.Ready)
            shot("settings")
            app.setLanguage(app.language === "de" ? "en" : "de")   // Listen werden neu aufgebaut
            break
        case 15:
            check("settings page kept", settingsDialog.page === settingsDialog.builtinPages && settingsDialog.pluginPage.status === Loader.Ready)
            settingsDialog.close()
            trigger("open-video")
            break
        case 16:
            pluginsMenu.itemAt(0).triggered()   // Menü "Plugins verwalten…"
            break
        case 17:
            check("manager in settings", settingsDialog.opened && settingsDialog.page === settingsDialog.pluginsPageIndex)
            check("one window", settingsDialog.pluginsPage.visible)
            shot("manager")
            settingsDialog.pluginsPage.openPluginSettings(demo + "/settings")
            break
        case 18:
            check("settings button", settingsDialog.opened && settingsDialog.page === settingsDialog.builtinPages)
            settingsDialog.page = settingsDialog.pluginsPageIndex
            settingsDialog.pluginsPage.askRemove(demoInfo())
            break
        case 19:
            shot("remove-confirm")
            settingsDialog.pluginsPage.confirmRemove()
            break
        case 20:
            check("pending", demoInfo().pendingRemoval === true && demoInfo().state === "deaktiviert")
            check("panels gone", !app.plugins.panels.some(p => p.pluginId === demo))
            check("modal closed with plugin", win.pluginModals[key] === undefined)
            check("stays on plugins page", settingsDialog.page === settingsDialog.pluginsPageIndex)
            shot("manager-pending")
            app.plugins.cancelRemoval(demo)
            break
        case 21:
            check("undo", demoInfo().pendingRemoval === false && demoInfo().state === "aktiv")
            check("removal", app.plugins.removePlugin(demo, true).ok)
            settingsDialog.close()
            break
        case 22:
            check("two movable panels", !!uvePanel && !!timerPanel && tabs.count === 3 && panelsMenu.count === 2)
            tabs.selectPanel(uvePanel.key)
            app.plugins.setPanelPlacement(timerPanel.key, "hidden")
            break
        case 23:
            check("timer tab gone", tabs.count === 2 && timerPanel.location === "hidden")
            check("sidebar stays open", win.sidebarVisible && sidebar.visible)
            check("current tab kept", tabs.currentIndex === 1 && sidebar.activeKey === uvePanel.key)
            check("page visible after hiding another", sidebarShowsPage())
            shot("panel-hidden")
            check("plugin cannot show hidden page", win.showPanel(timerPanel.key, false) === false && timerPanel.location === "hidden")
            app.plugins.setPanelPlacement(timerPanel.key, "settings")
            break
        case 24:
            check("timer not in sidebar", tabs.count === 2)
            check("timer page listed", settingsDialog.pluginPanels.some(p => p.key === timerPanel.key))
            check("plugin opens settings page", win.showPanel(timerPanel.key, false))
            break
        case 25:
            check("timer page loaded", settingsDialog.opened && settingsDialog.pluginPage.status === Loader.Ready)
            shot("timer-in-settings")
            settingsDialog.close()
            app.plugins.setPanelPlacement(uvePanel.key, "settings")
            break
        case 26:
            check("only playlist tab", tabs.count === 1 && tabs.currentIndex === 0)
            win.act("effects.panel")
            break
        case 27:
            check("effects page in settings", settingsDialog.opened && settingsDialog.currentPluginPanel !== null
                  && settingsDialog.currentPluginPanel.key === uvePanel.key && settingsDialog.pluginPage.status === Loader.Ready)
            shot("uve-in-settings")
            win.effects.requestGallery()
            break
        case 28:
            shot("gallery-in-settings")
            app.simulateClick(6, 60)
            break
        case 29:
            settingsDialog.close()
            app.plugins.setPanelPlacement(uvePanel.key, "hidden")
            break
        case 30:
            check("effects hidden", uvePanel.location === "hidden")
            win.act("effects.panel")
            break
        case 31:
            check("user action shows effects again", uvePanel.location === "sidebar" && tabs.count === 2 && tabs.currentIndex === 1)
            check("page visible when shown again", sidebarShowsPage())
            // Menü Ansicht → Plugin-Seiten
            for (var mi = 0; mi < panelsMenu.count; ++mi)
                if (panelsMenu.itemAt(mi).text === timerPanel.title) panelsMenu.itemAt(mi).triggered()
            break
        case 32:
            check("menu shows timer", timerPanel.location === "sidebar" && tabs.count === 3)
            check("current tab kept after menu", sidebar.panels[tabs.currentIndex - 1].key === uvePanel.key)
            check("page visible after menu", sidebarShowsPage())
            app.setLanguage(app.language === "de" ? "en" : "de")
            break
        case 33:
            check("page visible after language switch", tabs.currentIndex > 0 && sidebarShowsPage())
            shot("sidebar-after-language")
            ui.ok = ui.failures.length === 0
            ui.done = true
            selftestUi.stop()
            app.reportSelftestUi(ui)
            return
        }
        win.stPluginStep++
    }
    // Selbsttest Sprachwechsel (UVE_SELFTEST_ONLY=language): alle Sprachpaare, jeder Menütitel mit echten Mausereignissen.
    // Prüft die Pixel: Qt zeigt manche Titel nach einem Wechsel leer an, das steht in keiner QML-Eigenschaft.
    property int stLangPhase: 0
    property int stLangPair: 0
    property int stLangMenu: 0
    property int stLangShot: 0
    readonly property var stLangPairs: {
        var codes = ["de", "en", "el", "es", "it", "pl", "tr"]
        var out = []
        for (var a = 0; a < codes.length; ++a)
            for (var b = 0; b < codes.length; ++b)
                if (a !== b) out.push([codes[a], codes[b]])
        return out
    }
    function stBarItems() {
        var out = []
        var kids = mainMenuBar.contentItem.children
        for (var i = 0; i < kids.length; ++i) if (kids[i].menu) out.push(kids[i])
        return out
    }
    function stCheckTitle(ui, pair, item, state) {
        var r = item.mapToItem(win.contentItem.parent, 0, 0)
        var ink = app.selftestInkShare(r.x + 2, r.y + 2, item.width - 4, item.height - 4)
        ui.titlesChecked++
        if (ink < 0.002) ui.failures.push({ from: pair[0], to: pair[1], title: item.text, state: state, ink: ink })
    }
    function languageSelftestStep(ui) {
        selftestUi.interval = 100
        if (ui.failures === undefined) { ui.failures = []; ui.titlesChecked = 0; ui.pairs = 0 }
        var bar = win.stBarItems()
        var centreOf = (it) => it.mapToItem(win.contentItem.parent, it.width / 2, it.height / 2)
        if (win.stLangPair >= win.stLangPairs.length && win.stLangShot < 28) {
            // Aufnahmen je Sprache zur Sichtkontrolle: Menü "Plugins" und Einstellungen. Klick erst im Schritt nach dem Sprachwechsel,
            // vorher ist die Menüleiste noch nicht neu angeordnet.
            var shotCode = ["de", "en", "el", "es", "it", "pl", "tr"][Math.floor(win.stLangShot / 4)]
            selftestUi.interval = 500
            switch (win.stLangShot % 4) {
            case 0: app.setLanguage(shotCode); break
            case 1:
                var pluginsBar = bar.find(b => b.menu === pluginsMenu)
                var at = centreOf(pluginsBar)
                app.simulateMove(at.x, at.y)
                app.simulateClick(at.x, at.y)
                break
            case 2:
                if (!pluginsMenu.opened) ui.failures.push({ to: shotCode, title: pluginsMenu.title, state: "menu not open" })
                app.saveWindowShot(app.selftestPath("language-" + shotCode + "-menu.png"))
                pluginsMenu.close()
                settingsDialog.open()
                settingsDialog.page = 5
                break
            case 3:
                app.saveWindowShot(app.selftestPath("language-" + shotCode + "-settings.png"))
                settingsDialog.close()
                break
            }
            win.stLangShot++
            return
        }
        if (win.stLangPair >= win.stLangPairs.length) {
            ui.ok = ui.failures.length === 0 && ui.pairs === win.stLangPairs.length
            ui.done = true
            selftestUi.stop()
            app.reportSelftestUi(ui)
            return
        }
        var pair = win.stLangPairs[win.stLangPair]
        switch (win.stLangPhase) {
        case 0:
            app.setLanguage(pair[0])
            app.setLanguage(pair[1])
            win.stLangMenu = 0
            win.stLangPhase = 1
            break
        case 1:
            var first = centreOf(bar[0])
            app.simulateMove(first.x, first.y)
            app.simulateClick(first.x, first.y)
            win.stLangPhase = 2
            break
        case 2:
            var cur = bar[win.stLangMenu]
            if (!cur.menu.opened) ui.failures.push({ from: pair[0], to: pair[1], title: cur.text, state: "menu not open" })
            win.stCheckTitle(ui, pair, cur, "hover")
            for (var e = 0; e < cur.menu.count; ++e) {
                var entry = cur.menu.itemAt(e)
                if (entry.text && entry.visible && entry.enabled) win.stCheckTitle(ui, pair, entry, "entry")
            }
            if (++win.stLangMenu < bar.length) {
                var next = centreOf(bar[win.stLangMenu])
                app.simulateMove(next.x, next.y)
            } else {
                for (var i = 0; i < bar.length; ++i) bar[i].menu.close()
                app.simulateMove(win.width / 2, win.height / 2)
                win.stLangPhase = 3
            }
            break
        case 3:
            // Nach dem Überfahren bleibt ein leerer Titel leer: auch im Ruhezustand prüfen
            for (var j = 0; j < bar.length; ++j) win.stCheckTitle(ui, pair, bar[j], "idle")
            ui.pairs++
            win.stLangPair++
            win.stLangPhase = 0
            break
        }
    }
    function denoiseSelftestStep(ui) {
        var d = win.effects ? win.effects.denoise : null
        if (win.stStep === 0) {
            ui.available = !!d && d.available
            ui.modes = d ? d.modes.length : 0
            ui.initialMode = d ? d.mode : ""
            ui.initialStrength = d ? d.strength : -1
            ui.initialShaders = app.shaders.activeShaders.length
            tabs.selectTab("Effekte")
            win.effects.requestScroll("denoising")
            player.setProperty("loop-file", "inf")
            ui.baseShadersWithoutDenoise = ui.initialMode === "off" ? ui.initialShaders : -1
            selftestUi.interval = 1500
            win.stStep = 1
            return
        }
        if (win.stStep === 1) {
            win.denoiseSelftestRecord("start")
            d.setMode("off")
            win.stDenoiseWall = Date.now(); win.stDenoisePos = player.position
            selftestUi.interval = 800
            win.stStep = 2
            return
        }
        if (win.stStep === 2) {   // ein Modus je Schritt
            if (win.stDenoiseIndex > 0) win.denoiseSelftestRecord(win.stDenoiseModes[win.stDenoiseIndex - 1])
            if (win.stDenoiseIndex < win.stDenoiseModes.length) {
                d.setMode(win.stDenoiseModes[win.stDenoiseIndex++])
                selftestUi.interval = win.stDenoiseModes[win.stDenoiseIndex - 1] === "temporal" ? 4000 : 2500
                return
            }
            ui.shadersAfterOff = app.shaders.activeShaders.length
            ui.vfAfterOff = (player.getProperty("vf") || []).length
            ui.hwdecAfterOff = player.getProperty("hwdec-current")
            d.setMode("nlmeans")
            d.setStrength(77)
            win.effects.setCompare(true)
            selftestUi.interval = 2500
            win.stStep = 3
            return
        }
        if (win.stStep === 3) {
            var c = win.denoiseSelftestRecord("compare")
            ui.compareActive = win.effects.compareActive
            ui.compareNoError = c.uveError === "" && !c.problem
            win.effects.setCompare(false)
            selftestUi.interval = 1500
            win.stStep = 4
            return
        }
        ui.log = win.stDenoiseLog
        var ok = ui.available && ui.modes === 5 && ui.compareActive && ui.compareNoError && ui.vfAfterOff === 0
        ui.playbackContinuous = true
        for (var i = 0; i < win.stDenoiseLog.length; ++i) {
            var e = win.stDenoiseLog[i]
            if (e.state !== 2 || e.problem || e.uveError !== "") ui.playbackContinuous = false
        }
        ui.ok = ok && ui.playbackContinuous
        ui.done = true
        selftestUi.stop()
        app.reportSelftestUi(ui)
    }
    Timer {
        id: selftestUi
        interval: 6000
        onTriggered: {
            var ui = win.stUi
            if (!app.selftestUiMode) {
                ui.effectsPluginActive = win.effects !== null
                ui.sidebarTabs = tabs.count
                ui.hotkeys = app.hotkeys.items.length
                app.reportSelftestUi(ui)
                return
            }
            if (app.selftestOnly("plugins")) {
                win.pluginsSelftestStep(ui)
                win.stUi = ui
                if (!ui.done) restart()
                return
            }
            if (app.selftestOnly("language")) {
                win.languageSelftestStep(ui)
                win.stUi = ui
                if (!ui.done) restart()
                return
            }
            if (app.selftestOnly("denoise")) {
                win.denoiseSelftestStep(ui)
                win.stUi = ui
                if (!ui.done) restart()
                return
            }
            switch (win.stStep) {
            case 0:
                ui.effectsPluginActive = win.effects !== null
                ui.sidebarTabs = tabs.count
                ui.firstPlaying = player.state === 2
                tabs.currentIndex = 0
                var row = playlistPanel.rowCenter(1)
                ui.rowFound = row !== null
                if (row) app.simulateClick(row.x, row.y, 1, true)   // Doppelklick auf Zeile 2
                interval = 3500; break
            case 1:
                ui.dblclickPlaylistPlays = playlist.currentIndex === 1 && player.state === 2
                var c = win.videoCenter()
                app.simulateClick(c.x, c.y, 1, false)               // Klick aufs Bild → Pause
                interval = 900; break
            case 2:
                ui.clickPauses = player.paused === true
                var c2 = win.videoCenter()
                app.simulateClick(c2.x, c2.y, 1, false)             // erneut → Wiedergabe
                interval = 900; break
            case 3:
                ui.clickResumes = player.paused === false
                var c3 = win.videoCenter()
                app.simulateClick(c3.x, c3.y, 1, true)              // Doppelklick → Vollbild, keine Pause
                interval = 1200; break
            case 4:
                ui.dblclickFullscreen = win.fullscreen
                ui.dblclickKeepsPlaying = player.paused === false
                win.poke()                              // Steuerleiste im Vollbild einblenden
                interval = 700; win.stStep = 299; break   // danach ++ → 300
            case 300:
                app.saveWindowShot(app.selftestPath("fullscreen-controls.png"))
                // Steuerleiste vollständig im Fenster (Play-Knopf nicht abgeschnitten)
                var cb = controls.mapToItem(null, 0, 0)
                ui.fullscreenBarInside = cb.y + controls.height <= win.height + 0.5 && controls.height >= controls.implicitHeight - 0.5
                if (win.fullscreen) win.toggleFullscreen()
                interval = 900; win.stStep = 4; break
            case 5:
                var c5 = win.videoCenter()
                app.simulateClick(c5.x, c5.y, 2, false)             // Rechtsklick → Kontextmenü
                interval = 600; break
            case 6:
                ui.rightClickMenu = videoMenu.visible
                ui.videoMenuItems = videoMenu.count
                videoMenu.close()
                tabs.selectTab("Effekte")
                ui.effectsTabShown = tabs.currentIndex > 0
                // Wirkungsmessung: Standbild, Effekte aus
                player.setPaused(true)
                if (win.effects) win.effects.setEnabled(false)
                interval = 2000; break
            case 7:
                ui.fxOff = app.sampleWindow(win.videoRect().x, win.videoRect().y, win.videoRect().w, win.videoRect().h)
                // Effekte an mit starker Entsättigung → Bild muss sichtbar grauer werden
                if (win.effects) { win.effects.setEnabled(true); win.effects.setValue("baseTone", false); win.effects.setValue("saturation", -10); win.effects.setValue("vibrantSat", false) }
                interval = 2500; break
            case 8:
                ui.fxOn = app.sampleWindow(win.videoRect().x, win.videoRect().y, win.videoRect().w, win.videoRect().h)
                ui.fxChromaRatio = ui.fxOff && ui.fxOn && ui.fxOff.chroma > 0 ? ui.fxOn.chroma / ui.fxOff.chroma : -1
                ui.fxVisible = ui.fxChromaRatio >= 0 && ui.fxChromaRatio < 0.8
                if (win.effects) { win.effects.setValue("saturation", 0); win.effects.setValue("baseTone", true) }
                // Beispielprofil My_Profile.json von GitHub (lädt das benötigte LUT-Paket automatisch mit)
                if (win.effects) win.effects.loadExampleProfile()
                interval = 12000; break
            case 9:
                ui.exampleLutProfile = win.effects ? win.effects.values.lutProfile : ""
                ui.lutLibraryCount = win.effects ? win.effects.lutProfiles.length : 0
                ui.librarySource = win.effects ? win.effects.librarySource : ""
                ui.libraryStatus = win.effects ? win.effects.libraryStatus : ""
                ui.examplePipeline = win.effects ? win.effects.pipelineInfo : ""
                ui.exampleLoaded = ui.exampleLutProfile === "Fujifilm||DCI-P3 Fujifilm 3513DI D55" && ui.examplePipeline.indexOf("LUT-Profil") >= 0
                ui.exampleBasis = win.effects ? { sl: win.effects.values.sl, sr: win.effects.values.sr, bl: win.effects.values.bl, wl: win.effects.values.wl, darkMoody: win.effects.values.darkMoody, profile: win.effects.values.profile } : null
                ui.exampleBasisOk = !!ui.exampleBasis && Math.abs(ui.exampleBasis.bl - 0.7) < 1e-9 && Math.abs(ui.exampleBasis.wl - 0.2) < 1e-9
                                    && Math.abs(ui.exampleBasis.sr - 1.1) < 1e-9 && Math.abs(ui.exampleBasis.sl - 1.2) < 1e-9 && ui.exampleBasis.darkMoody === true
                ui.exampleShaderError = app.shaders.lastError
                ui.lutA = app.sampleWindow(win.videoRect().x, win.videoRect().y, win.videoRect().w, win.videoRect().h)
                if (win.effects) win.effects.setLutProfile("Fresh LUTs||Black&White 1")   // deutlich sichtbares LUT
                interval = 2500; break
            case 10:
                ui.lutB = app.sampleWindow(win.videoRect().x, win.videoRect().y, win.videoRect().w, win.videoRect().h)
                ui.lutBwChromaRatio = ui.lutA && ui.lutB && ui.lutA.chroma > 0 ? ui.lutB.chroma / ui.lutA.chroma : -1
                ui.lutVisible = ui.lutBwChromaRatio >= 0 && ui.lutBwChromaRatio < 0.5
                if (win.effects) win.effects.setLutProfile("Fujifilm||DCI-P3 Fujifilm 3513DI D55")
                interval = 1200; break
            case 11:
                if (win.effects) win.effects.requestScroll("top")
                interval = 600; break
            case 12:
                app.saveWindowShot(app.selftestPath("panel-top.png"))   // Presets mit Icon-Button
                if (win.effects) win.effects.requestScroll("bottom")
                interval = 600; break
            case 13:
                app.saveWindowShot(app.selftestPath("panel-lut.png"))   // LUT-Auswahl mit Icon-Buttons
                if (win.effects) win.effects.requestGallery()
                interval = 3500; break
            case 14:
                ui.galleryCount = win.effects ? win.effects.galleryItems.length : 0
                ui.galleryInfo = win.effects ? win.effects.galleryInfo : ""
                app.saveWindowShot(app.selftestPath("gallery.png"))     // Bildschirmfoto der Galerie
                settingsDialog.open(); settingsDialog.page = 9          // Einstellungen → Netzwerk
                interval = 900; break
            case 15:
                app.saveWindowShot(app.selftestPath("settings-network.png"))
                ui.networkPageShown = settingsDialog.visible && settingsDialog.page === 9
                settingsDialog.close()
                interval = 400; break
            case 16:
                settingsDialog.open(); settingsDialog.page = 11         // Einstellungen → Expert Settings
                interval = 900; break
            case 17:
                app.saveWindowShot(app.selftestPath("settings-expert.png"))
                ui.expertBlocks = win.effects ? win.effects.expertBlocks.length : 0
                settingsDialog.close()
                app.simulateClick(6, 60, 1, false)   // LUT-Galerie (modal) durch Klick außerhalb schließen
                // Vorher/Nachher-Trenner (Strg+Alt+U) und Auto-Scene-Match während der Wiedergabe
                player.setProperty("loop-file", "inf"); player.seek(0); player.setPaused(false)
                if (win.effects) { win.effects.setAutoOn(true); win.effects.setCompare(true); win.effects.setCompareSplit(0.3) }
                interval = 3000; break
            case 18:
                // Vergleichsbasis: 3 s ohne Ziehen (gleiche Quelle, Vergleich an); Anfang, damit keine Schleife dazwischenkommt
                player.seek(0)
                ui.idleBefore = { delayed: player.getProperty("vo-delayed-frame-count"), dropped: player.getProperty("frame-drop-count") }
                interval = 3000; win.stStep = 99; break   // danach ++ → 100
            case 100:
                ui.idleDroppedFrames = player.getProperty("frame-drop-count") - ui.idleBefore.dropped
                ui.idleDelayedFrames = player.getProperty("vo-delayed-frame-count") - ui.idleBefore.delayed
                // Schieber ziehen wie mit der Maus: 3 s lang alle 40 ms eine neue Position, Ruckler zählen
                ui.dragBefore = { delayed: player.getProperty("vo-delayed-frame-count"), dropped: player.getProperty("frame-drop-count") }
                dragSweep.n = 0; dragSweep.start()
                interval = 3600; win.stStep = 18; break
            case 19:
                ui.dragAfter = { delayed: player.getProperty("vo-delayed-frame-count"), dropped: player.getProperty("frame-drop-count") }
                ui.dragDelayedFrames = ui.dragAfter.delayed - ui.dragBefore.delayed
                ui.dragDroppedFrames = ui.dragAfter.dropped - ui.dragBefore.dropped
                // Hälften prüfen: rechts (mit Effekten) stark entsättigt, links Original
                if (win.effects) { win.effects.setCompareSplit(0.5); win.effects.setValue("saturation", -10); win.effects.setValue("baseTone", false) }
                interval = 3000; break
            case 20:
                app.saveWindowShot(app.selftestPath("compare-auto.png"))
                var cr = compareOverlay.mapToItem(null, 0, 0)
                ui.splitLeft = app.sampleWindow(cr.x + compareOverlay.width * 0.05, cr.y + compareOverlay.height * 0.2, compareOverlay.width * 0.4, compareOverlay.height * 0.6)
                ui.splitRight = app.sampleWindow(cr.x + compareOverlay.width * 0.55, cr.y + compareOverlay.height * 0.2, compareOverlay.width * 0.4, compareOverlay.height * 0.6)
                ui.splitHalvesOk = !!ui.splitLeft && !!ui.splitRight && ui.splitRight.chroma < ui.splitLeft.chroma * 0.8
                // Seitenverhältnis: zusammengesetzte Fläche wie das Video (Testvideo 16:9)
                ui.splitAspect = compareOverlay.width / compareOverlay.height
                ui.compareOverlay = compareOverlay.visible && Math.abs(compareOverlay.shown - 0.5) < 0.01
                if (win.effects) { win.effects.setValue("saturation", 0); win.effects.setValue("baseTone", true); win.effects.setCompareSplit(0.3) }
                ui.autoInfo = win.effects ? win.effects.autoInfo : ""
                ui.autoEq = { brightness: player.getProperty("brightness"), contrast: player.getProperty("contrast"),
                              saturation: player.getProperty("saturation"), hue: player.getProperty("hue") }
                // VORHER muss das echte Original sein: Standbild, deutlicher Equalizer, linke Hälfte messen …
                player.setPaused(true)
                if (win.effects) win.effects.setAutoOn(false)
                interval = 800; win.stStep = 199; break
            case 200:
                player.setProperty("brightness", 5); player.setProperty("contrast", -20)   // ohne Abschneiden (5 … 85 %)
                interval = 1200; break
            case 201:
                var lr = compareOverlay.mapToItem(null, 0, 0)
                win.stLeftRect = { x: lr.x + compareOverlay.width * 0.03, y: lr.y + compareOverlay.height * 0.2, w: compareOverlay.width * 0.22, h: compareOverlay.height * 0.6 }   // links vom Schieber (0.3)
                ui.leftInCompare = app.sampleWindow(win.stLeftRect.x, win.stLeftRect.y, win.stLeftRect.w, win.stLeftRect.h)
                // Gegenprobe: ohne Effekte, Equalizer noch aktiv (so sähe VORHER ohne Rückrechnung aus)
                if (win.effects) win.effects.setEnabled(false)
                interval = 1500; win.stStep = 202; break   // danach ++ → 203
            case 203:
                ui.leftWithEqOnly = app.sampleWindow(win.stLeftRect.x, win.stLeftRect.y, win.stLeftRect.w, win.stLeftRect.h)
                // … dann ohne Equalizer an derselben Stelle: das echte Original
                player.setProperty("brightness", 0); player.setProperty("contrast", 0)
                interval = 1200; win.stStep = 201; break
            case 202:
                ui.leftOriginal = app.sampleWindow(win.stLeftRect.x, win.stLeftRect.y, win.stLeftRect.w, win.stLeftRect.h)
                ui.leftDiff = ui.leftInCompare && ui.leftOriginal
                    ? Math.max(Math.abs(ui.leftInCompare.r - ui.leftOriginal.r), Math.abs(ui.leftInCompare.g - ui.leftOriginal.g), Math.abs(ui.leftInCompare.b - ui.leftOriginal.b)) : 1
                ui.leftIsOriginal = ui.leftDiff < 0.02
                if (win.effects) { win.effects.setEnabled(true); win.effects.setCompare(false); win.effects.setAutoOn(false)
                                   win.effects.setValue("saturation", -10); win.effects.setValue("baseTone", false); win.effects.setCompareSplit(0.5) }
                interval = 1500; win.stStep = 399; break
            case 400:
                // Übergang Vergleich AN: Bilder in schneller Folge aufnehmen (Auswertung: kein Doppelbild)
                app.saveWindowShotAsync(app.selftestPath("tr-on-before.png"))
                if (win.effects) win.effects.setCompare(true)
                trBurst.start("tr-on-")
                interval = 1800; break
            case 401:
                app.saveWindowShotAsync(app.selftestPath("tr-on-after.png"))
                ui.trOnShots = trBurst.n
                if (win.effects) win.effects.setCompare(false)
                trBurst.start("tr-off-")
                interval = 1800; break
            case 402:
                app.saveWindowShotAsync(app.selftestPath("tr-off-after.png"))
                ui.trOffShots = trBurst.n
                if (win.effects) { win.effects.setValue("saturation", 0); win.effects.setValue("baseTone", true); win.effects.setAutoOn(true) }
                player.setPaused(false)
                interval = 400; win.stStep = 20; break
            case 21:
                ui.shaderError = app.shaders.lastError
                ui.activeShaders = app.shaders.activeShaders.length
                ui.ok = ui.firstPlaying && ui.dblclickPlaylistPlays && ui.clickPauses && ui.clickResumes
                        && ui.dblclickFullscreen && ui.dblclickKeepsPlaying && ui.rightClickMenu
                        && ui.effectsPluginActive && ui.effectsTabShown && !ui.shaderError && ui.fxVisible
                        && ui.exampleLoaded && ui.exampleBasisOk && !ui.exampleShaderError && ui.lutVisible && ui.galleryCount >= 83
                        && ui.expertBlocks === 11 && ui.compareOverlay && ui.autoInfo.length > 0 && ui.splitHalvesOk && ui.leftIsOriginal && ui.fullscreenBarInside
                ui.done = true
                app.reportSelftestUi(ui)
                return
            }
            win.stUi = ui
            win.stStep++
            restart()
        }
    }
    // Selbsttest: Bildfolge während eines Übergangs aufnehmen (ca. 1 s)
    Timer {
        id: trBurst
        property int n: 0
        property string prefix: ""
        property double t0: 0
        interval: 1; repeat: true
        function start(p) { prefix = p; n = 0; t0 = Date.now(); restart() }
        onTriggered: {
            if (Date.now() - t0 > 1100) { stop(); return }
            app.saveWindowShotAsync(app.selftestPath(prefix + (n < 10 ? "0" : "") + n + ".png"))
            n++
        }
    }
    // Selbsttest: Ziehen des Vorher/Nachher-Schiebers nachstellen (75 Positionen in 3 s)
    Timer {
        id: dragSweep
        property int n: 0
        interval: 40; repeat: true
        onTriggered: {
            if (!win.effects || ++n > 75) { stop(); return }
            win.effects.setCompareSplit(0.1 + 0.8 * (n % 25) / 25)
        }
    }
}
