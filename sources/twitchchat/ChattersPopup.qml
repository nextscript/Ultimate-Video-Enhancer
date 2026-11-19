import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Zuschauer des Streams: Zahl (öffentlich) und Namensliste mit Suche. ✉ schreibt jemandem privat;
// mehrere ankreuzen → Gruppe. Die Namen aller Zuschauer gibt Twitch nur Moderatoren und dem
// Streamer; sonst stehen nur die da, die Twitch beim Betreten meldet (siehe Hinweis im Fenster).
// Man selbst steht immer ganz oben, aber ohne Ankreuzen und ✉: sich selbst schreiben geht nicht.
Popup {
    id: chattersPopup
    objectName: "chattersPopup"
    property var panel
    property var controller
    property var selected: []                // [{login,name}]
    property var list: []
    signal writeTo(string login, string name)
    signal createGroup(var members)
    signal userMenu(string login, string name, var badges)

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

    background: Rectangle {
        color: Qt.color(panel.textColor()).hslLightness > 0.5 ? "#1c1c21" : "#f7f7f9"
        radius: 6
        border.color: panel.accent()
    }

    function tr(de, en) { return panel.tr2(de, en) }
    function number(n) {
        return Number(n).toLocaleString(Qt.locale(panel.tr2("de_DE", "en_US")), "f", 0)
    }
    function refresh() { list = controller ? controller.chatters(search.text) : [] }
    function isSelected(login) {
        for (var i = 0; i < selected.length; ++i)
            if (selected[i].login === login) return true
        return false
    }
    function toggle(login, name) {
        var next = []
        var found = false
        for (var i = 0; i < selected.length; ++i) {
            if (selected[i].login === login) found = true
            else next.push(selected[i])
        }
        if (!found) next.push({ login: login, name: name })
        selected = next
    }
    function badgeText(badges) {
        if (!badges) return ""
        if (badges.indexOf("broadcaster") >= 0) return tr("Streamer", "Streamer")
        if (badges.indexOf("moderator") >= 0) return "MOD"
        if (badges.indexOf("vip") >= 0) return "VIP"
        if (badges.indexOf("subscriber") >= 0) return "SUB"
        return ""
    }

    onOpened: {
        selected = []
        search.text = ""
        refresh()
    }
    Connections {
        target: chattersPopup.controller
        ignoreUnknownSignals: true
        function onChattersChanged() { if (chattersPopup.visible) Qt.callLater(chattersPopup.refresh) }
    }

    contentItem: ColumnLayout {
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Label {
                objectName: "chattersTitle"
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: !controller || controller.viewerCount < 0 ? chattersPopup.tr("Zuschauer", "Viewers")
                      : controller.streamLive ? chattersPopup.tr("%1 Zuschauer", "%1 viewers").arg(chattersPopup.number(controller.viewerCount))
                      : chattersPopup.tr("Stream offline", "Stream offline")
                font.bold: true
                font.pixelSize: panel.chatFontSize + 3
                color: panel.textColor()
            }
            ToolButton {
                objectName: "chattersClose"
                text: "✕"
                implicitWidth: 30
                onClicked: chattersPopup.close()
            }
        }
        Label {
            objectName: "chattersListInfo"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: controller && controller.chattersComplete ? panel.textColor() : panel.muted()
            font.pixelSize: Math.max(9, panel.chatFontSize - 1)
            text: !controller ? ""
                  : controller.chattersComplete
                    ? chattersPopup.tr("Namensliste von Twitch: %1 im Chat angemeldet (Zuschauer ohne Anmeldung nennt Twitch nicht).",
                                       "Name list from Twitch: %1 signed in to chat (Twitch doesn't name viewers who aren't signed in).").arg(chattersPopup.number(controller.chatterCount))
                    : chattersPopup.tr("Die Namen aller Zuschauer gibt Twitch nur an Moderatoren und den Streamer heraus. Hier stehen nur %1, die Twitch beim Betreten des Chats gemeldet hat – in großen Kanälen meldet Twitch das gar nicht.",
                                       "Twitch only gives the names of all viewers to moderators and the streamer. Shown are only %1 that Twitch reported on joining the chat – in large channels Twitch doesn't report that at all.").arg(chattersPopup.number(controller.chatterCount))
        }
        TextField {
            id: search
            objectName: "chatterSearch"
            font.pixelSize: panel.chatFontSize
            Layout.fillWidth: true
            placeholderText: chattersPopup.tr("Suchen …", "Search …")
            onTextChanged: chattersPopup.refresh()
        }
        ListView {
            id: chatterList
            objectName: "chatterList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: chattersPopup.list
            delegate: RowLayout {
                objectName: "chatterRow"
                property string login: modelData.login
                width: chatterList.width
                spacing: 4
                CheckBox {
                    objectName: "chatterCheck"
                    checked: chattersPopup.isSelected(modelData.login)
                    enabled: modelData.self !== true
                    opacity: modelData.self === true ? 0 : 1     // Platz behalten: Namen bleiben bündig
                    onToggled: chattersPopup.toggle(modelData.login, modelData.name)
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    objectName: "chatterName"
                    text: modelData.self === true ? chattersPopup.tr("%1 (du)", "%1 (you)").arg(modelData.name) : modelData.name
                    font.pixelSize: panel.chatFontSize
                    TapHandler { onTapped: chattersPopup.userMenu(modelData.login, modelData.name, modelData.badges || []) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    color: modelData.color && modelData.color.length === 7 ? modelData.color : panel.textColor()
                }
                Label {
                    text: chattersPopup.badgeText(modelData.badges)
                    visible: text !== ""
                    color: panel.muted()
                    font.pixelSize: Math.max(8, panel.chatFontSize - 3)
                    font.bold: true
                }
                ToolButton {
                    objectName: "chatterWrite"
                    visible: modelData.self !== true
                    text: "✉"
                    implicitWidth: 30
                    ToolTip.visible: hovered
                    ToolTip.text: chattersPopup.tr("Privat schreiben", "Write privately")
                    onClicked: {
                        chattersPopup.close()
                        chattersPopup.writeTo(modelData.login, modelData.name)
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: chattersPopup.list.length === 0
            wrapMode: Text.WordWrap
            color: panel.muted()
            text: search.text !== "" ? chattersPopup.tr("Niemand gefunden.", "Nobody found.")
                                     : chattersPopup.tr("Twitch hat noch keine Namen gemeldet.",
                                                        "Twitch hasn't reported any names yet.")
        }
        Button {
            objectName: "chattersMakeGroup"
            Layout.fillWidth: true
            visible: chattersPopup.selected.length > 0
            text: chattersPopup.tr("Gruppe mit %1 erstellen", "Create group with %1").arg(chattersPopup.selected.length)
            onClicked: {
                var members = chattersPopup.selected
                chattersPopup.close()
                chattersPopup.createGroup(members)
            }
        }
    }
}
