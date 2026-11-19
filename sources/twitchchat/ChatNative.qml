import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Natives Chat-Panel – seit 2.2.0 der einzige Chat: eigene IRC-Verbindung über
// wss://irc-ws.chat.twitch.tv mit „Senden“ posten (dafür ist der Token-Login nötig).
// Nachrichten laufen als Segment-Flow (Text-Wörter + Emote-Bilder in einer Zeile):
// animierte Emotes spielen in AnimatedImage (WebP/GIF), BTTV-, FFZ- und 7TV-Emotes werden
// erkannt, Zero-Width-Emotes liegen über dem Emote davor, und ein Auto-Scroll-Knopf hält
// die Ansicht dauerhaft am Chat-Ende. Dazu der Emote-Browser (Tabs, Suche, ★-Favoriten)
// aus allen Quellen.
// Deklariert controller/plugin/theme – ChatPanel.qml reicht sie per
// Qt.binding durch, weil der Player sie erst nach dem Laden zuweist.
Item {
    id: root
    property var controller: null
    property var plugin: null
    property var theme: null
    property bool loginOpen: false
    property bool emotesOpen: false
    property string browserKind: "all"

    // Satzbau-Fundstelle am Cursor, sonst die erste: Sie steht in der Hinweiszeile über dem Eingabefeld.
    readonly property var activeGrammar: {
        var issues = sendField.grammarRanges
        if (!issues.length) return null
        var c = sendField.cursorPosition
        for (var i = 0; i < issues.length; ++i)
            if (c >= issues[i].start && c <= issues[i].start + issues[i].length)
                return issues[i]
        return issues[0]
    }
    readonly property color grammarColor: "#4a9eff"

    component AssistChip: Rectangle {
        property string label
        property color textColor: "#e8e8e8"
        property color borderColor: "#9147ff"
        signal activated()
        width: Math.min(chipText.implicitWidth + 14, parent ? parent.width : 300)
        height: chipText.implicitHeight + 6
        radius: 3
        color: "transparent"
        border.color: borderColor
        Text {
            id: chipText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 14)
            text: parent.label
            color: parent.textColor
            elide: Text.ElideRight
            font.pixelSize: root.chatFontSize
        }
        TapHandler { onTapped: parent.activated() }
    }

    function tr2(de, en) { return plugin && plugin.language === "de" ? de : en }
    function textColor()   { return theme && theme.text  ? theme.text  : "#e8e8e8" }
    function muted()       { return theme && theme.muted ? theme.muted : "#9a9a9a" }
    function accent()      { return theme && theme.accent ? theme.accent : "#9147ff" }
    // Benachrichtigungs-Boxen: Farben je Art und Event-Bots (Login → {label, color}), einstellbar per Glocke
    readonly property var boxColorMap: controller ? controller.boxColors : ({})
    readonly property var botMap: {
        var m = {}
        var list = controller ? controller.eventBots : []
        for (var i = 0; i < list.length; ++i) m[list[i].login] = list[i]
        return m
    }
    function boxColorOf(kind) {
        var c = boxColorMap[kind]
        if (c) return c
        if (kind === "mention") return accent()
        return kind === "twitch" || kind === "automod" ? "#4a9eff" : "#f1c40f"
    }
    function errorColor()  { return theme && theme.error ? theme.error : "#e74c3c" }

    // Markieren direkt in der Chatzeile: Zeichenbereich [selFrom, selTo) der Rohnachricht selRaw.
    property string selectingKey: ""
    property string selRaw: ""
    property int selFrom: -1
    property int selTo: -1
    readonly property string selectedChatText: selectingKey !== "" && selTo > selFrom ? selRaw.substring(selFrom, selTo) : ""
    function setSelection(key, raw, a, b) {
        selRaw = raw
        selFrom = Math.max(0, Math.min(a, b))
        selTo = Math.min(raw.length, Math.max(a, b))
        selectingKey = key
        selectionKeys.forceActiveFocus()     // für Strg+C und Esc
    }
    function stopSelecting() {
        if (selectingKey === "") return
        selectingKey = ""
        selFrom = -1
        selTo = -1
        selRaw = ""
        if (autoScrollOn()) pinToEnd()
    }
    function copyText(text) {
        clipboardHelper.text = text
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        clipboardHelper.text = ""
    }
    function openMessageMenu(key, text, name, url, marked, login) {
        messageMenu.login = login || ""
        messageMenu.key = key
        messageMenu.text = text
        messageMenu.name = name
        messageMenu.url = url
        messageMenu.marked = marked || ""
        messageMenu.popup()
    }

    // 950 → „950“, 1234 → „1,2K“, 45678 → „46K“, 1234567 → „1,2 Mio.“
    function compactCount(n) {
        var de = plugin && plugin.language === "de"
        function one(v) { var s = (Math.round(v * 10) / 10).toString(); return de ? s.replace(".", ",") : s }
        n = Math.round(Number(n) || 0)
        if (n < 1000) return String(n)
        if (n < 10000) return one(n / 1000) + "K"
        if (n < 1000000) return Math.round(n / 1000) + "K"
        return one(n / 1000000) + (de ? " Mio." : "M")
    }

    function syncPinned() {
        pinModel.clear()
        if (controller && controller.pinnedMessage.segments)
            pinModel.append({ segments: controller.pinnedMessage.segments })
    }
    function requestSpellCheck() {
        if (!controller) return
        if (sendField.text.length === 0) {
            sendField.spellRanges = []
            sendField.grammarRanges = []
        }
        controller.spellCheck(sendField.text, sendField.activeFocus ? sendField.cursorPosition : -1)
    }
    // Vorschau über dem Eingabefeld: nur wenn der Text ein Emote enthält
    // Emote-Namen im Eingabefeld: Lage im Text + Bild, für die Bild-Ebene über dem Feld
    function refreshEmotePreview() {
        var ranges = []
        if (controller && sendField.text.trim().length > 0) {
            var segments = controller.previewSegments(sendField.text)
            for (var i = 0; i < segments.length; ++i) {
                var s = segments[i]
                // Ohne Bild (lädt noch) bleibt der Name lesbar; onEmoteImageReady kommt hierher zurück.
                if (s.t !== "emote" || s.o === undefined || !s.src) continue
                ranges.push({ start: s.o, length: s.n, src: s.src, anim: s.anim === true,
                              width: sendField.inlineEmoteHeight * controller.imageAspect(s.src) })
            }
        }
        // Nur bei Änderung: Das Neuformatieren kann selbst wieder Änderungs-Signale auslösen.
        var fingerprint = JSON.stringify(ranges)
        if (fingerprint === sendField.emoteFingerprint) return
        sendField.emoteFingerprint = fingerprint
        if (controller) controller.setInputEmotes(sendField, ranges)
        sendField.emoteRanges = ranges
        sendField.emoteLayoutRevision++
    }
    // Klick neben das Eingabefeld gibt die Tastatur wieder frei (auch außerhalb des Panels, sonst
    // greifen die Tastenkürzel des Players nicht). Was direkt in die Eingabe schreibt, behält sie.
    readonly property var pressWindow: Window.window
    onPressWindowChanged: watchWindowPresses()
    function watchWindowPresses() {
        if (controller && pressWindow) controller.watchWindowPresses(pressWindow)
    }
    function keepsInputFocus(item, x, y) {
        if (!item || !item.visible) return false
        var p = item.mapFromItem(null, x, y)
        return p.x >= 0 && p.y >= 0 && p.x < item.width && p.y < item.height
    }
    function handleWindowPress(x, y) {
        if (!sendField.activeFocus) return
        for (const item of [sendBox, sendButton, emoteButton, emoteBrowser, predictionBar, grammarBar])
            if (keepsInputFocus(item, x, y)) return
        sendField.focus = false                    // das Fenster allein nimmt ihn dem Feld nicht weg
        pressWindow.contentItem.forceActiveFocus()
    }
    function refreshPredictions() {
        sendField.predictions = controller && sendField.text.length
            ? controller.predictWords(sendField.text, sendField.cursorPosition) : []
    }
    function acceptPrediction(word) {
        var cursor = sendField.cursorPosition
        var start = controller.predictionStart(sendField.text, cursor)
        sendField.remove(start, cursor)
        sendField.insert(start, word + " ")
        sendField.cursorPosition = start + word.length + 1
        sendField.forceActiveFocus()
    }
    function applyGrammar(replacement) {
        var issue = activeGrammar
        if (!issue || sendField.grammarText !== sendField.text) return
        sendField.remove(issue.start, issue.start + issue.length)
        sendField.insert(issue.start, replacement)
        sendField.cursorPosition = issue.start + replacement.length
        sendField.forceActiveFocus()
    }
    function grammarIssueAt(pos) {
        var issues = sendField.grammarRanges
        for (var i = 0; i < issues.length; ++i)
            if (pos >= issues[i].start && pos <= issues[i].start + issues[i].length)
                return issues[i]
        return null
    }
    function spellRangeAt(pos) {
        var ranges = sendField.spellRanges
        for (var i = 0; i < ranges.length; ++i)
            if (pos >= ranges[i].start && pos <= ranges[i].start + ranges[i].length)
                return ranges[i]
        return null
    }
    // Kontextmenü an der Maus öffnen, bei Tastaturauslösung am Text-Cursor. Das requested-Signal
    // nennt nur die Feldmitte, deshalb zählt die Position aus dem HoverHandler. Liegt dort ein Fehler?
    function openSpellMenu() {
        var mouse = sendHover.hovered ? sendHover.point.position : null
        var pos = mouse ? sendField.positionAt(mouse.x, mouse.y) : sendField.cursorPosition
        var range = spellRangeAt(pos)
        var issue = range ? null : grammarIssueAt(pos)
        if (issue) {
            spellMenu.target = { start: issue.start, length: issue.length, kind: "grammar",
                                 word: sendField.text.substr(issue.start, issue.length) }
            spellMenu.suggestions = issue.replacements
            spellMenu.loading = false
        } else {
            spellMenu.target = range
            spellMenu.suggestions = []
            spellMenu.loading = range !== null
            if (range && controller) controller.requestSpellSuggestions(range.word)
        }
        if (mouse) spellMenu.popup(mouse.x, mouse.y)
        else spellMenu.popup(sendField.cursorRectangle.x, sendField.cursorRectangle.y)
    }
    function replaceSpellTarget(replacement) {
        var r = spellMenu.target
        if (!r || sendField.text.substr(r.start, r.length) !== r.word) return
        sendField.remove(r.start, r.start + r.length)
        sendField.insert(r.start, replacement)
        sendField.cursorPosition = r.start + replacement.length
        sendField.forceActiveFocus()
    }

    function statusText() {
        if (!controller) return tr2("Bereit", "Idle")
        var k = controller.statusKey
        var s = k === "connected"  ? tr2("Verbunden", "Connected")
              : k === "connecting" ? tr2("Verbinde …", "Connecting …")
              : k === "closed"     ? tr2("Getrennt", "Disconnected")
              : k === "error"      ? tr2("Fehler", "Error")
              : tr2("Bereit", "Idle")
        if (controller.statusDetail && controller.statusDetail.length)
            s += " · " + controller.statusDetail
        return s
    }
    function statusColor() {
        if (!controller) return muted()
        if (controller.connected) return "#2ecc71"
        var k = controller.statusKey
        if (k === "connecting") return accent()
        if (k === "error") return errorColor()
        return muted()
    }
    function badgeLabel(b) {
        if (b === "moderator" || b === "mod") return "MOD"
        if (b === "vip") return "VIP"
        if (b === "subscriber") return "SUB"
        if (b === "staff") return "STAFF"
        if (b === "partner") return "PARTNER"
        if (b === "founder") return "FOUNDER"
        if (b === "turbo") return "TURBO"
        if (b === "broadcaster") return "STREAMER"
        return null
    }
    function badgeColor(b) {
        if (b === "moderator" || b === "mod" || b === "vip") return "#e00079"
        if (b === "subscriber" || b === "partner" || b === "broadcaster") return "#9147ff"
        if (b === "staff") return "#54d24f"
        if (b === "founder") return "#fd64c8"
        if (b === "turbo") return "#d93025"
        return "#8a8a8a"
    }
    readonly property int badgeSize: Math.max(18, chatFontSize + 4)

    // Chat-Schriftgröße (− / + in der Statuszeile). 0 im Controller = Standardschrift des Players.
    readonly property int minChatFontSize: 9
    readonly property int maxChatFontSize: 28
    readonly property int defaultChatFontSize: Qt.application.font.pixelSize > 0
        ? Qt.application.font.pixelSize : Math.round(Qt.application.font.pointSize * 4 / 3)
    readonly property int chatFontSize: controller && controller.chatFontSize > 0
        ? controller.chatFontSize : defaultChatFontSize
    function changeChatFontSize(step) {
        if (!controller) return
        controller.chatFontSize = Math.max(minChatFontSize, Math.min(maxChatFontSize, chatFontSize + step))
    }
    // Emotes wachsen ab etwa 13 px Schrift mit, damit sie neben großem Text nicht winzig wirken.
    readonly property int emoteHeight: Math.max(26, Math.round(chatFontSize * 2))
    FontMetrics { id: chatFontMetrics; font.pixelSize: root.chatFontSize }
    readonly property real spaceWidth: chatFontMetrics.advanceWidth(" ")

    // Ein Emote-Bild auf Zeilenhöhe, Breite nach Seitenverhältnis (auch sehr breite Emotes).
    // Die Bildart wird erst per Loader erzeugt: So kostet nur ein echtes Emote Dekoder-Arbeit.
    component EmoteImage: Item {
        id: emoteImage
        property string src: ""
        property bool anim: false
        readonly property real sourceWidth: emoteLoader.item ? emoteLoader.item.implicitWidth : 0
        readonly property real sourceHeight: emoteLoader.item ? emoteLoader.item.implicitHeight : 0
        height: root.emoteHeight
        width: sourceHeight > 0 ? Math.ceil(sourceWidth * root.emoteHeight / sourceHeight) : root.emoteHeight
        Component {
            id: staticEmote
            Image {
                source: emoteImage.src
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
        }
        Component {
            id: animatedEmote
            AnimatedImage {
                source: emoteImage.src
                fillMode: AnimatedImage.PreserveAspectFit
            }
        }
        Loader {
            id: emoteLoader
            anchors.fill: parent
            active: emoteImage.src.length > 0
            sourceComponent: emoteImage.anim ? animatedEmote : staticEmote
        }
    }

    // Eine Nachricht: Text-Wörter, Links und Emote-Bilder fließen in einer Zeile (RichText kann
    // keine Animation zeigen – deshalb Segment-Flow statt HTML). Chat und angeheftete Nachricht
    // nutzen sie beide; segments ist ein ListModel mit {t,s,url} bzw. {t:"emote",src,anim,overlays}.
    // Karte für Umfrage (kind "poll") oder Vorhersage (kind "prediction") – gleiche Form wie die Anheftung:
    // ▾/▸ klappt auf eine Zeile zusammen, ✕ blendet sie aus (📊 in der Statuszeile holt sie zurück).
    component EventCard: Rectangle {
        id: card
        property string kind: "poll"
        property var event: ({})
        property bool hidden: false
        property bool collapsed: false
        property double nowMs: 0
        signal toggleCollapsed()
        signal hideRequested()
        readonly property bool isPoll: kind === "poll"
        readonly property var rows: (isPoll ? event.choices : event.outcomes) || []
        // Nur solange noch abgestimmt werden kann
        readonly property bool canVote: isPoll ? !event.ended : event.status === "ACTIVE"
        readonly property int secondsLeft: event.endsAt > 0 ? Math.max(0, Math.ceil((event.endsAt - nowMs) / 1000)) : -1
        visible: !!event.id && !hidden
        width: parent ? parent.width : 0
        height: Math.min(cardColumn.implicitHeight + 12, chatArea.height * 0.5)
        radius: 6
        color: Qt.color(textColor()).hslLightness > 0.5 ? "#232329" : "#f4f4f6"
        border.color: isPoll ? accent() : "#387aff"
        border.width: 1
        clip: true

        function clock(sec) {
            const m = Math.floor(sec / 60), s = sec % 60
            return m + ":" + (s < 10 ? "0" : "") + s
        }
        function stateText() {
            if (isPoll) {
                if (event.ended) return tr2("Beendet", "Ended")
                return secondsLeft >= 0 ? clock(secondsLeft) : ""
            }
            if (event.status === "ACTIVE")
                return secondsLeft > 0 ? clock(secondsLeft) : tr2("Wird gesperrt …", "Locking …")
            if (event.status === "LOCKED") return tr2("Gesperrt", "Locked")
            if (event.status === "RESOLVED") return tr2("Ergebnis", "Result")
            return ""
        }
        function rowColor(row) {
            if (isPoll) return accent()
            return row.color === "PINK" ? "#f5009b" : "#387aff"
        }
        function rowDetail(row) {
            if (isPoll)
                return row.percent + " % · " + root.compactCount(row.votes)
            return row.percent + " % · " + root.compactCount(row.points) + " · 👤 " + root.compactCount(row.users)
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: wheel => wheel.accepted = true
        }
        Column {
            id: cardColumn
            x: 8
            y: 6
            width: parent.width - 16
            spacing: 4
            RowLayout {
                width: parent.width
                spacing: 6
                Label {
                    text: card.isPoll ? "📊" : "🔮"
                    font.pixelSize: 12
                }
                Label {
                    objectName: card.kind + "Heading"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    // eingeklappt: gleich die Frage, sonst die Art
                    text: card.collapsed ? (card.event.title || "")
                                         : (card.isPoll ? tr2("Umfrage", "Poll") : tr2("Vorhersage", "Prediction"))
                    font.bold: true
                    font.pixelSize: root.chatFontSize + 1
                    color: card.border.color
                }
                Label {
                    objectName: card.kind + "State"
                    text: card.stateText()
                    visible: text !== ""
                    color: muted()
                    font.pixelSize: Math.max(8, root.chatFontSize - 1)
                }
                ToolButton {
                    objectName: card.kind + "Collapse"
                    implicitWidth: 26
                    implicitHeight: 22
                    text: card.collapsed ? "▸" : "▾"
                    font.pixelSize: 12
                    Accessible.name: card.collapsed ? tr2("Aufklappen", "Expand") : tr2("Einklappen", "Collapse")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: card.collapsed ? tr2("Aufklappen", "Expand") : tr2("Einklappen", "Collapse")
                    onClicked: card.toggleCollapsed()
                }
                ToolButton {
                    objectName: card.kind + "Hide"
                    implicitWidth: 26
                    implicitHeight: 22
                    text: "✕"
                    font.pixelSize: 12
                    Accessible.name: tr2("Verbergen", "Hide")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: tr2("Verbergen – 📊 oben holt sie zurück", "Hide – 📊 at the top brings it back")
                    onClicked: card.hideRequested()
                }
            }
            Label {
                objectName: card.kind + "Title"
                width: parent.width
                visible: !card.collapsed
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: card.event.title || ""
                color: textColor()
                font.pixelSize: root.chatFontSize
            }
            Repeater {
                objectName: card.kind + "Rows"
                model: card.collapsed ? [] : card.rows
                delegate: Item {
                    required property var modelData
                    readonly property bool highlight: card.isPoll ? (modelData.leading && card.event.ended) : modelData.winner
                    width: cardColumn.width
                    height: rowText.implicitHeight + 8
                    // Balken = Anteil; Gewinner bzw. Umfrage-Sieger kräftiger
                    Rectangle {
                        anchors.fill: parent
                        radius: 4
                        color: Qt.alpha(card.rowColor(parent.modelData), 0.12)
                        border.width: parent.highlight ? 1 : 0
                        border.color: card.rowColor(parent.modelData)
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * Math.min(100, Math.max(0, parent.modelData.percent)) / 100
                        radius: 4
                        color: Qt.alpha(card.rowColor(parent.modelData), parent.highlight ? 0.55 : 0.35)
                    }
                    RowLayout {
                        id: rowText
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 6
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: (parent.parent.highlight ? "✔ " : "") + (parent.parent.modelData.title || "")
                            font.bold: parent.parent.highlight
                            color: textColor()
                            font.pixelSize: root.chatFontSize
                        }
                        Label {
                            text: card.rowDetail(parent.parent.modelData)
                            color: textColor()
                            font.pixelSize: Math.max(8, root.chatFontSize - 1)
                        }
                    }
                }
            }
            Label {
                objectName: card.kind + "Total"
                width: parent.width
                visible: !card.collapsed
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: card.isPoll
                      ? root.compactCount(card.event.total || 0) + tr2(" Stimmen", " votes")
                      : root.compactCount(card.event.totalPoints || 0) + tr2(" Kanalpunkte · ", " channel points · ")
                        + root.compactCount(card.event.totalUsers || 0) + tr2(" Teilnehmer", " participants")
                color: muted()
                font.pixelSize: Math.max(8, root.chatFontSize - 2)
            }
            // Abstimmen geht nur bei Twitch selbst (keine Schnittstelle für Zuschauer) →
            // Pop-out-Chat des Kanals im Browser, dort ist man wie gewohnt eingeloggt.
            // Immer da; nach Ablauf (gesperrt/Ergebnis) nur zum Ansehen.
            Button {
                objectName: card.kind + "Vote"
                width: parent.width
                implicitHeight: 26
                visible: !card.collapsed && !!card.event.channel
                text: card.canVote ? tr2("Auf Twitch abstimmen ↗", "Vote on Twitch ↗")
                                   : tr2("Im Twitch-Chat öffnen ↗", "Open in Twitch chat ↗")
                font.pixelSize: Math.max(9, root.chatFontSize - 1)
                Accessible.name: text
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: tr2("Öffnet den Pop-out-Chat des Kanals im Browser", "Opens the channel's pop-out chat in the browser")
                onClicked: Qt.openUrlExternally("https://www.twitch.tv/popout/" + encodeURIComponent(card.event.channel) + "/chat?popout=")
            }
        }
    }
    component SegmentFlow: Flow {
        id: segmentFlow
        property var segments
        property string messageType: "chat"
        property int selStart: -1         // markierter Bereich in Zeichen der Rohnachricht
        property int selEnd: -1
        property bool struck: false       // von Moderatoren gelöscht: durchgestrichen
        signal linkMenu(string url)       // Rechtsklick auf einen Link
        spacing: 0
        // Zeichenposition in der Rohnachricht unter (px, py): nächster Baustein, Zeilen zuerst.
        function indexAt(px, py) {
            var best = null, bestDistance = 1e12
            for (var i = 0; i < children.length; ++i) {
                var c = children[i]
                if (c.rawStart === undefined || c.rawStart < 0) continue
                var dy = py < c.y ? c.y - py : (py > c.y + c.height ? py - c.y - c.height : 0)
                var dx = px < c.x ? c.x - px : (px > c.x + c.width ? px - c.x - c.width : 0)
                var distance = dy * 100000 + dx
                if (distance < bestDistance) { bestDistance = distance; best = c }
            }
            if (!best) return 0
            if (best.isEmote) return px < best.x + best.width / 2 ? best.rawFrom : best.rawTo
            var local = px - best.x - best.textX
            var text = best.textValue
            var advance = 0
            for (var k = 0; k < text.length; ++k) {
                var w = chatFontMetrics.advanceWidth(text.charAt(k))
                if (advance + w / 2 > local) return best.rawFrom + k
                advance += w
            }
            return best.rawTo
        }
        Repeater {
            model: segmentFlow.segments
            delegate: Item {
                id: seg
                readonly property bool isEmote: model.t === "emote"
                // Lage in der Rohnachricht; ein Trenn-Leerzeichen vorn gehört zum Baustein.
                readonly property int rawStart: model.o === undefined || model.o === null ? -1 : model.o
                readonly property string textValue: isEmote ? "" : segText.text
                readonly property real textX: segText.x
                readonly property int lead: !isEmote && textValue.charAt(0) === " " ? 1 : 0
                readonly property int rawFrom: rawStart - lead
                readonly property int rawTo: isEmote ? rawStart + (model.n || 0) : rawStart - lead + textValue.length
                Rectangle {
                    objectName: "selectionHighlight"
                    readonly property int a: Math.max(segmentFlow.selStart, seg.rawFrom)
                    readonly property int b: Math.min(segmentFlow.selEnd, seg.rawTo)
                    visible: segmentFlow.selStart >= 0 && seg.rawStart >= 0 && b > a
                    x: seg.isEmote ? 0 : seg.textX + chatFontMetrics.advanceWidth(seg.textValue.substring(0, a - seg.rawFrom))
                    width: seg.isEmote ? seg.width : chatFontMetrics.advanceWidth(seg.textValue.substring(a - seg.rawFrom, b - seg.rawFrom))
                    height: seg.height
                    color: accent()
                    opacity: 0.45
                }
                readonly property string linkUrl: !isEmote && model.url ? model.url : ""
                readonly property var overlayList: isEmote ? model.overlays : null
                readonly property int gap: isEmote && index > 0 ? 3 : 0
                readonly property bool tooWide: segText.implicitWidth > segmentFlow.width
                height: isEmote ? root.emoteHeight : Math.max(root.emoteHeight, segText.height)
                width: isEmote ? gap + emoteStack.width : Math.min(segText.implicitWidth, segmentFlow.width)
                Text {
                    id: segText
                    visible: !seg.isEmote
                    text: seg.isEmote ? "" : model.s
                    // Das Leerzeichen vor einem Wort landet am Zeilenanfang links außerhalb;
                    // die Breite bleibt gleich, damit der Umbruch dadurch nicht springt.
                    x: seg.x === 0 && text.charAt(0) === " " ? -root.spaceWidth : 0
                    width: seg.tooWide ? segmentFlow.width : implicitWidth
                    height: seg.tooWide ? contentHeight : root.emoteHeight
                    verticalAlignment: Text.AlignVCenter
                    // Nur ein Wort breiter als die Zeile (z. B. ein langer Link) bricht im Wort um.
                    wrapMode: seg.tooWide ? Text.WrapAnywhere : Text.NoWrap
                    // Fremder Text: „<b>“ und Ähnliches darf nie als Formatierung wirken.
                    textFormat: Text.PlainText
                    color: seg.linkUrl ? "#6fb1ff" : segmentFlow.messageType === "system" ? muted() : textColor()
                    font.italic: segmentFlow.messageType === "action"
                    font.underline: seg.linkUrl !== ""
                    font.strikeout: segmentFlow.struck
                    font.pixelSize: root.chatFontSize
                }
                // Gelöschte Nachricht: Emote-Bilder bekommen dieselbe Linie wie der Text
                Rectangle {
                    objectName: "emoteStrike"
                    visible: segmentFlow.struck && seg.isEmote
                    x: seg.gap
                    width: seg.width - seg.gap
                    height: Math.max(1, Math.round(root.chatFontSize / 12))
                    anchors.verticalCenter: parent.verticalCenter
                    color: textColor()
                    z: 2
                }
                // Nur für Links geladen: Eine Nachricht besteht sonst aus vielen Wörtern.
                Loader {
                    anchors.fill: segText
                    active: seg.linkUrl !== ""
                    sourceComponent: MouseArea {
                        objectName: "chatLink"
                        property string url: seg.linkUrl
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton) segmentFlow.linkMenu(url)
                            else Qt.openUrlExternally(url)
                        }
                        ToolTip.visible: containsMouse
                        ToolTip.delay: 300
                        ToolTip.text: url
                    }
                }
                // Zero-Width-Emotes liegen mittig über dem Emote; die Breite richtet
                // sich nach dem breitesten Bild im Stapel.
                Item {
                    id: emoteStack
                    visible: seg.isEmote
                    x: seg.gap
                    height: root.emoteHeight
                    property real overlayWidth: 0
                    width: Math.max(baseEmote.width, overlayWidth)
                    EmoteImage {
                        id: baseEmote
                        anchors.centerIn: parent
                        src: seg.isEmote ? model.src : ""
                        anim: seg.isEmote && model.anim === true
                    }
                    Repeater {
                        model: seg.overlayList
                        delegate: EmoteImage {
                            anchors.centerIn: parent
                            src: model.src
                            anim: model.anim === true
                            onWidthChanged: emoteStack.overlayWidth = Math.max(emoteStack.overlayWidth, width)
                        }
                    }
                }
            }
        }
    }

    TextEdit { id: clipboardHelper; visible: false; textFormat: TextEdit.PlainText }
    Inbox {
        id: inbox
        panel: root
        controller: root.controller
    }
    BoxSettingsPopup {
        id: boxSettingsPopup
        panel: root
        controller: root.controller
    }
    ChattersPopup {
        id: chattersPopup
        panel: root
        controller: root.controller
        onWriteTo: (login, name) => inbox.openDirect(login, name)
        onCreateGroup: members => inbox.newGroup(members)
        onUserMenu: (login, name, badges) => root.openUserMenu({ login: login, name: name, userId: "", msgId: "", badges: badges })
    }
    Item {
        id: selectionKeys
        objectName: "selectionKeys"
        Keys.onPressed: event => {
            if (event.matches(StandardKey.Copy) && root.selectedChatText !== "") {
                root.copyText(root.selectedChatText)
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                root.stopSelecting()
                event.accepted = true
            }
        }
    }

    Menu {
        id: messageMenu
        objectName: "messageMenu"
        property string key
        property string text
        property string name
        property string url
        property string marked        // mit der Maus markierter Teil
        property string login         // Absender (Login); „Privat schreiben“ steht im Nutzer-Menü am Namen
        MenuItem {
            objectName: "copySelection"
            visible: messageMenu.marked !== ""
            height: visible ? implicitHeight : 0
            text: tr2("Markierung kopieren", "Copy selection")
            onTriggered: root.copyText(messageMenu.marked)
        }
        MenuItem {
            objectName: "copyMessage"
            text: tr2("Nachricht kopieren", "Copy message")
            onTriggered: root.copyText(messageMenu.text)
        }
        MenuItem {
            objectName: "copyLink"
            visible: messageMenu.url !== ""
            height: visible ? implicitHeight : 0
            text: tr2("Link kopieren", "Copy link")
            onTriggered: root.copyText(messageMenu.url)
        }
        MenuItem {
            objectName: "copyName"
            visible: messageMenu.name !== ""
            height: visible ? implicitHeight : 0
            text: tr2("Name kopieren", "Copy name")
            onTriggered: root.copyText(messageMenu.name)
        }
        MenuItem {
            objectName: "selectMessage"
            visible: messageMenu.key !== ""
            height: visible ? implicitHeight : 0
            text: tr2("Alles markieren", "Select all")
            onTriggered: root.setSelection(messageMenu.key, messageMenu.text, 0, messageMenu.text.length)
        }
    }

    // ——— Nutzer-Menü: Klick auf einen Namen im Chat. Einträge je nach eigener Rolle im Kanal ———
    function openUserMenu(target) {
        if (!target || !target.login) return
        userMenu.target = target
        userMenu.created = 0
        if (controller) controller.requestUserInfo(target.userId || "", target.login)   // auch ohne Login (öffentlich)
        userMenu.popup()
    }
    function automodReasonText(reason) {
        if (!reason) return ""
        var map = {
            "blocked_term": tr2("gesperrter Begriff", "blocked term"),
            "aggression": tr2("Aggression", "aggression"), "bullying": tr2("Mobbing", "bullying"),
            "disability": tr2("Behinderung", "disability"), "sexuality": tr2("Sexualität", "sexuality"),
            "misogyny": tr2("Frauenfeindlichkeit", "misogyny"), "race": tr2("Herkunft", "race"),
            "swearing": tr2("Schimpfwörter", "swearing"), "profanity": tr2("Schimpfwörter", "profanity"),
            "sex_based_terms": tr2("sexuelle Begriffe", "sex-based terms"), "religion": tr2("Religion", "religion")
        }
        return map[reason.toLowerCase()] || reason
    }
    function hasBadge(target, badge) {
        var badges = target && target.badges ? target.badges : []
        for (var i = 0; i < badges.length; ++i) if (badges[i] === badge) return true
        return false
    }
    function roleLabel(target) {
        if (hasBadge(target, "broadcaster")) return tr2("Streamer", "Streamer")
        if (hasBadge(target, "moderator")) return tr2("Moderator", "Moderator")
        if (hasBadge(target, "vip")) return "VIP"
        if (hasBadge(target, "subscriber")) return tr2("Abonnent", "Subscriber")
        return ""
    }
    function runUserAction(action, extra) {
        if (!controller) return
        var t = {}
        for (var k in userMenu.target) t[k] = userMenu.target[k]
        if (extra) for (var e in extra) t[e] = extra[e]
        controller.userAction(action, t)
    }
    // Folgenreiche Aktionen erst nach Bestätigung, auf Wunsch mit Grund
    function confirmUserAction(action, title, message, reasonMode, confirmText) {
        actionDialog.action = action
        actionDialog.target = userMenu.target
        actionDialog.title = title
        actionDialog.message = message
        actionDialog.reasonMode = reasonMode       // "" | "optional" | "required"
        actionDialog.confirmText = confirmText
        actionReason.text = ""
        actionDialog.open()
    }

    Connections {
        target: controller
        ignoreUnknownSignals: true
        function onUserInfo(login, info) {
            if (userMenu.target && userMenu.target.login === login) userMenu.created = info.createdAt
        }
    }

    Menu {
        id: userMenu
        objectName: "userMenu"
        property var target: ({})
        property real created: 0
        readonly property string my: controller ? controller.myRole : "anonymous"
        readonly property bool loggedIn: my !== "anonymous"
        readonly property bool isSelf: controller !== null && !!target.login && target.login === (controller.user || "").toLowerCase()
        readonly property bool targetBroadcaster: root.hasBadge(target, "broadcaster")
        readonly property bool targetMod: root.hasBadge(target, "moderator")
        readonly property bool targetVip: root.hasBadge(target, "vip")
        // Moderatoren und Streamer gegenüber normalen Nutzern; Mods und den Streamer kann niemand stummschalten
        readonly property bool canModerate: (my === "moderator" || my === "broadcaster") && !isSelf && !targetBroadcaster && !targetMod
        readonly property bool isBroadcaster: my === "broadcaster" && !isSelf && !targetBroadcaster

        component Entry: MenuItem {
            property bool shown: true
            visible: shown
            height: shown ? implicitHeight : 0
        }
        component Line: MenuSeparator {
            property bool shown: true
            visible: shown
            height: shown ? implicitHeight : 0
        }

        Entry {
            objectName: "userHeader"
            enabled: false
            font.bold: true
            text: (userMenu.target.name || userMenu.target.login || "")
                  + (root.roleLabel(userMenu.target) !== "" ? "  ·  " + root.roleLabel(userMenu.target) : "")
        }
        Entry {
            objectName: "userCreated"
            enabled: false
            shown: userMenu.created > 0
            font.pixelSize: 10
            text: tr2("Konto seit ", "Account since ") + Qt.formatDate(new Date(userMenu.created), "dd.MM.yyyy")
        }
        Line {}
        Entry {
            objectName: "userWhisper"
            shown: userMenu.loggedIn && !userMenu.isSelf
            text: tr2("Privat schreiben", "Write privately")
            onTriggered: inbox.openDirect(userMenu.target.login, userMenu.target.name)
        }
        Entry {
            objectName: "userMention"
            text: tr2("@Erwähnen", "@Mention")
            onTriggered: {
                var mention = "@" + (userMenu.target.name || userMenu.target.login) + " "
                sendField.text = sendField.text.length && !/\s$/.test(sendField.text) ? sendField.text + " " + mention : sendField.text + mention
                sendField.cursorPosition = sendField.text.length
                sendField.forceActiveFocus()
            }
        }
        Entry {
            objectName: "userCopyName"
            text: tr2("Name kopieren", "Copy name")
            onTriggered: root.copyText(userMenu.target.name || userMenu.target.login)
        }
        Entry {
            objectName: "userOpenChannel"
            text: tr2("Kanal auf Twitch öffnen", "Open channel on Twitch")
            onTriggered: Qt.openUrlExternally("https://www.twitch.tv/" + userMenu.target.login)
        }
        Entry {
            objectName: "userBlock"
            readonly property bool blocked: controller !== null && controller.blockedRevision >= 0 && controller.isBlocked(userMenu.target.login || "")
            shown: userMenu.loggedIn && !userMenu.isSelf && !userMenu.targetBroadcaster
            text: blocked ? tr2("Blockierung aufheben", "Unblock") : tr2("Blockieren …", "Block …")
            onTriggered: blocked ? root.runUserAction("unblock")
                                 : root.confirmUserAction("block", tr2("Blockieren", "Block"),
                                       tr2("%1 blockieren? Nachrichten werden ausgeblendet, private Nachrichten von Twitch gesperrt.",
                                           "Block %1? Their messages are hidden and Twitch blocks their whispers.").arg(userMenu.target.name),
                                       "", tr2("Blockieren", "Block"))
        }

        // Moderation
        Line { shown: userMenu.canModerate }
        Entry {
            objectName: "userDeleteMessage"
            shown: userMenu.canModerate && !!userMenu.target.msgId
            text: tr2("Diese Nachricht löschen", "Delete this message")
            onTriggered: root.runUserAction("delete")
        }
        Entry {
            objectName: "userPurge"
            shown: userMenu.canModerate
            text: tr2("Nachrichten entfernen (1 s Timeout)", "Purge messages (1 s timeout)")
            onTriggered: root.runUserAction("purge")
        }
        Entry { objectName: "userTimeout60";    shown: userMenu.canModerate; text: tr2("Timeout 1 Minute", "Timeout 1 minute");   onTriggered: root.runUserAction("timeout", { duration: 60 }) }
        Entry { objectName: "userTimeout600";   shown: userMenu.canModerate; text: tr2("Timeout 10 Minuten", "Timeout 10 minutes"); onTriggered: root.runUserAction("timeout", { duration: 600 }) }
        Entry { objectName: "userTimeout3600";  shown: userMenu.canModerate; text: tr2("Timeout 1 Stunde", "Timeout 1 hour");     onTriggered: root.runUserAction("timeout", { duration: 3600 }) }
        Entry { objectName: "userTimeout86400"; shown: userMenu.canModerate; text: tr2("Timeout 24 Stunden", "Timeout 24 hours");  onTriggered: root.runUserAction("timeout", { duration: 86400 }) }
        Entry {
            objectName: "userWarn"
            shown: userMenu.canModerate
            text: tr2("Verwarnen …", "Warn …")
            onTriggered: root.confirmUserAction("warn", tr2("Verwarnen", "Warn"),
                                                tr2("%1 verwarnen. Twitch zeigt den Grund an, bis er bestätigt wird.",
                                                    "Warn %1. Twitch shows the reason until they acknowledge it.").arg(userMenu.target.name),
                                                "required", tr2("Verwarnen", "Warn"))
        }
        Entry {
            objectName: "userBan"
            shown: userMenu.canModerate
            text: tr2("Bannen …", "Ban …")
            onTriggered: root.confirmUserAction("ban", tr2("Bannen", "Ban"),
                                                tr2("%1 dauerhaft aus dem Chat bannen?", "Permanently ban %1 from the chat?").arg(userMenu.target.name),
                                                "optional", tr2("Bannen", "Ban"))
        }
        Entry {
            objectName: "userUnban"
            shown: userMenu.canModerate
            text: tr2("Bann/Timeout aufheben", "Remove ban/timeout")
            onTriggered: root.runUserAction("unban")
        }
        Entry {
            objectName: "userShoutout"
            shown: (userMenu.my === "moderator" || userMenu.my === "broadcaster") && !userMenu.isSelf && !userMenu.targetBroadcaster
                   && controller !== null && controller.streamLive
            text: tr2("Shoutout", "Shoutout")
            onTriggered: root.runUserAction("shoutout")
        }

        // Nur der Streamer
        Line { shown: userMenu.isBroadcaster }
        Entry {
            objectName: "userModToggle"
            shown: userMenu.isBroadcaster
            text: userMenu.targetMod ? tr2("Moderator entfernen …", "Remove moderator …") : tr2("Zum Moderator machen …", "Make moderator …")
            onTriggered: userMenu.targetMod
                ? root.confirmUserAction("unmod", tr2("Moderator entfernen", "Remove moderator"),
                                         tr2("%1 die Moderatorrechte entziehen?", "Remove %1's moderator rights?").arg(userMenu.target.name), "", tr2("Entfernen", "Remove"))
                : root.confirmUserAction("mod", tr2("Zum Moderator machen", "Make moderator"),
                                         tr2("%1 zum Moderator machen?", "Make %1 a moderator?").arg(userMenu.target.name), "", tr2("Moderator machen", "Make moderator"))
        }
        Entry {
            objectName: "userVipToggle"
            shown: userMenu.isBroadcaster && !userMenu.targetMod
            text: userMenu.targetVip ? tr2("VIP entfernen …", "Remove VIP …") : tr2("VIP geben …", "Give VIP …")
            onTriggered: userMenu.targetVip
                ? root.confirmUserAction("unvip", tr2("VIP entfernen", "Remove VIP"),
                                         tr2("%1 den VIP-Status entziehen?", "Remove %1's VIP status?").arg(userMenu.target.name), "", tr2("Entfernen", "Remove"))
                : root.confirmUserAction("vip", tr2("VIP geben", "Give VIP"),
                                         tr2("%1 zum VIP machen?", "Make %1 a VIP?").arg(userMenu.target.name), "", tr2("VIP geben", "Give VIP"))
        }
        Entry {
            objectName: "userRaid"
            shown: userMenu.isBroadcaster
            text: tr2("Diesen Kanal raiden …", "Raid this channel …")
            onTriggered: root.confirmUserAction("raid", tr2("Raid", "Raid"),
                                                tr2("Deine Zuschauer zu %1 schicken? Twitch zählt danach 90 Sekunden herunter.",
                                                    "Send your viewers to %1? Twitch then counts down 90 seconds.").arg(userMenu.target.name),
                                                "", tr2("Raid starten", "Start raid"))
        }
    }

    Popup {
        id: actionDialog
        objectName: "actionDialog"
        property string action
        property var target: ({})
        property string title
        property string message
        property string reasonMode        // "" | "optional" | "required"
        property string confirmText
        parent: root
        anchors.centerIn: parent
        width: Math.min(root.width - 20, 300)
        modal: true
        dim: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 12
        background: Rectangle {
            color: Qt.color(textColor()).hslLightness > 0.5 ? "#232329" : "#f4f4f6"
            radius: 6
            border.color: accent()
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: actionDialog.title
                font.bold: true
                font.pixelSize: root.chatFontSize + 2
                color: textColor()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                text: actionDialog.message
                font.pixelSize: root.chatFontSize
                color: textColor()
            }
            TextField {
                id: actionReason
                objectName: "actionReason"
                Layout.fillWidth: true
                visible: actionDialog.reasonMode !== ""
                maximumLength: 500
                font.pixelSize: root.chatFontSize
                placeholderText: actionDialog.reasonMode === "required" ? tr2("Grund (nötig)", "Reason (required)") : tr2("Grund (freiwillig)", "Reason (optional)")
                onAccepted: actionConfirm.clicked()
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "actionCancel"
                    text: tr2("Abbrechen", "Cancel")
                    onClicked: actionDialog.close()
                }
                Button {
                    id: actionConfirm
                    objectName: "actionConfirm"
                    text: actionDialog.confirmText
                    enabled: actionDialog.reasonMode !== "required" || actionReason.text.trim().length > 0
                    onClicked: {
                        var t = {}
                        for (var k in actionDialog.target) t[k] = actionDialog.target[k]
                        if (actionDialog.reasonMode !== "") t.reason = actionReason.text.trim()
                        actionDialog.close()
                        if (controller) controller.userAction(actionDialog.action, t)
                    }
                }
            }
        }
    }

    ListModel { id: chatModel }
    // Eine Zeile {segments:[…]} wie im Chat: Als verschachtelte Liste behält jedes Segment seine
    // eigenen Felder. Direkt angehängt legte das erste Wort die Felder fest, und Links verlören url.
    ListModel { id: pinModel }
    // Restzeit von Umfrage/Vorhersage: tickt nur, solange eine Karte herunterzählt
    property double eventNowMs: Date.now()
    Timer {
        interval: 1000
        repeat: true
        running: (pollCard.visible && pollCard.event.endsAt > 0) || (predictionCard.visible && predictionCard.event.endsAt > 0)
        triggeredOnStart: true
        onTriggered: root.eventNowMs = Date.now()
    }
    ListModel { id: browserModel }

    function autoScrollOn() { return controller ? controller.autoScroll : true }
    function pinToEnd() { Qt.callLater(function () { chatView.positionViewAtEnd() }) }

    // Der Browser ist eine ListView aus Kachelzeilen: Nur sie kann Abschnitte mit Überschrift
    // zeigen (GridView nicht) und lädt trotzdem nur die sichtbaren Bilder.
    readonly property int browserColumns: Math.max(1, Math.floor(emoteGrid.width / 70))
    property int browserCount: 0
    onBrowserColumnsChanged: reloadBrowser()

    property string browserSignature: ""

    // keepPosition: Der Nutzer soll an seiner Stelle bleiben, wenn sich nur die Daten ändern
    // (Favorit gesetzt, neues Emote im Chat gesehen). Bleibt die Kachelfolge gleich, werden nur die
    // geänderten Werte gesetzt: kein Neuaufbau, keine neu startenden Animationen.
    function reloadBrowser(keepPosition) {
        if (!controller || !root.emotesOpen) {
            browserModel.clear()
            browserCount = 0
            browserSignature = ""
            return
        }
        var list = controller.emoteList(root.browserKind)
        var needle = searchField.text.trim().toLowerCase()
        var rows = []
        var row = null
        var count = 0
        var signature = browserColumns + "|"
        for (var i = 0; i < list.length; ++i) {
            var e = list[i]
            if (needle.length && e.name.toLowerCase().indexOf(needle) < 0) continue
            if (!row || row.sec !== e.section || row.tiles.length >= browserColumns) {
                row = { sec: e.section, tiles: [] }
                rows.push(row)
                signature += "|" + e.section
            }
            row.tiles.push(e)
            signature += "," + e.id
            ++count
        }

        if (keepPosition === true && signature === browserSignature) {
            for (var r = 0; r < rows.length; ++r) {
                var tileModel = browserModel.get(r).tiles
                for (var t = 0; t < rows[r].tiles.length; ++t) {
                    var fresh = rows[r].tiles[t]
                    var shown = tileModel.get(t)
                    if (shown.favorite !== fresh.favorite) tileModel.setProperty(t, "favorite", fresh.favorite)
                    if (shown.locked !== fresh.locked) tileModel.setProperty(t, "locked", fresh.locked)
                    if (shown.animated !== fresh.animated) tileModel.setProperty(t, "animated", fresh.animated)
                    if (shown.url !== fresh.url) tileModel.setProperty(t, "url", fresh.url)
                }
            }
            return
        }

        // Neuaufbau: oberste sichtbare Zeile merken. Ein gemerkter contentY-Wert reicht nicht,
        // weil die ListView ihren Ursprung nach dem Leeren neu setzt.
        var topRow = -1
        var topOffset = 0
        if (keepPosition === true) {
            for (var probe = 1; probe < 60 && topRow < 0; probe += 29)   // kann auf einer Überschrift liegen
                topRow = emoteGrid.indexAt(1, emoteGrid.contentY + probe)
            var topItem = topRow >= 0 ? emoteGrid.itemAtIndex(topRow) : null
            if (topItem) topOffset = emoteGrid.contentY - topItem.y
        }
        browserModel.clear()
        browserModel.append(rows)
        browserCount = count
        browserSignature = signature
        if (topRow >= 0 && rows.length > 0) {
            emoteGrid.forceLayout()
            emoteGrid.positionViewAtIndex(Math.min(topRow, rows.length - 1), ListView.Beginning)
            if (topRow < rows.length) emoteGrid.contentY += topOffset
        }
    }

    function sectionTitle(sec) {
        var channel = tr2("Kanal", "Channel")
        var titles = {
            "subs": tr2("Abos", "Subscriptions"),
            "twitch": "Twitch",
            "twitch-channel": "Twitch – " + channel,
            "twitch-global": "Twitch – Global",
            "twitch-hype": "TwitchHypeTrain",
            "twitch-seen": tr2("Twitch – Im Chat gesehen", "Twitch – Seen in chat"),
            "7tv": "7TV",
            "7tv-channel": "7TV – " + channel,
            "7tv-global": "7TV – Global",
            "bttv": "BetterTTV",
            "bttv-channel": "BetterTTV – " + channel,
            "bttv-global": "BetterTTV – Global",
            "ffz": "FrankerFaceZ",
            "ffz-channel": "FrankerFaceZ – " + channel,
            "ffz-global": "FrankerFaceZ – Global"
        }
        return titles[sec] || sec
    }

    // Der Player weist den Controller erst nach dem Laden zu. Der Verlauf muss in jedem Fall
    // übernommen werden: messageUpdated(i) adressiert Zeilen über ihren Verlaufsindex.
    function loadHistory() {
        chatModel.clear()
        if (controller) {
            var h = controller.history()
            for (var i = 0; i < h.length; ++i)
                chatModel.append(h[i])
        }
        Qt.callLater(function () { chatView.positionViewAtEnd() })
    }

    Component.onCompleted: { loadHistory(); reloadBrowser(); watchWindowPresses() }
    onControllerChanged: { loadHistory(); reloadBrowser(); watchWindowPresses() }
    onEmotesOpenChanged: { reloadBrowser() }
    onBrowserKindChanged: { reloadBrowser() }

    Connections {
        target: controller
        ignoreUnknownSignals: true
        function onNewMessage(m) {
            var followed = (autoScrollOn() || chatView.atYEnd) && root.selectingKey === ""
            chatModel.append(m)
            if (chatModel.count > 400)
                chatModel.remove(0, chatModel.count - 400)
            if (followed) pinToEnd()
        }
        function onChannelChanged() { channelField.text = controller ? controller.channel : ""; reloadBrowser() }
        function onEmotesChanged() {
            Qt.callLater(reloadBrowser, true)   // fasst Meldungen zusammen
            Qt.callLater(root.requestSpellCheck)   // neue Emote-Namen sind keine Tippfehler
        }
        function onSpellSettingsChanged() { root.requestSpellCheck() }
        function onPinnedChanged() { root.syncPinned() }
        function onEmoteImageReady() { Qt.callLater(root.refreshEmotePreview) }
        function onWindowPressed(window, x, y) { if (window === root.pressWindow) root.handleWindowPress(x, y) }
        function onHistoryCleared() {
            root.stopSelecting()
            chatModel.clear()
        }
        function onPredictionsChanged() { root.refreshPredictions() }
        function onGrammarResult(text, issues) {
            if (text === sendField.text) {
                sendField.grammarText = text
                sendField.grammarRanges = issues
            }
        }
        function onSpellResult(text, ranges) {
            if (text === sendField.text) sendField.spellRanges = ranges
        }
        function onSpellSuggestions(word, suggestions) {
            if (spellMenu.target && spellMenu.target.kind !== "grammar" && spellMenu.target.word === word) {
                spellMenu.suggestions = suggestions
                spellMenu.loading = false
            }
        }
        function onMessageUpdated(i) {
            // Emoticon-Bild/katalog wurde nachgeladen → Zeile neu rendern
            if (controller && i >= 0 && i < chatModel.count) {
                var followed = autoScrollOn() || chatView.atYEnd
                chatModel.set(i, controller.historyItem(i))
                if (followed) pinToEnd()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // ——— Kopfzeile: Kanal + Verbinden ———
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: tr2("Kanal:", "Channel:")
                color: muted()
                font.pixelSize: 12
            }
            TextField {
                id: channelField
                Layout.fillWidth: true
                placeholderText: "xnowski"
                text: controller ? controller.channel : ""
                onEditingFinished: { if (controller) controller.channel = text }
            }
            Button {
                flat: true
                text: controller && (controller.connected || controller.statusKey === "connecting")
                      ? tr2("Trennen", "Disconnect") : tr2("Verbinden", "Connect")
                enabled: controller !== undefined && controller !== null
                onClicked: {
                    if (!controller) return
                    if (controller.connected || controller.statusKey === "connecting")
                        controller.closeChat()
                    else
                        controller.openChat()
                }
            }
        }

        // ——— Statuszeile: Verbindung ———
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle { width: 10; height: 10; radius: 5; color: statusColor() }
            Label {
                objectName: "statusLabel"
                Layout.fillWidth: true
                text: statusText()
                color: statusColor()
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            Button {
                objectName: "pinnedShow"
                flat: true
                visible: controller !== null && !!controller.pinnedMessage.id && controller.pinnedHidden
                implicitWidth: 30
                implicitHeight: 22
                text: "📌"
                font.pixelSize: 12
                Accessible.name: tr2("Angeheftete Nachricht zeigen", "Show pinned message")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: tr2("Angeheftete Nachricht zeigen", "Show pinned message")
                onClicked: if (controller) controller.showPinned()
            }
            Button {
                objectName: "eventsShow"
                flat: true
                visible: controller !== null && ((!!controller.channelPoll.id && controller.pollHidden)
                                                 || (!!controller.channelPrediction.id && controller.predictionHidden))
                implicitWidth: 30
                implicitHeight: 22
                text: "📊"
                font.pixelSize: 12
                Accessible.name: tr2("Umfrage/Vorhersage zeigen", "Show poll/prediction")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: tr2("Umfrage/Vorhersage zeigen", "Show poll/prediction")
                onClicked: if (controller) controller.showChannelEvents()
            }
            // Leute im Chat
            Button {
                objectName: "chattersButton"
                flat: true
                implicitWidth: Math.max(30, implicitContentWidth + 10)
                implicitHeight: 22
                text: controller && controller.streamLive ? "👥 " + root.compactCount(controller.viewerCount) : "👥"
                font.pixelSize: 12
                enabled: controller !== null
                Accessible.name: tr2("Zuschauer", "Viewers")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: !controller || controller.viewerCount < 0 ? tr2("Zuschauer", "Viewers")
                              : controller.streamLive ? tr2("%1 Zuschauer", "%1 viewers").arg(Number(controller.viewerCount).toLocaleString(Qt.locale(plugin && plugin.language === "de" ? "de_DE" : "en_US"), "f", 0))
                              : tr2("Stream offline", "Stream offline")
                onClicked: chattersPopup.open()
            }
            // Live-Kanäle: eigenes Fenster über dem Bild (modal.open, UVE Player ≥ 1.1.3)
            Button {
                objectName: "liveButton"
                flat: true
                visible: !!plugin && typeof plugin.hasCommand === "function" && plugin.hasCommand("modal.open")
                implicitWidth: 30
                implicitHeight: 22
                text: "📺"
                font.pixelSize: 13
                Accessible.name: tr2("Live-Kanäle", "Live channels")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: tr2("Live-Kanäle: Vorschau, Kategorie, Zuschauer", "Live channels: preview, category, viewers")
                onClicked: {
                    // zuletzt gezogene Fenstergröße wieder verwenden
                    var size = controller ? controller.liveWindowSize() : ({})
                    var args = { panel: "live" }
                    if (size.width > 0) { args.width = size.width; args.height = size.height }
                    plugin.call("modal.open", args)
                }
            }
            // Postfach: leuchtet bei ungelesenen Nachrichten, Zahl im roten Badge
            Item {
                id: inboxIcon
                readonly property int unread: controller && controller.whispers ? controller.whispers.unreadTotal : 0
                implicitWidth: 30
                implicitHeight: 22
                Rectangle {
                    id: inboxGlow
                    anchors.fill: parent
                    radius: 4
                    color: accent()
                    opacity: 0
                    SequentialAnimation on opacity {
                        running: inboxIcon.unread > 0
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) inboxGlow.opacity = 0
                        NumberAnimation { to: 0.55; duration: 700; easing.type: Easing.InOutQuad }
                        NumberAnimation { to: 0.12; duration: 700; easing.type: Easing.InOutQuad }
                    }
                }
                Button {
                    objectName: "inboxButton"
                    anchors.fill: parent
                    flat: true
                    text: "✉"
                    font.pixelSize: 14
                    enabled: controller !== null
                    Accessible.name: tr2("Postfach", "Inbox")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: inboxIcon.unread > 0 ? tr2("Postfach – %1 ungelesen", "Inbox – %1 unread").arg(inboxIcon.unread)
                                                       : tr2("Postfach (private Nachrichten)", "Inbox (private messages)")
                    onClicked: inbox.openList()
                }
                Rectangle {
                    objectName: "inboxBadge"
                    visible: inboxIcon.unread > 0
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: -3
                    anchors.topMargin: -4
                    implicitWidth: Math.max(15, badgeText.implicitWidth + 6)
                    implicitHeight: 15
                    radius: 7.5
                    color: errorColor()
                    Label {
                        id: badgeText
                        anchors.centerIn: parent
                        text: inboxIcon.unread > 99 ? "99+" : inboxIcon.unread
                        color: "white"
                        font.pixelSize: 9
                        font.bold: true
                    }
                }
            }
            // Benachrichtigungs-Boxen: Farben und Event-Bots einstellen
            Button {
                objectName: "boxSettingsButton"
                flat: true
                implicitWidth: 30
                implicitHeight: 22
                text: "🔔"
                font.pixelSize: 12
                enabled: controller !== null
                Accessible.name: tr2("Benachrichtigungs-Boxen", "Notification boxes")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: tr2("Benachrichtigungs-Boxen: Farben und Bots", "Notification boxes: colours and bots")
                onClicked: boxSettingsPopup.open()
            }
            // Chat-Schrift kleiner / größer
            Button {
                objectName: "fontSmaller"
                flat: true
                enabled: controller !== null && root.chatFontSize > root.minChatFontSize
                implicitWidth: 26
                implicitHeight: 22
                text: "−"
                font.pixelSize: 15
                font.bold: true
                Accessible.name: tr2("Chat-Schrift kleiner", "Smaller chat text")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: tr2("Chat-Schrift kleiner (%1 px)", "Smaller chat text (%1 px)").arg(root.chatFontSize)
                onClicked: root.changeChatFontSize(-1)
            }
            Button {
                objectName: "fontLarger"
                flat: true
                enabled: controller !== null && root.chatFontSize < root.maxChatFontSize
                implicitWidth: 26
                implicitHeight: 22
                text: "+"
                font.pixelSize: 15
                font.bold: true
                Accessible.name: tr2("Chat-Schrift größer", "Larger chat text")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: tr2("Chat-Schrift größer (%1 px)", "Larger chat text (%1 px)").arg(root.chatFontSize)
                onClicked: root.changeChatFontSize(1)
            }
        }

        // ——— Auto-Kanal + Auto-Scroll + Knöpfe ———
        // Flow statt RowLayout: Die Mindestbreite einer Layout-Zeile gilt für das ganze
        // ColumnLayout. Wäre sie breiter als die Seitenleiste (330 px), liefe das gesamte Panel
        // rechts hinaus.
        Flow {
            Layout.fillWidth: true
            spacing: 8
            CheckBox {
                id: autoBox
                text: tr2("Auto-Kanal", "Auto channel")
                font.pixelSize: 12
                checked: controller ? controller.autoChannel : true
                enabled: controller !== undefined && controller !== null
                onToggled: { if (controller) controller.autoChannel = checked }
            }
            CheckBox {
                id: scrollBox
                text: tr2("Auto-Scroll", "Auto scroll")
                font.pixelSize: 12
                checked: controller ? controller.autoScroll : true
                enabled: controller !== undefined && controller !== null
                onToggled: {
                    if (controller) controller.autoScroll = checked
                    if (checked) pinToEnd()          // beim Einschalten sofort ans Chat-Ende
                }
            }
            Button {
                flat: true
                font.pixelSize: 12
                implicitWidth: implicitContentWidth + leftPadding + rightPadding
                text: tr2("Login", "Login")
                onClicked: root.loginOpen = !root.loginOpen
            }
        }

        // ——— Login (nur nötig, um selbst zu schreiben) ———
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: root.loginOpen
            // Anmelden ohne Einfügen: Twitch-Device-Code, der Token kommt von selbst
            Button {
                objectName: "deviceLogin"
                Layout.fillWidth: true
                visible: !controller || controller.loginState !== "waiting"
                highlighted: true
                text: controller && controller.authenticated ? tr2("Neu mit Twitch anmelden", "Sign in with Twitch again")
                                                             : tr2("Mit Twitch anmelden", "Sign in with Twitch")
                onClicked: if (controller) controller.startLogin()
            }
            ColumnLayout {
                objectName: "deviceLoginWaiting"
                Layout.fillWidth: true
                spacing: 4
                visible: controller !== null && controller.loginState === "waiting"
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: textColor()
                    font.pixelSize: 12
                    text: controller && controller.loginCode
                          ? tr2("Im Browser bei Twitch auf „Autorisieren“ klicken – der Rest geht von selbst. Code: %1",
                                "Click 'Authorize' on Twitch in your browser – the rest happens by itself. Code: %1").arg(controller.loginCode)
                          : tr2("Twitch wird gefragt …", "Asking Twitch …")
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Button {
                        objectName: "loginReopen"
                        text: tr2("Link erneut öffnen", "Open link again")
                        enabled: controller !== null && controller.loginUrl !== ""
                        onClicked: Qt.openUrlExternally(controller.loginUrl)
                    }
                    Button {
                        objectName: "loginCancel"
                        flat: true
                        text: tr2("Abbrechen", "Cancel")
                        onClicked: if (controller) controller.cancelLogin()
                    }
                }
            }
            Label {
                objectName: "loginError"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: controller !== null && controller.loginState === "error"
                text: controller ? controller.loginError : ""
                color: errorColor()
                font.pixelSize: 11
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: !controller || !controller.authenticated
                text: tr2("Nur zum Mitschreiben nötig – lesen geht auch ohne Anmeldung.",
                          "Only needed to post yourself – reading works without signing in.")
                color: muted()
                font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: controller ? controller.authenticated : false
                Label {
                    objectName: "loginStatus"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    // Alter, von Hand eingefügter Token (bis 2.1.0): läuft ab, erneuert sich nicht
                    text: controller && controller.autoRenew
                          ? tr2("Angemeldet als %1 – der Token erneuert sich selbst", "Signed in as %1 — the token renews itself").arg(controller.user)
                          : tr2("Angemeldet als %1. Bitte einmal „Mit Twitch anmelden“, damit sich der Token künftig selbst erneuert.",
                                "Signed in as %1. Please use 'Sign in with Twitch' once so the token renews itself from now on.").arg(controller ? controller.user : "")
                    color: controller && controller.autoRenew ? "#2ecc71" : accent()
                    font.pixelSize: 11
                }
                Button {
                    objectName: "logoutButton"
                    flat: true
                    text: tr2("Logout", "Logout")
                    onClicked: { if (controller) controller.setAuth("", "") }
                }
            }
        }

        // ——— Chat-Liste ———
        Item {
            id: chatArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: chatView
                objectName: "chatView"
                // Unter Anheftung, Umfrage und Vorhersage statt dahinter: Der Chat wird kürzer, nichts wird verdeckt.
                anchors.top: topCards.height > 0 ? topCards.bottom : parent.top
                anchors.topMargin: topCards.height > 0 ? 6 : 0
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 6
                // Karte erscheint, klappt auf oder zu: Bei Auto-Scroll bleibt die neueste Nachricht sichtbar.
                onHeightChanged: if (autoScrollOn() && root.selectingKey === "") pinToEnd()
                spacing: 6
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: 600
                clip: true
                model: chatModel
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                // Erwähnung (@deinname oder Antwort auf dich): Nachricht steht in einem farbigen Kasten.
                delegate: Item {
                    id: msgRow
                    objectName: model.mention ? "mentionRow" : "chatRow"
                    readonly property bool mention: model.mention === true
                    // AutoMod: zurückgehaltene Nachricht (nur für Moderatoren/Streamer) in einer blauen Box
                    readonly property bool automod: model.type === "automod"
                    // Meldungen von Twitch selbst (tmi.twitch.tv): ebenfalls blaue Box
                    readonly property bool fromTwitch: model.fromTwitch === true
                    readonly property bool automodNotice: fromTwitch && !!model.noticeId
                                                          && (model.noticeId.indexOf("automod") === 0 || model.noticeId.indexOf("msg_rejected") === 0)
                    readonly property bool blueBox: automod || fromTwitch
                    // Nachricht eines eingetragenen Event-Bots (Liste in den Box-Einstellungen, gilt auch nachträglich)
                    readonly property var bot: model.userNotice !== true && model.type !== "system" && model.type !== "automod"
                                               && !!model.user ? (root.botMap[model.user] || null) : null
                    // Sub-, Raid- und ähnliche Meldungen: gelbe Box „System“
                    readonly property bool yellowBox: model.userNotice === true || model.bits > 0 || bot !== null
                    // Von Moderatoren gelöscht (Löschen, Timeout, Bann): graue Box, Text durchgestrichen
                    readonly property bool deleted: model.deleted === true
                    readonly property bool boxed: mention || blueBox || yellowBox || deleted
                    readonly property string boxKind: model.bits > 0 ? "bits" : bot !== null ? "bot"
                                                      : model.userNotice === true ? (model.noticeId === "follow" ? "follow" : "system")
                                                      : automod || automodNotice ? "automod" : fromTwitch ? "twitch" : "mention"
                    readonly property color boxColor: deleted ? muted()
                                                      : boxKind === "bot" && bot.color ? bot.color : root.boxColorOf(boxKind)
                    width: chatView.width - 12
                    height: msgItem.height + (boxed ? 12 : 0)
                    Rectangle {
                        objectName: msgRow.deleted ? "deletedBox" : msgRow.automod ? "automodBox" : msgRow.yellowBox ? "systemBox" : msgRow.fromTwitch ? "twitchBox" : "mentionBox"
                        visible: msgRow.boxed
                        anchors.fill: parent
                        radius: 6
                        color: Qt.rgba(msgRow.boxColor.r, msgRow.boxColor.g, msgRow.boxColor.b, 0.18)
                        border.color: msgRow.boxColor
                        border.width: 2
                        Label {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.rightMargin: 6
                            anchors.topMargin: 3
                            objectName: "boxLabel"
                            text: msgRow.deleted ? (model.deletedReason === "timeout" ? "Timeout"
                                                     : model.deletedReason === "ban" ? tr2("gebannt", "banned")
                                                     : tr2("gelöscht", "deleted"))
                                  : model.bits > 0 ? model.bits + " Bits" : msgRow.bot !== null ? msgRow.bot.label
                                  : msgRow.yellowBox ? (model.noticeId === "follow" ? tr2("Follow", "Follow") : "System") : msgRow.automod || msgRow.automodNotice ? "AutoMod" : msgRow.fromTwitch ? "tmi.twitch.tv" : "@"
                            font.bold: true
                            font.pixelSize: msgRow.blueBox || msgRow.yellowBox || msgRow.deleted ? Math.max(9, root.chatFontSize - 2) : root.chatFontSize
                            color: msgRow.boxColor
                        }
                    }
                Column {
                    id: msgItem
                    x: msgRow.boxed ? 8 : 0
                    y: msgRow.boxed ? 6 : 0
                    width: msgRow.width - (msgRow.boxed ? 16 : 0)
                    spacing: 2
                    opacity: msgRow.deleted ? 0.7 : 1
                    property string mtype: model.type
                    readonly property var rowSegments: model.segments
                    readonly property string key: model.ts + "|" + model.name + "|" + model.raw
                    readonly property var badgeList: model.badgeTags ? model.badgeTags.split(",") : []
                    Flow {
                        width: parent.width
                        spacing: 4
                        // Abzeichen als Bild („set/version“). Solange es fehlt (Katalog oder Download läuft,
                        // kein Netz), steht bei den wichtigsten das Textschild da; unbekannte bleiben unsichtbar.
                        Repeater {
                            model: msgItem.badgeList
                            Item {
                                id: badgeItem
                                objectName: "chatBadge"
                                readonly property string setId: modelData.split("/")[0]
                                readonly property string imageUrl: controller && controller.badgeRevision >= 0
                                                                   ? controller.badgeImage(modelData) : ""
                                visible: imageUrl !== "" || badgeLabel(setId) !== null
                                width: imageUrl !== "" ? root.badgeSize : bLabel.implicitWidth + 8
                                height: imageUrl !== "" ? root.badgeSize : bLabel.implicitHeight + 4
                                Image {
                                    objectName: "chatBadgeImage"
                                    anchors.fill: parent
                                    visible: badgeItem.imageUrl !== ""
                                    source: badgeItem.imageUrl
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    mipmap: true
                                    asynchronous: true
                                }
                                Rectangle {
                                    objectName: "chatBadgeText"
                                    anchors.fill: parent
                                    visible: badgeItem.imageUrl === ""
                                    radius: 4
                                    color: badgeColor(badgeItem.setId)
                                    Label {
                                        id: bLabel
                                        anchors.centerIn: parent
                                        text: badgeLabel(badgeItem.setId) || ""
                                        color: "white"
                                        font.pixelSize: 9
                                        font.bold: true
                                    }
                                }
                            }
                        }
                        Label {
                            objectName: "chatName"
                            text: model.name
                            font.bold: true
                            font.pixelSize: root.chatFontSize + 1
                            color: model.color && model.color.length === 7 ? model.color : accent()
                            // Klick (links oder rechts) öffnet das Nutzer-Menü; Systemzeilen haben keinen Nutzer
                            HoverHandler { cursorShape: model.user ? Qt.PointingHandCursor : Qt.ArrowCursor }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                enabled: !!model.user
                                // badgeText statt badges: Die Liste kommt im Modell als Untermodell an, nicht als Array.
                                onTapped: root.openUserMenu({ login: model.user, name: model.name, userId: model.userId || "",
                                                              msgId: model.msgId || "",
                                                              badges: model.badgeText ? model.badgeText.split(",") : [] })
                            }
                        }
                        Label {
                            text: Qt.formatDateTime(new Date(model.ts), "hh:mm")
                            color: muted()
                            font.pixelSize: Math.max(8, root.chatFontSize - 2)
                            opacity: 0.7
                        }
                    }
                    // Nachricht: Text-Wörter und Emote-Bilder fließen in einer Zeile. Markiert wird direkt
                    // darauf: Ziehen markiert Zeichen, Doppelklick alles, Rechtsklick öffnet das Kopiermenü.
                    // preventStealing: Sonst übernimmt die Chatliste den Zug nach der ersten Bewegung.
                    // Gescrollt wird mit Mausrad, Scrollbalken oder in den Lücken zwischen den Nachrichten.
                    Item {
                        id: textArea
                        width: parent.width
                        height: msgFlow.height
                        readonly property bool selecting: root.selectingKey !== "" && root.selectingKey === msgItem.key
                        MouseArea {
                            id: msgMouse
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            preventStealing: true
                            property point pressPos
                            property int pressIndex: 0
                            property bool dragSelecting: false
                            onPressed: mouse => {
                                pressPos = Qt.point(mouse.x, mouse.y)
                                dragSelecting = false
                                if (mouse.button !== Qt.LeftButton) return
                                root.stopSelecting()
                                pressIndex = msgFlow.indexAt(mouse.x, mouse.y)
                            }
                            onPositionChanged: mouse => {
                                if (!(mouse.buttons & Qt.LeftButton)) return
                                if (!dragSelecting && Math.abs(mouse.x - pressPos.x) < 4 && Math.abs(mouse.y - pressPos.y) < 4) return
                                dragSelecting = true
                                root.setSelection(msgItem.key, model.raw, pressIndex, msgFlow.indexAt(mouse.x, mouse.y))
                            }
                            onReleased: dragSelecting = false
                            onDoubleClicked: mouse => {
                                if (mouse.button === Qt.LeftButton) root.setSelection(msgItem.key, model.raw, 0, model.raw.length)
                            }
                            onClicked: mouse => {
                                if (mouse.button !== Qt.RightButton) return
                                root.openMessageMenu(msgItem.key, model.raw, model.name, "",
                                                     textArea.selecting ? root.selectedChatText : "", model.user)
                            }
                        }
                        SegmentFlow {
                            id: msgFlow
                            width: parent.width
                            segments: msgItem.rowSegments
                            messageType: msgItem.mtype
                            struck: msgRow.deleted
                            selStart: textArea.selecting ? root.selFrom : -1
                            selEnd: textArea.selecting ? root.selTo : -1
                            onLinkMenu: url => root.openMessageMenu(msgItem.key, model.raw, model.name, url,
                                                                     textArea.selecting ? root.selectedChatText : "", model.user)
                        }
                    }
                    // AutoMod: Grund, Entscheidung, Knöpfe solange offen
                    Flow {
                        objectName: "automodActions"
                        visible: msgRow.automod
                        width: parent.width
                        spacing: 6
                        Label {
                            objectName: "automodState"
                            width: parent.width
                            wrapMode: Text.WordWrap
                            font.pixelSize: Math.max(9, root.chatFontSize - 1)
                            color: model.automodStatus === "approved" ? "#2ecc71" : model.automodStatus === "denied" ? errorColor() : "#4a9eff"
                            text: {
                                var reason = root.automodReasonText(model.automodReason)
                                var by = model.automodBy ? " " + tr2("von ", "by ") + model.automodBy : ""
                                if (model.automodStatus === "approved") return tr2("Freigegeben", "Approved") + by
                                if (model.automodStatus === "denied") return tr2("Abgelehnt", "Denied") + by
                                if (model.automodStatus === "expired") return tr2("Abgelaufen – nicht mehr entscheidbar", "Expired – can no longer be decided")
                                return tr2("Wartet auf Prüfung", "Awaiting review") + (reason ? " · " + reason : "")
                            }
                        }
                        Button {
                            objectName: "automodAllow"
                            visible: model.automodStatus === "pending"
                            text: tr2("Freigeben", "Approve")
                            font.pixelSize: Math.max(9, root.chatFontSize - 1)
                            onClicked: if (controller) controller.automodAction(model.automodId, true)
                        }
                        Button {
                            objectName: "automodDeny"
                            visible: model.automodStatus === "pending"
                            flat: true
                            text: tr2("Ablehnen", "Deny")
                            font.pixelSize: Math.max(9, root.chatFontSize - 1)
                            onClicked: if (controller) controller.automodAction(model.automodId, false)
                        }
                    }
                }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: chatModel.count === 0
                color: muted()
                text: (controller && controller.channel)
                      ? tr2("Warte auf Nachrichten im Chat …", "Waiting for chat messages …")
                      : tr2("Kein Kanal gewählt.", "No channel selected.")
            }
            // Karten über dem Chat: Anheftung, Umfrage, Vorhersage – untereinander, der Chat beginnt darunter.
            Column {
                id: topCards
                objectName: "topCards"
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: 10
                spacing: 6
            // Angeheftete Nachricht des Kanals.
            // ▾/▸ klappt sie auf eine Zeile zusammen, ✕ blendet sie aus.
            Rectangle {
                id: pinCard
                objectName: "pinnedCard"
                readonly property var pin: controller ? controller.pinnedMessage : ({})
                readonly property bool collapsed: controller !== null && controller.pinnedCollapsed
                visible: controller !== null && !!pin.id && !controller.pinnedHidden
                width: parent.width
                height: Math.min(pinColumn.implicitHeight + 12, chatArea.height * 0.5)
                radius: 6
                color: Qt.color(textColor()).hslLightness > 0.5 ? "#232329" : "#f4f4f6"
                border.color: accent()
                border.width: 1
                // Klicks und Mausrad gehen nicht an den Chat dahinter.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                    onWheel: wheel => wheel.accepted = true
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton)
                            root.openMessageMenu("", pinCard.pin.text || "", pinCard.pin.sender || "", "")
                    }
                }
                Column {
                    id: pinColumn
                    x: 8
                    y: 6
                    width: parent.width - 16
                    spacing: 4
                    RowLayout {
                        width: parent.width
                        spacing: 6
                        Label {
                            text: "📌"
                            font.pixelSize: 12
                        }
                        Label {
                            objectName: "pinnedSender"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: pinCard.pin.sender || ""
                            font.bold: true
                            font.pixelSize: root.chatFontSize + 1
                            color: pinCard.pin.color && pinCard.pin.color.length === 7 ? pinCard.pin.color : accent()
                        }
                        ToolButton {
                            objectName: "pinnedCollapse"
                            implicitWidth: 26
                            implicitHeight: 22
                            text: pinCard.collapsed ? "▸" : "▾"
                            font.pixelSize: 12
                            Accessible.name: pinCard.collapsed ? tr2("Aufklappen", "Expand") : tr2("Einklappen", "Collapse")
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: pinCard.collapsed ? tr2("Aufklappen", "Expand") : tr2("Einklappen", "Collapse")
                            onClicked: if (controller) controller.pinnedCollapsed = !controller.pinnedCollapsed
                        }
                        ToolButton {
                            objectName: "pinnedHide"
                            implicitWidth: 26
                            implicitHeight: 22
                            text: "✕"
                            font.pixelSize: 12
                            Accessible.name: tr2("Angeheftete Nachricht verbergen", "Hide pinned message")
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: tr2("Verbergen – 📌 oben holt sie zurück", "Hide – 📌 at the top brings it back")
                            onClicked: if (controller) controller.hidePinned()
                        }
                    }
                    // Eingeklappt: nur der Anfang der Nachricht in einer Zeile
                    Label {
                        objectName: "pinnedPreview"
                        width: parent.width
                        visible: pinCard.collapsed
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        text: (pinCard.pin.text || "").replace(/\s+/g, " ")
                        color: textColor()
                        font.pixelSize: root.chatFontSize
                    }
                    Label {
                        width: parent.width
                        visible: !pinCard.collapsed && !!pinCard.pin.pinnedBy && pinCard.pin.pinnedBy !== pinCard.pin.sender
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        text: tr2("Angeheftet von ", "Pinned by ") + (pinCard.pin.pinnedBy || "")
                        color: muted()
                        font.pixelSize: Math.max(8, root.chatFontSize - 2)
                    }
                    // Lange Nachrichten scrollen innerhalb der Karte.
                    Flickable {
                        objectName: "pinnedBody"
                        visible: !pinCard.collapsed
                        width: parent.width
                        height: Math.min(pinFlow.height, chatArea.height * 0.5 - 50)
                        contentHeight: pinFlow.height
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: contentHeight > height
                        SegmentFlow {
                            id: pinFlow
                            objectName: "pinnedText"
                            width: parent.width
                            segments: pinModel.count > 0 ? pinModel.get(0).segments : null
                            onLinkMenu: url => root.openMessageMenu("", pinCard.pin.text || "", pinCard.pin.sender || "", url)
                        }
                    }
                }
            }
            // Umfrage des Kanals: Balken je Antwort, Restzeit, nach dem Ende kurz das Ergebnis.
            EventCard {
                id: pollCard
                objectName: "pollCard"
                kind: "poll"
                event: controller ? controller.channelPoll : ({})
                hidden: controller !== null && controller.pollHidden
                collapsed: controller !== null && controller.pollCollapsed
                nowMs: root.eventNowMs
                onToggleCollapsed: if (controller) controller.pollCollapsed = !controller.pollCollapsed
                onHideRequested: if (controller) controller.hidePoll()
            }
            // Vorhersage des Kanals: Kanalpunkte je Ausgang, gesperrt/aufgelöst mit Gewinner.
            EventCard {
                id: predictionCard
                objectName: "predictionCard"
                kind: "prediction"
                event: controller ? controller.channelPrediction : ({})
                hidden: controller !== null && controller.predictionHidden
                collapsed: controller !== null && controller.predictionCollapsed
                nowMs: root.eventNowMs
                onToggleCollapsed: if (controller) controller.predictionCollapsed = !controller.predictionCollapsed
                onHideRequested: if (controller) controller.hidePrediction()
            }
            }
        }

        // ——— Emote-Browser (Tabs + Suche + Fliesen) ———
        ColumnLayout {
            id: emoteBrowser
            Layout.fillWidth: true
            Layout.fillHeight: false      // sonst erbt das Layout es vom GridView und verdeckt den Chat
            Layout.preferredHeight: root.emotesOpen ? 290 : 0
            spacing: 6
            visible: root.emotesOpen

            Flow {
                Layout.fillWidth: true
                spacing: 10
                Repeater {
                    model: ListModel {
                        ListElement { k: "all";       de: "Alle";      en: "All" }
                        ListElement { k: "subs";      de: "Abos";      en: "Subs" }
                        ListElement { k: "twitch";    de: "Twitch";    en: "Twitch" }
                        ListElement { k: "7tv";       de: "7TV";       en: "7TV" }
                        ListElement { k: "bttv";      de: "BTTV";      en: "BTTV" }
                        ListElement { k: "ffz";       de: "FFZ";       en: "FFZ" }
                        ListElement { k: "favorites"; de: "Favoriten"; en: "Favorites" }
                    }
                    delegate: Label {
                        text: tr2(model.de, model.en)
                        color: root.browserKind === model.k ? accent() : muted()
                        font.pixelSize: 12
                        font.bold: root.browserKind === model.k
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.browserKind = model.k
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    placeholderText: tr2("Suchen …", "Search …")
                    font.pixelSize: 12
                    onTextChanged: reloadBrowser()
                }
                Label { text: "" + root.browserCount; color: muted(); font.pixelSize: 11 }
            }
            ListView {
                id: emoteGrid
                objectName: "emoteGrid"
                Layout.fillWidth: true
                Layout.fillHeight: true
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                model: browserModel
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                section.property: "sec"
                section.criteria: ViewSection.FullString
                section.delegate: Item {
                    required property string section
                    width: ListView.view.width
                    height: 26
                    Label {
                        objectName: "emoteSectionTitle"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 4
                        text: root.sectionTitle(parent.section)
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        font.bold: true
                        color: accent()
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: muted()
                        opacity: 0.35
                    }
                }

                delegate: Row {
                    height: 90
                    Repeater {
                        model: tiles
                        delegate: Item {
                            width: Math.floor(emoteGrid.width / root.browserColumns)
                            height: 90
                            // Rechtsklick auf die ganze Kachel = Favorit, auch bei gesperrten Emotes.
                            // Liegt unter den übrigen Flächen; die nehmen nur die linke Taste an.
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onClicked: { if (controller) controller.toggleEmoteFavorite(model.name, model.id) }
                            }
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 2
                                radius: 6
                                color: model.favorite ? "#189147ff" : "transparent"
                                border.color: model.favorite ? accent() : "transparent"
                            }
                            Item {
                                id: tileImg
                                objectName: "emoteTileImage"
                                width: 56
                                height: 44
                                opacity: model.locked === true ? 0.3 : 1
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 2
                                Image {
                                    id: tileStatic
                                    visible: model.animated !== true
                                    anchors.fill: parent
                                    source: model.animated !== true ? model.url : ""
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                }
                                AnimatedImage {
                                    id: tileAnim
                                    visible: model.animated === true
                                    anchors.fill: parent
                                    source: model.animated === true ? model.url : ""
                                    fillMode: AnimatedImage.PreserveAspectFit
                                }
                            }
                            Label {
                                width: parent.width
                                y: 48
                                text: model.name
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 9
                                color: textColor()
                                opacity: model.locked === true ? 0.4 : 1
                            }
                            Label {
                                visible: model.provider !== "twitch"
                                anchors.left: parent.left
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: 4
                                text: model.provider.toUpperCase()
                                font.pixelSize: 8
                                font.bold: true
                                color: accent()
                                opacity: 0.85
                            }
                            Label {
                                id: starLabel
                                anchors.top: parent.top
                                anchors.right: parent.right
                                text: model.favorite ? "★" : "☆"
                                font.pixelSize: 14
                                color: model.favorite ? accent() : muted()
                                width: Math.max(implicitWidth, 18)
                                height: Math.max(implicitHeight, 18)
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { if (controller) controller.toggleEmoteFavorite(model.name, model.id) }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                anchors.topMargin: 20                  // ★-Knopf bleibt frei klickbar
                                enabled: model.locked !== true         // ohne Abo nicht einsetzbar
                                onClicked: { sendField.text += model.name + " " }
                            }
                        }
                    }
                }
            }
            Label {
                objectName: "emoteScopeHint"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: controller !== null && controller.emoteScopeMissing === true
                         && (root.browserKind === "all" || root.browserKind === "twitch")
                text: tr2("TwitchHypeTrain: Für deine freigeschalteten Emotes braucht der Token das Recht user:read:emotes. Erzeuge ihn neu mit chat:read, chat:edit und user:read:emotes und trage ihn unter „Login“ ein.",
                          "TwitchHypeTrain: Your unlocked emotes need a token with the user:read:emotes scope. Create a new one with chat:read, chat:edit and user:read:emotes and enter it under 'Login'.")
                color: errorColor()
                font.pixelSize: 11
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: root.browserCount === 0 && root.browserKind === "subs"
                      ? tr2("Keine Abo-Emotes gefunden. Sie erscheinen, wenn du über „Login“ angemeldet und mit einem Kanal verbunden bist.",
                            "No subscription emotes found. They appear once you are signed in via 'Login' and connected to a channel.")
                      : root.browserCount === 0 && root.browserKind === "twitch"
                      ? tr2("Noch keine Twitch-Emotes. Sie erscheinen, sobald du mit einem Kanal verbunden bist.",
                            "No Twitch emotes yet. They appear once you are connected to a channel.")
                      : root.browserCount === 0
                      ? tr2("Noch keine Emotes gesammelt – verbinde dich mit einem Kanal; gesehene, Twitch-, BTTV-, FFZ- und 7TV-Emotes erscheinen hier automatisch.",
                            "No emotes collected yet — connect to a channel; seen, Twitch, BTTV, FFZ and 7TV emotes appear here automatically.")
                      : tr2("Klicken setzt den Namen ins Eingabefeld, Rechtsklick markiert als Favorit. BTTV-, FFZ- und 7TV-Emotes sehen andere nur mit der passenden Erweiterung.",
                            "Clicking puts the name into the input, right-clicking marks it as a favorite. Others see BTTV, FFZ and 7TV emotes only with the matching extension.")
                color: muted()
                font.pixelSize: 11
            }
        }

        // ——— Satzbau-Hinweis mit Korrektur ———
        Flow {
            id: grammarBar
            objectName: "grammarBar"
            Layout.fillWidth: true
            spacing: 4
            visible: root.activeGrammar !== null
            Label {
                objectName: "grammarMessage"
                width: parent.width
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                color: root.grammarColor
                font.pixelSize: Math.max(9, root.chatFontSize - 1)
                text: root.activeGrammar ? tr2(root.activeGrammar.message, root.activeGrammar.messageEn) : ""
            }
            Repeater {
                model: root.activeGrammar ? root.activeGrammar.replacements : []
                delegate: AssistChip {
                    required property string modelData
                    objectName: "grammarChip"
                    label: modelData
                    textColor: root.textColor()
                    borderColor: root.grammarColor
                    onActivated: root.applyGrammar(modelData)
                }
            }
        }

        // ——— Wortvorschläge: Tab übernimmt den ersten ———
        Flow {
            id: predictionBar
            objectName: "predictionBar"
            Layout.fillWidth: true
            spacing: 4
            visible: sendField.predictions.length > 0
            Repeater {
                model: sendField.predictions
                delegate: AssistChip {
                    required property string modelData
                    required property int index
                    objectName: "predictionChip"
                    label: index === 0 ? modelData + "  ⇥" : modelData
                    textColor: root.textColor()
                    borderColor: root.muted()
                    onActivated: root.acceptPrediction(modelData)
                }
            }
        }

        // ——— Eingabezeile (immer sichtbar: ohne Login steht nur ein Hinweis,
        //     weil Senden einen Token braucht) ———
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            // Einzeilige Eingabe. TextArea statt TextField, weil nur ein Textdokument einzelnen Stellen
            // eine eigene Breite geben kann (Emotes, InputEmoteStyler). Rahmen und waagerechtes
            // Scrollen übernimmt deshalb dieses Item.
            Item {
                id: sendBox
                Layout.fillWidth: true
                implicitWidth: 120
                implicitHeight: sendField.implicitHeight
                clip: true
                Rectangle {
                    anchors.fill: parent
                    radius: 2
                    color: sendField.palette.base
                    border.color: sendField.activeFocus ? sendField.palette.highlight : sendField.palette.mid
                }
            TextArea {
                id: sendField
                objectName: "sendField"
                background: null
                wrapMode: TextEdit.NoWrap
                textFormat: TextEdit.PlainText
                x: -scrollX
                width: Math.max(sendBox.width, contentWidth + leftPadding + rightPadding + 2)
                height: sendBox.height
                property real scrollX: 0
                function keepCursorVisible() {
                    var c = cursorRectangle.x
                    var next = scrollX
                    if (c - next > sendBox.width - rightPadding) next = c - sendBox.width + rightPadding + 1
                    else if (c - next < leftPadding) next = c - leftPadding
                    scrollX = Math.max(0, Math.min(next, width - sendBox.width))
                }
                onWidthChanged: keepCursorVisible()
                Connections {
                    target: sendBox
                    function onWidthChanged() { sendField.keepCursorVisible() }
                }
                font.pixelSize: root.chatFontSize        // wächst mit „−“/„+“ wie der Chat
                property var spellRanges: []     // [{start,length,word}] des aktuellen Texts
                property var grammarRanges: []   // [{start,length,message,messageEn,replacements,source}]
                property var emoteRanges: []     // [{start,length,src,anim,width}] – Emote-Namen, die als Bild erscheinen
                property string emoteFingerprint: "[]"
                property int emoteLayoutRevision: 0
                readonly property real inlineEmoteHeight: Math.min(sendBox.height - 4, cursorRectangle.height + 6)
                onInlineEmoteHeightChanged: Qt.callLater(root.refreshEmotePreview)
                property string grammarText      // Text, zu dem grammarRanges gehören
                property var predictions: []
                signal accepted()
                Keys.onReturnPressed: accepted()
                Keys.onEnterPressed: accepted()
                onTextChanged: {
                    // Eine Zeile: Eingefügte Umbrüche und Tabs werden Leerzeichen (gleiche Länge, Cursor bleibt).
                    if (/[\n\r\t]/.test(text)) {
                        var cursor = cursorPosition
                        text = text.replace(/[\n\r\t]/g, " ")
                        cursorPosition = Math.min(cursor, text.length)
                        return
                    }
                    root.requestSpellCheck()
                    Qt.callLater(root.refreshPredictions)
                    Qt.callLater(root.refreshEmotePreview)
                }
                onCursorPositionChanged: {
                    Qt.callLater(root.requestSpellCheck)
                    Qt.callLater(root.refreshPredictions)
                }
                Keys.onTabPressed: event => {
                    if (predictions.length) root.acceptPrediction(predictions[0])
                    else nextItemInFocusChain().forceActiveFocus()   // sonst fügte das Dokument einen Tab ein
                }
                onGrammarRangesChanged: spellLine.requestPaint()
                onActiveFocusChanged: Qt.callLater(root.requestSpellCheck)
                onSpellRangesChanged: spellLine.requestPaint()
                onCursorRectangleChanged: { keepCursorVisible(); spellLine.requestPaint() }
                onContentWidthChanged: spellLine.requestPaint()
                // Das Signal kommt nur ohne fertiges Menü; das Standardmenü des Stils bleibt deshalb aus.
                ContextMenu.menu: null
                ContextMenu.onRequested: root.openSpellMenu()
                HoverHandler { id: sendHover }
                enabled: controller ? controller.connected : false
                placeholderText: !controller ? ""
                    : !controller.authenticated
                        ? tr2("Erst anmelden (Knopf „Login“), dann schreiben",
                              "Log in first (the 'Login' button), then write")
                        : controller.connected
                            ? tr2("Nachricht schreiben …", "Write a message …")
                            : tr2("Verbindung wird aufgebaut …", "Connecting …")
                onAccepted: { if (controller && controller.send(text)) text = "" }

                // Emote-Bilder direkt im Feld: Das Feld bleibt Text (Cursor, Rechtschreibung, Vorschläge
                // arbeiten weiter mit Zeichenpositionen). Der Name ist unsichtbar und genau so breit wie
                // sein Bild (InputEmoteStyler), das Bild liegt darüber. Die Rechtecke von
                // positionToRectangle zählen bei TextArea samt Innenabstand, also ab Feldecke.
                Item {
                    id: emoteLayer
                    objectName: "emoteLayer"
                    width: sendField.width
                    height: sendField.height
                    Repeater {
                        model: sendField.emoteRanges
                        delegate: Item {
                            objectName: "inlineEmote"
                            required property var modelData
                            // Abhängigkeiten nennen: Text, Scrollen und Breite verschieben die Lage
                            readonly property rect from: {
                                sendField.text; sendField.cursorRectangle; sendField.contentWidth; sendField.emoteLayoutRevision
                                return sendField.positionToRectangle(modelData.start)
                            }
                            readonly property rect to: {
                                sendField.text; sendField.cursorRectangle; sendField.contentWidth; sendField.emoteLayoutRevision
                                return sendField.positionToRectangle(modelData.start + modelData.length)
                            }
                            x: from.x
                            y: from.y
                            width: Math.max(1, to.x - from.x)
                            height: from.height
                            Loader {
                                anchors.centerIn: parent
                                width: parent.width
                                height: sendField.inlineEmoteHeight
                                sourceComponent: modelData.anim ? animatedInline : staticInline
                            }
                            Component {
                                id: staticInline
                                Image { source: modelData.src; fillMode: Image.PreserveAspectFit; asynchronous: true }
                            }
                            Component {
                                id: animatedInline
                                AnimatedImage { source: modelData.src; fillMode: AnimatedImage.PreserveAspectFit }
                            }
                        }
                    }
                }

                // Rote Wellenlinie unter unbekannten Wörtern, als eigene Ebene über dem Text.
                Canvas {
                    id: spellLine
                    objectName: "spellLine"
                    width: sendField.width
                    height: sendField.height
                    onWidthChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.lineWidth = 1
                        drawWaves(ctx, sendField.grammarRanges, root.grammarColor)
                        drawWaves(ctx, sendField.spellRanges, errorColor())
                    }
                    function drawWaves(ctx, ranges, color) {
                        ctx.strokeStyle = color
                        for (var i = 0; i < ranges.length; ++i) {
                            var a = sendField.positionToRectangle(ranges[i].start)
                            var b = sendField.positionToRectangle(ranges[i].start + ranges[i].length)
                            var left = a.x
                            var right = b.x
                            var y = a.y + a.height - 1
                            ctx.beginPath()
                            ctx.moveTo(left, y)
                            for (var px = left, up = true; px < right; px += 2, up = !up)
                                ctx.lineTo(Math.min(px + 2, right), up ? y - 2 : y)
                            ctx.stroke()
                        }
                    }
                }
                Menu {
                    id: spellMenu
                    objectName: "spellMenu"
                    property var target: null        // falsch geschriebenes Wort unter der Maus
                    property var suggestions: []
                    property bool loading: false

                    Instantiator {
                        model: spellMenu.suggestions
                        delegate: MenuItem {
                            objectName: "spellSuggestion"
                            required property string modelData
                            text: modelData
                            font.bold: true
                            onTriggered: root.replaceSpellTarget(modelData)
                        }
                        onObjectAdded: (index, object) => spellMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => spellMenu.removeItem(object)
                    }
                    MenuItem {
                        objectName: "spellStatus"
                        visible: spellMenu.target !== null && spellMenu.suggestions.length === 0
                        height: visible ? implicitHeight : 0
                        enabled: false
                        text: spellMenu.loading ? tr2("Suche Vorschläge …", "Looking for suggestions …")
                                                : tr2("Keine Vorschläge", "No suggestions")
                    }
                    MenuItem {
                        objectName: "spellAddWord"
                        visible: spellMenu.target !== null && spellMenu.target.kind !== "grammar"
                        height: visible ? implicitHeight : 0
                        text: tr2("Zum Wörterbuch hinzufügen", "Add to dictionary")
                        onTriggered: if (controller && spellMenu.target) controller.addSpellWord(spellMenu.target.word)
                    }
                    MenuSeparator {
                        visible: spellMenu.target !== null
                        height: visible ? implicitHeight : 0
                    }
                    MenuItem {
                        text: tr2("Ausschneiden", "Cut")
                        enabled: sendField.selectedText.length > 0
                        onTriggered: sendField.cut()
                    }
                    MenuItem {
                        text: tr2("Kopieren", "Copy")
                        enabled: sendField.selectedText.length > 0
                        onTriggered: sendField.copy()
                    }
                    MenuItem {
                        text: tr2("Einfügen", "Paste")
                        enabled: sendField.canPaste
                        onTriggered: sendField.paste()
                    }
                    MenuSeparator {}
                    MenuItem {
                        objectName: "spellGermanToggle"
                        checkable: true
                        checked: controller ? controller.spellGerman : false
                        text: tr2("Deutsch prüfen", "Check German")
                        onTriggered: if (controller) controller.spellGerman = checked
                    }
                    MenuItem {
                        objectName: "spellEnglishToggle"
                        checkable: true
                        checked: controller ? controller.spellEnglish : false
                        text: tr2("Englisch prüfen", "Check English")
                        onTriggered: if (controller) controller.spellEnglish = checked
                    }
                    MenuSeparator {}
                    MenuItem {
                        objectName: "grammarToggle"
                        checkable: true
                        checked: controller ? controller.grammarCheck : false
                        text: tr2("Satzbau prüfen", "Check grammar")
                        onTriggered: if (controller) controller.grammarCheck = checked
                    }
                    MenuItem {
                        objectName: "grammarOnlineToggle"
                        checkable: true
                        enabled: controller ? controller.grammarCheck : false
                        checked: controller ? controller.grammarOnline : false
                        text: tr2("Online mit LanguageTool", "Online with LanguageTool")
                        onTriggered: if (controller) controller.grammarOnline = checked
                    }
                    MenuItem {
                        objectName: "predictToggle"
                        checkable: true
                        checked: controller ? controller.predictText : false
                        text: tr2("Wortvorschläge", "Word suggestions")
                        onTriggered: if (controller) controller.predictText = checked
                    }
                }
            }
            }
            Button {
                id: emoteButton
                objectName: "emoteButton"
                flat: true
                implicitWidth: implicitHeight
                Accessible.name: tr2("Emotes", "Emotes")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: tr2("Emotes", "Emotes")
                onClicked: root.emotesOpen = !root.emotesOpen
                // Selbst gezeichnet: unabhängig davon, welche Emoji-Schrift installiert ist.
                Canvas {
                    id: emoteIcon
                    readonly property color iconColor: root.emotesOpen ? accent() : textColor()
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    onIconColorChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d")
                        var size = Math.min(width, height)
                        var cx = width / 2
                        var cy = height / 2
                        var r = size / 2 - 1
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = iconColor
                        ctx.fillStyle = iconColor
                        ctx.lineWidth = 1.5
                        ctx.beginPath()
                        ctx.arc(cx, cy, r, 0, 2 * Math.PI)
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.arc(cx, cy, r * 0.58, 0.15 * Math.PI, 0.85 * Math.PI)
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.arc(cx - r * 0.36, cy - r * 0.26, size * 0.07, 0, 2 * Math.PI)
                        ctx.arc(cx + r * 0.36, cy - r * 0.26, size * 0.07, 0, 2 * Math.PI)
                        ctx.fill()
                    }
                }
            }
            Button {
                id: sendButton
                objectName: "sendButton"
                text: tr2("Senden", "Send")
                enabled: controller ? (controller.connected && controller.authenticated) : false
                onClicked: { if (controller && controller.send(sendField.text)) sendField.text = "" }
            }
        }
    }
}
