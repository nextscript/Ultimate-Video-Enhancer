import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Eine Senderzeile: Logo, Name, Land, Codec/Bitrate, Stern, Abspielen (und optional Entfernen).
// Doppelklick oder ▶ startet die Wiedergabe; ein Klick wählt nur aus.
Rectangle {
    id: row
    property var theme
    property var controller
    property var station: ({})
    property bool selected: false
    property bool showRemove: false
    property string removeTip: qsTr("Entfernen")
    signal playRequested()
    signal removeRequested()
    signal clicked()

    readonly property bool isCurrent: !!controller && !!controller.current && controller.current.id === station.id
                                      && (controller.state === "playing" || controller.state === "connecting" || controller.state === "checking")
    readonly property bool favorite: controller ? (controller.libraryRevision, controller.isFavorite(station.id)) : false

    implicitHeight: 54
    radius: 6
    color: selected ? (theme ? theme.panel2 : "#1b1f26") : hover.hovered ? Qt.rgba(1, 1, 1, theme && !theme.dark ? 0 : 0.03) : "transparent"
    border.color: isCurrent ? (theme ? theme.accent : "#ff8a1f") : "transparent"
    border.width: 1

    function details() {
        var parts = []
        if (station.custom) parts.push(qsTr("Eigener Stream"))
        if (station.country) parts.push(station.country)
        else if (station.countryCode) parts.push(station.countryCode)
        var tech = []
        if (station.codec) tech.push(station.codec)
        if (station.bitrate > 0) tech.push(station.bitrate + " kbit/s")
        if (station.hls) tech.push("HLS")
        if (tech.length) parts.push(tech.join(" · "))
        return parts.join("  ·  ")
    }

    HoverHandler { id: hover }
    TapHandler {
        onTapped: row.clicked()
        onDoubleTapped: row.playRequested()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 4
        spacing: 8
        StationLogo {
            theme: row.theme
            controller: row.controller
            favicon: station.favicon || ""
            name: station.name || ""
            size: 40
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: station.name || ""
                textFormat: Text.PlainText
                color: row.isCurrent ? (theme ? theme.accent : "#ff8a1f") : (theme ? theme.text : "#e6e8eb")
                font.pixelSize: 13
                font.bold: row.isCurrent
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: row.details()
                    textFormat: Text.PlainText
                    color: theme ? theme.muted : "#8b93a1"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Text {
                    visible: !!station.insecure
                    text: "HTTP"
                    color: theme ? theme.muted : "#8b93a1"
                    font.pixelSize: 10
                    font.bold: true
                    ToolTip.visible: insecureHover.hovered
                    ToolTip.text: qsTr("Unverschlüsselte Verbindung (HTTP)")
                    HoverHandler { id: insecureHover }
                }
            }
        }
        ToolButton {
            id: star
            implicitWidth: 30
            implicitHeight: 30
            text: row.favorite ? "★" : "☆"
            font.pixelSize: 17
            hoverEnabled: true
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: row.favorite ? qsTr("Aus Favoriten entfernen") : qsTr("Zu Favoriten hinzufügen")
            Accessible.name: ToolTip.text
            onClicked: controller.toggleFavorite(station)
            contentItem: Text {
                text: star.text
                font: star.font
                color: row.favorite ? (theme ? theme.accent : "#ff8a1f") : (theme ? theme.muted : "#8b93a1")
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { radius: 15; color: star.hovered ? (theme ? theme.panel2 : "#1b1f26") : "transparent" }
        }
        ToolButton {
            id: playBtn
            implicitWidth: 30
            implicitHeight: 30
            text: "▶"
            font.pixelSize: 13
            hoverEnabled: true
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: qsTr("Abspielen")
            Accessible.name: ToolTip.text
            onClicked: row.playRequested()
            contentItem: Text {
                text: playBtn.text
                font: playBtn.font
                color: "#111"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 15
                color: playBtn.down ? Qt.darker(theme ? theme.accent : "#ff8a1f", 1.2) : (theme ? theme.accent : "#ff8a1f")
                opacity: playBtn.hovered || row.isCurrent || hover.hovered ? 1 : 0.75
            }
        }
        ToolButton {
            id: removeBtn
            visible: row.showRemove
            implicitWidth: 26
            implicitHeight: 30
            text: "✕"
            font.pixelSize: 12
            hoverEnabled: true
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: row.removeTip
            Accessible.name: ToolTip.text
            onClicked: row.removeRequested()
            contentItem: Text {
                text: removeBtn.text
                font: removeBtn.font
                color: removeBtn.hovered ? (theme ? theme.error : "#ff6b6b") : (theme ? theme.muted : "#8b93a1")
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Item {}
        }
    }
}
