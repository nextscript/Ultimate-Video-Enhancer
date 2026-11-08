import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Modal nach der Start-Prüfung: neue Versionen von yt-dlp / libmpv
Dialog {
    id: dlg
    property var theme
    property var updates: []
    property var chosen: ({})
    property var results: []
    property bool installing: false
    property string progressText: ""
    property real progressValue: 0
    property int queueIndex: -1
    title: installing ? qsTr("Updates werden installiert") : (results.length ? qsTr("Update-Ergebnis") : qsTr("Updates verfügbar"))
    modal: true
    closePolicy: installing ? Popup.NoAutoClose : Popup.CloseOnEscape
    width: Math.min(620, parent ? parent.width - 40 : 620)
    anchors.centerIn: parent

    function show(list) {
        updates = list
        var c = {}; for (var i = 0; i < list.length; ++i) c[list[i].tool] = true
        chosen = c; results = []; installing = false; queueIndex = -1; skip.checked = false
        open()
    }
    function fmtMB(b) { return (b / 1048576).toFixed(1) + qsTr(" MB") }
    function startInstall() {
        installing = true; results = []; queueIndex = -1
        nextInQueue()
    }
    function nextInQueue() {
        do { queueIndex++ } while (queueIndex < updates.length && !chosen[updates[queueIndex].tool])
        if (queueIndex >= updates.length) { installing = false; return }
        progressValue = 0
        progressText = updates[queueIndex].name + qsTr(" wird heruntergeladen…")
        app.updater.install(updates[queueIndex].tool)
    }
    Connections {
        target: app.updater
        function onProgress(tool, received, total) {
            dlg.progressValue = total > 0 ? received / total : 0
            dlg.progressText = tool + ": " + dlg.fmtMB(received) + (total > 0 ? " / " + dlg.fmtMB(total) : "")
        }
        function onInstallFinished(tool, ok, message, restartRequired) {
            if (!dlg.installing) return
            dlg.results = dlg.results.concat([{ ok: ok, text: message, restart: restartRequired }])
            dlg.nextInQueue()
        }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Später"); visible: !dlg.installing && !dlg.results.length; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button {
            text: qsTr("Jetzt aktualisieren"); visible: !dlg.installing && !dlg.results.length
            enabled: Object.keys(dlg.chosen).some(k => dlg.chosen[k])
            highlighted: true
            onClicked: dlg.startInstall()
        }
        Button { text: qsTr("Schließen"); visible: !dlg.installing && dlg.results.length > 0; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
    }
    onRejected: {
        if (!skip.checked) return
        var s = Object.assign({}, app.setting("skippedToolVersions", {}))
        for (var i = 0; i < updates.length; ++i) s[updates[i].tool] = updates[i].id
        app.setSetting("skippedToolVersions", s)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        Label { visible: !dlg.installing && !dlg.results.length; text: qsTr("Für folgende Werkzeuge gibt es neue Versionen:"); color: theme.text }
        Repeater {
            model: dlg.installing || dlg.results.length ? [] : dlg.updates
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                CheckBox {
                    checked: dlg.chosen[modelData.tool] === true
                    onToggled: { var c = Object.assign({}, dlg.chosen); c[modelData.tool] = checked; dlg.chosen = c }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label { text: modelData.name; font.bold: true; color: theme.text }
                    Label {
                        text: modelData.current + "  →  " + modelData.latest + (modelData.size > 0 ? "   ·   " + dlg.fmtMB(modelData.size) : "")
                        color: theme.muted; font.family: "Consolas"
                    }
                }
            }
        }
        Label {
            visible: !dlg.installing && !dlg.results.length
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.muted
            font.pixelSize: 12
            text: qsTr("yt-dlp wird für YouTube, Twitch und andere Webseiten verwendet – neue Versionen sind oft nötig, wenn Seiten etwas ändern. ")
                + qsTr("libmpv ist die Wiedergabe-Engine; ein Update wird nach einem Neustart aktiv. Downloads werden per SHA-256 geprüft.")
        }
        CheckBox { id: skip; visible: !dlg.installing && !dlg.results.length; text: qsTr("Diese Versionen überspringen") }

        ColumnLayout {
            visible: dlg.installing
            Layout.fillWidth: true
            Label { text: dlg.progressText; color: theme.text }
            ProgressBar { Layout.fillWidth: true; value: dlg.progressValue }
        }
        Repeater {
            model: dlg.results
            delegate: Label {
                required property var modelData
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: (modelData.ok ? "✓ " : "✗ ") + modelData.text
                color: modelData.ok ? theme.text : theme.error
            }
        }
    }
}
