import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Einstellungen: Allgemein, Wiedergabe, Video & GPU, Audio, Untertitel, Plugins, Effekte, Tastenkürzel, Datenschutz, Netzwerk, Erweitert
Dialog {
    id: dlg
    property var theme
    signal openPlugins()
    signal openEffects()
    title: qsTr("Einstellungen")
    modal: true
    width: Math.min(900, parent ? parent.width - 40 : 900)
    height: Math.min(640, parent ? parent.height - 40 : 640)
    anchors.centerIn: parent
    standardButtons: Dialog.Close
    Component.onCompleted: { var b = standardButton(Dialog.Close); if (b) b.text = qsTr("Schließen") }
    property alias page: nav.currentIndex
    property string capturingId: ""
    property string conflictMsg: ""

    onAboutToShow: { audioBox.reload(); capturingId = ""; conflictMsg = "" }

    component SectionLabel: Label { color: theme.muted; font.pixelSize: 11; font.letterSpacing: 1; Layout.topMargin: 8 }
    component Row2: RowLayout { Layout.fillWidth: true; spacing: 12 }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        ListView {
            id: nav
            Layout.preferredWidth: 180
            Layout.fillHeight: true
            model: [qsTr("Allgemein"), qsTr("Wiedergabe"), qsTr("Video & GPU"), qsTr("Audio"), qsTr("Untertitel"), qsTr("Plugins"), qsTr("Effekte"), qsTr("Tastenkürzel"), qsTr("Datenschutz"), qsTr("Netzwerk"), qsTr("Erweitert")]
            currentIndex: 0
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                width: ListView.view.width
                text: modelData.replace("&", "&&")   // "&" sonst als Tastenkürzel-Markierung gedeutet
                highlighted: ListView.isCurrentItem
                onClicked: nav.currentIndex = index
            }
        }
        Rectangle { Layout.fillHeight: true; width: 1; color: theme.line }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            currentIndex: nav.currentIndex

            // Allgemein
            ColumnLayout {
                spacing: 8
                Row2 {
                    Label { text: qsTr("Sprache") + (app.language !== "en" ? " / Language" : ""); color: theme.text; Layout.fillWidth: true }
                    ComboBox {
                        id: langBox
                        Layout.preferredWidth: 260
                        model: app.languages
                        textRole: "nativeName"
                        currentIndex: Math.max(0, app.languages.findIndex(l => l.code === app.language))
                        delegate: ItemDelegate {
                            required property var modelData
                            width: langBox.width
                            text: modelData.nativeName + (modelData.name && modelData.name !== modelData.nativeName ? "  ·  " + modelData.name : "")
                        }
                        onActivated: (i) => { var e = app.setLanguage(app.languages[i].code); langMsg.text = e }
                    }
                }
                Label { id: langMsg; visible: text.length > 0; color: theme.error; Layout.fillWidth: true; wrapMode: Text.Wrap }
                RowLayout {
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.muted; font.pixelSize: 11
                        text: qsTr("Eigene Sprache: lang/uve_en.ts als z. B. uve_fr.ts kopieren, mit Qt Linguist übersetzen und als uve_fr.qm veröffentlichen – Anleitung in lang/README.md.")
                    }
                    Button { text: qsTr("Sprachordner öffnen"); onClicked: app.openFolder(app.languageFolders()[0]) }
                }
                Row2 { Label { text: qsTr("Farbschema"); color: theme.text; Layout.fillWidth: true }
                    ComboBox { model: [qsTr("Dunkel"), qsTr("Hell")]; currentIndex: app.theme === "light" ? 1 : 0; onActivated: (i) => app.theme = i === 1 ? "light" : "dark" } }
                CheckBox { text: qsTr("Letzte Wiedergabeliste beim Start wiederherstellen"); checked: app.setting("restoreSession", true); onToggled: app.setSetting("restoreSession", checked) }
                CheckBox { text: qsTr("Beim Start nach Updates für yt-dlp und libmpv suchen (Netzwerkzugriff auf GitHub)"); checked: app.setting("checkToolUpdates", true); onToggled: app.setSetting("checkToolUpdates", checked) }
                Button { text: qsTr("Jetzt nach Updates suchen"); enabled: !app.updater.busy; onClicked: app.updater.check() }
                Item { Layout.fillHeight: true }
            }
            // Wiedergabe
            ColumnLayout {
                spacing: 8
                CheckBox { text: qsTr("Wiedergabe an der letzten Position fortsetzen"); checked: app.setting("resumePlayback", true); onToggled: app.setSetting("resumePlayback", checked) }
                Row2 { Label { text: qsTr("Sprungweite Pfeiltasten (Sekunden)"); color: theme.text; Layout.fillWidth: true }
                    SpinBox { from: 1; to: 600; value: app.setting("seekStep", 10); editable: true; onValueModified: app.setSetting("seekStep", value) } }
                Row2 { Label { text: qsTr("Maximale Auflösung für YouTube/Twitch"); color: theme.text; Layout.fillWidth: true }
                    ComboBox {
                        model: [0, 2160, 1440, 1080, 720, 480]
                        currentIndex: Math.max(0, model.indexOf(app.setting("streamQualityLimit", 0)))
                        displayText: currentValue > 0 ? currentValue + "p" : qsTr("Beste")
                        delegate: ItemDelegate { required property int modelData; required property int index; width: ListView.view ? ListView.view.width : 120
                            text: modelData > 0 ? modelData + "p" : qsTr("Beste") }
                        onActivated: (i) => app.setSetting("streamQualityLimit", model[i])
                    } }
                Item { Layout.fillHeight: true }
            }
            // Video & GPU
            ColumnLayout {
                spacing: 8
                Row2 { Label { text: qsTr("Hardware-Dekodierung"); color: theme.text; Layout.fillWidth: true }
                    ComboBox {
                        id: hw
                        model: [["auto-safe", qsTr("Automatisch (empfohlen)")], ["auto-copy", qsTr("Automatisch mit Kopie (kompatibler)")], ["d3d11va", qsTr("Direct3D 11 (d3d11va)")],
                                ["dxva2", qsTr("DXVA2")], ["nvdec", qsTr("NVIDIA NVDEC")], ["vulkan", qsTr("Vulkan")], ["no", qsTr("Aus (Software)")]]
                        textRole: "1"
                        currentIndex: Math.max(0, model.findIndex(m => m[0] === app.setting("hwdec", "auto-safe")))
                        displayText: model[currentIndex][1]
                        delegate: ItemDelegate { required property var modelData; width: hw.width; text: modelData[1] }
                        onActivated: (i) => app.applyHwdec(model[i][0])
                    } }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.muted
                    text: qsTr("Aktiv: ") + (app.player.diagnostics["hwdec-current"] || qsTr("Software")) + qsTr(". Bei Bildfehlern auf „mit Kopie“ oder „Aus“ wechseln. ")
                        + qsTr("Die Videoausgabe läuft über die libmpv-Render-API (OpenGL); Effekt-Shader nutzen die mpv-Benutzer-Shader.")
                }
                Item { Layout.fillHeight: true }
            }
            // Audio
            ColumnLayout {
                spacing: 8
                Row2 { Label { text: qsTr("Audiogerät"); color: theme.text; Layout.fillWidth: true }
                    ComboBox {
                        id: audioBox
                        Layout.preferredWidth: 360
                        property var devices: []
                        function reload() { devices = app.audioDevices() }
                        model: devices
                        textRole: "description"
                        currentIndex: Math.max(0, devices.findIndex(d => d.name === app.setting("audioDevice", "auto")))
                        onActivated: (i) => app.setAudioDevice(devices[i].name)
                    } }
                Row2 { Label { text: qsTr("Bevorzugte Audiosprachen (z. B. de,en)"); color: theme.text; Layout.fillWidth: true }
                    TextField { text: app.setting("audioLanguages", "de,ger,deu,en,eng"); onEditingFinished: app.setSetting("audioLanguages", text) } }
                Item { Layout.fillHeight: true }
            }
            // Untertitel
            ColumnLayout {
                spacing: 8
                CheckBox { text: qsTr("Untertiteldateien mit gleichem Namen automatisch laden"); checked: app.setting("subAuto", true); onToggled: app.setSetting("subAuto", checked) }
                Row2 { Label { text: qsTr("Schriftgröße"); color: theme.text; Layout.fillWidth: true }
                    SpinBox { from: 20; to: 100; value: app.setting("subFontSize", 46); onValueModified: app.setSetting("subFontSize", value) } }
                Row2 { Label { text: qsTr("Bevorzugte Untertitelsprachen"); color: theme.text; Layout.fillWidth: true }
                    TextField { text: app.setting("subLanguages", "de,ger,deu,en,eng"); onEditingFinished: app.setSetting("subLanguages", text) } }
                Item { Layout.fillHeight: true }
            }
            // Plugins
            ColumnLayout {
                spacing: 8
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.text
                        text: app.safeMode ? qsTr("Safe Mode aktiv: ") + app.safeModeReason : ((app.plugins ? app.plugins.plugins.length : 0) + qsTr(" Plugins gefunden.")) }
                Button { text: qsTr("Plugin-Verwaltung öffnen…"); onClicked: dlg.openPlugins() }
                Button { visible: app.safeMode; text: qsTr("Safe Mode beenden"); onClicked: app.resetSafeMode() }
                Item { Layout.fillHeight: true }
            }
            // Effekte
            ColumnLayout {
                spacing: 8
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.text
                        text: qsTr("Effekte stellt das Ultimate-Video-Enhancer-Plugin bereit (Seitenleiste → Effekte). Dort lassen sich Presets, ein Standard-Preset für neue Dateien und Presets pro Datei festlegen.") }
                Button { text: qsTr("Effekt-Panel öffnen"); onClicked: dlg.openEffects() }
                Item { Layout.fillHeight: true }
            }
            // Tastenkürzel
            ColumnLayout {
                spacing: 6
                RowLayout {
                    Label { Layout.fillWidth: true; color: dlg.conflictMsg ? theme.error : theme.muted; wrapMode: Text.Wrap
                            text: dlg.conflictMsg || (dlg.capturingId ? qsTr("Neue Taste drücken … (Esc = Abbrechen, Entf = leeren)") : qsTr("„Ändern“ klicken und die neue Tastenkombination drücken.")) }
                    Button { text: qsTr("Alle zurücksetzen"); onClicked: { app.hotkeys.resetAll(); dlg.conflictMsg = "" } }
                }
                ListView {
                    id: keys
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: app.hotkeys.items
                    ScrollBar.vertical: ScrollBar {}
                    section.property: "category"
                    section.delegate: Label { required property string section; text: section; color: theme.accent; font.bold: true; topPadding: 8 }
                    delegate: RowLayout {
                        required property var modelData
                        width: keys.width - 12
                        Label { text: modelData.label; color: theme.text; Layout.fillWidth: true }
                        Label {
                            text: dlg.capturingId === modelData.id ? "…" : (modelData.nativeText || "—")
                            color: modelData.modified ? theme.accent : theme.muted
                            font.family: "Consolas"
                            Layout.preferredWidth: 140
                        }
                        Button { text: qsTr("Ändern"); onClicked: { dlg.capturingId = modelData.id; dlg.conflictMsg = ""; capture.forceActiveFocus() } }
                        Button { text: "↺"; enabled: modelData.modified; onClicked: app.hotkeys.reset(modelData.id) }
                    }
                }
                Item {
                    id: capture
                    focus: dlg.capturingId !== ""
                    Keys.onPressed: (e) => {
                        if (!dlg.capturingId) return
                        e.accepted = true
                        if (e.key === Qt.Key_Escape) { dlg.capturingId = ""; return }
                        if (e.key === Qt.Key_Delete) { app.hotkeys.setShortcut(dlg.capturingId, "", true); dlg.capturingId = ""; return }
                        var seq = app.hotkeys.sequenceFromKey(e.key, e.modifiers)
                        if (!seq) return
                        var other = app.hotkeys.conflictFor(dlg.capturingId, seq)
                        if (other) {
                            dlg.conflictMsg = "„" + seq + qsTr("“ ist bereits „") + app.hotkeys.labelFor(other) + qsTr("“ zugewiesen. Erneut drücken, um es zu übernehmen (die andere Aktion verliert die Belegung).")
                            if (capture.pendingSeq === seq) { app.hotkeys.setShortcut(dlg.capturingId, seq, true); dlg.capturingId = ""; dlg.conflictMsg = "" }
                            capture.pendingSeq = seq
                            return
                        }
                        app.hotkeys.setShortcut(dlg.capturingId, seq, false)
                        dlg.capturingId = ""; capture.pendingSeq = ""
                    }
                    property string pendingSeq: ""
                }
            }
            // Datenschutz
            ColumnLayout {
                spacing: 8
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.text
                        text: qsTr("Der Player sendet keine Telemetrie. Netzwerkzugriffe erfolgen nur für geöffnete Streams und – falls aktiviert – für die Update-Prüfung bei GitHub.") }
                CheckBox { text: qsTr("Verlauf (zuletzt geöffnet) speichern"); checked: app.setting("rememberHistory", true); onToggled: app.setSetting("rememberHistory", checked) }
                Button { text: qsTr("Verlauf löschen (") + app.recentFiles.length + ")"; onClicked: app.clearRecentFiles() }
                Button { text: qsTr("Gemerkte Wiedergabepositionen löschen"); onClicked: app.clearResumePositions() }
                Item { Layout.fillHeight: true }
            }
            // Netzwerk (Proxy)
            ColumnLayout {
                id: net
                spacing: 8
                property var cur: ({})
                property string mode: "none"
                property string testMsg: ""
                property bool testOk: false
                property bool testing: false
                property string saveMsg: ""
                function load() {
                    cur = app.proxySettings()
                    mode = cur.mode
                    pType.currentIndex = cur.type === "socks5" ? 1 : 0
                    pHost.text = cur.host; pPort.value = cur.port; pUser.text = cur.user
                    pPass.text = ""; pPass.placeholderText = cur.hasPassword ? qsTr("•••••• (gespeichert – leer lassen zum Behalten)") : qsTr("Passwort (optional)")
                    pBypass.text = cur.bypass
                    testMsg = ""; saveMsg = ""
                }
                function collect() {
                    var m = { mode: mode, type: pType.currentIndex === 1 ? "socks5" : "http", host: pHost.text.trim(), port: pPort.value,
                              user: pUser.text, bypass: pBypass.text }
                    if (pPass.text.length) m.password = pPass.text
                    if (!pUser.text.length) m.clearPassword = true
                    return m
                }
                Component.onCompleted: load()
                Connections {
                    target: dlg
                    function onAboutToShow() { net.load() }
                }
                Connections {
                    target: app
                    function onProxyTestFinished(ok, message) { net.testing = false; net.testOk = ok; net.testMsg = message }
                }

                SectionLabel { text: qsTr("PROXY FÜR NETZWERKSTREAMS") }
                ButtonGroup { id: modeGroup }
                RadioButton { text: qsTr("Kein Proxy (direkte Verbindung)"); ButtonGroup.group: modeGroup; checked: net.mode === "none"; onClicked: net.mode = "none" }
                RadioButton { text: qsTr("Systemproxy von Windows verwenden (") + (net.cur.systemProxy || "") + ")"; ButtonGroup.group: modeGroup; checked: net.mode === "system"; onClicked: net.mode = "system" }
                RadioButton { text: qsTr("Manuell"); ButtonGroup.group: modeGroup; checked: net.mode === "manual"; onClicked: net.mode = "manual" }
                GridLayout {
                    enabled: net.mode === "manual"
                    opacity: enabled ? 1 : 0.5
                    columns: 4; columnSpacing: 10; rowSpacing: 6
                    Layout.fillWidth: true
                    Label { text: qsTr("Typ"); color: theme.text }
                    ComboBox { id: pType; model: ["HTTP", qsTr("SOCKS5")]; Layout.columnSpan: 3 }
                    Label { text: qsTr("Adresse"); color: theme.text }
                    TextField { id: pHost; Layout.fillWidth: true; placeholderText: qsTr("proxy.example.org oder 10.0.0.1"); inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly }
                    Label { text: qsTr("Port"); color: theme.text }
                    SpinBox { id: pPort; from: 1; to: 65535; editable: true; value: 8080
                              textFromValue: function (v) { return String(v) }; valueFromText: function (t) { return parseInt(t) || 0 } }
                    Label { text: qsTr("Benutzer"); color: theme.text }
                    TextField { id: pUser; Layout.fillWidth: true; placeholderText: qsTr("optional") }
                    Label { text: qsTr("Passwort"); color: theme.text }
                    TextField { id: pPass; Layout.preferredWidth: 220; echoMode: TextInput.Password }
                }
                Row2 {
                    Label { text: qsTr("Ausnahmen (kein Proxy)"); color: theme.text }
                    TextField { id: pBypass; Layout.fillWidth: true; placeholderText: "localhost,127.0.0.1,::1"; enabled: net.mode !== "none" }
                }
                RowLayout {
                    Button {
                        text: qsTr("Übernehmen"); highlighted: true
                        onClicked: {
                            var e = app.setProxy(net.collect())
                            if (!e.length) net.load()   // setzt Felder (z. B. Passwort-Hinweis) neu
                            net.saveMsg = e.length ? e : qsTr("Übernommen – gilt ab dem nächsten geöffneten Stream.")
                        }
                    }
                    Button {
                        text: net.testing ? qsTr("Teste …") : qsTr("Verbindung testen"); enabled: !net.testing
                        onClicked: { net.testing = true; net.testMsg = ""; app.testProxy(net.collect()) }
                    }
                    BusyIndicator { running: net.testing; visible: running; implicitWidth: 22; implicitHeight: 22 }
                }
                Label { visible: net.testMsg.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; text: net.testMsg; color: net.testOk ? "#5fd38d" : theme.error }
                Label { visible: net.saveMsg.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; text: net.saveMsg; color: theme.muted }
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.text; text: qsTr("Aktiv: ") + app.proxyStatus }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.muted; font.pixelSize: 11
                    text: qsTr("Über den Proxy laufen: HTTP(S)-, HLS- und DASH-Streams, YouTube/Twitch (yt-dlp), tcp://-Streams sowie Update- und LUT-Downloads. ")
                        + qsTr("SOCKS5 wird für mpv über eine lokale Brücke (nur 127.0.0.1) bereitgestellt; Hostnamen werden am Proxy aufgelöst. ")
                        + qsTr("Nicht über Proxys möglich: udp://, rtp://, rtmp://, rtsp:// und srt://. Das Passwort wird mit Windows-DPAPI verschlüsselt gespeichert.")
                }
                Item { Layout.fillHeight: true }
            }
            // Erweitert
            ColumnLayout {
                spacing: 8
                GridLayout {
                    columns: 2; columnSpacing: 12
                    Label { text: "libmpv"; color: theme.muted }
                    Label { text: app.mpvLibrary; color: theme.text; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                    Label { text: "yt-dlp"; color: theme.muted }
                    Label { text: (app.updater.installed["yt-dlp"] || {}).path || qsTr("nicht gefunden"); color: theme.text; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                    Label { text: qsTr("Datenordner"); color: theme.muted }
                    Label { text: app.dataDir; color: theme.text; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                }
                RowLayout {
                    Button { text: qsTr("Datenordner öffnen"); onClicked: app.openFolder(app.dataDir) }
                    Button { text: qsTr("Protokoll öffnen"); onClicked: app.openFolder(app.logFile()) }
                }
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.muted
                        text: qsTr("Start ohne Plugins: UVEPlayer.exe --safe-mode. Portables Profil: Datei „portable.txt“ neben die EXE legen (Daten in ./data).") }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
