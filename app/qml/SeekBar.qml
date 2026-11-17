import QtQuick

// Präzise Seekbar: Hover-Zeitanzeige, Pufferstand, A-B-Markierung, Live-Anzeige.
// Live-Streams mit Rückspul-Puffer (von einem Plugin gemeldet, app.liveRewind, seit 1.2.5): die Leiste
// zeigt den Puffer (links = ältester Moment, rechts = jetzt), Klicken/Ziehen springt zurück, und nach dem
// Zurückspulen erscheint am Ende ein LIVE-Badge, das zurück zu Live springt.
Item {
    id: bar
    property var player
    property var theme
    readonly property var rw: typeof app !== "undefined" && app ? app.liveRewind : null
    readonly property bool rewind: !!rw && rw.active === true && rw.window > 0
    readonly property bool live: player && player.duration <= 0 && player.state !== 0
    readonly property bool atLive: !rewind || rw.atLive === true
    readonly property real fraction: player && player.duration > 0 ? Math.min(1, player.position / player.duration) : 0
    // Rückspul-Modus: kleinster erreichbarer Anteil und aktuelle Stelle
    readonly property real minFrac: rewind ? Math.max(0, 1 - rw.limit / rw.window) : 0
    readonly property real rewindFrac: rewind ? Math.max(0, Math.min(1, 1 - rw.behind / rw.window)) : 1
    readonly property real shownFrac: dragFraction >= 0 ? dragFraction : rewind ? rewindFrac : fraction
    readonly property color liveColor: "#eb0400"
    property real dragFraction: -1
    implicitHeight: 20

    function fmtBehind(sec) {
        var s = Math.max(0, Math.round(sec))
        var m = Math.floor(s / 60)
        return "−" + m + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
    }
    function behindAt(f) { return rewind ? (1 - f) * rw.window : 0 }

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: liveBadge.visible ? parent.width - liveBadge.width - 8 : parent.width
        height: mouse.containsMouse || mouse.pressed ? 6 : 4
        radius: height / 2
        color: theme.line
        Rectangle {   // Pufferstand (Datei) bzw. nicht mehr erreichbarer Anfang (Rückspul-Puffer)
            visible: !bar.rewind || bar.minFrac > 0
            width: bar.rewind ? parent.width * bar.minFrac
                              : player && player.duration > 0 ? parent.width * Math.min(1, player.bufferedUntil / player.duration) : 0
            height: parent.height; radius: parent.radius
            color: bar.rewind ? Qt.rgba(0, 0, 0, 0.3) : theme.dark ? "#3a414d" : "#bfc4cc"
        }
        Rectangle {
            objectName: "seekFill"
            x: bar.rewind ? parent.width * bar.minFrac : 0
            width: bar.rewind ? Math.max(0, parent.width * (bar.shownFrac - bar.minFrac))
                   : bar.live ? parent.width : parent.width * bar.shownFrac
            height: parent.height; radius: parent.radius
            color: theme.accent
        }
    }
    Rectangle {
        visible: (bar.rewind || !bar.live) && (mouse.containsMouse || mouse.pressed)
        x: track.width * bar.shownFrac - width / 2
        anchors.verticalCenter: parent.verticalCenter
        width: 13; height: 13; radius: 7
        color: "white"
        border.color: theme.accent; border.width: 2
    }
    Rectangle {
        id: tip
        visible: mouse.containsMouse && !!player && (player.duration > 0 || bar.rewind)
        color: "#000"; radius: 4
        width: tipText.implicitWidth + 12; height: 22
        y: -height - 4
        x: Math.max(0, Math.min(bar.width - width, mouse.mouseX - width / 2))
        Text {
            id: tipText
            anchors.centerIn: parent
            color: "white"; font.pixelSize: 12; font.family: "Consolas"
            text: bar.rewind ? bar.fmtBehind(bar.behindAt(mouse.frac(mouse.mouseX)))
                  : player && player.duration > 0 ? app.formatTime(Math.max(0, Math.min(1, mouse.mouseX / track.width)) * player.duration) : ""
        }
    }

    MouseArea {
        id: mouse
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: track.width
        anchors.topMargin: -6; anchors.bottomMargin: -6
        hoverEnabled: true
        enabled: bar.rewind ? !bar.rw.busy : (!bar.live && player && player.seekable)
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        function frac(x) { return Math.max(bar.rewind ? bar.minFrac : 0, Math.min(1, x / track.width)) }
        onPressed: (e) => bar.dragFraction = frac(e.x)
        onPositionChanged: (e) => { if (pressed) bar.dragFraction = frac(e.x) }
        onReleased: (e) => {
            if (bar.rewind) app.liveRewindSeek(bar.behindAt(frac(e.x)))
            else if (player && player.duration > 0) player.seek(frac(e.x) * player.duration, false)
            bar.dragFraction = -1
        }
        onCanceled: bar.dragFraction = -1
    }

    // Nur nach dem Zurückspulen: kleines LIVE-Badge am Ende, ein Klick springt zurück zu Live
    Rectangle {
        id: liveBadge
        objectName: "seekLiveBadge"
        visible: bar.rewind && !bar.atLive
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: badgeText.implicitWidth + 10
        height: badgeText.implicitHeight + 4
        radius: 3
        color: badgeMouse.containsMouse ? Qt.darker(bar.liveColor, 1.2) : bar.liveColor
        Text {
            id: badgeText
            anchors.centerIn: parent
            text: "LIVE"
            color: "white"
            font.pixelSize: 10
            font.bold: true
        }
        MouseArea {
            id: badgeMouse
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            enabled: !bar.rw || !bar.rw.busy
            onClicked: app.liveRewindGoLive()
        }
    }
}
