import QtQuick
import QtQuick.Controls

// Schaltfläche im UVE-Stil: flach, Akzentfarbe für die Hauptaktion
Button {
    id: control
    property var theme
    property bool accent: false
    property bool danger: false
    property string tip: ""
    implicitHeight: 30
    padding: 6
    leftPadding: 12
    rightPadding: 12
    font.pixelSize: 13
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    readonly property color _accent: theme ? theme.accent : "#ff8a1f"
    readonly property color _text: theme ? theme.text : "#e6e8eb"
    readonly property color _line: theme ? theme.line : "#262b34"
    readonly property color _panel2: theme ? theme.panel2 : "#1b1f26"
    readonly property color _error: theme ? theme.error : "#ff6b6b"

    ToolTip.visible: tip !== "" && hovered
    ToolTip.delay: 600
    ToolTip.text: tip

    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? Qt.darker(control._text, 1.8)
             : control.accent ? "#111" : (control.danger ? control._error : control._text)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 6
        color: control.accent ? (control.down ? Qt.darker(control._accent, 1.2) : control.hovered ? Qt.lighter(control._accent, 1.1) : control._accent)
                              : (control.down ? Qt.darker(control._panel2, 1.2) : control.hovered ? Qt.lighter(control._panel2, 1.25) : control._panel2)
        border.color: control.visualFocus ? (theme ? theme.focus : "#4da3ff") : (control.accent ? "transparent" : control._line)
        border.width: control.visualFocus ? 2 : 1
        opacity: control.enabled ? 1 : 0.6
    }
}
