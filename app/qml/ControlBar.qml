import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Unteres Steuerfeld im Stil der bisherigen UVE-Player-Oberfläche:
// orangener Play-Button, schlichte weiße Symbole, "Pillen" für Geschwindigkeit/CC/Audio/Bild/UVE,
// Seekbar mit Zeit, Lautstärke bis 150 %
Rectangle {
    id: bar
    property var player
    property var playlist
    property var theme
    property bool overlay: false
    property bool effectsAvailable: false
    property bool effectsOn: false
    property string aspectText: qsTr("Einpassen")
    signal action(string id)
    signal openSubtitleMenu(Item anchor)
    signal openAudioMenu(Item anchor)
    signal openSpeedMenu(Item anchor)

    readonly property color iconColor: overlay ? "#f0f0f0" : theme.text
    implicitHeight: col.implicitHeight + 16
    color: overlay ? "transparent" : theme.panel

    Rectangle { visible: !bar.overlay; width: parent.width; height: 1; color: theme.line }
    Rectangle {
        visible: bar.overlay
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.45; color: "#b3000000" }
            GradientStop { position: 1.0; color: "#e6000000" }
        }
    }

    // Symbol-Button (32×32, Hover-Hintergrund) mit Vektor-Symbol oder Textzeichen
    component IconBtn: AbstractButton {
        id: ib
        property string vicon: ""    // VecIcon-Art
        property string glyph: ""    // alternativ Textzeichen
        property string tip: ""
        implicitWidth: 32; implicitHeight: 32
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        ToolTip.text: tip; ToolTip.visible: tip.length > 0 && hovered; ToolTip.delay: 500
        background: Rectangle { radius: 6; color: ib.down ? bar.theme.line : (ib.hovered ? (bar.overlay ? "#33ffffff" : bar.theme.panel2) : "transparent") }
        contentItem: Item {
            VecIcon { visible: ib.vicon.length > 0; anchors.centerIn: parent; width: 16; height: 16; kind: ib.vicon; color: ib.enabled ? bar.iconColor : bar.theme.muted }
            Text {
                visible: ib.glyph.length > 0
                anchors.centerIn: parent
                text: ib.glyph
                font.family: "Segoe UI Symbol"; font.pixelSize: 17
                color: ib.enabled ? bar.iconColor : bar.theme.muted
            }
        }
    }
    // "Pille" wie in der bisherigen Oberfläche
    component Pill: AbstractButton {
        id: pl
        property bool accent: false
        property bool on: false
        property string tip: ""
        implicitHeight: 26
        implicitWidth: Math.max(36, label.implicitWidth + 20)
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        ToolTip.text: tip; ToolTip.visible: tip.length > 0 && hovered; ToolTip.delay: 500
        background: Rectangle {
            radius: 13
            color: pl.on ? (bar.theme.dark ? "#3a2a18" : "#ffe4c7") : (bar.overlay ? "#26ffffff" : bar.theme.panel2)
            border.color: pl.accent ? (pl.enabled ? "#80ff8a1f" : bar.theme.line) : (pl.hovered ? (bar.theme.dark ? "#3b4350" : "#b9bec6") : bar.theme.line)
        }
        contentItem: Text {
            id: label
            text: pl.text
            font.pixelSize: 12
            font.bold: pl.accent
            color: !pl.enabled ? bar.theme.muted : (pl.accent ? bar.theme.accent : bar.iconColor)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 8
        anchors.leftMargin: 12; anchors.rightMargin: 12
        anchors.topMargin: bar.overlay ? 22 : 8
        spacing: 6

        SeekBar { Layout.fillWidth: true; player: bar.player; theme: bar.theme }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            // Play/Pause: orangener Button wie bisher
            AbstractButton {
                id: playBtn
                implicitWidth: 40; implicitHeight: 40
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                ToolTip.text: qsTr("Wiedergabe / Pause (Leertaste)"); ToolTip.visible: hovered; ToolTip.delay: 500
                background: Rectangle { radius: 6; color: playBtn.hovered ? bar.theme.accent2 : bar.theme.accent }
                contentItem: Item {
                    VecIcon { anchors.centerIn: parent; width: 18; height: 18; color: "#111111"
                              kind: bar.player && bar.player.state === 2 ? "pause" : "play" }
                }
                onClicked: bar.action("play.toggle")
            }
            IconBtn { vicon: "prev"; tip: qsTr("Vorheriger (P)"); onClicked: bar.action("play.prev") }
            IconBtn { vicon: "stop"; tip: qsTr("Stopp (S)"); onClicked: bar.action("play.stop") }
            IconBtn { vicon: "next"; tip: qsTr("Nächster (N)"); onClicked: bar.action("play.next") }
            Label {
                Layout.leftMargin: 8
                font.family: "Consolas"; font.pixelSize: 13
                color: bar.overlay ? "#dddddd" : bar.theme.muted
                text: {
                    if (!bar.player || bar.player.state === 0) return "00:00 / 00:00"
                    if (bar.player.duration <= 0) return app.formatTime(bar.player.position) + qsTr("  ● LIVE")
                    return app.formatTime(bar.player.position) + " / " + app.formatTime(bar.player.duration)
                }
            }
            Label {
                Layout.leftMargin: 6
                visible: !!bar.player && bar.player.abLoopActive
                text: bar.player ? bar.player.abLoopText : ""
                color: bar.theme.focus; font.pixelSize: 12
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 10
                elide: Text.ElideRight
                color: bar.overlay ? "#bbbbbb" : bar.theme.muted
                text: bar.player ? bar.player.mediaTitle : ""
            }
            Pill { id: speedPill; text: bar.player ? bar.player.speed.toFixed(2) + "×" : "1.00×"; tip: qsTr("Geschwindigkeit ([ ] =)"); onClicked: bar.openSpeedMenu(speedPill) }
            Pill { id: ccPill; text: qsTr("CC"); tip: qsTr("Untertitel (V)"); onClicked: bar.openSubtitleMenu(ccPill) }
            Pill { id: audioPill; text: qsTr("Audio"); tip: qsTr("Audiospur (B)"); onClicked: bar.openAudioMenu(audioPill) }
            Pill { text: bar.aspectText; tip: qsTr("Seitenverhältnis (A)"); onClicked: bar.action("aspect.cycle") }
            Pill { text: qsTr("Foto"); tip: qsTr("Bildschirmfoto (Umschalt+S)"); onClicked: bar.action("snapshot") }
            Pill {
                text: qsTr("UVE"); accent: true; on: bar.effectsOn
                enabled: bar.effectsAvailable
                tip: bar.effectsAvailable ? qsTr("Effekte an/aus (Strg+Alt+E)") : qsTr("Effekt-Plugin nicht aktiv")
                onClicked: bar.action("effects.toggle")
            }
            IconBtn { glyph: "☰"; tip: qsTr("Seitenleiste (Strg+L)"); onClicked: bar.action("sidebar.toggle") }
            IconBtn { glyph: "⛶"; tip: qsTr("Vollbild (F)"); onClicked: bar.action("fullscreen") }
            IconBtn {
                vicon: !bar.player || bar.player.muted || bar.player.volume === 0 ? "mute" : (bar.player.volume > 100 ? "volumeHigh" : "volume")
                tip: qsTr("Stumm (M)")
                onClicked: bar.player.setMuted(!bar.player.muted)
            }
            Slider {
                implicitWidth: 100
                from: 0; to: 150; stepSize: 1
                value: bar.player ? bar.player.volume : 100
                focusPolicy: Qt.NoFocus
                onMoved: { bar.player.setVolume(value); if (bar.player.muted) bar.player.setMuted(false) }
                ToolTip.text: qsTr("Lautstärke (bis 150 %)"); ToolTip.visible: hovered
            }
            Label {
                text: (bar.player ? Math.round(bar.player.volume) : 100) + " %"
                color: bar.overlay ? "#dddddd" : bar.theme.muted
                font.family: "Consolas"; font.pixelSize: 12
                Layout.preferredWidth: 44
            }
        }
    }
}
