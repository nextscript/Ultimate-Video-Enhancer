import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Fenster eines Plugins (Befehl "modal.open", siehe docs/PLUGIN_API.md).
// scope: "player" = über dem ganzen Player und blockierend; "video"/"sidebar" = auf diesen Bereich begrenzt, der Rest bleibt bedienbar.
// mode: "movable" = an der Titelleiste verschiebbar; "fixed" = feste Position; "fill" = füllt den Bereich (bis maxWidth/maxHeight).
Popup {
    id: root
    property var request: ({})
    property var theme
    property Item area
    property var closeResult
    property real offsetX: 0
    property real offsetY: 0
    signal finished(string key, var result)

    readonly property string scope: request.scope || "player"
    readonly property string mode: request.mode || "movable"
    readonly property bool blocking: scope === "player"
    readonly property Item overlayItem: Overlay.overlay
    readonly property alias card: card
    readonly property alias titleBar: titleBar
    readonly property alias closeButton: closeButton
    readonly property alias page: page

    function closeWith(result) { closeResult = result; close() }

    parent: area
    x: 0
    y: 0
    width: area ? area.width : 0
    height: area ? area.height : 0
    padding: 0
    popupType: Popup.Item
    modal: blocking
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    onClosed: finished(request.key, closeResult)

    background: Rectangle {
        color: root.blocking ? "#80000000" : "#40000000"
        TapHandler {
            enabled: root.request.closeOnOutsideClick === true
            onTapped: root.closeWith(undefined)
        }
    }

    Rectangle {
        id: card
        readonly property real limitW: root.request.maxWidth > 0 ? Math.min(root.width, root.request.maxWidth) : root.width
        readonly property real limitH: root.request.maxHeight > 0 ? Math.min(root.height, root.request.maxHeight) : root.height
        readonly property real pageH: page.item && page.item.implicitHeight > 0 ? page.item.implicitHeight + titleBar.height : 480
        readonly property real rangeX: Math.max(0, (root.width - width) / 2)
        readonly property real rangeY: Math.max(0, (root.height - height) / 2)
        width: root.mode === "fill" ? limitW : Math.min(limitW, root.request.width > 0 ? root.request.width : Math.min(720, root.width - 40))
        height: root.mode === "fill" ? limitH : Math.min(limitH, root.request.height > 0 ? root.request.height : Math.min(pageH, root.height - 40))
        // Bleibt immer im Bereich, auch wenn er kleiner wird
        x: Math.round((root.width - width) / 2 + Math.max(-rangeX, Math.min(rangeX, root.offsetX)))
        y: Math.round((root.height - height) / 2 + Math.max(-rangeY, Math.min(rangeY, root.offsetY)))
        color: root.theme.panel
        border.color: root.theme.line
        radius: root.mode === "fill" ? 0 : 8
        clip: true

        // Klicks auf das Fenster nicht an den Hintergrund weitergeben
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (wheel) => wheel.accepted = true }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0
            Rectangle {
                id: titleBar
                Layout.fillWidth: true
                implicitHeight: 36
                color: root.theme.panel2
                MouseArea {
                    anchors.fill: parent
                    enabled: root.mode === "movable"
                    cursorShape: enabled ? Qt.SizeAllCursor : Qt.ArrowCursor
                    preventStealing: true
                    property point pressPos
                    property point pressOffset
                    onPressed: (mouse) => {
                        pressPos = mapToItem(root.contentItem, mouse.x, mouse.y)
                        pressOffset = Qt.point(root.offsetX, root.offsetY)
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed) return
                        var p = mapToItem(root.contentItem, mouse.x, mouse.y)
                        root.offsetX = Math.max(-card.rangeX, Math.min(card.rangeX, pressOffset.x + p.x - pressPos.x))
                        root.offsetY = Math.max(-card.rangeY, Math.min(card.rangeY, pressOffset.y + p.y - pressPos.y))
                    }
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 4
                    Label {
                        Layout.fillWidth: true
                        text: root.request.title || ""
                        font.bold: true
                        color: root.theme.text
                        elide: Text.ElideRight
                    }
                    ToolButton {
                        id: closeButton
                        text: "✕"
                        onClicked: root.closeWith(undefined)
                        ToolTip.text: qsTr("Schließen")
                        ToolTip.visible: hovered
                    }
                }
            }
            Loader {
                id: page
                Layout.fillWidth: true
                Layout.fillHeight: true
                source: root.request.url || ""
                onLoaded: {
                    if ("theme" in item) item.theme = root.theme
                    if ("host" in item) item.host = app
                    if ("controller" in item) item.controller = root.request.controller
                    if ("plugin" in item) item.plugin = root.request.plugin
                    if ("modal" in item) item.modal = handle
                }
                Label {
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: page.status === Loader.Error
                    wrapMode: Text.Wrap
                    color: root.theme.error
                    text: qsTr("Das Fenster des Plugins konnte nicht geladen werden (siehe Plugin-Fehlerprotokoll). Die Wiedergabe ist davon nicht betroffen.")
                }
            }
        }
    }

    // Für die Seite des Plugins (Eigenschaft "modal"): modal.data, modal.scope, modal.mode, modal.close(result)
    QtObject {
        id: handle
        readonly property var data: root.request.data
        readonly property string scope: root.scope
        readonly property string mode: root.mode
        function close(result) { root.closeWith(result) }
    }
}
