import QtQuick

// Logo des Senders aus dem lokalen Cache – oder ein Platzhalter mit Anfangsbuchstaben.
// Das Logo lädt im Hintergrund und blockiert weder Suche noch Start.
Rectangle {
    id: root
    property var theme
    property var controller
    property string favicon: ""
    property string name: ""
    property int size: 40
    width: size
    height: size
    radius: 6
    color: theme ? theme.panel2 : "#1b1f26"
    clip: true

    readonly property string localUrl: controller && favicon !== "" ? (controller.logoRevision, controller.logoUrl(favicon)) : ""

    Text {
        anchors.centerIn: parent
        visible: logo.status !== Image.Ready
        text: root.name.length ? root.name.replace(/^[^A-Za-z0-9À-ɏ]+/, "").charAt(0).toUpperCase() || "♪" : "♪"
        color: theme ? theme.accent : "#ff8a1f"
        font.pixelSize: root.size * 0.45
        font.bold: true
    }
    Image {
        id: logo
        anchors.fill: parent
        anchors.margins: 2
        source: root.localUrl
        asynchronous: true
        cache: true
        fillMode: Image.PreserveAspectFit
        sourceSize.width: root.size * 2
        sourceSize.height: root.size * 2
        smooth: true
        visible: status === Image.Ready
    }
}
