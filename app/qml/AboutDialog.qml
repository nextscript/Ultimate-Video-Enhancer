import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dlg
    property var theme
    title: qsTr("Über UVE Player")
    modal: true
    width: Math.min(620, parent ? parent.width - 40 : 620)
    anchors.centerIn: parent
    standardButtons: Dialog.Close
    Component.onCompleted: { var b = standardButton(Dialog.Close); if (b) b.text = Qt.binding(() => qsTr("Schließen")) }

    // Update-Prüfung wie bei Chrome: Öffnen des Fensters prüft sofort, das Ergebnis steht direkt hier,
    // ein Release lässt sich hier installieren (Download mit SHA-256-Prüfung, danach Neustart).
    signal toolUpdatesRequested(var updates)
    signal commitsRequested(var result)
    // checking | current | update | commits | error | installing | restarting | handedoff | failed
    property string playerState: "checking"
    property var updateInfo: ({})
    property string errorText: ""
    property real progressValue: 0
    property string progressText: ""
    property bool toolsChecking: false
    property var toolUpdates: []
    property string toolsError: ""
    readonly property bool working: playerState === "checking" || playerState === "installing" || playerState === "restarting"
    readonly property bool problem: playerState === "error" || playerState === "failed"

    function fmtMB(b) { return (b / 1048576).toFixed(1) + qsTr(" MB") }
    function startCheck() {
        playerState = "checking"; errorText = ""; updateInfo = ({})
        toolsChecking = true; toolUpdates = []; toolsError = ""
        if (!app.appUpdater.busy) app.appUpdater.check()   // sonst läuft die Start-Prüfung noch – ihr Ergebnis kommt hier an
        if (!app.updater.busy) app.updater.check()
    }
    function install() {
        errorText = ""; progressValue = 0
        progressText = qsTr("Update wird heruntergeladen …")
        playerState = "installing"
        app.appUpdater.install()
    }
    function statusTitle() {
        switch (playerState) {
        case "checking": return qsTr("Suche nach Updates …")
        case "current": return qsTr("UVE Player ist auf dem neuesten Stand")
        case "update": return qsTr("Update verfügbar: UVE Player %1").arg(updateInfo.latestVersion || "")
        case "commits": return qsTr("Neue Commits auf GitHub (noch kein Release)")
        case "error": return qsTr("Update-Prüfung fehlgeschlagen")
        case "installing": return qsTr("UVE Player wird aktualisiert …")
        case "restarting": return qsTr("Installation wird gestartet – der Player wird neu gestartet …")
        case "handedoff": return qsTr("Update bereit")
        case "failed": return qsTr("Das Update wurde nicht installiert")
        }
        return ""
    }
    function statusDetail() {
        switch (playerState) {
        case "current": return qsTr("Version %1").arg(app.version)
        case "update":
            return updateInfo.canInstall === true
                ? qsTr("Version %1 · %2 – wird per SHA-256 geprüft, danach startet der Player neu.").arg(updateInfo.latestVersion || "").arg(updateInfo.assetSize > 0 ? fmtMB(updateInfo.assetSize) : "")
                : qsTr("Für diese Installation gibt es kein automatisches Update. Das neue Paket liegt auf der GitHub-Seite des Releases.")
        case "commits": return qsTr("Neue Commits: %1 – sobald es ein Release gibt, kann es hier installiert werden.").arg((updateInfo.newCommits || []).length)
        case "installing": case "handedoff": return progressText
        case "error": case "failed": return errorText
        }
        return ""
    }
    onOpened: {
        // Kein Knopf mit Fokus: sonst löst das Loslassen von Enter (Menü „Über UVE Player“ per Tastatur) ihn aus
        contentItem.forceActiveFocus()
        if (playerState !== "installing" && playerState !== "restarting") startCheck()
    }

    Connections {
        target: app.appUpdater
        function onCheckFinished(r, error) {
            if (!dlg.visible || dlg.playerState === "installing" || dlg.playerState === "restarting") return
            dlg.updateInfo = r
            if (error && r.updateAvailable !== true) { dlg.errorText = error; dlg.playerState = "error" }
            else if (r.updateAvailable === true) dlg.playerState = "update"
            else if (r.newerCommits === true) dlg.playerState = "commits"
            else dlg.playerState = "current"
        }
        function onProgress(received, total) {
            if (dlg.playerState !== "installing") return
            dlg.progressValue = total > 0 ? received / total : 0
            dlg.progressText = dlg.fmtMB(received) + (total > 0 ? " / " + dlg.fmtMB(total) : "")
        }
        function onRetrying(attempt, maxAttempts, delaySeconds) {
            if (dlg.playerState === "installing") dlg.progressText = qsTr("GitHub antwortet gerade nicht – neuer Versuch in %1 s (der Download wird fortgesetzt)").arg(delaySeconds)
        }
        function onPackageReady(file) { if (dlg.playerState === "installing") { dlg.playerState = "restarting"; dlg.progressValue = 1 } }
        function onInstallFailed(message) { if (dlg.playerState === "installing") { dlg.errorText = message; dlg.playerState = "failed" } }
        function onInstallHandedOff(message) { if (dlg.playerState === "installing") { dlg.progressText = message; dlg.playerState = "handedoff" } }
    }
    Connections {
        target: app.updater
        function onCheckFinished(updates, errors) {
            if (!dlg.visible) return
            dlg.toolsChecking = false
            dlg.toolUpdates = updates
            dlg.toolsError = errors.join("; ")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        RowLayout {
            spacing: 14
            Image { source: "qrc:/qt/qml/UvePlayer/resources/icon.png"; sourceSize.width: 72; sourceSize.height: 72 }
            ColumnLayout {
                Label { text: qsTr("UVE Player ") + app.version; font.pixelSize: 20; font.bold: true; color: theme.text }
                Label { text: qsTr("Medienplayer mit libmpv und Ultimate-Video-Enhancer-Plugin"); color: theme.muted }
            }
        }
        // ---------------------------------------------------------------- Updates
        Rectangle {
            objectName: "aboutUpdateBox"
            Layout.fillWidth: true
            implicitHeight: updateCol.implicitHeight + 20
            radius: 8
            color: theme.panel2
            border.color: theme.line
            ColumnLayout {
                id: updateCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Item {
                        implicitWidth: 26; implicitHeight: 26
                        Layout.alignment: Qt.AlignTop
                        BusyIndicator { anchors.fill: parent; running: dlg.working; visible: running }
                        Label {
                            anchors.centerIn: parent
                            visible: !dlg.working
                            font.pixelSize: 20; font.bold: true
                            text: dlg.playerState === "current" ? "✓" : dlg.problem ? "!" : "↑"
                            color: dlg.playerState === "current" ? "#3fb950" : dlg.problem ? theme.error : theme.accent
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.text; font.bold: true; text: dlg.statusTitle() }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: dlg.problem ? theme.error : theme.muted
                            font.pixelSize: 12
                            visible: text !== ""
                            text: dlg.statusDetail()
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            visible: dlg.playerState === "installing"
                            value: dlg.progressValue
                            indeterminate: dlg.progressValue === 0
                        }
                    }
                    Button {
                        visible: dlg.playerState === "update" && dlg.updateInfo.canInstall === true
                        text: qsTr("Jetzt aktualisieren")
                        highlighted: true
                        onClicked: dlg.install()
                    }
                    Button { visible: dlg.playerState === "failed"; text: qsTr("Erneut versuchen"); highlighted: true; onClicked: dlg.install() }
                    Button { visible: dlg.playerState === "installing"; text: qsTr("Abbrechen"); onClicked: app.appUpdater.cancelInstall() }
                    Button { visible: dlg.playerState === "commits"; text: qsTr("Details"); onClicked: dlg.commitsRequested(dlg.updateInfo) }
                    Button { visible: dlg.playerState === "error"; text: qsTr("Erneut prüfen"); onClicked: dlg.startCheck() }
                    Button {
                        visible: dlg.playerState === "failed" || (dlg.playerState === "update" && dlg.updateInfo.canInstall !== true)
                        text: qsTr("Auf GitHub ansehen")
                        onClicked: app.appUpdater.openReleasePage()
                    }
                }
                // Werkzeuge (yt-dlp, libmpv)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                        color: dlg.toolsError !== "" && dlg.toolUpdates.length === 0 ? theme.error : theme.muted
                        text: dlg.toolsChecking ? qsTr("yt-dlp und libmpv werden geprüft …")
                              : dlg.toolUpdates.length > 0 ? qsTr("Updates für Werkzeuge verfügbar: %1").arg(dlg.toolUpdates.map(u => u.tool).join(", "))
                              : dlg.toolsError !== "" ? qsTr("Werkzeug-Prüfung fehlgeschlagen: %1").arg(dlg.toolsError)
                              : qsTr("yt-dlp und libmpv sind aktuell")
                    }
                    Button { visible: !dlg.toolsChecking && dlg.toolUpdates.length > 0; text: qsTr("Anzeigen"); onClicked: dlg.toolUpdatesRequested(dlg.toolUpdates) }
                }
            }
        }

        GridLayout {
            columns: 2
            columnSpacing: 14
            Label { text: qsTr("mpv / libmpv"); color: theme.muted }
            Label { text: app.mpvVersion; color: theme.text; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
            Label { text: qsTr("libmpv-Datei"); color: theme.muted }
            Label { text: app.mpvLibrary; color: theme.text; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
            Label { text: qsTr("Qt"); color: theme.muted }
            Label { text: app.qtVersion; color: theme.text }
            Label { text: qsTr("Datenordner"); color: theme.muted }
            Label { text: app.dataDir; color: theme.text; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.muted
            font.pixelSize: 12
            text: qsTr("Wiedergabe-Engine: mpv/libmpv (LGPL/GPL, je nach Build), FFmpeg-Bibliotheken in libmpv enthalten. Oberfläche: Qt 6 (LGPLv3). ")
                + qsTr("Ultimate Video Enhancer: Freak288 / nextscript – nativer Port der WebGL-Effekte. Lizenzübersicht: docs/THIRD_PARTY_LICENSES.md. ")
                + qsTr("Keine Telemetrie; Netzwerkzugriffe nur für Streams und – mit Einwilligung – die Update-Prüfung.")
        }
        RowLayout {
            Button { text: qsTr("Datenordner öffnen"); onClicked: app.openFolder(app.dataDir) }
            Button { text: qsTr("Protokoll öffnen"); onClicked: app.openFolder(app.logFile()) }
        }
    }
}
