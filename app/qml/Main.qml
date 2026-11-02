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

    // ------------------------------------------------------------- Aktionen
    function osd(text, ms) { osdLabel.text = text; osdBox.opacity = 1; osdTimer.interval = ms || 1500; osdTimer.restart() }
    function seekBy(s) { if (player.state !== 0) { player.seekRelative(s); osd((s < 0 ? "◀◀ " : "▶▶ ") + app.formatTime(Math.max(0, player.position + s))) } }
    function setVolume(v) { player.setVolume(v); if (player.muted && v > 0) player.setMuted(false); osd(qsTr("Lautstärke ") + Math.round(Math.max(0, Math.min(150, v))) + " %") }
    function setSpeed(s) { player.setSpeed(Math.round(s * 100) / 100); osd(qsTr("Geschwindigkeit ") + (Math.round(s * 100) / 100).toFixed(2) + "×") }
    function toggleFullscreen() {
        if (fullscreen) visibility = wasVisibility === Window.Maximized ? Window.Maximized : Window.Windowed
        else { wasVisibility = visibility; visibility = Window.FullScreen }
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
        case "shuffle.toggle": playlist.shuffle = !playlist.shuffle; osd(qsTr("Zufall: ") + (playlist.shuffle ? "an" : "aus")); break
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
            var modes = ["fit", "fill", "stretch", "16:9", "4:3", "21:9"]
            act("aspect.set", modes[(modes.indexOf(aspect) + 1) % modes.length]); break
        case "aspect.set":
            aspect = arg; player.setAspect(arg); app.setSetting("keepAspect", arg)
            osd(qsTr("Bild: ") + ({ "fit": qsTr("Einpassen"), "fill": qsTr("Füllen"), "stretch": qsTr("Strecken") }[arg] || arg)); break
        case "snapshot": if (player.state !== 0) app.snapshot(); break
        case "diagnostics": diagnosticsVisible = !diagnosticsVisible; break
        case "effects.toggle": if (effects) effects.toggleEnabled(); else osd(qsTr("Effekt-Plugin nicht aktiv")); break
        case "effects.compare": if (effects) effects.toggleCompare(); else osd(qsTr("Effekt-Plugin nicht aktiv")); break
        case "effects.panel": sidebarVisible = true; app.setSetting("showSidebar", true); tabs.selectTab("Effekte"); break
        case "open.file": openFileDialog.open(); break
        case "open.folder": openFolderDialog.open(); break
        case "open.url": urlDialog.playNow = true; urlDialog.open(); break
        case "playlist.save": if (playlist.count > 0) savePlaylistDialog.open(); break
        case "sidebar.toggle": sidebarVisible = !sidebarVisible; app.setSetting("showSidebar", sidebarVisible); break
        case "mediainfo": if (player.state !== 0) mediaInfoDialog.open(); break
        case "settings": settingsDialog.open(); break
        case "quit": Qt.quit(); break
        case "minimal": minimal = !minimal; break
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
                context: Qt.ApplicationShortcut
                onActivated: win.act(modelData.id)
            }
        }
    }
    Shortcut { sequence: "Esc"; enabled: win.modalCount === 0; onActivated: { if (win.fullscreen) win.toggleFullscreen(); else if (win.minimal) win.minimal = false } }
    Shortcut { sequence: "F1"; enabled: win.modalCount === 0; onActivated: { settingsDialog.open(); settingsDialog.page = 7 } }

    // ------------------------------------------------------------- Menüleiste
    menuBar: MenuBar {
        visible: !win.overlayControls
        height: visible ? implicitHeight : 0
        Menu {
            title: qsTr("&Datei")
            MenuItem { text: qsTr("Datei öffnen…"); onTriggered: act("open.file") }
            MenuItem { text: qsTr("Ordner öffnen…"); onTriggered: act("open.folder") }
            MenuItem { text: qsTr("Netzwerkstream öffnen…"); onTriggered: act("open.url") }
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
            MenuItem { text: qsTr("Wiederholen: ") + (playlist.repeat === "none" ? "aus" : playlist.repeat === "all" ? "alle" : "eins"); onTriggered: act("repeat.cycle") }
            MenuItem { text: qsTr("Zufallswiedergabe"); checkable: true; checked: playlist.shuffle; onTriggered: act("shuffle.toggle") }
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
            MenuItem { text: qsTr("Effekt-Panel öffnen"); enabled: win.effects !== null; onTriggered: act("effects.panel") }
            MenuItem { text: qsTr("LUT-Galerie…"); enabled: win.effects !== null; onTriggered: { act("effects.panel"); win.effects.requestGallery() } }
            MenuItem { text: qsTr("Beispielprofil von GitHub laden (My_Profile.json)"); enabled: win.effects !== null; onTriggered: win.effects.loadExampleProfile() }
            MenuItem { text: qsTr("LUT-Paket von GitHub laden"); enabled: win.effects !== null; onTriggered: win.effects.downloadLutPackage() }
            MenuItem { text: win.effects ? qsTr("Ultimate Video Enhancer aktiv") : qsTr("Ultimate Video Enhancer nicht aktiv"); enabled: false }
        }
        Menu {
            title: qsTr("&Plugins")
            MenuItem { text: qsTr("Plugins verwalten…"); onTriggered: pluginsDialog.open() }
            MenuItem { text: qsTr("Safe Mode beenden"); visible: app.safeMode; height: visible ? implicitHeight : 0; onTriggered: app.resetSafeMode() }
        }
        Menu {
            title: qsTr("A&nsicht")
            MenuItem { text: qsTr("Seitenleiste"); checkable: true; checked: win.sidebarVisible; onTriggered: act("sidebar.toggle") }
            MenuItem { text: qsTr("Minimale Oberfläche"); checkable: true; checked: win.minimal; onTriggered: act("minimal") }
            MenuItem { text: qsTr("Medieninformationen…"); onTriggered: act("mediainfo") }
            MenuItem { text: qsTr("Helles Farbschema"); checkable: true; checked: app.theme === "light"; onTriggered: app.theme = (app.theme === "light" ? "dark" : "light") }
            MenuSeparator {}
            MenuItem { text: qsTr("Einstellungen…"); onTriggered: act("settings") }
        }
        Menu {
            title: qsTr("&Hilfe")
            MenuItem { text: qsTr("Tastenkürzel"); onTriggered: { settingsDialog.open(); settingsDialog.page = 7 } }
            MenuItem { text: qsTr("Nach Updates für yt-dlp und libmpv suchen"); onTriggered: { app.updater.check(); osd(qsTr("Suche nach Updates …"), 2500); updateCheckManual.active = true } }
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

            MpvVideo {
                id: video
                controller: app.player
                anchors { left: parent.left; right: parent.right; top: parent.top; bottom: parent.bottom
                          bottomMargin: win.overlayControls ? 0 : controls.height }
            }

            // Audio ohne Bild
            Column {
                anchors.centerIn: video
                visible: player.state !== 0 && !player.hasVideo
                spacing: 14
                Label { text: "♪"; font.pixelSize: 96; color: theme.accent; anchors.horizontalCenter: parent.horizontalCenter }
                Label { text: player.mediaTitle; color: "#bbb"; font.pixelSize: 18; anchors.horizontalCenter: parent.horizontalCenter }
            }

            // Startbildschirm
            Rectangle {
                anchors.fill: video
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
                anchors.fill: video
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
                aspectText: ({ "fit": qsTr("Einpassen"), "fill": qsTr("Füllen"), "stretch": qsTr("Strecken") })[win.aspect] || win.aspect
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
            readonly property var panels: (app.plugins ? app.plugins.plugins : []).filter(p => p.panelUrl && String(p.panelUrl).length > 0)

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
                                if (sidebar.panels[i].id === "org.nextscript.ultimate-video-enhancer") { currentIndex = i + 1; return }
                        }
                        currentIndex = 0
                    }
                    TabButton { text: qsTr("Wiedergabeliste") }
                    Repeater {
                        model: sidebar.panels
                        TabButton { required property var modelData; text: modelData.panelTitle }
                    }
                }
                StackLayout {
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
                            source: modelData.panelUrl
                            onLoaded: { item.theme = theme; item.host = app; item.controller = modelData.controller }
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

    OpenUrlDialog { id: urlDialog; theme: theme; onOpened: win.modalCount++; onClosed: win.modalCount = Math.max(0, win.modalCount - 1) }
    MediaInfoDialog { id: mediaInfoDialog; theme: theme; onOpened: win.modalCount++; onClosed: win.modalCount = Math.max(0, win.modalCount - 1) }
    AboutDialog { id: aboutDialog; theme: theme; onOpened: win.modalCount++; onClosed: win.modalCount = Math.max(0, win.modalCount - 1) }
    PluginsDialog { id: pluginsDialog; theme: theme; onOpened: win.modalCount++; onClosed: win.modalCount = Math.max(0, win.modalCount - 1) }
    UpdateDialog { id: updateDialog; theme: theme; onOpened: win.modalCount++; onClosed: win.modalCount = Math.max(0, win.modalCount - 1) }
    SettingsDialog {
        id: settingsDialog
        theme: theme
        onOpened: win.modalCount++
        onClosed: win.modalCount = Math.max(0, win.modalCount - 1)
        onOpenPlugins: { close(); pluginsDialog.open() }
        onOpenEffects: { close(); win.act("effects.panel") }
    }

    TrackedDialog {
        id: gotoDialog
        title: qsTr("Zu Zeit springen")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Component.onCompleted: { var c = standardButton(Dialog.Cancel); if (c) c.text = qsTr("Abbrechen") }
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

    // Manuelle Update-Prüfung: Ergebnis auch melden, wenn alles aktuell ist
    QtObject { id: updateCheckManual; property bool active: false }
    Connections {
        target: app.updater
        function onCheckFinished(updates, errors) {
            if (!updateCheckManual.active) return
            updateCheckManual.active = false
            if (updates.length === 0) win.osd(errors.length ? qsTr("Update-Prüfung fehlgeschlagen: ") + errors.join("; ") : qsTr("yt-dlp und libmpv sind aktuell"), 3500)
        }
    }
    Connections {
        target: app
        function onOsd(text, ms) { win.osd(text, ms) }
        function onToolUpdatesAvailable(updates) { updateDialog.show(updates) }
        function onRequestRaise() { win.raise(); win.requestActivate() }
    }
    Component.onCompleted: {
        player.setAspect(aspect)
        if (app.selftest) selftestUi.start()
    }

    // Selbsttest der Oberfläche. Im UI-Modus (UVE_SELFTEST_UI=1) werden echte Mausereignisse an das Fenster
    // gesendet: Doppelklick in der Wiedergabeliste, Klick/Doppelklick/Rechtsklick auf das Bild, Effekt-Vergleich.
    property var stUi: ({})
    property int stStep: 0
    function videoCenter() { return stageMouse.mapToItem(null, stageMouse.width / 2, stageMouse.height / 2) }
    function videoRect() { var p = video.mapToItem(null, video.width * 0.2, video.height * 0.25); return { x: p.x, y: p.y, w: video.width * 0.6, h: video.height * 0.5 } }
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
                if (win.fullscreen) win.toggleFullscreen()
                interval = 900; break
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
                ui.shaderError = app.shaders.lastError
                ui.activeShaders = app.shaders.activeShaders.length
                ui.ok = ui.firstPlaying && ui.dblclickPlaylistPlays && ui.clickPauses && ui.clickResumes
                        && ui.dblclickFullscreen && ui.dblclickKeepsPlaying && ui.rightClickMenu
                        && ui.effectsPluginActive && ui.effectsTabShown && !ui.shaderError && ui.fxVisible
                        && ui.exampleLoaded && ui.exampleBasisOk && !ui.exampleShaderError && ui.lutVisible && ui.galleryCount >= 83
                ui.done = true
                app.reportSelftestUi(ui)
                return
            }
            win.stUi = ui
            win.stStep++
            restart()
        }
    }
}
