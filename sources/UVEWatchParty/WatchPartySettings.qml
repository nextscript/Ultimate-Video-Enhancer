import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var controller
    property var theme
    implicitHeight: col.implicitHeight + 24
    function t(de, en) { return controller ? controller.text(de, en) : en }
    readonly property var s: controller ? controller.settings : ({})

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Label { text: "Watch Party"; font.bold: true; color: theme ? theme.text : "white" }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme ? theme.muted : "#999"
            text: root.t("Funktioniert ohne Einrichtung: Räume finden sich über öffentliche Nostr-Relays (Ende-zu-Ende verschlüsselt), das Video läuft direkt zwischen den Geräten (WebRTC, Cloudflare STUN). Die Felder unten sind nur für Sonderfälle.",
                         "Works without setup: rooms meet through public Nostr relays (end-to-end encrypted), the video runs directly between the devices (WebRTC, Cloudflare STUN). The fields below are only for special cases.")
        }
        Label { text: root.t("Nostr-Relays (optional, leer = Standardliste)", "Nostr relays (optional, empty = default list)"); color: theme ? theme.muted : "#999" }
        TextField { id: relays; Layout.fillWidth: true; text: s.relays || ""; placeholderText: "wss://nos.lol, wss://relay.primal.net, …" }
        Label { text: root.t("STUN-Server (optional, leer = Cloudflare STUN)", "STUN servers (optional, empty = Cloudflare STUN)"); color: theme ? theme.muted : "#999" }
        TextField { id: stun; Layout.fillWidth: true; text: s.stunServers || ""; placeholderText: "stun:stun.cloudflare.com:3478" }
        Label { text: root.t("TURN-Relay (optional, leitet Daten weiter)", "TURN relay (optional, relays data)"); color: theme ? theme.muted : "#999" }
        TextField { id: turn; Layout.fillWidth: true; text: s.turnUrl || ""; placeholderText: "turn:turn.example.com:3478" }
        RowLayout {
            Layout.fillWidth: true
            TextField { id: turnUser; Layout.fillWidth: true; text: s.turnUser || ""; placeholderText: root.t("Benutzer", "User") }
            TextField { id: turnPass; Layout.fillWidth: true; text: s.turnPassword || ""; echoMode: TextInput.Password; placeholderText: root.t("Passwort", "Password") }
        }
        CheckBox { id: upload; checked: s.uploadEnabled !== false; text: root.t("Empfangene Teile an andere Teilnehmer weitergeben (Upload)", "Pass received parts on to other members (upload)") }
        RowLayout {
            Label { text: root.t("Upload-Limit (KB/s, 0 = frei)", "Upload limit (KB/s, 0 = none)"); color: theme ? theme.text : "white" }
            SpinBox { id: limit; from: 0; to: 1000000; stepSize: 100; editable: true; value: s.uploadLimitKBps || 0 }
        }
        RowLayout {
            Label { text: root.t("Cache (MB)", "Cache (MB)"); color: theme ? theme.text : "white" }
            SpinBox { id: cache; from: 256; to: 65536; stepSize: 256; editable: true; value: s.cacheMB || 2048 }
        }
        CheckBox { id: loop; checked: !!s.allowInsecureLoopback; text: root.t("ws:// zu localhost erlauben (nur Entwicklung)", "Allow ws:// to localhost (development only)") }
        Button {
            text: root.t("Speichern", "Save")
            onClicked: controller.saveSettings({ relays: relays.text, stunServers: stun.text, turnUrl: turn.text, turnUser: turnUser.text,
                                                 turnPassword: turnPass.text, uploadEnabled: upload.checked, uploadLimitKBps: limit.value,
                                                 cacheMB: cache.value, allowInsecureLoopback: loop.checked })
        }
    }
}
