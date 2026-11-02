import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var controller
    property var theme
    property var host

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10
        Label { text: qsTr("Einschlaf-Timer"); font.bold: true; color: theme ? theme.text : "white" }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme ? theme.muted : "#999"
            text: qsTr("Beispiel-Plugin für die Plugin-API: pausiert die Wiedergabe nach Ablauf der Zeit.")
        }
        RowLayout {
            Label { text: qsTr("Minuten"); color: theme ? theme.text : "white" }
            SpinBox { from: 1; to: 600; value: controller ? controller.minutes : 30; editable: true; onValueModified: controller.minutes = value }
        }
        RowLayout {
            Button { text: controller && controller.running ? qsTr("Stopp") : qsTr("Start"); onClicked: controller.running ? controller.stop() : controller.start() }
            Label {
                visible: controller && controller.running
                color: theme ? theme.accent : "orange"
                text: controller ? Math.floor(controller.remainingSeconds / 60) + ":" + String(controller.remainingSeconds % 60).padStart(2, "0") + qsTr(" verbleibend") : ""
            }
        }
        Item { Layout.fillHeight: true }
    }
}
