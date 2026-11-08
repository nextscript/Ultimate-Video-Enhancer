import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Plugin-Verwaltung: Status (Lebenszyklus), Aktivieren/Deaktivieren zur Laufzeit, Fehler, Safe Mode
Dialog {
    id: dlg
    property var theme
    title: qsTr("Plugins")
    modal: true
    width: Math.min(760, parent ? parent.width - 40 : 760)
    height: Math.min(600, parent ? parent.height - 40 : 600)
    anchors.centerIn: parent
    // Zustandsnamen aus PluginManager (Schlüssel) für die Übersetzung
    readonly property var stateNames: [QT_TR_NOOP("gefunden"), QT_TR_NOOP("abgelehnt"), QT_TR_NOOP("geprüft"), QT_TR_NOOP("geladen"), QT_TR_NOOP("initialisiert"), QT_TR_NOOP("aktiv"), QT_TR_NOOP("pausiert"), QT_TR_NOOP("deaktiviert"), QT_TR_NOOP("entladen"), QT_TR_NOOP("Fehler"), QT_TR_NOOP("Safe Mode")]
    standardButtons: Dialog.Close
    Component.onCompleted: { var b = standardButton(Dialog.Close); if (b) b.text = qsTr("Schließen") }

    // Einstellungsseiten der Plugins (contributes.panels mit location "settings")
    function settingsPanelsFor(pluginId) {
        return (app.plugins ? app.plugins.panels : []).filter(p => p.location === "settings" && p.pluginId === pluginId)
    }
    function openSettings(key) {
        var ps = (app.plugins ? app.plugins.panels : []).filter(p => p.key === key)
        if (ps.length === 0) return
        if (!dlg.visible) dlg.open()
        settingsPopup.panel = ps[0]
        settingsPopup.open()
    }
    property string installMessage: ""

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Rectangle {
            visible: app.safeMode
            Layout.fillWidth: true
            color: "#3a2a12"; radius: 6
            implicitHeight: safeRow.implicitHeight + 16
            RowLayout {
                id: safeRow
                anchors.fill: parent; anchors.margins: 8
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#ffd9a0"; text: qsTr("Safe Mode: Plugins wurden nicht geladen. ") + app.safeModeReason }
                Button { text: qsTr("Plugins wieder aktivieren"); onClicked: app.resetSafeMode() }
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: app.plugins ? app.plugins.plugins : []
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                implicitHeight: card.implicitHeight + 20
                radius: 8
                color: "transparent"
                border.color: theme.line
                ColumnLayout {
                    id: card
                    anchors.fill: parent; anchors.margins: 10
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: modelData.name; font.bold: true; color: theme.text }
                        Label { text: "v" + modelData.version; color: theme.muted }
                        Label { text: modelData.type; color: theme.muted; font.pixelSize: 11 }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: "● " + qsTr(modelData.state)  // Zustand ist ein Schlüssel, siehe stateNames
                            color: modelData.state === "aktiv" ? "#6fd38c" : (modelData.state === "Fehler" || modelData.state === "abgelehnt" ? theme.error : theme.muted)
                        }
                        Switch {
                            checked: modelData.enabled
                            enabled: !app.safeMode && modelData.state !== "abgelehnt"
                            onToggled: app.plugins.setEnabled(modelData.id, checked)
                        }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: modelData.description; color: theme.muted }
                    Label { text: qsTr("ID: ") + modelData.id + qsTr("   ·   API ") + modelData.apiVersion + "   ·   " + (modelData.author || ""); color: theme.muted; font.pixelSize: 11 }
                    Label { visible: !!modelData.permissions && modelData.permissions.length > 0; text: qsTr("Berechtigungen: ") + (modelData.permissions || []).join(", "); color: theme.muted; font.pixelSize: 11 }
                    Label { visible: modelData.error.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Fehler: ") + modelData.error; color: theme.error }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: dlg.settingsPanelsFor(modelData.id).length > 0
                        Repeater {
                            model: dlg.settingsPanelsFor(modelData.id)
                            Button {
                                required property var modelData
                                text: modelData.title
                                icon.name: "configure"
                                onClicked: { settingsPopup.panel = modelData; settingsPopup.open() }
                            }
                        }
                    }
                    Label { visible: modelData.shaderDisabled === true; text: qsTr("GPU-Stufe nach Shaderfehler deaktiviert (Wiedergabe läuft ohne Effekte weiter)"); color: theme.error }
                }
            }
        }
        Label {
            visible: dlg.installMessage !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.text
            text: dlg.installMessage
        }
        RowLayout {
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.muted
                font.pixelSize: 12
                text: qsTr("Plugins liegen im Programmordner (plugins/) und im Benutzerordner. Inkompatible Plugins werden vor dem Laden abgelehnt; ")
                    + qsTr("nach wiederholten Abstürzen startet der Player im Safe Mode (auch per --safe-mode).")
            }
            Button {
                text: qsTr("Plugin installieren …")
                onClicked: packageDialog.open()
                ToolTip.text: qsTr("Plugin-Paket (.zip mit manifest.json) in den Benutzerordner installieren"); ToolTip.visible: hovered
            }
            Button { text: qsTr("Fehlerprotokoll"); onClicked: app.openFolder(app.plugins.errorLogPath()) }
            Button {
                text: qsTr("Plugin-Ordner")
                onClicked: app.openPluginFolder(false)
                ToolTip.text: qsTr("Ordner mit den installierten Plugins öffnen"); ToolTip.visible: hovered
            }
            Button {
                text: qsTr("Eigene Plugins")
                onClicked: app.openPluginFolder(true)
                ToolTip.text: qsTr("Benutzerordner für zusätzliche Plugins öffnen (hat Vorrang)"); ToolTip.visible: hovered
            }
        }
    }

    FileDialog {
        id: packageDialog
        title: qsTr("Plugin installieren")
        nameFilters: [qsTr("Plugin-Paket (*.zip)"), qsTr("Plugin-Manifest (manifest.json)")]
        onAccepted: {
            var r = app.plugins.installPackageInfo(selectedFile.toString())
            if (r.ok) { dlg.installMessage = ""; installedDialog.show(r) }
            else dlg.installMessage = qsTr("Installation fehlgeschlagen: ") + r.error
        }
    }

    // Rückmeldung nach der Installation: Eine neue Plugin-Version wird erst mit einem Neustart aktiv
    Dialog {
        id: installedDialog
        property var pluginInfo: ({})
        function show(r) { pluginInfo = r; open() }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent ? parent.width - 40 : 480)
        modal: true
        closePolicy: Popup.CloseOnEscape
        title: pluginInfo.kind === "update" ? qsTr("Plugin aktualisiert")
             : pluginInfo.kind === "new" ? qsTr("Plugin installiert")
             : qsTr("Plugin ersetzt")
        onRejected: dlg.installMessage = qsTr("„%1“ %2 wird beim nächsten Start des Players aktiv.").arg(pluginInfo.name).arg(pluginInfo.version)

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.text
                text: installedDialog.pluginInfo.kind === "update"
                      ? qsTr("„%1“ wurde von Version %2 auf %3 aktualisiert.").arg(installedDialog.pluginInfo.name).arg(installedDialog.pluginInfo.previousVersion).arg(installedDialog.pluginInfo.version)
                      : installedDialog.pluginInfo.kind === "new"
                        ? qsTr("„%1“ %2 wurde installiert.").arg(installedDialog.pluginInfo.name).arg(installedDialog.pluginInfo.version)
                        : qsTr("„%1“ %2 ersetzt die bisherige Version %3.").arg(installedDialog.pluginInfo.name).arg(installedDialog.pluginInfo.version).arg(installedDialog.pluginInfo.previousVersion)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.muted
                text: qsTr("Die Änderung wird nach einem Neustart des Players aktiv. Einstellungen des Plugins bleiben erhalten.")
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Später"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { text: qsTr("Jetzt neu starten"); highlighted: true; onClicked: app.restartApp() }
        }
    }

    Popup {
        id: settingsPopup
        property var panel: null
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(560, dlg.width)
        height: Math.min(620, dlg.height)
        modal: true
        padding: 0
        background: Rectangle { color: theme.panel; border.color: theme.line; radius: 8 }
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            RowLayout {
                Layout.fillWidth: true; Layout.margins: 10
                Label { text: settingsPopup.panel ? settingsPopup.panel.pluginName + " – " + settingsPopup.panel.title : ""; font.bold: true; color: theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                Button { text: qsTr("Schließen"); onClicked: settingsPopup.close() }
            }
            Loader {
                Layout.fillWidth: true; Layout.fillHeight: true
                active: settingsPopup.opened && settingsPopup.panel !== null
                source: settingsPopup.panel ? settingsPopup.panel.url : ""
                onLoaded: {
                    if ("theme" in item) item.theme = theme
                    if ("host" in item) item.host = app
                    if ("controller" in item) item.controller = settingsPopup.panel.controller
                    if ("plugin" in item) item.plugin = settingsPopup.panel.plugin
                }
            }
        }
    }
}
