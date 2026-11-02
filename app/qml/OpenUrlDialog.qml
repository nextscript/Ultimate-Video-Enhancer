import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dlg
    property var theme
    property bool playNow: true
    title: qsTr("Netzwerkstream öffnen")
    modal: true
    width: Math.min(640, parent ? parent.width - 40 : 640)
    anchors.centerIn: parent
    standardButtons: Dialog.Open | Dialog.Cancel
    onOpened: { field.text = ""; err.text = ""; field.forceActiveFocus() }
    onAccepted: app.openPaths([field.text.trim()], playNow)

    // Öffnen nur mit gültiger Adresse
    Component.onCompleted: standardButton(Dialog.Open).enabled = Qt.binding(() => app.validateUrl(field.text) === "")

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        TextField {
            id: field
            Layout.fillWidth: true
            placeholderText: qsTr("https://www.youtube.com/watch?v=…  ·  https://www.twitch.tv/kanal  ·  tcp://host:port")
            onTextChanged: err.text = text.length ? app.validateUrl(text) : ""
            onAccepted: if (app.validateUrl(text) === "") dlg.accept()
        }
        Label { id: err; color: theme.error; visible: text.length > 0 }
        GridLayout {
            columns: 2
            columnSpacing: 16
            Label { text: qsTr("YouTube, Twitch, Webseiten"); color: theme.text; font.bold: true }
            Label { text: qsTr("über yt-dlp (H.264 bevorzugt, Auflösung in den Einstellungen)"); color: theme.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: qsTr("tcp:// udp:// rtmp:// rtsp:// srt://"); color: theme.text; font.bold: true }
            Label { text: qsTr("Live-Übertragungen direkt über mpv"); color: theme.muted }
            Label { text: qsTr(".m3u8 / .mpd / .mp4 …"); color: theme.text; font.bold: true }
            Label { text: qsTr("HLS, DASH und direkte Medien-URLs"); color: theme.muted }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.muted
            font.pixelSize: 12
            text: qsTr("Zugangs- oder Werbeschutzsysteme werden nicht umgangen; DRM-geschützte Inhalte sind nicht abspielbar.")
        }
    }
}
