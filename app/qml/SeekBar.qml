import QtQuick

// Präzise Seekbar: Hover-Zeitanzeige, Pufferstand, A-B-Markierung, Live-Anzeige
Item {
    id: bar
    property var player
    property var theme
    readonly property bool live: player && player.duration <= 0 && player.state !== 0
    readonly property real fraction: player && player.duration > 0 ? Math.min(1, player.position / player.duration) : 0
    property real dragFraction: -1
    implicitHeight: 20

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: mouse.containsMouse || mouse.pressed ? 6 : 4
        radius: height / 2
        color: theme.line
        Rectangle {
            width: player && player.duration > 0 ? parent.width * Math.min(1, player.bufferedUntil / player.duration) : 0
            height: parent.height; radius: parent.radius
            color: theme.dark ? "#3a414d" : "#bfc4cc"
        }
        Rectangle {
            width: bar.live ? parent.width : parent.width * (bar.dragFraction >= 0 ? bar.dragFraction : bar.fraction)
            height: parent.height; radius: parent.radius
            color: theme.accent
        }
    }
    Rectangle {
        visible: !bar.live && (mouse.containsMouse || mouse.pressed)
        x: track.width * (bar.dragFraction >= 0 ? bar.dragFraction : bar.fraction) - width / 2
        anchors.verticalCenter: parent.verticalCenter
        width: 13; height: 13; radius: 7
        color: "white"
        border.color: theme.accent; border.width: 2
    }
    Rectangle {
        id: tip
        visible: mouse.containsMouse && !!player && player.duration > 0
        color: "#000"; radius: 4
        width: tipText.implicitWidth + 12; height: 22
        y: -height - 4
        x: Math.max(0, Math.min(bar.width - width, mouse.mouseX - width / 2))
        Text {
            id: tipText
            anchors.centerIn: parent
            color: "white"; font.pixelSize: 12; font.family: "Consolas"
            text: player && player.duration > 0 ? app.formatTime(Math.max(0, Math.min(1, mouse.mouseX / bar.width)) * player.duration) : ""
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        anchors.topMargin: -6; anchors.bottomMargin: -6
        hoverEnabled: true
        enabled: !bar.live && player && player.seekable
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        function frac(x) { return Math.max(0, Math.min(1, x / bar.width)) }
        onPressed: (e) => bar.dragFraction = frac(e.x)
        onPositionChanged: (e) => { if (pressed) bar.dragFraction = frac(e.x) }
        onReleased: (e) => {
            if (player && player.duration > 0) player.seek(frac(e.x) * player.duration, false)
            bar.dragFraction = -1
        }
        onCanceled: bar.dragFraction = -1
    }
}
