import QtQuick
import QtQuick.Controls

// Schlichter Symbol-Button mit Tooltip
ToolButton {
    id: b
    property string tip: ""
    property bool highlighted2: false
    property var theme
    implicitWidth: 34
    implicitHeight: 34
    font.pixelSize: 17
    font.family: "Segoe UI Symbol"
    focusPolicy: Qt.NoFocus
    ToolTip.text: tip
    ToolTip.visible: tip.length > 0 && hovered
    ToolTip.delay: 500
    contentItem: Text {
        text: b.text
        font: b.font
        color: b.highlighted2 ? (b.theme ? b.theme.accent : "orange") : (b.enabled ? (b.theme ? b.theme.text : "white") : (b.theme ? b.theme.muted : "gray"))
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 6
        color: b.down ? (b.theme ? b.theme.line : "#333") : (b.hovered ? (b.theme ? b.theme.panel2 : "#222") : "transparent")
    }
}
