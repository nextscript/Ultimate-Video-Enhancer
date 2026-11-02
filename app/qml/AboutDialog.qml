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
    Component.onCompleted: { var b = standardButton(Dialog.Close); if (b) b.text = qsTr("Schließen") }

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
