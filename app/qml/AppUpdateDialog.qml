import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Update des Players selbst: neue Version und Commits auf GitHub anzeigen, der Benutzer entscheidet über den Zeitpunkt
Dialog {
    id: dlg
    property var theme
    property var info: ({})
    property bool installing: false
    property string progressText: ""
    property real progressValue: 0
    property string errorText: ""
    property string doneText: ""

    readonly property bool hasRelease: info.updateAvailable === true
    readonly property var commits: info.newCommits || []

    title: hasRelease ? qsTr("Update für UVE Player verfügbar") : qsTr("Neue Commits auf GitHub")
    modal: true
    closePolicy: installing ? Popup.NoAutoClose : Popup.CloseOnEscape
    width: Math.min(640, parent ? parent.width - 40 : 640)
    anchors.centerIn: parent

    function show(result) {
        info = result
        installing = false; errorText = ""; doneText = ""; progressValue = 0; progressText = ""
        skip.checked = false
        open()
    }
    function fmtMB(b) { return (b / 1048576).toFixed(1) + qsTr(" MB") }
    function startInstall() {
        errorText = ""; progressValue = 0
        progressText = qsTr("Update wird heruntergeladen …")
        installing = true
        app.appUpdater.install()
    }
    function versionLine(version, shortSha) {
        return shortSha ? version + "  (" + shortSha + ")" : version
    }

    Connections {
        target: app.appUpdater
        function onProgress(received, total) {
            dlg.progressValue = total > 0 ? received / total : 0
            dlg.progressText = dlg.fmtMB(received) + (total > 0 ? " / " + dlg.fmtMB(total) : "")
        }
        function onRetrying(attempt, maxAttempts, delaySeconds) {
            dlg.progressText = qsTr("GitHub antwortet gerade nicht – neuer Versuch in %1 s (der Download wird fortgesetzt)").arg(delaySeconds)
        }
        function onPackageReady(file) { dlg.progressText = qsTr("Installation wird gestartet – der Player wird neu gestartet …"); dlg.progressValue = 1 }
        function onInstallFailed(message) { dlg.installing = false; dlg.errorText = message }
        function onInstallHandedOff(message) { dlg.installing = false; dlg.doneText = message }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Abbrechen"); visible: dlg.installing
            onClicked: { app.appUpdater.cancelInstall() }
        }
        Button {
            text: qsTr("Auf GitHub ansehen"); visible: !dlg.installing && !dlg.doneText
            onClicked: app.appUpdater.openReleasePage()
        }
        Button {
            text: dlg.hasRelease ? qsTr("Später") : qsTr("Schließen"); visible: !dlg.installing && !dlg.doneText
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            text: dlg.errorText ? qsTr("Erneut versuchen") : qsTr("Jetzt installieren")
            visible: !dlg.installing && !dlg.doneText && dlg.info.canInstall === true
            highlighted: true
            onClicked: dlg.startInstall()
        }
        Button { text: qsTr("Schließen"); visible: dlg.doneText !== ""; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
    }
    onRejected: if (skip.checked && hasRelease) app.setSetting("skippedPlayerVersion", info.latestVersion)

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        GridLayout {
            columns: 2; columnSpacing: 14; rowSpacing: 2
            Label { text: qsTr("Installiert"); color: theme.muted }
            Label { text: dlg.versionLine(dlg.info.currentVersion || "", dlg.info.currentShort || ""); color: theme.text; font.family: "Consolas" }
            Label { visible: dlg.hasRelease; text: qsTr("Neue Version"); color: theme.muted }
            Label {
                visible: dlg.hasRelease
                text: dlg.versionLine(dlg.info.latestVersion || "", dlg.info.latestShort || "")
                    + (dlg.info.assetSize > 0 ? "   ·   " + dlg.fmtMB(dlg.info.assetSize) : "")
                color: theme.text; font.family: "Consolas"; font.bold: true
            }
            Label { visible: !!dlg.info.branchShort; text: qsTr("Neuester Commit"); color: theme.muted }
            Label { visible: !!dlg.info.branchShort; text: dlg.info.branchShort || ""; color: theme.text; font.family: "Consolas" }
        }

        Label {
            visible: dlg.commits.length > 0
            text: dlg.info.commitsTruncated ? qsTr("Neue Commits (die letzten %1):").arg(dlg.commits.length) : qsTr("Neue Commits: %1").arg(dlg.commits.length)
            color: theme.text; font.bold: true
        }
        ListView {
            visible: dlg.commits.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 180)
            clip: true
            spacing: 3
            model: dlg.commits
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                spacing: 8
                Label { text: modelData.shortSha; color: theme.muted; font.family: "Consolas" }
                Label { text: String(modelData.date).slice(0, 10); color: theme.muted; font.family: "Consolas" }
                Label { text: modelData.title; color: theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
            }
        }

        Label {
            visible: !dlg.installing && !dlg.doneText
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.muted
            font.pixelSize: 12
            text: !dlg.hasRelease
                  ? qsTr("Diese Commits sind noch nicht als Installationspaket veröffentlicht. Sobald es ein Release gibt, kann es hier installiert werden.")
                  : dlg.info.canInstall === true
                    ? qsTr("„Jetzt installieren“ lädt das Update, prüft es per SHA-256, beendet den Player und startet ihn danach neu. Bei „Später“ erscheint dieser Hinweis beim nächsten Start wieder.")
                    : qsTr("Für diese Installation gibt es kein automatisches Update. Das neue Paket liegt auf der GitHub-Seite des Releases.")
        }
        CheckBox { id: skip; visible: dlg.hasRelease && !dlg.installing && !dlg.doneText; text: qsTr("Diese Version überspringen") }

        ColumnLayout {
            visible: dlg.installing
            Layout.fillWidth: true
            Label { text: dlg.progressText; color: theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap }
            ProgressBar { Layout.fillWidth: true; value: dlg.progressValue; indeterminate: dlg.progressValue === 0 }
        }
        Label {
            visible: dlg.errorText !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.error
            text: qsTr("Das Update wurde nicht installiert: %1. Der Download wird beim nächsten Versuch fortgesetzt; alternativ lässt sich das Paket über „Auf GitHub ansehen“ im Browser laden.").arg(dlg.errorText)
        }
        Label { visible: dlg.doneText !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.text; text: dlg.doneText }
    }
}
