import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    property var theme
    property var plugin
    property var modal   // modal.data, modal.scope, modal.mode, modal.close(result)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8
        Label { text: "Scope: " + (modal ? modal.scope : ""); color: theme ? theme.text : "white" }
        Label { text: "Mode: " + (modal ? modal.mode : ""); color: theme ? theme.text : "white" }
        Item { Layout.fillHeight: true }
        Button { text: "OK"; Layout.alignment: Qt.AlignRight; onClicked: modal.close("ok") }
    }
}
