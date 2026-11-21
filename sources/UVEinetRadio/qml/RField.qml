import QtQuick
import QtQuick.Controls

// Eingabefeld im UVE-Stil
TextField {
    id: control
    property var theme
    property bool invalid: false
    implicitHeight: 30
    font.pixelSize: 13
    color: theme ? theme.text : "#e6e8eb"
    placeholderTextColor: theme ? theme.muted : "#8b93a1"
    selectByMouse: true
    selectionColor: theme ? theme.accent : "#ff8a1f"
    selectedTextColor: "#111"
    leftPadding: 10
    rightPadding: 10
    background: Rectangle {
        radius: 6
        color: theme ? theme.panel2 : "#1b1f26"
        border.width: control.activeFocus || control.invalid ? 2 : 1
        border.color: control.invalid ? (theme ? theme.error : "#ff6b6b")
                    : control.activeFocus ? (theme ? theme.focus : "#4da3ff") : (theme ? theme.line : "#262b34")
    }
}
