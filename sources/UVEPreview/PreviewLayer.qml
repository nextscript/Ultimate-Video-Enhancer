import QtQuick

// UVEPreview: Ebene in der SeekBar des Players. Reines QML; liest nur den Zustand der Zeitleiste (MouseArea,
// Leiste, Tooltip) und zeigt das Bild des Controllers. Nimmt keine Mausereignisse entgegen – Klicken, Ziehen,
// Rückspulen und LIVE-Badge bleiben vollständig bei der Zeitleiste.
Item {
    id: pv
    property var player
    property var controller
    property Item bar
    property Item mouse
    property Item track
    property Item tip
    anchors.fill: parent
    z: 50
    enabled: false   // keine Eingaben

    readonly property real dur: player && player.duration > 0 ? player.duration : 0
    readonly property bool hovering: !!mouse && (mouse.containsMouse || mouse.pressed)
    // Nur seekbare Videos (Datei oder Stream) mit bekannter Dauer; Live, Rückspulpuffer usw. behalten die normale Zeitleiste
    readonly property bool usable: !!controller && controller.enabled && controller.available && dur > 0
                                   && !!mouse && mouse.enabled && !!bar && bar.rewind !== true && bar.live !== true
    // Dieselbe Rechnung wie Tooltip und Sprung der SeekBar
    readonly property real frac: !mouse || !track || track.width <= 0 ? 0 : Math.max(0, Math.min(1, mouse.mouseX / track.width))
    readonly property real hoverTime: frac * dur
    readonly property bool active: usable && hovering

    onHoverTimeChanged: if (active) controller.hover(hoverTime)
    onActiveChanged: {
        if (!controller) return
        if (active) controller.hover(hoverTime)
        else controller.leave()
    }
    Component.onDestruction: if (controller) controller.leave()

    function fmt(sec) {
        if (typeof app !== "undefined" && app && app.formatTime) return app.formatTime(sec)
        var s = Math.max(0, Math.floor(sec)), h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60
        var ss = s % 60
        return (h > 0 ? h + ":" + (m < 10 ? "0" : "") : "") + m + ":" + (ss < 10 ? "0" : "") + ss
    }

    // ---- Bild: zwei Puffer, damit beim schnellen Wechsel nie ein leeres Bild aufblitzt
    readonly property string wanted: controller ? controller.image : ""
    property bool aFront: true
    readonly property Image front: aFront ? imgA : imgB
    readonly property bool haveImage: wanted !== "" && front.status === Image.Ready && front.source.toString() !== ""
    onWantedChanged: {
        if (wanted === "") { imgA.source = ""; imgB.source = ""; return }
        if (front.source.toString() === wanted) return
        (aFront ? imgB : imgA).source = wanted
    }
    readonly property bool shown: active && haveImage

    // Solange ein Bild da ist, übernimmt das Popup den Zeitstempel; sonst bleibt der normale Tooltip
    Binding {
        target: pv.tip
        property: "opacity"
        value: 0
        when: pv.shown && pv.tip !== null
        restoreMode: Binding.RestoreBindingOrValue
    }

    // Position der SeekBar im Fenster (für die Begrenzung an den sichtbaren Rändern)
    readonly property point origin: {
        pv.Window.width; pv.Window.height; pv.width; pv.height; pv.hoverTime; pv.shown
        return pv.mapToItem(null, 0, 0)
    }
    readonly property real winW: pv.Window.width > 0 ? pv.Window.width : pv.width
    readonly property real iw: controller && controller.imageWidth > 0 ? controller.imageWidth : 16
    readonly property real ih: controller && controller.imageHeight > 0 ? controller.imageHeight : 9
    readonly property int labelH: 20
    readonly property int pad: 2
    // Bild passt in 240 × 180 (Hochformat bleibt schmal), in den Platz über der Leiste und in die Fensterbreite
    readonly property real scaleF: Math.max(0.1, Math.min(240 / iw, 180 / ih,
                                                          (origin.y - 4 - 8 - labelH - 2 * pad) / ih,
                                                          (winW - 8 - 2 * pad) / iw))

    Rectangle {
        id: popup
        objectName: "uvePreviewPopup"
        visible: pv.shown
        width: Math.round(pv.iw * pv.scaleF) + 2 * pv.pad
        height: Math.round(pv.ih * pv.scaleF) + pv.labelH + 2 * pv.pad
        x: Math.max(-pv.origin.x + 4, Math.min(pv.winW - pv.origin.x - width - 4,
                                                (pv.mouse ? pv.mouse.mouseX : 0) - width / 2))
        y: -height - 4
        radius: 4
        color: "#000"
        border.color: "#80ffffff"
        border.width: 1

        Item {
            id: frame
            x: pv.pad; y: pv.pad
            width: parent.width - 2 * pv.pad
            height: parent.height - pv.labelH - 2 * pv.pad
            clip: true
            Image {
                id: imgA
                anchors.fill: parent
                visible: pv.aFront
                asynchronous: true
                cache: false
                smooth: true
                fillMode: Image.Stretch
                onStatusChanged: if (status === Image.Ready && source.toString() === pv.wanted) pv.aFront = true
            }
            Image {
                id: imgB
                anchors.fill: parent
                visible: !pv.aFront
                asynchronous: true
                cache: false
                smooth: true
                fillMode: Image.Stretch
                onStatusChanged: if (status === Image.Ready && source.toString() === pv.wanted) pv.aFront = false
            }
        }
        Text {
            objectName: "uvePreviewTime"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            height: pv.labelH
            verticalAlignment: Text.AlignVCenter
            color: "white"
            font.pixelSize: 12
            font.family: "Consolas"
            text: pv.fmt(pv.hoverTime)
        }
    }
}
