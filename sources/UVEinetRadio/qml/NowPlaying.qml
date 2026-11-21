import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// „Jetzt läuft“: Sendername, Live-Status, Künstler/Titel (nur aus empfangenen Metadaten), Fehler mit Aktualisieren
Rectangle {
    id: root
    property var theme
    property var controller
    RadioTexts { id: texts }

    readonly property var cur: controller ? controller.current : ({})
    readonly property bool hasStation: !!cur && !!cur.id
    readonly property string st: controller ? controller.state : "idle"
    readonly property bool live: st === "playing"

    implicitHeight: col.implicitHeight + 20
    radius: 8
    color: theme ? theme.panel2 : "#1b1f26"
    border.color: st === "error" ? (theme ? theme.error : "#ff6b6b") : live ? (theme ? theme.accent : "#ff8a1f") : (theme ? theme.line : "#262b34")
    border.width: 1

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            StationLogo {
                theme: root.theme
                controller: root.controller
                favicon: root.hasStation ? (root.cur.favicon || "") : ""
                name: root.hasStation ? root.cur.name : ""
                size: 48
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    spacing: 6
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: root.st === "error" ? (theme ? theme.error : "#ff6b6b")
                             : root.live ? "#3ecf6e" : (theme ? theme.muted : "#8b93a1")
                        SequentialAnimation on opacity {
                            running: root.st === "connecting" || root.st === "checking" || (!!controller && controller.buffering)
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.2; duration: 500 }
                            NumberAnimation { to: 1; duration: 500 }
                            onStopped: parent.opacity = 1
                        }
                    }
                    Text {
                        text: root.live ? qsTr("LIVE") + " · " + texts.stateText(root.st, controller.buffering, controller.paused)
                                        : texts.stateText(root.st, false, false)
                        color: root.live ? "#3ecf6e" : (theme ? theme.muted : "#8b93a1")
                        font.pixelSize: 11
                        font.bold: true
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.hasStation ? root.cur.name : qsTr("Kein Sender gewählt")
                    textFormat: Text.PlainText
                    color: theme ? theme.text : "#e6e8eb"
                    font.pixelSize: 15
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: root.live && controller.title !== ""
                    text: controller ? (controller.artist !== "" ? controller.artist + " – " + controller.title : controller.title) : ""
                    textFormat: Text.PlainText
                    color: theme ? theme.accent2 : "#ffb15c"
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: {
                        if (!root.live || !controller) return ""
                        var p = []
                        if (controller.liveCodec) p.push(controller.liveCodec.toUpperCase())
                        if (controller.liveBitrate > 0) p.push(controller.liveBitrate + " kbit/s")
                        return p.join(" · ")
                    }
                    color: theme ? theme.muted : "#8b93a1"
                    font.pixelSize: 11
                }
            }
            RButton {
                theme: root.theme
                visible: root.st === "playing" || root.st === "connecting" || root.st === "checking"
                text: "■"
                tip: qsTr("Stopp")
                Accessible.name: qsTr("Stopp")
                implicitWidth: 34
                onClicked: controller.stop()
            }
            RButton {
                theme: root.theme
                accent: true
                visible: root.hasStation && (root.st === "stopped" || root.st === "error" || root.st === "ended" || root.st === "idle")
                text: "▶"
                tip: qsTr("Erneut abspielen")
                Accessible.name: qsTr("Erneut abspielen")
                implicitWidth: 34
                onClicked: controller.play(root.cur, "history")
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.st === "error"
            text: controller ? texts.errorText(controller.errorCode, controller.errorDetail) : ""
            textFormat: Text.PlainText
            color: theme ? theme.error : "#ff6b6b"
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
        RowLayout {
            visible: !!controller && controller.canRefresh
            spacing: 8
            RButton {
                theme: root.theme
                text: controller && controller.refreshing ? qsTr("Aktualisiere …") : qsTr("Katalogeintrag aktualisieren")
                enabled: !!controller && !controller.refreshing
                tip: qsTr("Holt die aktuelle Stream-Adresse des Senders aus dem Katalog und versucht es erneut")
                onClicked: controller.refreshCurrent()
            }
        }
    }
}
