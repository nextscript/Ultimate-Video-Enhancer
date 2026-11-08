import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Einstellungen → Plugins: Status (Lebenszyklus), Aktivieren/Deaktivieren zur Laufzeit, Fehler, Safe Mode, Installieren, Entfernen
ColumnLayout {
    id: page
    property var theme
    signal openPluginSettings(string key)
    spacing: 8

    // Zustandsnamen aus PluginManager (Schlüssel) für die Übersetzung
    readonly property var stateNames: [QT_TR_NOOP("gefunden"), QT_TR_NOOP("abgelehnt"), QT_TR_NOOP("geprüft"), QT_TR_NOOP("geladen"), QT_TR_NOOP("initialisiert"), QT_TR_NOOP("aktiv"), QT_TR_NOOP("pausiert"), QT_TR_NOOP("deaktiviert"), QT_TR_NOOP("entladen"), QT_TR_NOOP("Fehler"), QT_TR_NOOP("Safe Mode")]
    property string installMessage: ""

    // Einstellungsseiten der Plugins (contributes.panels mit location "settings")
    function settingsPanelsFor(pluginId) {
        return (app.plugins ? app.plugins.panels : []).filter(p => p.location === "settings" && p.pluginId === pluginId)
    }
    // Entfernen mit Rückfrage; die Dateien werden beim nächsten Start gelöscht
    function askRemove(plugin) { removeDialog.pluginInfo = plugin; removeData.checked = false; removeDialog.open() }
    function confirmRemove() {
        var r = app.plugins.removePlugin(removeDialog.pluginInfo.id, removeData.checked)
        removeDialog.close()
        page.installMessage = r.ok ? "" : qsTr("Entfernen fehlgeschlagen: ") + r.error
    }

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
                    Label { text: modelData.name; font.bold: true; color: theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                    Label { text: "v" + modelData.version; color: theme.muted }
                    Label { text: modelData.type; color: theme.muted; font.pixelSize: 11 }
                    Label {
                        text: "● " + qsTr(modelData.state)  // Zustand ist ein Schlüssel, siehe stateNames
                        color: modelData.state === "aktiv" ? "#6fd38c" : (modelData.state === "Fehler" || modelData.state === "abgelehnt" ? theme.error : theme.muted)
                    }
                    Switch {
                        checked: modelData.enabled
                        enabled: !app.safeMode && modelData.state !== "abgelehnt" && !modelData.pendingRemoval
                        onToggled: app.plugins.setEnabled(modelData.id, checked)
                    }
                }
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: modelData.description; color: theme.muted }
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("ID: ") + modelData.id + qsTr("   ·   API ") + modelData.apiVersion + "   ·   " + (modelData.author || ""); color: theme.muted; font.pixelSize: 11 }
                Label { visible: !!modelData.permissions && modelData.permissions.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Berechtigungen: ") + (modelData.permissions || []).join(", "); color: theme.muted; font.pixelSize: 11 }
                Label { visible: modelData.error.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Fehler: ") + modelData.error; color: theme.error }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: page.settingsPanelsFor(modelData.id).length > 0
                    Repeater {
                        model: page.settingsPanelsFor(modelData.id)
                        Button {
                            required property var modelData
                            text: modelData.title
                            icon.name: "configure"
                            onClicked: page.openPluginSettings(modelData.key)
                        }
                    }
                }
                Label { visible: modelData.shaderDisabled === true; Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("GPU-Stufe nach Shaderfehler deaktiviert (Wiedergabe läuft ohne Effekte weiter)"); color: theme.error }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 11
                        color: modelData.pendingRemoval ? theme.error : theme.muted
                        text: modelData.pendingRemoval ? qsTr("Wird beim nächsten Start entfernt")
                              : modelData.removable ? "" : qsTr("Mitgeliefert – kann nur deaktiviert werden")
                    }
                    Button { visible: modelData.removable && !modelData.pendingRemoval; text: qsTr("Entfernen"); onClicked: page.askRemove(modelData) }
                    Button { visible: modelData.pendingRemoval; text: qsTr("Entfernen rückgängig"); onClicked: app.plugins.cancelRemoval(modelData.id) }
                    Button { visible: modelData.pendingRemoval; text: qsTr("Jetzt neu starten"); onClicked: app.restartApp() }
                }
            }
        }
    }
    Label {
        visible: page.installMessage !== ""
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: theme.text
        text: page.installMessage
    }
    Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: theme.muted
        font.pixelSize: 12
        text: qsTr("Plugins liegen im Programmordner (plugins/) und im Benutzerordner. Inkompatible Plugins werden vor dem Laden abgelehnt; ")
            + qsTr("nach wiederholten Abstürzen startet der Player im Safe Mode (auch per --safe-mode).")
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
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

    FileDialog {
        id: packageDialog
        title: qsTr("Plugin installieren")
        nameFilters: [qsTr("Plugin-Paket (*.zip)"), qsTr("Plugin-Manifest (manifest.json)")]
        onAccepted: {
            var r = app.plugins.installPackageInfo(selectedFile.toString())
            if (r.ok) { page.installMessage = ""; installedDialog.show(r) }
            else page.installMessage = qsTr("Installation fehlgeschlagen: ") + r.error
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
        onRejected: page.installMessage = qsTr("„%1“ %2 wird beim nächsten Start des Players aktiv.").arg(pluginInfo.name).arg(pluginInfo.version)

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

    Dialog {
        id: removeDialog
        property var pluginInfo: ({})
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent ? parent.width - 40 : 480)
        modal: true
        closePolicy: Popup.CloseOnEscape
        title: qsTr("Plugin entfernen")
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.text
                text: qsTr("„%1“ wirklich entfernen? Das Plugin wird sofort deaktiviert und beim nächsten Start gelöscht.").arg(removeDialog.pluginInfo.name || "")
            }
            CheckBox { id: removeData; text: qsTr("Auch Einstellungen und Daten des Plugins löschen") }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Abbrechen"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { text: qsTr("Entfernen"); highlighted: true; onClicked: page.confirmRemove() }
        }
    }
}
