import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var controller
    property var theme
    implicitHeight: col.implicitHeight + 24
    function t(de, en) { return controller ? controller.text(de, en) : en }
    readonly property color fg: theme ? theme.text : "white"
    readonly property color muted: theme ? theme.muted : "#999"
    readonly property var c: controller
    readonly property var sh: c ? c.share : ({})
    readonly property var st: c ? c.stream : ({})


    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label { visible: !!c && c.status !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; text: c ? c.status : ""; color: c && c.statusError ? "#e66" : root.muted }

        // ---------------------------------------------------------- not in a room: two tabs
        ColumnLayout {
            visible: !!c && !c.inRoom
            Layout.fillWidth: true
            spacing: 6

            TabBar {
                id: tabs
                Layout.fillWidth: true
                TabButton { text: root.t("Raum erstellen", "Create room") }
                TabButton { text: root.t("Raum beitreten", "Join room") }
            }

            StackLayout {
                Layout.fillWidth: true
                currentIndex: tabs.currentIndex

                // Create
                ColumnLayout {
                    spacing: 6
                    TextField { id: createNick; Layout.fillWidth: true; text: c ? (c.settings.nick || "") : ""; placeholderText: root.t("Anzeigenick", "Nickname") }
                    TextField { id: roomName; Layout.fillWidth: true; placeholderText: root.t("Raumname, z. B. Freitag Kino", "Room name, e.g. Friday cinema") }
                    CheckBox { id: priv; text: root.t("Privater Raum", "Private room") }
                    TextField { id: createPw; visible: priv.checked; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: root.t("Passwort", "Password") }
                    Button {
                        text: root.t("Raum erstellen", "Create room")
                        enabled: !!c && !c.busy
                        onClicked: c.createRoom(roomName.text, createNick.text, priv.checked, createPw.text)
                    }
                }

                // Join
                ColumnLayout {
                    spacing: 6
                    TextField { id: joinNick; Layout.fillWidth: true; text: c ? (c.settings.nick || "") : ""; placeholderText: root.t("Anzeigenick", "Nickname") }
                    TextField { id: joinId; Layout.fillWidth: true; placeholderText: root.t("Raum-ID (6 Zeichen, z. B. K7M3QX)", "Room ID (6 characters, e.g. K7M3QX)") }
                    TextField {
                        id: joinPw
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: c && c.needPassword ? root.t("Passwort", "Password") : root.t("Passwort (nur private Räume)", "Password (private rooms only)")
                    }
                    Button {
                        text: root.t("Raum beitreten", "Join room")
                        enabled: !!c && !c.busy
                        onClicked: c.joinRoom(joinId.text, joinNick.text, joinPw.text)
                    }
                }
            }
        }

        // ---------------------------------------------------------- in a room
        ColumnLayout {
            visible: !!c && c.inRoom
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: c ? c.roomName : ""; font.bold: true; color: root.fg; elide: Text.ElideRight }
                Button { text: root.t("Name kopieren", "Copy name"); onClicked: c.copyRoomName() }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: (c ? c.roomId : "") + (c && c.roomPrivate ? "  🔒" : ""); color: root.fg; font.family: "monospace" }
                Button { text: root.t("Raum-ID kopieren", "Copy room ID"); onClicked: c.copyRoomId() }
            }
            Label {
                color: root.muted
                text: root.t("Host: ", "Host: ") + (c ? c.hostNick : "") + " · " +
                      (c && c.connState === "online" ? root.t("verbunden", "connected")
                       : c && c.connState === "reconnecting" ? root.t("Verbindung wird wiederhergestellt", "Restoring connection") : root.t("getrennt", "disconnected"))
                      + (c && c.relays > 0 ? " · " + c.relays + root.t(" Relays", " relays") : "")
            }
            Repeater {
                model: c ? c.members : []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true; color: root.fg; elide: Text.ElideRight
                        text: modelData.nick + (modelData.host ? " ★" : "") + (modelData.me ? root.t(" (du)", " (you)") : "") + " – "
                              + (!modelData.connected ? root.t("getrennt", "disconnected") : modelData.buffering ? root.t("puffert", "buffering")
                                 : modelData.ready ? root.t("bereit", "ready") : root.t("nicht bereit", "not ready"))
                              + (modelData.mode === "stream" ? root.t(" · Raumstream", " · room stream") : "")
                    }
                    Button { visible: c.isHost && !modelData.me && modelData.connected; text: root.t("Host übergeben", "Make host"); onClicked: c.transferHost(modelData.id) }
                }
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.fg
                text: c && c.mediaGen > 0 ? root.t("Medium: ", "Medium: ") + c.mediaTitle
                                            + (c.mediaLive ? root.t(" (live)", " (live)") : c.mediaDuration > 0 ? " (" + c.formatTime(c.mediaDuration) + ")" : "")
                                            + (c.mediaUrl !== "" ? root.t(" · Link", " · link") : "")
                                          : root.t("Noch kein Medium – der Host öffnet eine Datei.", "No medium yet – the host opens a file.")
            }
            Label { visible: !!c && c.mediaWarning !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#e9a33b"; text: c ? c.mediaWarning : "" }
            Button { visible: !!c && !c.isHost && c.mediaGen > 0 && c.localFileLoaded && c.localMode !== "stream" && !c.localReady; text: root.t("Bereit", "Ready"); onClicked: c.markReady() }

            // host: relay every medium itself (YouTube & co. too) – the others need no site plugin
            Switch {
                visible: !!c && c.isHost
                Layout.fillWidth: true
                checked: !!c && c.streamAll
                text: root.t("Alles selbst als Quelle streamen (auch YouTube & Co.)", "Stream everything myself as the source (YouTube & co. too)")
                onToggled: c.setStreamAll(checked)
            }
            Label {
                visible: !!c && c.isHost && c.streamAll
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.muted
                text: root.t("Die anderen sehen genau dein Video mit Ton über deinen Upload – sie brauchen kein Plugin für die Seite.",
                             "The others get exactly your video with sound through your upload – they need no plugin for the site.")
            }

            // chat
            Label { text: root.t("Chat", "Chat"); font.bold: true; color: root.fg }
            Rectangle {
                Layout.fillWidth: true
                // The chat takes the free height of the sidebar (no gaps between the other elements)
                Layout.fillHeight: true
                Layout.minimumHeight: 320
                Layout.preferredHeight: 420
                color: theme && theme.panel ? Qt.lighter(theme.panel, 1.08) : "#1f1f1f"
                border.color: theme && theme.line ? theme.line : "#333333"
                radius: 6
                ListView {
                    id: chatList
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    spacing: 3
                    model: c ? c.chat : []
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: chatList.contentHeight > chatList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
                    onCountChanged: Qt.callLater(chatList.positionViewAtEnd)
                    delegate: Label {
                        required property var modelData
                        width: chatList.width - 8
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        color: modelData.system ? root.muted : root.fg
                        font.italic: !!modelData.system
                        text: modelData.time + "  " + (modelData.system ? modelData.nick + " " + modelData.text
                                                                       : (modelData.me ? root.t("Du", "You") : modelData.nick) + ": " + modelData.text)
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: chatList.count === 0
                        color: root.muted
                        text: root.t("Noch keine Nachrichten – sag Hallo!", "No messages yet – say hello!")
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: chatInput
                    Layout.fillWidth: true
                    maximumLength: 500
                    placeholderText: root.t("Nachricht an den Raum …", "Message to the room …")
                    onAccepted: { c.sendChat(text); text = "" }
                }
                Button { text: root.t("Senden", "Send"); enabled: chatInput.text.trim() !== ""; onClicked: { c.sendChat(chatInput.text); chatInput.text = "" } }
            }

            // stream
            Label { text: root.t("Raumstream", "Room stream"); font.bold: true; color: root.fg }
            Label {
                visible: !!sh.active; Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.muted
                text: sh.active ? root.t("Quelle: ", "Source: ") + sh.sourceNick + " · " + sh.title
                                  + (sh.status === "pending" ? root.t(" (wartet auf Bestätigung des Hosts)", " (waiting for the host to confirm)") : "")
                                  + (sh.sourceOnline === false ? root.t(" · Quelle offline", " · source offline") : "") : ""
            }
            Button { visible: !!c && c.isHost && sh.active && sh.status === "pending"; text: root.t("Medium bestätigen", "Confirm medium"); onClicked: c.confirmShare() }
            // The host's medium goes to the room by itself – no buttons
            Label {
                visible: !!c && c.isHost
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.muted
                text: root.t("Was du abspielst, geht automatisch an den Raum (direkt zwischen den Geräten, verschlüsselt). Zugelassene Teilnehmer können deine Netzwerkadresse erfahren.",
                             "Whatever you play goes to the room automatically (directly between the devices, encrypted). Admitted members can learn your network address.")
            }
            Label { visible: !!c && c.preparing; text: root.t("Wird für den Raum vorbereitet …", "Preparing for the room …"); color: root.muted }
            ProgressBar { visible: !!c && c.preparing; Layout.fillWidth: true; from: 0; to: 100; value: c ? c.prepareProgress : 0 }
            // The room stream starts by itself for everybody without an own file – no button
            Label {
                visible: !!st.active && !st.seed; Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.muted
                text: root.t("Du schaust über den Raumstream (direkt zwischen den Geräten). Zugelassene Teilnehmer können deine Netzwerkadresse erfahren.",
                             "You are watching the room stream (directly between the devices). Admitted members can learn your network address.")
            }
            Label {
                visible: !!st.active; Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.muted
                text: st.active ? (st.seed ? root.t("Du bist die Quelle", "You are the source") : root.t("Puffer: ", "Buffer: ") + st.percent + " %")
                                  + " · ↑ " + Math.round(st.upRate / 1024) + " KB/s · ↓ " + Math.round(st.downRate / 1024) + " KB/s"
                                  + " · " + st.direct + root.t(" direkt", " direct") + ", " + st.relay + " TURN" : ""
            }
            Label { visible: !!st.active && st.stall !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#e66"; text: st.stall || "" }

            RowLayout {
                Button { text: root.t("Raum verlassen", "Leave room"); onClicked: c.leaveRoom() }
                Button { visible: !!c && c.isHost; text: root.t("Raum schließen", "Close room"); onClicked: c.closeRoom() }
            }
        }
        // Outside a room: keep everything at the top (no gaps spread between the elements)
        Item { visible: !c || !c.inRoom; Layout.fillHeight: true }
    }
}
