import QtQuick

// Einfarbige Vektor-Symbole (unabhängig von Emoji-Schriftarten): play, pause, stop, prev, next, volume, mute, volumeHigh
Canvas {
    id: c
    property string kind: "play"
    property color color: "white"
    implicitWidth: 16
    implicitHeight: 16
    onKindChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        var ctx = getContext("2d")
        var w = width, h = height
        ctx.reset()
        ctx.fillStyle = c.color
        ctx.strokeStyle = c.color
        ctx.lineWidth = Math.max(1.5, w / 10)
        ctx.lineCap = "round"
        function tri(x0, y0, x1, y1, x2, y2) { ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.lineTo(x2, y2); ctx.closePath(); ctx.fill() }
        function speaker() {
            ctx.beginPath()
            ctx.moveTo(w * 0.08, h * 0.36); ctx.lineTo(w * 0.30, h * 0.36); ctx.lineTo(w * 0.55, h * 0.12)
            ctx.lineTo(w * 0.55, h * 0.88); ctx.lineTo(w * 0.30, h * 0.64); ctx.lineTo(w * 0.08, h * 0.64)
            ctx.closePath(); ctx.fill()
        }
        switch (c.kind) {
        case "play": tri(w * 0.22, h * 0.12, w * 0.88, h * 0.5, w * 0.22, h * 0.88); break
        case "pause": ctx.fillRect(w * 0.2, h * 0.14, w * 0.2, h * 0.72); ctx.fillRect(w * 0.6, h * 0.14, w * 0.2, h * 0.72); break
        case "stop": ctx.fillRect(w * 0.18, h * 0.18, w * 0.64, h * 0.64); break
        case "prev":
            ctx.fillRect(w * 0.12, h * 0.16, w * 0.14, h * 0.68)
            tri(w * 0.88, h * 0.16, w * 0.30, h * 0.5, w * 0.88, h * 0.84); break
        case "next":
            ctx.fillRect(w * 0.74, h * 0.16, w * 0.14, h * 0.68)
            tri(w * 0.12, h * 0.16, w * 0.70, h * 0.5, w * 0.12, h * 0.84); break
        case "mute":
            speaker()
            ctx.beginPath(); ctx.moveTo(w * 0.66, h * 0.34); ctx.lineTo(w * 0.94, h * 0.66)
            ctx.moveTo(w * 0.94, h * 0.34); ctx.lineTo(w * 0.66, h * 0.66); ctx.stroke(); break
        case "volume":
        case "volumeHigh":
            speaker()
            ctx.beginPath(); ctx.arc(w * 0.55, h * 0.5, w * 0.2, -0.9, 0.9); ctx.stroke()
            if (c.kind === "volumeHigh") { ctx.beginPath(); ctx.arc(w * 0.55, h * 0.5, w * 0.38, -0.9, 0.9); ctx.stroke() }
            break
        }
    }
}
