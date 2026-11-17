import QtQuick

// Präzise Seekbar: Hover-Zeitanzeige, Pufferstand, A-B-Markierung, Live-Anzeige.
// Live-Streams mit Rückspul-Puffer (von einem Plugin gemeldet, app.liveRewind, seit 1.2.5): wie im Web-Player
// reicht die Leiste vom Beginn des Zuschauens (links, rw.window) bis Live (rechts). Was davon nicht mehr im
// Puffer ist (älter als rw.limit), ist schraffiert und mit einer orangen Linie „bis hier zurückspulen“ markiert
// (seit 1.2.7). Klicken/Ziehen springt zurück; nach dem Zurückspulen erscheint am Ende ein LIVE-Badge.
Item {
    id: bar
    property var player
    property var theme
    readonly property var rw: typeof app !== "undefined" && app ? app.liveRewind : null
    readonly property bool rewind: !!rw && rw.active === true && rw.limit > 0
    readonly property bool live: player && player.duration <= 0 && player.state !== 0
    readonly property bool atLive: !rewind || rw.atLive === true
    readonly property real fraction: player && player.duration > 0 ? Math.min(1, player.position / player.duration) : 0
    // Rückspul-Modus: 0 = Beginn des Zuschauens, 1 = Live; links von minFrac ist nichts mehr im Puffer
    readonly property real span: rewind ? Math.max(rw.window, rw.limit) : 0
    readonly property real minFrac: rewind ? Math.max(0, Math.min(1, 1 - rw.limit / span)) : 0
    readonly property real rewindFrac: !rewind || atLive ? 1 : Math.max(minFrac, Math.min(1, 1 - rw.behind / span))
    readonly property real shownFrac: dragFraction >= 0 ? dragFraction : rewind ? rewindFrac : fraction
    readonly property bool limitShown: rewind && track.width * minFrac >= 1
    readonly property color liveColor: "#eb0400"
    readonly property color limitColor: "#f59e0b"
    property real dragFraction: -1
    implicitHeight: 20

    function fmtBehind(sec) {
        var s = Math.max(0, Math.round(sec))
        var m = Math.floor(s / 60)
        return "−" + m + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
    }
    function behindAt(f) { return rewind ? (1 - Math.max(f, minFrac)) * span : 0 }

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: liveBadge.visible ? parent.width - liveBadge.width - 8 : parent.width
        height: mouse.containsMouse || mouse.pressed ? 6 : 4
        radius: height / 2
        color: theme.line
        Rectangle {   // Pufferstand (nur Dateien)
            visible: !bar.rewind
            width: player && player.duration > 0 ? parent.width * Math.min(1, player.bufferedUntil / player.duration) : 0
            height: parent.height; radius: parent.radius
            color: theme.dark ? "#3a414d" : "#bfc4cc"
        }
        Rectangle {
            objectName: "seekFill"
            width: bar.live && !bar.rewind ? parent.width : parent.width * bar.shownFrac
            height: parent.height; radius: parent.radius
            color: theme.accent
        }
    }
    // Rückspul-Grenze wie im Web-Player: schraffiert, was nicht mehr im Puffer ist, orange Linie = so weit zurück
    Canvas {
        id: limitShade
        objectName: "seekLimitShade"
        visible: bar.limitShown
        anchors.verticalCenter: parent.verticalCenter
        width: track.width * bar.minFrac
        height: track.height + 4
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = theme.dark ? "rgba(15,23,42,0.75)" : "rgba(100,116,139,0.35)"
            ctx.fillRect(0, 0, width, height)
            ctx.strokeStyle = theme.dark ? "rgba(100,116,139,0.8)" : "rgba(51,65,85,0.55)"
            ctx.lineWidth = 2
            ctx.beginPath()
            for (var x = -height; x < width + height; x += 6) {
                ctx.moveTo(x, height)
                ctx.lineTo(x + height, 0)
            }
            ctx.stroke()
        }
    }
    Rectangle {
        objectName: "seekLimitMark"
        visible: bar.limitShown
        x: track.width * bar.minFrac - 1
        anchors.verticalCenter: parent.verticalCenter
        width: 3; height: 14; radius: 2
        color: bar.limitColor
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
            text: !bar.rewind ? (player && player.duration > 0 ? app.formatTime(Math.max(0, Math.min(1, mouse.mouseX / track.width)) * player.duration) : "")
                  : mouse.mouseX / track.width < bar.minFrac ? qsTr("Zurückspulen bis %1").arg(bar.fmtBehind(bar.rw.limit))
                  : bar.fmtBehind(bar.behindAt(mouse.frac(mouse.mouseX)))
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
