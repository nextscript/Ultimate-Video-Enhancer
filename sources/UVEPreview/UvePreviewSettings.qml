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
    readonly property color textColor: theme ? theme.text : "white"
    readonly property color mutedColor: theme ? theme.muted : "#999"

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.t("Vorschaubild über der Zeitleiste", "Picture preview above the timeline")
                font.bold: true; color: root.textColor; Layout.fillWidth: true; wrapMode: Text.Wrap
            }
            Switch {
                objectName: "uvePreviewEnabled"
                checked: !!controller && controller.enabled
                onToggled: controller.enabled = checked
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: root.mutedColor
            text: root.t("Beim Überfahren oder Ziehen des Fortschrittsbalkens erscheint ein kleines Bild der Stelle mit Zeitstempel. "
                         + "Gilt für lokale Videodateien; bei Audio, Netzwerk- und Livestreams sowie Discs zeigt die Zeitleiste wie gewohnt die Zeit. "
                         + "Die Vorschau dekodiert die Datei separat – Position, Pause, Ton und Effekte der Wiedergabe bleiben unverändert.",
                         "Hovering over or dragging the progress bar shows a small picture of that position with a time stamp. "
                         + "Works for local video files; for audio, network and live streams and discs the timeline shows the time as usual. "
                         + "The preview decodes the file separately – position, pause, sound and effects of the playback stay unchanged.")
        }
        Label {
            visible: !!controller && controller.status !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: root.textColor
            text: controller ? controller.status : ""
        }
    }
}
