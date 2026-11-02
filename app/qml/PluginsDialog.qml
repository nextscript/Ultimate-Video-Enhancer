import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
                    Label { visible: modelData.shaderDisabled === true; text: qsTr("GPU-Stufe nach Shaderfehler deaktiviert (Wiedergabe läuft ohne Effekte weiter)"); color: theme.error }
                }
            }
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
            Button { text: qsTr("Fehlerprotokoll"); onClicked: app.openFolder(app.plugins.errorLogPath()) }
            Button { text: qsTr("Plugin-Ordner"); onClicked: app.openFolder(app.dataDir + "/plugins") }
        }
    }
}
