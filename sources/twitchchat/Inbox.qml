import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Postfach für private Nachrichten (Twitch-Whispers): liegt als Fenster über dem ganzen Panel.
// Seiten: Liste aller Gespräche → Gespräch mit Eingabezeile; dazu „Neue Nachricht“ und
// „Gruppe“ (Verteiler: Jedes Mitglied bekommt die Nachricht einzeln).
Popup {
    id: inbox
    objectName: "inbox"
    property var panel
    property var controller
    readonly property var service: controller ? controller.whispers : null
    property string page: "list"             // list | chat | compose | group
    property string currentId: ""
    property var currentInfo: ({})
    property string groupEditId: ""          // leer = neue Gruppe
    property var groupMembers: []            // [{login,name}]

    parent: panel
    x: 0
    y: 0
    width: panel ? panel.width : 300
    height: panel ? panel.height : 400
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 10

    readonly property color cardColor: Qt.color(panel.textColor()).hslLightness > 0.5 ? "#1c1c21" : "#f7f7f9"
    background: Rectangle {
        color: inbox.cardColor
        radius: 6
        border.color: panel.accent()
    }

    function tr(de, en) { return panel.tr2(de, en) }
    function tinted(color, alpha) {
        var c = Qt.color(color)
        return Qt.rgba(c.r, c.g, c.b, alpha)
    }
    function formatTime(ts) {
        if (!ts) return ""
        var d = new Date(ts)
        return d.toDateString() === new Date().toDateString()
            ? Qt.formatDateTime(d, "hh:mm") : Qt.formatDateTime(d, "dd.MM. hh:mm")
    }
    function stateText() {
        if (!service) return ""
        var s = service.state
        if (s === "nologin") return tr("Zum Schreiben und Empfangen erst über „Login“ anmelden.",
                                       "Log in via 'Login' first to send and receive.")
        if (s === "checking") return tr("Token wird geprüft …", "Checking token …")
        if (s === "scope") return tr("Dem Token fehlen die Rechte für private Nachrichten. Bitte unter „Login“ neu „Mit Twitch anmelden“.",
                                     "The token lacks the whisper permissions. Please use 'Sign in with Twitch' under 'Login' again.")
        if (s === "connecting") return tr("Verbinde mit Twitch …", "Connecting to Twitch …")
        if (s === "error") return tr("Fehler: ", "Error: ") + service.stateDetail
        return ""
    }

    function openList() {
        page = "list"
        currentId = ""
        reload()
        open()
    }
    function openConversation(id) {
        if (!service || !id) return
        currentId = id
        page = "chat"
        service.markRead(id)
        reload()
        open()
        Qt.callLater(function () { messageList.positionViewAtEnd() })
    }
    function openDirect(login, name) {
        if (service) openConversation(service.openDirect(login, name))
    }
    function newGroup(members) {
        groupEditId = ""
        groupMembers = members || []
        groupName.text = ""
        memberField.text = ""
        page = "group"
        open()
    }
    function editGroup() {
        groupEditId = currentId
        groupMembers = currentInfo.members ? currentInfo.members.slice() : []
        groupName.text = currentInfo.title || ""
        memberField.text = ""
        page = "group"
    }
    function hasMember(login) {
        for (var i = 0; i < groupMembers.length; ++i)
            if (groupMembers[i].login === login) return true
        return false
    }
    function toggleMember(login, name) {
        var list = []
        var found = false
        for (var i = 0; i < groupMembers.length; ++i) {
            if (groupMembers[i].login === login) found = true
            else list.push(groupMembers[i])
        }
        if (!found) list.push({ login: login, name: name || login })
        groupMembers = list
    }
    function addTypedMember() {
        var login = memberField.text.trim().toLowerCase().replace("@", "")
        if (!/^[a-z0-9_]{2,25}$/.test(login)) return
        if (!hasMember(login)) toggleMember(login, login)
        memberField.text = ""
    }
    function saveGroup() {
        if (!service || groupMembers.length === 0) return
        if (groupEditId !== "") {
            service.renameGroup(groupEditId, groupName.text)
            service.setGroupMembers(groupEditId, groupMembers)
            openConversation(groupEditId)
        } else {
            openConversation(service.createGroup(groupName.text, groupMembers))
        }
    }
    function sendNow() {
        if (service && service.send(currentId, whisperInput.text)) whisperInput.text = ""
    }
    function reload() {
        if (!service) return
        convModel.clear()
        var list = service.conversations()
        for (var i = 0; i < list.length; ++i) convModel.append(list[i])
        if (currentId !== "") {
            currentInfo = service.conversation(currentId)
            msgModel.clear()
            var msgs = service.messages(currentId)
            for (var k = 0; k < msgs.length; ++k) msgModel.append(msgs[k])
        }
    }
    function chatterSuggestions(filter) {
        return controller ? controller.chatters(filter).slice(0, 100) : []
    }

    Connections {
        target: inbox.service
        ignoreUnknownSignals: true
        function onChanged() { if (inbox.visible) Qt.callLater(inbox.reload) }
        function onMessageAdded(id, outgoing) {
            if (inbox.visible && inbox.page === "chat" && id === inbox.currentId) {
                inbox.service.markRead(id)
                Qt.callLater(function () { messageList.positionViewAtEnd() })
            }
        }
    }
    ListModel { id: convModel }
    ListModel { id: msgModel }

    component Header: RowLayout {
        property alias title: headerTitle.text
        property bool back: false
        signal backClicked()
        Layout.fillWidth: true
        spacing: 4
        ToolButton {
            objectName: "inboxBack"
            visible: parent.back
            text: "←"
            implicitWidth: 30
            onClicked: parent.backClicked()
        }
        Label {
            id: headerTitle
            Layout.fillWidth: true
            elide: Text.ElideRight
            textFormat: Text.PlainText
            font.bold: true
            font.pixelSize: panel.chatFontSize + 3
            color: panel.textColor()
        }
    }
    component HintLabel: Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: panel.muted()
        font.pixelSize: Math.max(9, panel.chatFontSize - 1)
    }

    contentItem: StackLayout {
        currentIndex: inbox.page === "chat" ? 1 : inbox.page === "compose" ? 2 : inbox.page === "group" ? 3 : 0

        // ——— Gesprächsliste ———
        ColumnLayout {
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Header { title: inbox.tr("Postfach", "Inbox") }
                ToolButton {
                    objectName: "inboxCompose"
                    text: "✎"
                    implicitWidth: 30
                    ToolTip.visible: hovered
                    ToolTip.text: inbox.tr("Neue Nachricht", "New message")
                    onClicked: { composeField.text = ""; inbox.page = "compose" }
                }
                ToolButton {
                    objectName: "inboxNewGroup"
                    text: "👥"
                    implicitWidth: 30
                    ToolTip.visible: hovered
                    ToolTip.text: inbox.tr("Neue Gruppe", "New group")
                    onClicked: inbox.newGroup([])
                }
                ToolButton {
                    objectName: "soundToggle"
                    readonly property bool on: inbox.controller ? inbox.controller.notifySound : true
                    text: on ? "🔔" : "🔕"
                    implicitWidth: 30
                    opacity: on ? 1 : 0.6
                    Accessible.name: on ? inbox.tr("Ton ausschalten", "Turn sound off") : inbox.tr("Ton einschalten", "Turn sound on")
                    ToolTip.visible: hovered
                    ToolTip.text: on ? inbox.tr("Ton bei neuen Nachrichten: an (klicken zum Ausschalten)", "Sound for new messages: on (click to turn off)")
                                     : inbox.tr("Ton bei neuen Nachrichten: aus (klicken zum Einschalten)", "Sound for new messages: off (click to turn on)")
                    onClicked: if (inbox.controller) inbox.controller.notifySound = !on
                }
                ToolButton {
                    objectName: "inboxClose"
                    text: "✕"
                    implicitWidth: 30
                    onClicked: inbox.close()
                }
            }
            // Lautstärke des Tons bei neuen Nachrichten; Loslassen spielt eine Hörprobe.
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label {
                    text: inbox.tr("Lautstärke", "Volume")
                    color: panel.muted()
                    font.pixelSize: Math.max(9, panel.chatFontSize - 1)
                }
                Slider {
                    id: volumeSlider
                    objectName: "volumeSlider"
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    stepSize: 5
                    snapMode: Slider.SnapAlways
                    value: inbox.controller ? inbox.controller.notifyVolume : 30
                    opacity: inbox.controller && inbox.controller.notifySound ? 1 : 0.5
                    Accessible.name: inbox.tr("Lautstärke des Benachrichtigungstons", "Notification sound volume")
                    onMoved: if (inbox.controller) inbox.controller.notifyVolume = Math.round(value)
                    onPressedChanged: if (!pressed && inbox.controller) inbox.controller.previewNotifySound()
                }
                Label {
                    objectName: "volumeLabel"
                    Layout.preferredWidth: volumeMetrics.advanceWidth("100 %") + 2
                    horizontalAlignment: Text.AlignRight
                    text: Math.round(volumeSlider.value) + " %"
                    color: panel.textColor()
                    font.pixelSize: Math.max(9, panel.chatFontSize - 1)
                    FontMetrics { id: volumeMetrics; font.pixelSize: Math.max(9, panel.chatFontSize - 1) }
                }
            }
            HintLabel {
                objectName: "inboxState"
                visible: text !== ""
                text: inbox.stateText()
                color: inbox.service && (inbox.service.state === "scope" || inbox.service.state === "error") ? panel.errorColor() : panel.muted()
            }
            ListView {
                id: convList
                objectName: "conversationList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: convModel
                delegate: Rectangle {
                    objectName: "conversationRow"
                    property string convId: model.id
                    width: convList.width
                    height: convColumn.implicitHeight + 12
                    radius: 4
                    color: rowHover.hovered ? inbox.tinted(panel.accent(), 0.18) : model.unread > 0 ? inbox.tinted(panel.accent(), 0.08) : "transparent"
                    Column {
                        id: convColumn
                        x: 8
                        y: 6
                        width: parent.width - 16
                        spacing: 2
                        RowLayout {
                            width: parent.width
                            spacing: 6
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                                text: (model.isGroup ? "👥 " : "") + model.title
                                font.bold: model.unread > 0
                                font.pixelSize: panel.chatFontSize + 1
                                color: panel.textColor()
                            }
                            Label {
                                text: inbox.formatTime(model.lastTs)
                                color: panel.muted()
                                font.pixelSize: Math.max(8, panel.chatFontSize - 2)
                            }
                            Rectangle {
                                visible: model.unread > 0
                                implicitWidth: Math.max(18, unreadLabel.implicitWidth + 8)
                                implicitHeight: 18
                                radius: 9
                                color: panel.errorColor()
                                Label {
                                    id: unreadLabel
                                    anchors.centerIn: parent
                                    text: model.unread > 99 ? "99+" : model.unread
                                    color: "white"
                                    font.pixelSize: Math.max(8, panel.chatFontSize - 2)
                                    font.bold: true
                                }
                            }
                        }
                        Label {
                            width: parent.width
                            elide: Text.ElideRight
                            maximumLineCount: 1
                            textFormat: Text.PlainText
                            text: (model.lastOutgoing ? inbox.tr("Du: ", "You: ")
                                   : model.isGroup && model.lastFrom ? model.lastFrom + ": " : "")
                                  + (model.lastText || inbox.tr("Noch keine Nachrichten", "No messages yet")).replace(/\s+/g, " ")
                            color: panel.muted()
                            font.pixelSize: panel.chatFontSize
                        }
                    }
                    HoverHandler { id: rowHover }
                    TapHandler { onTapped: inbox.openConversation(parent.convId) }
                }
            }
            HintLabel {
                visible: convModel.count === 0
                text: inbox.tr("Noch keine privaten Nachrichten. ✎ schreibt jemandem, 👥 macht eine Gruppe. Wer gerade im Chat ist, zeigt 👥 in der Statuszeile.",
                               "No private messages yet. ✎ writes to someone, 👥 creates a group. 👥 in the status bar shows who is in the chat.")
            }
        }

        // ——— Gespräch ———
        ColumnLayout {
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Header {
                    back: true
                    title: (inbox.currentInfo.isGroup ? "👥 " : "") + (inbox.currentInfo.title || "")
                    onBackClicked: inbox.openList()
                }
                ToolButton {
                    objectName: "groupEdit"
                    visible: inbox.currentInfo.isGroup === true
                    text: "✎"
                    implicitWidth: 30
                    ToolTip.visible: hovered
                    ToolTip.text: inbox.tr("Gruppe bearbeiten", "Edit group")
                    onClicked: inbox.editGroup()
                }
                ToolButton {
                    id: deleteButton
                    objectName: "chatDelete"
                    property bool armed: false
                    text: armed ? inbox.tr("Löschen?", "Delete?") : "🗑"
                    implicitWidth: armed ? implicitContentWidth + 16 : 30
                    ToolTip.visible: hovered && !armed
                    ToolTip.text: inbox.tr("Gespräch löschen (zweimal klicken)", "Delete conversation (click twice)")
                    onClicked: {
                        if (!armed) { armed = true; disarm.restart(); return }
                        armed = false
                        inbox.service.deleteConversation(inbox.currentId)
                        inbox.openList()
                    }
                    Timer { id: disarm; interval: 3000; onTriggered: deleteButton.armed = false }
                }
            }
            HintLabel {
                visible: inbox.currentInfo.isGroup === true
                text: {
                    var names = []
                    var members = inbox.currentInfo.members || []
                    for (var i = 0; i < members.length; ++i) names.push(members[i].name)
                    return inbox.tr("An: ", "To: ") + names.join(", ")
                         + inbox.tr("  ·  Jedes Mitglied bekommt die Nachricht einzeln; die anderen sehen Antworten nicht.",
                                    "  ·  Each member gets the message separately; the others don't see replies.")
                }
            }
            HintLabel {
                visible: text !== ""
                text: inbox.stateText()
                color: panel.errorColor()
            }
            ListView {
                id: messageList
                objectName: "messageList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: msgModel
                delegate: Item {
                    width: messageList.width
                    height: bubble.height
                    Rectangle {
                        id: bubble
                        objectName: "messageBubble"
                        anchors.right: model.outgoing ? parent.right : undefined
                        anchors.left: model.outgoing ? undefined : parent.left
                        width: Math.min(messageList.width * 0.85,
                                        Math.max(measure.implicitWidth, metaLabel.implicitWidth, nameLabel.visible ? nameLabel.implicitWidth : 0) + 16)
                        height: bubbleColumn.implicitHeight + 10
                        radius: 8
                        color: model.outgoing ? inbox.tinted(panel.accent(), 0.35) : inbox.tinted(panel.textColor(), 0.08)
                        Text {
                            id: measure
                            visible: false
                            text: model.text
                            textFormat: Text.PlainText
                            font.pixelSize: panel.chatFontSize
                        }
                        Column {
                            id: bubbleColumn
                            x: 8
                            y: 5
                            width: parent.width - 16
                            spacing: 2
                            Label {
                                id: nameLabel
                                visible: !model.outgoing && inbox.currentInfo.isGroup === true
                                text: model.name
                                textFormat: Text.PlainText
                                font.bold: true
                                font.pixelSize: Math.max(9, panel.chatFontSize - 1)
                                color: panel.accent()
                            }
                            TextEdit {
                                width: parent.width
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.Wrap
                                textFormat: TextEdit.PlainText
                                text: model.text
                                color: panel.textColor()
                                selectionColor: panel.accent()
                                selectedTextColor: "white"
                                font.pixelSize: panel.chatFontSize
                            }
                            Label {
                                id: metaLabel
                                width: parent.width
                                wrapMode: Text.WordWrap
                                horizontalAlignment: model.outgoing ? Text.AlignRight : Text.AlignLeft
                                text: inbox.formatTime(model.ts)
                                      + (model.status === "sending" ? inbox.tr("  · wird gesendet …", "  · sending …")
                                         : model.status === "failed" ? inbox.tr("  · nicht zugestellt: ", "  · not delivered: ") + model.error : "")
                                color: model.status === "failed" ? panel.errorColor() : panel.muted()
                                font.pixelSize: Math.max(8, panel.chatFontSize - 3)
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    id: whisperInput
                    objectName: "whisperInput"
                    font.pixelSize: panel.chatFontSize
                    Layout.fillWidth: true
                    maximumLength: 500
                    placeholderText: inbox.service && inbox.service.canSend ? inbox.tr("Nachricht schreiben …", "Write a message …")
                                                                            : inbox.tr("Senden nicht möglich – siehe Hinweis", "Cannot send – see note")
                    onAccepted: inbox.sendNow()
                }
                Button {
                    objectName: "whisperSend"
                    text: inbox.tr("Senden", "Send")
                    enabled: inbox.service !== null && inbox.service.canSend && whisperInput.text.trim().length > 0
                    onClicked: inbox.sendNow()
                }
            }
        }

        // ——— Neue Nachricht ———
        ColumnLayout {
            spacing: 6
            Header {
                back: true
                title: inbox.tr("Neue Nachricht", "New message")
                onBackClicked: inbox.openList()
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: composeField
                    objectName: "composeField"
                    font.pixelSize: panel.chatFontSize
                    Layout.fillWidth: true
                    placeholderText: inbox.tr("Twitch-Name …", "Twitch name …")
                    onAccepted: composeOpen.clicked()
                }
                Button {
                    id: composeOpen
                    objectName: "composeOpen"
                    text: inbox.tr("Schreiben", "Write")
                    enabled: /^@?[a-zA-Z0-9_]{2,25}$/.test(composeField.text.trim())
                    onClicked: inbox.openDirect(composeField.text.trim().toLowerCase().replace("@", ""), "")
                }
            }
            HintLabel { text: inbox.tr("Oder aus dem Chat wählen:", "Or pick someone from the chat:") }
            ListView {
                id: composeList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: inbox.page === "compose" ? inbox.chatterSuggestions(composeField.text) : []
                delegate: ItemDelegate {
                    objectName: "composeSuggestion"
                    font.pixelSize: panel.chatFontSize
                    width: composeList.width
                    text: modelData.name
                    onClicked: inbox.openDirect(modelData.login, modelData.name)
                }
            }
        }

        // ——— Gruppe anlegen / bearbeiten ———
        ColumnLayout {
            spacing: 6
            Header {
                back: true
                title: inbox.groupEditId !== "" ? inbox.tr("Gruppe bearbeiten", "Edit group") : inbox.tr("Neue Gruppe", "New group")
                onBackClicked: inbox.groupEditId !== "" ? inbox.openConversation(inbox.groupEditId) : inbox.openList()
            }
            TextField {
                id: groupName
                objectName: "groupName"
                font.pixelSize: panel.chatFontSize
                Layout.fillWidth: true
                placeholderText: inbox.tr("Name der Gruppe", "Group name")
            }
            Label {
                font.pixelSize: panel.chatFontSize
                text: inbox.tr("Mitglieder (%1)", "Members (%1)").arg(inbox.groupMembers.length)
                color: panel.textColor()
                font.bold: true
            }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: inbox.groupMembers
                    delegate: Rectangle {
                        objectName: "memberChip"
                        width: chipRow.implicitWidth + 10
                        height: chipRow.implicitHeight + 4
                        radius: 3
                        color: inbox.tinted(panel.accent(), 0.25)
                        Row {
                            id: chipRow
                            x: 5
                            y: 2
                            spacing: 4
                            Label { text: modelData.name; color: panel.textColor(); font.pixelSize: panel.chatFontSize; textFormat: Text.PlainText }
                            Label {
                                text: "✕"
                                color: panel.muted()
                                font.pixelSize: Math.max(9, panel.chatFontSize - 1)
                                TapHandler { onTapped: inbox.toggleMember(modelData.login, modelData.name) }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: memberField
                    objectName: "memberField"
                    font.pixelSize: panel.chatFontSize
                    Layout.fillWidth: true
                    placeholderText: inbox.tr("Name suchen oder eintippen …", "Search or type a name …")
                    onAccepted: inbox.addTypedMember()
                }
                Button {
                    text: "+"
                    implicitWidth: 34
                    enabled: /^@?[a-zA-Z0-9_]{2,25}$/.test(memberField.text.trim())
                    onClicked: inbox.addTypedMember()
                }
            }
            ListView {
                id: memberList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: inbox.page === "group" ? inbox.chatterSuggestions(memberField.text) : []
                delegate: CheckDelegate {
                    objectName: "memberCandidate"
                    font.pixelSize: panel.chatFontSize
                    width: memberList.width
                    text: modelData.name
                    checked: inbox.hasMember(modelData.login)
                    onToggled: inbox.toggleMember(modelData.login, modelData.name)
                }
            }
            HintLabel {
                text: inbox.tr("Twitch kennt keine Gruppen: Jedes Mitglied bekommt deine Nachrichten einzeln als private Nachricht und sieht die anderen nicht. Höchstens 40 verschiedene Empfänger pro Tag.",
                               "Twitch has no groups: each member gets your messages as a separate whisper and doesn't see the others. At most 40 different recipients per day.")
            }
            Button {
                objectName: "groupSave"
                Layout.fillWidth: true
                text: inbox.groupEditId !== "" ? inbox.tr("Speichern", "Save") : inbox.tr("Gruppe erstellen", "Create group")
                enabled: inbox.groupMembers.length > 0
                onClicked: inbox.saveGroup()
            }
        }
    }
}
