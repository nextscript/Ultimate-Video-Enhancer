import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Senderkarte: drehbarer Globus mit allen Orten, an denen Radio-Browser-Sender verortet sind.
// Ziehen dreht, Mausrad zoomt, Klick auf einen Punkt (oder Loslassen über dem Fadenkreuz) zeigt die Sender dort.
// Abgespielt wird erst mit ▶ oder Doppelklick in der Liste.
Item {
    id: root
    property var controller
    property var theme
    property var modal
    implicitWidth: 1040
    implicitHeight: 660

    readonly property var geo: controller ? controller.geo : null
    property var globe: null
    property int place: -1
    readonly property color cText: theme ? theme.text : "#e6e8eb"
    readonly property color cMuted: theme ? theme.muted : "#8b93a1"
    readonly property color cLine: theme ? theme.line : "#262b34"
    readonly property bool dark: !theme || theme.dark !== false
    readonly property real bgAlpha: modal && modal.backgroundAlpha !== undefined ? modal.backgroundAlpha : 1

    function stationsHere() { return geo && place >= 0 ? (geo.placeCount, geo.stationsAt(place)) : [] }
    function goToPlaying() {
        if (!globe || !geo || !controller.current || !controller.current.id) return
        var p = geo.placeOfStation(controller.current.id)
        if (p >= 0) { place = p; globe.selectedPlace = p; globe.flyToPlace(p, Math.max(globe.zoom, 4)) }
    }

    // Der Player setzt controller/theme/modal erst in Loader.onLoaded (nach Component.onCompleted)
    Component.onCompleted: init()
    onControllerChanged: init()
    onThemeChanged: applyColors()
    function init() {
        if (!controller || globe) return
        controller.loadGeo(false)
        globe = controller.createGlobe(globeHost)
        if (!globe) return
        globe.placeClicked.connect(function(p) { root.place = p })
        applyColors()
    }
    function applyColors() {
        if (!globe) return
        globe.oceanColor = dark ? "#0c1826" : "#bcd6ec"
        globe.landColor = dark ? "#1c3528" : "#e4ecd9"
        globe.borderColor = dark ? "#3d6150" : "#9fb39a"
        globe.dotColor = dark ? "#3ecf6e" : "#1f9d4c"
        globe.accentColor = theme ? theme.accent : "#ff8a1f"
    }
    onDarkChanged: applyColors()

    // Laufenden Sender auf der Karte orange markieren
    Binding {
        target: root.globe
        when: root.globe !== null && root.geo !== null
        property: "playingPlace"
        value: root.geo && root.controller && root.controller.current && root.controller.current.id
               && (root.controller.state === "playing" || root.controller.state === "connecting")
               ? (root.geo.placeCount, root.geo.placeOfStation(root.controller.current.id)) : -1
    }

    Rectangle {
        anchors.fill: parent
        color: theme ? theme.panel : "#14171c"
        opacity: root.bgAlpha
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------------------- Globus
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            Rectangle {   // Weltraum
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0; color: root.dark ? "#05080d" : "#e9eef4" }
                    GradientStop { position: 1; color: root.dark ? "#0b111a" : "#d9e2ec" }
                }
                opacity: root.bgAlpha
            }
            Item { id: globeHost; anchors.fill: parent }

            // Name des Ortes unter dem Fadenkreuz (beim Drehen) bzw. unter der Maus
            Rectangle {
                readonly property int p: root.globe ? (root.globe.hoveredPlace >= 0 ? root.globe.hoveredPlace : root.globe.centerPlace) : -1
                visible: p >= 0 && root.geo !== null
                x: root.globe && root.globe.hoveredPlace >= 0 ? Math.min(parent.width - width - 8, root.globe.hoverPos.x + 14) : (parent.width - width) / 2
                y: root.globe && root.globe.hoveredPlace >= 0 ? Math.max(8, root.globe.hoverPos.y - height - 10) : parent.height / 2 + 30
                width: hoverLabel.implicitWidth + 16
                height: hoverLabel.implicitHeight + 8
                radius: 5
                color: "#cc000000"
                Text {
                    id: hoverLabel
                    anchors.centerIn: parent
                    color: "white"
                    font.pixelSize: 12
                    textFormat: Text.PlainText
                    text: parent.p >= 0 && root.geo ? root.geo.placeLabel(parent.p) + "  ·  " + qsTr("%n Sender", "", root.geo.placeStationCount(parent.p)) : ""
                }
            }

            // Zustand der Daten
            Rectangle {
                anchors.centerIn: parent
                visible: root.geo && root.geo.state !== "ready"
                width: Math.min(parent.width - 40, 380)
                height: stateCol.implicitHeight + 24
                radius: 8
                color: "#dd000000"
                ColumnLayout {
                    id: stateCol
                    anchors.centerIn: parent
                    width: parent.width - 24
                    spacing: 8
                    BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: root.geo && root.geo.state === "loading"; visible: running }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: "white"
                        font.pixelSize: 13
                        text: !root.geo ? "" : root.geo.state === "loading" ? qsTr("Lade Senderkarte aus dem Katalog …")
                            : root.geo.state === "error" ? qsTr("Senderkarte konnte nicht geladen werden: %1").arg(root.geo.error) : ""
                    }
                    RButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: root.geo && root.geo.state === "error"
                        theme: root.theme
                        text: qsTr("Erneut versuchen")
                        onClicked: root.controller.loadGeo(true)
                    }
                }
            }

            // Werkzeuge
            ColumnLayout {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 12
                spacing: 6
                RButton { theme: root.theme; text: "+"; implicitWidth: 34; tip: qsTr("Vergrößern"); onClicked: if (root.globe) root.globe.zoomBy(1.5) }
                RButton { theme: root.theme; text: "−"; implicitWidth: 34; tip: qsTr("Verkleinern"); onClicked: if (root.globe) root.globe.zoomBy(1 / 1.5) }
                RButton {
                    theme: root.theme; text: "◎"; implicitWidth: 34
                    tip: qsTr("Zum laufenden Sender")
                    enabled: !!root.controller && !!root.controller.current && !!root.controller.current.id
                    onClicked: root.goToPlaying()
                }
                RButton { theme: root.theme; text: "⌂"; implicitWidth: 34; tip: qsTr("Ganze Erde"); onClicked: if (root.globe) root.globe.flyTo(30, 10, 1) }
            }
            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 10
                color: root.cMuted
                font.pixelSize: 10
                text: (root.geo && root.geo.state === "ready"
                       ? qsTr("%1 Sender an %2 Orten").arg(root.geo.stationCount).arg(root.geo.placeCount) + (root.geo.stale ? "  ·  " + qsTr("Katalog offline – gespeicherter Stand") : "") + "  ·  "
                       : "") + qsTr("Ziehen: drehen · Mausrad: zoomen · Klick: Ort wählen") + "  ·  Radio Browser, Natural Earth"
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: root.cLine }

        // ------------------------------------------------------------- Sender am Ort
        ColumnLayout {
            Layout.fillWidth: false   // sonst übernimmt die Spalte das fillWidth ihrer Kinder
            Layout.preferredWidth: Math.min(330, root.width * 0.38)
            Layout.maximumWidth: Layout.preferredWidth
            Layout.fillHeight: true
            Layout.margins: 8
            spacing: 8

            NowPlaying {
                Layout.fillWidth: true
                theme: root.theme
                controller: root.controller
            }
            Text {
                Layout.fillWidth: true
                text: root.place >= 0 && root.geo ? root.geo.placeLabel(root.place) : qsTr("Kein Ort gewählt")
                textFormat: Text.PlainText
                color: root.cText
                font.pixelSize: 15
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: root.place >= 0
                text: root.geo ? qsTr("%n Sender", "", root.geo.placeStationCount(root.place)) : ""
                color: root.cMuted
                font.pixelSize: 11
            }
            ListView {
                id: placeList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: root.stationsHere()
                ScrollBar.vertical: ScrollBar {}
                delegate: StationRow {
                    required property var modelData
                    width: placeList.width - 8
                    theme: root.theme
                    controller: root.controller
                    station: modelData
                    onPlayRequested: root.controller.play(modelData, "map")
                }
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 20
                    visible: placeList.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: root.cMuted
                    font.pixelSize: 12
                    text: qsTr("Dreh die Erde und klick auf einen grünen Punkt – oder lass einen Punkt unter dem Fadenkreuz los.")
                }
            }
        }
    }

    // Ziehgriff unten rechts: Größe des Fensters ändern. Der Player hält das Fenster zentriert, deshalb wächst es um
    // das Doppelte des Mauswegs – so bleibt die Ecke unter dem Mauszeiger. Die Größe wird gemerkt.
    MouseArea {
        id: grip
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 22
        height: 22
        z: 10
        cursorShape: Qt.SizeFDiagCursor
        hoverEnabled: true
        preventStealing: true
        property point start
        property size startSize
        property size last
        visible: !!root.modal && root.modal.mode !== "fill"
        onPressed: function(mouse) {
            start = mapToGlobal(mouse.x, mouse.y)
            startSize = Qt.size(root.width, root.height + 36)   // + Titelleiste des Player-Fensters
            last = startSize
        }
        onPositionChanged: function(mouse) {
            if (!pressed || !root.controller) return
            var p = mapToGlobal(mouse.x, mouse.y)
            var w = Math.round(startSize.width + 2 * (p.x - start.x))
            var h = Math.round(startSize.height + 2 * (p.y - start.y))
            if (Math.abs(w - last.width) < 4 && Math.abs(h - last.height) < 4) return
            last = Qt.size(w, h)
            root.controller.resizeGlobe(w, h, false)
        }
        onReleased: if (root.controller) root.controller.resizeGlobe(root.width, root.height + 36, true)
        Canvas {
            anchors.fill: parent
            opacity: grip.containsMouse || grip.pressed ? 1 : 0.6
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = root.cMuted
                ctx.lineWidth = 1.5
                for (var i = 0; i < 3; ++i) {
                    var o = 6 + i * 5
                    ctx.beginPath(); ctx.moveTo(width - 3, height - o); ctx.lineTo(width - o, height - 3); ctx.stroke()
                }
            }
        }
        ToolTip.visible: containsMouse && !pressed
        ToolTip.delay: 600
        ToolTip.text: qsTr("Ziehen: Fenstergröße ändern")
    }
}
