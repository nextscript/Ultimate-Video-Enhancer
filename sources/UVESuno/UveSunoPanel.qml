import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var controller
    property var theme
    implicitHeight: content.implicitHeight + 24

    readonly property bool de: !controller || controller.language.indexOf("de") === 0
    function t(deText, enText) { return root.de ? deText : enText }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10
        Label { text: "UVESuno"; font.bold: true; color: theme ? theme.text : "white" }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme ? theme.muted : "#999"
            text: root.t("Öffne einfach einen Link von suno.com: einen Song, eine Playlist, ein Profil (suno.com/@name) oder einen Kurzlink (suno.com/s/…). Playlists und Profile kommen komplett in die Wiedergabeliste. Als Bild erscheint das Cover des Songs. Es wird nichts gespeichert.",
                         "Just open a link from suno.com: a song, a playlist, a profile (suno.com/@name) or a short link (suno.com/s/…). Playlists and profiles go into the playlist completely. The song's cover is shown as the picture. Nothing is saved.")
        }
        Label {
            visible: !!controller && controller.status !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme ? theme.text : "white"
            text: controller ? controller.status : ""
        }
        Item { Layout.fillHeight: true }
    }
}
