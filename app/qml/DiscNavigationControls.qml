import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Kompakte Disc-Steuerung (DVD/Blu-ray): Pfeile, Auswählen, Menü, Zurück. Liegt unten links über dem Bild,
// damit sie weder die Bildmitte noch den Vorher/Nachher-Schieber verdeckt. Rendert nichts von der Disc selbst –
// die Menügrafik liefert die Disc über libmpv, hier wird nur "discnav" weitergereicht.
Rectangle {
    id: bar
    property var theme
    readonly property var disc: app.disc
    signal used()

    radius: 8
    color: "#b3000000"
    implicitWidth: row.implicitWidth + 16
    implicitHeight: row.implicitHeight + 10

    component NavButton: Button {
        property string navAction
        flat: true
        focusPolicy: Qt.NoFocus          // Tastaturfokus bleibt beim Fenster (Pfeiltasten steuern das Menü)
        implicitWidth: 34; implicitHeight: 30
        palette.buttonText: "white"
        onClicked: { bar.disc.navigate(navAction); bar.used() }
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Label { text: bar.disc.menuActive ? qsTr("Disc-Menü") : qsTr("Disc"); color: "white"; font.bold: true; rightPadding: 6 }
        NavButton { navAction: "left"; text: "◀"; ToolTip.visible: hovered; ToolTip.text: qsTr("Links") }
        NavButton { navAction: "up"; text: "▲"; ToolTip.visible: hovered; ToolTip.text: qsTr("Hoch") }
        NavButton { navAction: "down"; text: "▼"; ToolTip.visible: hovered; ToolTip.text: qsTr("Runter") }
        NavButton { navAction: "right"; text: "▶"; ToolTip.visible: hovered; ToolTip.text: qsTr("Rechts") }
        NavButton { navAction: "select"; text: qsTr("OK"); implicitWidth: 44; ToolTip.visible: hovered; ToolTip.text: qsTr("Auswählen (Enter)") }
        NavButton { navAction: "prev"; text: "↩"; ToolTip.visible: hovered; ToolTip.text: qsTr("Zurück (Rücktaste)") }
        NavButton { navAction: "menu"; text: qsTr("Menü"); implicitWidth: 56; ToolTip.visible: hovered; ToolTip.text: qsTr("Disc-Menü aufrufen") }
    }
}
