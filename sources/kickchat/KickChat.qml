import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Kick-Chat-Panel: Kopfzeile (Kanal, Auto-Kanal, Verbinden), Statuszeile (Live, Zuschauer,
// Chat-Modi, Schriftgröße, Auto-Scroll, Anmeldung), angeheftete Nachricht, Chat als
// Segment-Flow (Text, Links, animierte Emotes), Eingabe mit Emote-Browser, @-Vorschlägen
// und Antworten.
Item {
    id: root
    property var controller: null
    property var plugin: null
    property var theme: null

    property bool emotesOpen: false
    property string browserKind: "all"
    property string replyId: ""
    property string replyName: ""
    property string replyText: ""
    property string sendError: ""

    readonly property color kickGreen: "#53fc18"
    readonly property int chatFontSize: controller ? controller.chatFontSize : 13
    readonly property int emoteHeight: Math.round(chatFontSize * 2.0)
    readonly property bool loggedIn: controller ? controller.authenticated : false

    function tr2(de, en) { return plugin && plugin.language === "de" ? de : en }
    function textColor() { return theme && theme.text ? theme.text : "#e8e8e8" }
    function muted()     { return theme && theme.muted ? theme.muted : "#9a9a9a" }
    function accent()    { return kickGreen }
    function errorColor(){ return theme && theme.error ? theme.error : "#ff5c5c" }
    function panelBg()   { return Qt.rgba(1, 1, 1, 0.04) }

    function formatCount(n) {
        if (n < 0) return "–"
        if (n >= 1000000) return (n / 1000000).toFixed(1).replace(".", tr2(",", ".")) + "M"
        if (n >= 1000) return (n / 1000).toFixed(1).replace(".", tr2(",", ".")) + "K"
        return "" + n
    }

    function copyText(t) {
        clipHelper.text = t
        clipHelper.selectAll()
        clipHelper.copy()
        clipHelper.text = ""
    }

    function pinToEnd() { if (selectingKey === "") endTimer.restart() }

    // ——— Markieren direkt in der Chatzeile: Zeichenbereich [selFrom, selTo) der Rohnachricht selRaw
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
        if (controller && controller.autoScroll) endTimer.restart()
    }
    // Zeichenposition in der Rohnachricht unter (px, py) im Flow: nächster Baustein, Zeilen zuerst
    function rawIndexAt(flow, px, py) {
        var best = null, bestDistance = 1e12
        for (var i = 0; i < flow.children.length; ++i) {
            var c = flow.children[i]
            if (c.rawStart === undefined || c.rawStart < 0 || !c.visible) continue
            var dy = py < c.y ? c.y - py : (py > c.y + c.height ? py - c.y - c.height : 0)
            var dx = px < c.x ? c.x - px : (px > c.x + c.width ? px - c.x - c.width : 0)
            var distance = dy * 100000 + dx
            if (distance < bestDistance) { bestDistance = distance; best = c }
        }
        if (!best) return 0
        if (best.isEmote) return px < best.x + best.width / 2 ? best.rawFrom : best.rawTo
        var local = px - best.x
        var text = best.s
        var advance = 0
        for (var k = 0; k < text.length; ++k) {
            var w = chatFontMetrics.advanceWidth(text.charAt(k))
            if (advance + w / 2 > local) return best.rawFrom + k
            advance += w
        }
        return best.rawTo
    }

    // ——— Satzbau: Fundstelle am Cursor, sonst die erste
    readonly property color grammarColor: "#4a9eff"
    readonly property var activeGrammar: {
        var issues = sendField.grammarRanges
        if (!issues.length || sendField.grammarText !== sendField.text) return null
        var c = sendField.cursorPosition
        for (var i = 0; i < issues.length; ++i)
            if (c >= issues[i].start && c <= issues[i].start + issues[i].length) return issues[i]
        return issues[0]
    }
    function grammarIssueAt(pos) {
        var r = sendField.grammarRanges
        if (sendField.grammarText !== sendField.text) return null
        for (var i = 0; i < r.length; ++i)
            if (pos >= r[i].start && pos <= r[i].start + r[i].length) return r[i]
        return null
    }
    function applyGrammar(replacement, issue) {
        issue = issue || activeGrammar
        if (!issue || sendField.grammarText !== sendField.text) return
        sendField.remove(issue.start, issue.start + issue.length)
        sendField.insert(issue.start, replacement)
        sendField.cursorPosition = issue.start + replacement.length
        sendField.forceActiveFocus()
    }

    // Farbe/Etikett der Benachrichtigungs-Boxen
    function boxColorOf(kind, botName) {
        if (kind === "bot" && controller) {
            var bots = controller.eventBots
            for (var i = 0; i < bots.length; ++i)
                if (bots[i].label === botName && bots[i].color) return bots[i].color
        }
        var c = controller ? controller.boxColors : null
        return c && c[kind] ? c[kind] : "#f0a500"
    }
    function boxLabel(kind, botName) {
        if (kind === "mention") return "@"
        if (kind === "bot") return botName
        if (kind === "follow") return "Follow"
        if (kind === "kicks") return "Kicks"
        if (kind === "mod") return tr2("Moderation", "Moderation")
        return tr2("System", "System")
    }
    readonly property bool isMod: controller && (controller.myRole === "moderator" || controller.myRole === "broadcaster")
    property string actionInfo: ""
    property bool actionOk: true
    function showAction(ok, text) { actionOk = ok; actionInfo = text; actionInfoTimer.restart() }
    function actionName(a) {
        var de = { "delete": "Nachricht gelöscht", "timeout": "Timeout gesetzt", "ban": "Gebannt",
                   "unban": "Bann/Timeout aufgehoben", "block": "Blockiert", "unblock": "Blockierung aufgehoben" }
        var en = { "delete": "Message deleted", "timeout": "Timed out", "ban": "Banned",
                   "unban": "Ban/timeout removed", "block": "Blocked", "unblock": "Unblocked" }
        return tr2(de[a] || a, en[a] || a)
    }

    function loadHistory() {
        chatModel.clear()
        if (!controller) return
        var h = controller.history()
        for (var i = 0; i < h.length; ++i) chatModel.append(h[i])
        pinToEnd()
    }

    function reloadBrowser() {
        if (!controller || !emotesOpen) return
        emoteModel.clear()
        var list = controller.emoteList(browserKind, emoteSearch.text)
        for (var i = 0; i < list.length && i < 600; ++i) emoteModel.append(list[i])
    }

    function insertText(t) {
        var pos = sendField.cursorPosition
        var before = sendField.text.substring(0, pos)
        var after = sendField.text.substring(pos)
        var pre = before.length && before.charAt(before.length - 1) !== " " ? " " : ""
        var ins = pre + t + " "
        sendField.text = before + ins + after
        sendField.cursorPosition = pos + ins.length
        sendField.forceActiveFocus()
    }

    function startReply(m) {
        replyId = m.id
        replyName = m.name
        replyText = m.raw
        sendField.forceActiveFocus()
    }

    // Emote-Namen im Eingabefeld als Bild (ohne Lücke: Name exakt auf Bildbreite)
    function refreshInputEmotes() {
        var ranges = []
        if (controller && sendField.text.trim().length > 0) {
            var found = controller.inputEmotes(sendField.text)
            for (var i = 0; i < found.length; ++i) {
                var r = found[i]
                ranges.push({ start: r.start, length: r.length, src: r.src, anim: r.anim,
                              width: Math.round(sendField.inlineEmoteHeight * controller.imageAspect(r.src)) })
            }
        }
        var fingerprint = JSON.stringify(ranges)
        if (fingerprint === sendField.emoteFingerprint) return
        sendField.emoteFingerprint = fingerprint
        if (controller) controller.setInputEmotes(sendField, ranges)
        sendField.emoteRanges = ranges
        sendField.emoteLayoutRevision++
    }

    // ——— Rechtschreibung und Wortvorschläge (lokal, Hunspell)
    function requestSpellCheck() {
        if (!controller) return
        if (sendField.text.length === 0) sendField.spellRanges = []
        controller.spellCheck(sendField.text, sendField.activeFocus ? sendField.cursorPosition : -1)
    }
    function refreshPredictions() {
        sendField.predictions = controller && sendField.text.length && mentionBar.candidates.length === 0
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
    function spellRangeAt(pos) {
        var r = sendField.spellRanges
        for (var i = 0; i < r.length; ++i)
            if (pos >= r[i].start && pos <= r[i].start + r[i].length) return r[i]
        return null
    }
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
        var p = mouse ? sendField.mapToItem(root, mouse.x, mouse.y)
                      : sendField.mapToItem(root, sendField.cursorRectangle.x, sendField.cursorRectangle.y)
        spellMenu.popup(p.x, p.y)
    }
    function replaceSpellTarget(replacement) {
        var r = spellMenu.target
        if (!r || sendField.text.substr(r.start, r.length) !== r.word) return
        sendField.remove(r.start, r.start + r.length)
        sendField.insert(r.start, replacement)
        sendField.cursorPosition = r.start + replacement.length
        sendField.forceActiveFocus()
    }

    function doSend() {
        if (!controller || sendField.text.trim().length === 0) return
        sendError = ""
        if (controller.send(sendField.text, replyId)) {
            sendField.text = ""
            replyId = ""
        }
    }

    // aktuelles @-Wort am Cursor
    function mentionPrefix() {
        var t = sendField.text.substring(0, sendField.cursorPosition)
        var m = t.match(/(^|\s)@([A-Za-z0-9_\-]*)$/)
        return m ? m[2] : null
    }
    function applyMention(name) {
        var pos = sendField.cursorPosition
        var t = sendField.text.substring(0, pos)
        var start = t.lastIndexOf("@")
        var ins = "@" + name + " "
        sendField.text = sendField.text.substring(0, start) + ins + sendField.text.substring(pos)
        sendField.cursorPosition = start + ins.length
    }

    // Fokus abgeben, sobald woanders hingeklickt wird (Chat, Video, Player): sonst bleiben
    // Leertaste & Co. im Eingabefeld gefangen.
    readonly property var pressWindow: Window.window
    function watchWindowPresses() {
        if (controller && pressWindow) controller.watchWindowPresses(pressWindow)
    }
    function inside(item, x, y) {
        if (!item || !item.visible) return false
        var p = item.mapFromItem(null, x, y)
        return p.x >= 0 && p.y >= 0 && p.x < item.width && p.y < item.height
    }
    function handleWindowPress(x, y) {
        var released = false
        if (spellMenu.opened || chatMenu.opened) return
        if (selectingKey !== "" && !inside(chatView, x, y)) {
            stopSelecting()
            if (selectionKeys.activeFocus && pressWindow) pressWindow.contentItem.forceActiveFocus()
        }
        if (sendField.activeFocus) {
            var keep = [sendBox, sendButton, emoteButton, emoteBrowser, mentionBar, predictionBar, grammarBar]
            for (var i = 0; i < keep.length; ++i)
                if (inside(keep[i], x, y)) return
            sendField.focus = false
            released = true
        }
        if (channelField.activeFocus && !inside(channelField, x, y)) {
            channelField.focus = false
            released = true
        }
        if (released && pressWindow) pressWindow.contentItem.forceActiveFocus()
    }

    Component.onCompleted: { loadHistory(); watchWindowPresses() }
    onControllerChanged: { loadHistory(); watchWindowPresses() }
    onPressWindowChanged: watchWindowPresses()
    onEmotesOpenChanged: reloadBrowser()
    onBrowserKindChanged: reloadBrowser()

    ListModel { id: chatModel }
    ListModel { id: emoteModel }
    TextEdit { id: clipHelper; visible: false }
    // Tastatur fürs Markieren im Chat: Strg+C kopiert, Esc hebt auf
    Item {
        id: selectionKeys
        objectName: "selectionKeys"
        Keys.onPressed: function (event) {
            if (event.matches(StandardKey.Copy) && root.selectedChatText !== "") {
                root.copyText(root.selectedChatText)
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                root.stopSelecting()
                if (root.pressWindow) root.pressWindow.contentItem.forceActiveFocus()
                event.accepted = true
            }
        }
    }
    FontMetrics { id: chatFontMetrics; font.pixelSize: root.chatFontSize }
    Timer { id: browserRefresh; interval: 400; onTriggered: root.reloadBrowser() }
    Timer { id: actionInfoTimer; interval: 6000; onTriggered: root.actionInfo = "" }

    Connections {
        target: controller
        ignoreUnknownSignals: true
        function onNewMessage(m) {
            var follow = (controller.autoScroll || chatView.atYEnd) && !chatMenu.opened && root.selectingKey === ""
            chatModel.append(m)
            if (chatModel.count > 400) chatModel.remove(0, chatModel.count - 400)
            if (follow) pinToEnd()
        }
        function onMessageUpdated(id, m) {
            for (var i = chatModel.count - 1; i >= 0; --i) {
                if (chatModel.get(i).id === id) {
                    var follow = controller.autoScroll || chatView.atYEnd
                    chatModel.set(i, m)
                    if (follow) pinToEnd()
                    return
                }
            }
        }
        function onHistoryCleared() { chatModel.clear() }
        function onSpellResult(text, ranges) { if (text === sendField.text) sendField.spellRanges = ranges }
        function onGrammarResult(text, issues) {
            if (text === sendField.text) {
                sendField.grammarText = text
                sendField.grammarRanges = issues
            }
        }
        function onSpellSuggestions(word, suggestions) {
            if (spellMenu.target && spellMenu.target.word === word) {
                spellMenu.suggestions = suggestions
                spellMenu.loading = false
            }
        }
        function onSpellSettingsChanged() { root.requestSpellCheck() }
        function onPredictionsChanged() { root.refreshPredictions() }
        function onWindowPressed(window, x, y) { if (window === root.pressWindow) root.handleWindowPress(x, y) }
        function onChannelChanged() { channelField.text = controller.channel; root.replyId = "" }
        function onEmotesChanged() { browserRefresh.restart(); Qt.callLater(root.requestSpellCheck) }
        function onEmoteImageReady() { if (root.emotesOpen) browserRefresh.restart(); Qt.callLater(root.refreshInputEmotes) }
        function onUserActionDone(action, ok, err) {
            if (ok) { root.showAction(true, "✓ " + root.actionName(action)); return }
            var why = err === "forbidden" ? tr2("keine Berechtigung (Mod-Rechte bzw. bei der Anmeldung erteilte Rechte fehlen – ggf. ab- und wieder anmelden)",
                                                "not allowed (missing mod rights or login permissions – log out and in again)")
                    : err === "login" ? tr2("bitte anmelden", "please log in") : err
            root.showAction(false, "✗ " + root.actionName(action) + ": " + why)
        }
        function onSendResult(ok, err) {
            if (ok) { root.sendError = ""; return }
            if (err === "login") root.sendError = tr2("Bitte zuerst bei Kick anmelden.", "Please log in to Kick first.")
            else if (err === "rate") root.sendError = tr2("Zu schnell – kurz warten.", "Too fast – wait a moment.")
            else if (err === "channel") root.sendError = tr2("Kanal noch nicht geladen.", "Channel not loaded yet.")
            else root.sendError = tr2("Senden fehlgeschlagen: ", "Sending failed: ") + err
        }
    }

    // ———————————————————————————————————————————— Bausteine
    component EmoteImage: Item {
        id: emoteImage
        property string src: ""
        property bool anim: false
        property real size: root.emoteHeight
        readonly property real sw: loader.item ? loader.item.implicitWidth : 0
        readonly property real sh: loader.item ? loader.item.implicitHeight : 0
        height: size
        width: sh > 0 ? Math.min(Math.ceil(sw * size / sh), size * 4) : size
        Component { id: staticImg; Image { source: emoteImage.src; fillMode: Image.PreserveAspectFit; asynchronous: true; smooth: true; mipmap: true } }
        Component { id: animImg; AnimatedImage { source: emoteImage.src; fillMode: Image.PreserveAspectFit; smooth: true } }
        Loader {
            id: loader
            anchors.fill: parent
            active: emoteImage.src.length > 0
            sourceComponent: emoteImage.anim ? animImg : staticImg
        }
        Rectangle {   // Platzhalter, solange das Bild lädt
            anchors.fill: parent
            visible: emoteImage.src.length === 0
            radius: 3
            color: Qt.rgba(1, 1, 1, 0.08)
        }
    }

    // Ein Baustein einer Nachricht: Wort (ggf. Link) oder Emote-Bild
    component SegmentItem: Item {
        id: seg
        property string t: "text"
        property string s: ""
        property string url: ""
        property string src: ""
        property bool anim: false
        property string name: ""
        property bool notFirst: false
        property real maxWidth: 300
        property color baseColor: textColor()
        property int rawStart: -1          // Lage in der Rohnachricht (ohne führendes Leerzeichen)
        property int rawLen: 0
        property int selStart: -1          // markierter Bereich der Rohnachricht
        property int selEnd: -1
        readonly property int lead: !isEmote && s.charAt(0) === " " ? 1 : 0
        readonly property int rawFrom: rawStart - lead
        readonly property int rawTo: rawStart + rawLen
        readonly property bool isEmote: t === "emote"
        readonly property bool tooWide: segText.implicitWidth > maxWidth
        height: isEmote ? root.emoteHeight : Math.max(root.emoteHeight, segText.height)
        width: isEmote ? emote.width + (notFirst ? 3 : 0) : Math.min(segText.implicitWidth, maxWidth)
        Rectangle {
            objectName: "selectionHighlight"
            readonly property int a: Math.max(seg.selStart, seg.rawFrom)
            readonly property int b: Math.min(seg.selEnd, seg.rawTo)
            visible: seg.selStart >= 0 && seg.rawStart >= 0 && b > a
            x: seg.isEmote ? 0 : chatFontMetrics.advanceWidth(seg.s.substring(0, a - seg.rawFrom))
            width: seg.isEmote ? seg.width : chatFontMetrics.advanceWidth(seg.s.substring(a - seg.rawFrom, b - seg.rawFrom))
            height: seg.height
            color: root.kickGreen
            opacity: 0.35
        }
        Text {
            id: segText
            visible: !seg.isEmote
            text: seg.isEmote ? "" : seg.s
            width: seg.tooWide ? seg.maxWidth : implicitWidth
            height: seg.tooWide ? contentHeight : root.emoteHeight
            verticalAlignment: Text.AlignVCenter
            wrapMode: seg.tooWide ? Text.WrapAnywhere : Text.NoWrap
            textFormat: Text.PlainText
            color: seg.url ? "#6fb1ff" : seg.baseColor
            font.underline: seg.url !== ""
            font.pixelSize: root.chatFontSize
        }
        MouseArea {
            anchors.fill: segText
            enabled: seg.url !== ""
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            onClicked: controller.openInBrowser(seg.url)
            ToolTip.visible: containsMouse
            ToolTip.delay: 400
            ToolTip.text: seg.url
        }
        EmoteImage {
            id: emote
            visible: seg.isEmote
            x: seg.notFirst ? 3 : 0
            src: seg.isEmote ? seg.src : ""
            anim: seg.anim
            HoverHandler { id: emoteHover }
            ToolTip.visible: seg.isEmote && emoteHover.hovered
            ToolTip.delay: 300
            ToolTip.text: seg.name
        }
    }

    component SegmentFlow: Flow {
        id: flow
        property var segments
        spacing: 0
        Repeater {
            model: flow.segments
            delegate: SegmentItem {
                t: model.t; s: model.s || ""; url: model.url || ""; src: model.src || ""
                anim: model.anim || false; name: model.name || ""
                notFirst: index > 0
                maxWidth: flow.width
            }
        }
    }

    // ———————————————————————————————————————————— Layout
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // Kopfzeile
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
                objectName: "channelField"
                Layout.fillWidth: true
                placeholderText: tr2("Kick-Kanal oder kick.com/…", "Kick channel or kick.com/…")
                text: controller ? controller.channel : ""
                selectByMouse: true
                onAccepted: if (controller) controller.channel = text
            }
            Button {
                objectName: "connectButton"
                text: controller && controller.statusKey !== "idle" && controller.statusKey !== "notfound"
                      ? tr2("Trennen", "Disconnect") : tr2("Verbinden", "Connect")
                onClicked: {
                    if (!controller) return
                    if (controller.statusKey !== "idle" && controller.statusKey !== "notfound") controller.closeChat()
                    else if (channelField.text !== controller.channel) controller.channel = channelField.text
                    else controller.openChat()
                }
            }
        }

        // Statuszeile
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                width: 8; height: 8; radius: 4
                color: !controller ? muted()
                      : controller.statusKey === "connected" ? root.kickGreen
                      : controller.statusKey === "connecting" ? "#f0a500"
                      : controller.statusKey === "error" || controller.statusKey === "notfound" ? errorColor() : muted()
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: muted()
                font.pixelSize: 11
                text: {
                    if (!controller) return ""
                    var k = controller.statusKey
                    if (k === "connected") return controller.channelDisplay
                    if (k === "connecting") return tr2("Verbinde …", "Connecting …")
                    if (k === "notfound") return tr2("Kanal nicht gefunden: ", "Channel not found: ") + controller.statusDetail
                    if (k === "error") return tr2("Fehler: ", "Error: ") + controller.statusDetail
                    if (k === "closed") return tr2("Getrennt – verbinde neu …", "Disconnected – reconnecting …")
                    return controller.channel ? tr2("Nicht verbunden", "Not connected") : tr2("Kein Kanal", "No channel")
                }
                ToolTip.visible: statusHover.hovered && controller && controller.streamTitle.length > 0
                ToolTip.text: controller ? controller.streamTitle : ""
                HoverHandler { id: statusHover }
            }
            Rectangle {
                visible: controller && controller.streamLive
                color: "#e9113c"; radius: 3
                implicitWidth: liveText.implicitWidth + 8; implicitHeight: liveText.implicitHeight + 2
                Text { id: liveText; anchors.centerIn: parent; text: "LIVE"; color: "white"; font.bold: true; font.pixelSize: 10 }
            }
            ToolButton {
                id: viewerButton
                objectName: "viewerButton"
                visible: controller && controller.channel !== ""
                text: "👥 " + (controller && controller.viewerCount >= 0 ? formatCount(controller.viewerCount)
                                                                          : (controller ? "" + controller.chatterCount : ""))
                font.pixelSize: 11
                implicitHeight: 22
                onClicked: chattersPopup.open()
                ToolTip.visible: hovered
                ToolTip.text: tr2("Zuschauer (alle 60 s aktualisiert) – Klick: Leute im Chat",
                                  "Viewers (updated every 60 s) – click: people in chat")
            }
            Label {
                visible: controller && controller.chatModes.length > 0
                text: "⚙"
                color: muted(); font.pixelSize: 12
                ToolTip.visible: modeHover.hovered
                ToolTip.text: {
                    if (!controller) return ""
                    var parts = controller.chatModes.split(",")
                    var out = []
                    for (var i = 0; i < parts.length; ++i) {
                        var p = parts[i].split(":")
                        if (p[0] === "slow") out.push(tr2("Langsam-Modus: ", "Slow mode: ") + p[1] + " s")
                        else if (p[0] === "followers") out.push(tr2("Nur Follower", "Followers only") + (p[1] > 0 ? " (" + p[1] + " min)" : ""))
                        else if (p[0] === "subscribers") out.push(tr2("Nur Abonnenten", "Subscribers only"))
                        else if (p[0] === "emotes") out.push(tr2("Nur Emotes", "Emotes only"))
                    }
                    return out.join("\n")
                }
                HoverHandler { id: modeHover }
            }
            ToolButton {
                visible: controller && controller.pinnedHidden
                text: "📌"; font.pixelSize: 11
                implicitWidth: 24; implicitHeight: 22
                onClicked: controller.showPinned()
                ToolTip.visible: hovered; ToolTip.text: tr2("Angeheftete Nachricht zeigen", "Show pinned message")
            }
            // Benachrichtigungen (wie im Twitch-Chat): Ton, Box-Farben und Event-Bots
            Button {
                id: bellButton
                objectName: "bellButton"
                flat: true
                implicitWidth: 30
                implicitHeight: 22
                text: controller && !controller.notifySound ? "🔕" : "🔔"
                font.pixelSize: 12
                enabled: controller !== null
                Accessible.name: tr2("Benachrichtigungen", "Notifications")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: tr2("Benachrichtigungen: Ton bei Erwähnungen, Box-Farben und Bots", "Notifications: mention sound, box colours and bots")
                onClicked: notifyPopup.open()
            }
            ToolButton {
                visible: controller && controller.pollHidden
                text: "📊"; font.pixelSize: 11
                implicitWidth: 24; implicitHeight: 22
                onClicked: controller.showPoll()
                ToolTip.visible: hovered; ToolTip.text: tr2("Umfrage zeigen", "Show poll")
            }
            ToolButton {
                text: "−"; implicitWidth: 22; implicitHeight: 22
                onClicked: if (controller) controller.chatFontSize = controller.chatFontSize - 1
                ToolTip.visible: hovered; ToolTip.text: tr2("Schrift kleiner", "Smaller text")
            }
            ToolButton {
                text: "+"; implicitWidth: 22; implicitHeight: 22
                onClicked: if (controller) controller.chatFontSize = controller.chatFontSize + 1
                ToolTip.visible: hovered; ToolTip.text: tr2("Schrift größer", "Larger text")
            }
            ToolButton {
                id: menuButton
                objectName: "menuButton"
                text: "☰"; implicitWidth: 24; implicitHeight: 22
                onClicked: optionsMenu.popup(menuButton, 0, menuButton.height)
            }
        }

        // Angeheftete Nachricht
        Rectangle {
            id: pinCard
            Layout.fillWidth: true
            visible: controller && controller.pinnedMessage.id !== undefined && !controller.pinnedHidden
            implicitHeight: visible ? pinColumn.implicitHeight + 12 : 0
            radius: 6
            color: Qt.rgba(0.33, 0.99, 0.09, 0.08)
            border.color: Qt.rgba(0.33, 0.99, 0.09, 0.45)
            property var pin: controller ? controller.pinnedMessage : ({})
            ColumnLayout {
                id: pinColumn
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6 }
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "📌"; font.pixelSize: 11 }
                    Label {
                        Layout.fillWidth: true
                        text: (pinCard.pin.name || "") + (pinCard.pin.pinnedBy ? tr2("  · angeheftet von ", "  · pinned by ") + pinCard.pin.pinnedBy : "")
                        color: pinCard.pin.color || root.kickGreen
                        font.bold: true; font.pixelSize: Math.max(10, root.chatFontSize - 2)
                        elide: Text.ElideRight
                    }
                    ToolButton {
                        text: "✕"; implicitWidth: 20; implicitHeight: 20
                        onClicked: controller.hidePinned()
                        ToolTip.visible: hovered; ToolTip.text: tr2("Ausblenden", "Hide")
                    }
                }
                SegmentFlow {
                    Layout.fillWidth: true
                    segments: pinCard.pin.segments || []
                }
            }
        }

        // Umfrage
        Rectangle {
            id: pollCard
            objectName: "pollCard"
            Layout.fillWidth: true
            property var poll: controller ? controller.poll : ({})
            visible: controller && poll.title !== undefined && !controller.pollHidden
            implicitHeight: visible ? pollColumn.implicitHeight + 12 : 0
            radius: 6
            color: Qt.rgba(0.29, 0.62, 1, 0.08)
            border.color: Qt.rgba(0.29, 0.62, 1, 0.5)
            property int secondsLeft: 0
            Timer {
                interval: 1000; repeat: true; running: pollCard.visible
                triggeredOnStart: true
                onTriggered: pollCard.secondsLeft = Math.max(0, Math.round(((pollCard.poll.endsAt || 0) - Date.now()) / 1000))
            }
            ColumnLayout {
                id: pollColumn
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6 }
                spacing: 3
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "📊"; font.pixelSize: 11 }
                    Label {
                        Layout.fillWidth: true
                        text: pollCard.poll.title || ""
                        color: textColor(); font.bold: true
                        font.pixelSize: Math.max(10, root.chatFontSize - 1)
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                    Label {
                        text: pollCard.poll.ended ? tr2("beendet", "ended") : pollCard.secondsLeft + " s"
                        color: muted(); font.pixelSize: 10
                    }
                    ToolButton {
                        text: "✕"; implicitWidth: 20; implicitHeight: 20
                        onClicked: controller.hidePoll()
                    }
                }
                Repeater {
                    model: pollCard.poll.options || []
                    delegate: Item {
                        Layout.fillWidth: true
                        implicitHeight: optText.implicitHeight + 6
                        Rectangle {
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                            width: parent.width * modelData.percent / 100
                            radius: 3
                            color: modelData.leading ? Qt.rgba(0.29, 0.62, 1, 0.45) : Qt.rgba(1, 1, 1, 0.12)
                        }
                        Text {
                            id: optText
                            anchors { left: parent.left; right: pct.left; verticalCenter: parent.verticalCenter; leftMargin: 4 }
                            text: modelData.label
                            color: textColor(); elide: Text.ElideRight
                            font.pixelSize: Math.max(10, root.chatFontSize - 2)
                            font.bold: modelData.leading
                            textFormat: Text.PlainText
                        }
                        Text {
                            id: pct
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 4 }
                            text: modelData.percent + " % (" + modelData.votes + ")"
                            color: muted(); font.pixelSize: Math.max(9, root.chatFontSize - 3)
                        }
                    }
                }
                Label {
                    text: tr2("Abstimmen auf kick.com · %1 Stimmen", "Vote on kick.com · %1 votes").arg(pollCard.poll.total || 0)
                    color: muted(); font.pixelSize: 10
                }
            }
        }

        // Chat
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: panelBg()
            radius: 6
            clip: true

            ListView {
                id: chatView
                objectName: "chatView"
                anchors.fill: parent
                anchors.margins: 6
                model: chatModel
                spacing: 3
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                // Nachgeladene Bilder ändern die Höhe: gebündelt ans Ende springen (nie direkt im
                // Höhen-Signal – positionViewAtEnd erzeugt Zeilen und ändert die Höhe erneut).
                onContentHeightChanged: if (controller && controller.autoScroll && !moving && !chatMenu.opened) endTimer.restart()
                onHeightChanged: if (controller && controller.autoScroll) endTimer.restart()
                Timer { id: endTimer; interval: 50; onTriggered: if (root.selectingKey === "") chatView.positionViewAtEnd() }

                delegate: Rectangle {
                    id: row
                    width: chatView.width - 10
                    readonly property bool isSystem: model.type === "system"
                    readonly property string boxKind: {
                        if (model.mention) return "mention"
                        if (model.kind === "bot") return "bot"
                        if (isSystem) {
                            var k = model.kind
                            return k === "follow" || k === "kicks" || k === "mod" ? k : "system"
                        }
                        return ""
                    }
                    readonly property color boxColor: boxKind !== "" ? root.boxColorOf(boxKind, model.botName) : "transparent"
                    implicitHeight: col.implicitHeight + (boxKind !== "" ? 8 : 2)
                    radius: 4
                    color: boxKind !== "" ? Qt.rgba(boxColor.r, boxColor.g, boxColor.b, 0.13) : "transparent"
                    border.width: boxKind !== "" ? 1 : 0
                    border.color: boxKind !== "" ? Qt.rgba(boxColor.r, boxColor.g, boxColor.b, 0.6) : "transparent"
                    opacity: model.deleted ? 0.4 : 1
                    readonly property bool selecting: root.selectingKey !== "" && root.selectingKey === model.id

                    ColumnLayout {
                        id: col
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: row.border.width ? 5 : 0; rightMargin: 4 }
                        spacing: 1

                        // Antwort auf …
                        Label {
                            Layout.fillWidth: true
                            visible: model.replyName !== ""
                            text: "↪ @" + model.replyName + ": " + model.replyText
                            color: muted()
                            elide: Text.ElideRight
                            font.pixelSize: Math.max(9, root.chatFontSize - 2)
                            textFormat: Text.PlainText
                        }

                        Flow {
                            id: lineFlow
                            Layout.fillWidth: true
                            spacing: 0

                            Text {
                                visible: controller && controller.showTimestamps
                                text: model.time + " "
                                color: muted()
                                height: root.emoteHeight
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Math.max(9, root.chatFontSize - 2)
                            }
                            Item {
                                visible: row.boxKind !== ""
                                height: root.emoteHeight
                                width: boxPill.width + 4
                                Rectangle {
                                    id: boxPill
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: boxPillText.implicitWidth + 8
                                    height: boxPillText.implicitHeight + 2
                                    radius: 3
                                    color: row.boxColor
                                    Text {
                                        id: boxPillText
                                        anchors.centerIn: parent
                                        text: row.boxKind !== "" ? root.boxLabel(row.boxKind, model.botName) : ""
                                        color: "#0b0e0f"
                                        font.bold: true
                                        font.pixelSize: Math.max(8, root.chatFontSize - 4)
                                    }
                                }
                            }
                            Repeater {
                                model: badges
                                delegate: Item {
                                    readonly property bool hasImage: model.src !== ""
                                    height: root.emoteHeight
                                    width: (hasImage ? badgeImg.width : pill.width) + 3
                                    Image {
                                        id: badgeImg
                                        visible: parent.hasImage
                                        source: model.src
                                        height: Math.round(root.chatFontSize * 1.3)
                                        width: height
                                        anchors.verticalCenter: parent.verticalCenter
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        mipmap: true
                                    }
                                    Rectangle {
                                        id: pill
                                        visible: !parent.hasImage
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: pillText.implicitWidth + 6
                                        height: pillText.implicitHeight + 2
                                        radius: 3
                                        color: model.color
                                        Text {
                                            id: pillText
                                            anchors.centerIn: parent
                                            text: model.label
                                            color: "#0b0e0f"
                                            font.bold: true
                                            font.pixelSize: Math.max(8, root.chatFontSize - 4)
                                        }
                                    }
                                    HoverHandler { id: badgeHover }
                                    ToolTip.visible: badgeHover.hovered
                                    ToolTip.delay: 300
                                    ToolTip.text: model.tip
                                }
                            }
                            Text {
                                id: nameText
                                visible: !row.isSystem
                                text: model.name + ": "
                                color: model.color
                                font.bold: true
                                font.pixelSize: root.chatFontSize
                                height: root.emoteHeight
                                verticalAlignment: Text.AlignVCenter
                                textFormat: Text.PlainText
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: function (mouse) {
                                        if (mouse.button === Qt.RightButton) chatMenu.openFor(chatModel.get(index), null)
                                        else userCard.openFor(chatModel.get(index))
                                    }
                                }
                            }
                            Repeater {
                                model: segments
                                delegate: SegmentItem {
                                    t: model.t; s: model.s || ""; url: model.url || ""; src: model.src || ""
                                    anim: model.anim || false; name: model.name || ""
                                    notFirst: index > 0
                                    maxWidth: lineFlow.width
                                    baseColor: textColor()
                                    rawStart: model.o !== undefined && model.o !== null ? model.o : -1
                                    rawLen: model.n || 0
                                    selStart: row.selecting ? root.selFrom : -1
                                    selEnd: row.selecting ? root.selTo : -1
                                }
                            }
                        }
                    }

                    // Markieren: Ziehen markiert Zeichen, Doppelklick die ganze Nachricht, Rechtsklick Menü.
                    // preventStealing: sonst übernimmt die Chatliste den Zug. Scrollen: Mausrad/Scrollbalken.
                    MouseArea {
                        id: rowMouse
                        objectName: "rowMouse"
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        preventStealing: true
                        cursorShape: Qt.IBeamCursor
                        z: -1
                        property point pressPos
                        property int pressIndex: 0
                        property bool dragSelecting: false
                        function flowIndex(mx, my) {
                            var p = lineFlow.mapFromItem(rowMouse, mx, my)
                            return root.rawIndexAt(lineFlow, p.x, p.y)
                        }
                        onPressed: function (mouse) {
                            pressPos = Qt.point(mouse.x, mouse.y)
                            dragSelecting = false
                            if (mouse.button !== Qt.LeftButton) return
                            root.stopSelecting()
                            pressIndex = flowIndex(mouse.x, mouse.y)
                        }
                        onPositionChanged: function (mouse) {
                            if (!(mouse.buttons & Qt.LeftButton)) return
                            if (!dragSelecting && Math.abs(mouse.x - pressPos.x) < 4 && Math.abs(mouse.y - pressPos.y) < 4) return
                            dragSelecting = true
                            root.setSelection(model.id, model.raw, pressIndex, flowIndex(mouse.x, mouse.y))
                        }
                        onReleased: dragSelecting = false
                        onDoubleClicked: function (mouse) {
                            if (mouse.button === Qt.LeftButton) root.setSelection(model.id, model.raw, 0, model.raw.length)
                        }
                        onClicked: function (mouse) {
                            if (mouse.button === Qt.RightButton) chatMenu.openFor(chatModel.get(index), null)
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: chatModel.count === 0
                width: parent.width - 30
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: muted()
                text: !controller || !controller.channel
                      ? tr2("Öffne einen Kick-Stream (z. B. https://kick.com/jomotic) oder tippe oben einen Kanal ein.",
                            "Open a Kick stream (e.g. https://kick.com/jomotic) or type a channel above.")
                      : tr2("Warte auf Nachrichten …", "Waiting for messages …")
            }

            Button {
                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 8 }
                visible: !chatView.atYEnd && chatModel.count > 0
                text: tr2("▼ Neueste", "▼ Latest")
                onClicked: chatView.positionViewAtEnd()
            }
        }

        // Emote-Browser
        Rectangle {
            id: emoteBrowser
            objectName: "emoteBrowser"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(260, root.height * 0.4)
            visible: root.emotesOpen
            color: panelBg()
            radius: 6
            border.color: Qt.rgba(1, 1, 1, 0.1)
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: [
                            { k: "all", de: "Alle", en: "All" },
                            { k: "channel", de: "Kanal", en: "Channel" },
                            { k: "global", de: "Global", en: "Global" },
                            { k: "emoji", de: "Emojis", en: "Emojis" },
                            { k: "7tv", de: "7TV", en: "7TV" },
                            { k: "favorites", de: "★", en: "★" }
                        ]
                        delegate: Rectangle {
                            width: tabText.implicitWidth + 12; height: tabText.implicitHeight + 6
                            radius: 3
                            color: root.browserKind === modelData.k ? root.kickGreen : "transparent"
                            border.color: root.kickGreen
                            Text {
                                id: tabText
                                anchors.centerIn: parent
                                text: tr2(modelData.de, modelData.en)
                                color: root.browserKind === modelData.k ? "#0b0e0f" : textColor()
                                font.pixelSize: 11
                            }
                            TapHandler { onTapped: root.browserKind = modelData.k }
                        }
                    }
                }
                TextField {
                    id: emoteSearch
                    Layout.fillWidth: true
                    placeholderText: tr2("Emote suchen …", "Search emote …")
                    onTextChanged: browserRefresh.restart()
                }
                GridView {
                    id: emoteGrid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    cellWidth: 44
                    cellHeight: 44
                    model: emoteModel
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item {
                        width: 44; height: 44
                        Rectangle {
                            anchors.fill: parent; anchors.margins: 2
                            radius: 4
                            color: tileHover.hovered ? Qt.rgba(1, 1, 1, 0.1) : "transparent"
                            opacity: model.subOnly ? 0.6 : 1
                        }
                        EmoteImage {
                            anchors.centerIn: parent
                            size: 32
                            src: model.src
                            anim: model.anim
                        }
                        Text {
                            visible: model.favorite
                            anchors { right: parent.right; top: parent.top; margins: 2 }
                            text: "★"; color: "#f0c000"; font.pixelSize: 10
                        }
                        HoverHandler { id: tileHover }
                        ToolTip.visible: tileHover.hovered
                        ToolTip.delay: 300
                        ToolTip.text: model.name + " · " + model.provider + (model.subOnly ? tr2(" · nur Abonnenten", " · subscribers only") : "")
                                      + "\n" + tr2("Rechtsklick: Favorit", "Right-click: favourite")
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function (mouse) {
                                if (mouse.button === Qt.RightButton) controller.toggleEmoteFavorite(model.name)
                                else root.insertText(model.name)
                            }
                        }
                    }
                }
            }
        }

        // @-Vorschläge
        Flow {
            id: mentionBar
            Layout.fillWidth: true
            spacing: 4
            property var candidates: []
            onCandidatesChanged: Qt.callLater(root.refreshPredictions)
            visible: candidates.length > 0
            Repeater {
                model: mentionBar.candidates
                delegate: Rectangle {
                    width: mText.implicitWidth + 12; height: mText.implicitHeight + 6
                    radius: 3; color: "transparent"; border.color: root.kickGreen
                    Text { id: mText; anchors.centerIn: parent; text: "@" + modelData; color: textColor(); font.pixelSize: 11 }
                    TapHandler { onTapped: { root.applyMention(modelData); sendField.forceActiveFocus() } }
                }
            }
        }

        // Satzbau-Hinweis mit Korrektur zum Anklicken
        ColumnLayout {
            id: grammarBar
            objectName: "grammarBar"
            Layout.fillWidth: true
            spacing: 4
            visible: root.activeGrammar !== null
            Label {
                objectName: "grammarMessage"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                color: root.grammarColor
                font.pixelSize: Math.max(9, root.chatFontSize - 2)
                text: root.activeGrammar ? "✎ " + tr2(root.activeGrammar.message, root.activeGrammar.messageEn || root.activeGrammar.message) : ""
            }
            Flow {
              Layout.fillWidth: true
              spacing: 4
              Repeater {
                model: root.activeGrammar ? root.activeGrammar.replacements : []
                delegate: Rectangle {
                    objectName: "grammarChip"
                    width: gText.implicitWidth + 12; height: gText.implicitHeight + 6
                    radius: 3; color: "transparent"; border.color: root.grammarColor
                    Text { id: gText; anchors.centerIn: parent; text: modelData; color: textColor(); font.pixelSize: Math.max(10, root.chatFontSize - 2) }
                    TapHandler { onTapped: root.applyGrammar(modelData) }
                }
              }
            }
        }

        // Wortvorschläge: Tab oder Klick übernimmt
        Flow {
            id: predictionBar
            objectName: "predictionBar"
            Layout.fillWidth: true
            spacing: 4
            visible: sendField.predictions.length > 0 && mentionBar.candidates.length === 0
            Repeater {
                model: sendField.predictions
                delegate: Rectangle {
                    width: pText.implicitWidth + 12; height: pText.implicitHeight + 6
                    radius: 3; color: "transparent"; border.color: muted()
                    Text {
                        id: pText
                        anchors.centerIn: parent
                        text: index === 0 ? modelData + "  ⇥" : modelData
                        color: textColor()
                        font.pixelSize: Math.max(10, root.chatFontSize - 2)
                    }
                    TapHandler { onTapped: root.acceptPrediction(modelData) }
                }
            }
        }

        // Antwort-Leiste
        Rectangle {
            Layout.fillWidth: true
            visible: root.replyId !== ""
            implicitHeight: replyRow.implicitHeight + 6
            radius: 4
            color: panelBg()
            RowLayout {
                id: replyRow
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 4 }
                Label {
                    Layout.fillWidth: true
                    text: tr2("Antwort an @", "Replying to @") + root.replyName + ": " + root.replyText
                    color: muted(); elide: Text.ElideRight; font.pixelSize: 11
                    textFormat: Text.PlainText
                }
                ToolButton { text: "✕"; implicitWidth: 20; implicitHeight: 20; onClicked: root.replyId = "" }
            }
        }

        Label {
            objectName: "actionInfo"
            Layout.fillWidth: true
            visible: root.actionInfo !== ""
            text: root.actionInfo
            color: root.actionOk ? root.kickGreen : errorColor()
            wrapMode: Text.WordWrap
            font.pixelSize: 11
        }
        Label {
            Layout.fillWidth: true
            visible: root.sendError !== ""
            text: root.sendError
            color: errorColor()
            wrapMode: Text.WordWrap
            font.pixelSize: 11
        }

        // Eingabe
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ToolButton {
                id: emoteButton
                objectName: "emoteButton"
                text: "☺"
                font.pixelSize: 16
                implicitWidth: 30
                checkable: true
                checked: root.emotesOpen
                onClicked: root.emotesOpen = !root.emotesOpen
                ToolTip.visible: hovered; ToolTip.text: tr2("Emotes", "Emotes")
            }
            // Eingabe: TextArea statt TextField, weil nur sie ein Textdokument hat. Emote-Namen
            // bleiben Text (Cursor, Senden), werden aber unsichtbar und genau so breit wie ihr Bild
            // (InputEmoteStyler) – das Bild liegt darüber. So stehen Emotes ohne Lücke im Feld.
            Rectangle {
                id: sendBox
                objectName: "sendBox"
                Layout.fillWidth: true
                implicitHeight: Math.max(32, Math.round(root.chatFontSize * 2.4))
                radius: 4
                color: Qt.rgba(1, 1, 1, sendField.enabled ? 0.06 : 0.03)
                border.color: sendField.activeFocus ? root.kickGreen : Qt.rgba(1, 1, 1, 0.18)
                clip: true

                TextArea {
                    id: sendField
                    objectName: "sendField"
                    x: -scrollX
                    width: Math.max(sendBox.width, contentWidth + leftPadding + rightPadding + 2)
                    height: sendBox.height
                    leftPadding: 8
                    rightPadding: 8
                    topPadding: 0
                    bottomPadding: 0
                    verticalAlignment: TextEdit.AlignVCenter
                    wrapMode: TextEdit.NoWrap
                    background: null
                    color: textColor()
                    selectByMouse: true
                    font.pixelSize: root.chatFontSize
                    enabled: root.loggedIn
                    placeholderText: root.loggedIn ? tr2("Nachricht an ", "Message ") + (controller ? controller.channelDisplay : "")
                                                   : tr2("Zum Schreiben bei Kick anmelden (☰ → Anmelden)", "Log in to Kick to chat (☰ → Log in)")
                    placeholderTextColor: muted()
                    property real scrollX: 0
                    property var spellRanges: []          // [{start,length,word}] unbekannte Wörter
                    property var grammarRanges: []        // [{start,length,message,messageEn,replacements,source}]
                    property string grammarText: ""       // Text, zu dem grammarRanges gehören
                    onGrammarRangesChanged: spellLine.requestPaint()
                    property var predictions: []          // Wortvorschläge
                    onSpellRangesChanged: spellLine.requestPaint()
                    onContentWidthChanged: spellLine.requestPaint()
                    onActiveFocusChanged: Qt.callLater(root.requestSpellCheck)
                    onCursorPositionChanged: {
                        Qt.callLater(root.requestSpellCheck)
                        Qt.callLater(root.refreshPredictions)
                    }
                    HoverHandler { id: sendHover }
                    // Eigenes Menü (Vorschläge, Wörterbuch); das Standardmenü des Stils bleibt aus.
                    ContextMenu.menu: null
                    ContextMenu.onRequested: root.openSpellMenu()
                    property var emoteRanges: []          // [{start,length,src,anim,width}]
                    property string emoteFingerprint: "[]"
                    property int emoteLayoutRevision: 0
                    readonly property real inlineEmoteHeight: Math.min(sendBox.height - 4, Math.round(root.chatFontSize * 1.9))
                    onInlineEmoteHeightChanged: Qt.callLater(root.refreshInputEmotes)

                    function keepCursorVisible() {
                        var c = cursorRectangle.x
                        var next = scrollX
                        if (c - next > sendBox.width - rightPadding) next = c - sendBox.width + rightPadding + 1
                        else if (c - next < leftPadding) next = c - leftPadding
                        scrollX = Math.max(0, Math.min(next, width - sendBox.width))
                    }
                    onCursorRectangleChanged: { keepCursorVisible(); spellLine.requestPaint() }
                    onWidthChanged: keepCursorVisible()

                    Keys.onReturnPressed: root.doSend()
                    Keys.onEnterPressed: root.doSend()
                    onTextChanged: {
                        // Eine Zeile, höchstens 500 Zeichen
                        if (/[\n\r\t]/.test(text)) {
                            var cur = cursorPosition
                            text = text.replace(/[\n\r\t]/g, " ")
                            cursorPosition = Math.min(cur, text.length)
                            return
                        }
                        if (text.length > 500) { text = text.substring(0, 500); cursorPosition = 500; return }
                        var p = root.mentionPrefix()
                        mentionBar.candidates = p === null || !controller ? [] : controller.mentionCandidates(p)
                        Qt.callLater(root.refreshInputEmotes)
                        root.requestSpellCheck()
                        Qt.callLater(root.refreshPredictions)
                    }
                    Keys.onTabPressed: function (event) {
                        if (mentionBar.candidates.length > 0) root.applyMention(mentionBar.candidates[0])
                        else if (sendField.predictions.length > 0) root.acceptPrediction(sendField.predictions[0])
                        event.accepted = true
                    }
                    Keys.onEscapePressed: {
                        if (root.replyId !== "" || mentionBar.candidates.length > 0) { root.replyId = ""; mentionBar.candidates = [] }
                        else { sendField.focus = false; if (root.pressWindow) root.pressWindow.contentItem.forceActiveFocus() }
                    }

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
                                // Bereich noch vom vorigen Text (die neuen Bereiche kommen gleich): nicht abfragen
                                readonly property bool valid: modelData.start + modelData.length <= sendField.text.length
                                readonly property rect from: {
                                    sendField.cursorRectangle; sendField.contentWidth; sendField.emoteLayoutRevision
                                    return modelData.start + modelData.length <= sendField.text.length
                                        ? sendField.positionToRectangle(modelData.start) : Qt.rect(0, 0, 0, 0)
                                }
                                readonly property rect to: {
                                    sendField.cursorRectangle; sendField.contentWidth; sendField.emoteLayoutRevision
                                    return modelData.start + modelData.length <= sendField.text.length
                                        ? sendField.positionToRectangle(modelData.start + modelData.length) : Qt.rect(0, 0, 0, 0)
                                }
                                visible: valid
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
                                    Image { source: modelData.src; fillMode: Image.PreserveAspectFit; asynchronous: true; mipmap: true }
                                }
                                Component {
                                    id: animatedInline
                                    AnimatedImage { source: modelData.src; fillMode: Image.PreserveAspectFit }
                                }
                            }
                        }
                    }

                    // Rote Wellenlinie unter unbekannten Wörtern
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
                            if (sendField.grammarText === sendField.text) drawWaves(ctx, sendField.grammarRanges, root.grammarColor)
                            drawWaves(ctx, sendField.spellRanges, errorColor())
                        }
                        function drawWaves(ctx, ranges, color) {
                            ctx.strokeStyle = color
                            for (var i = 0; i < ranges.length; ++i) {
                                if (ranges[i].start + ranges[i].length > sendField.length) continue
                                var a = sendField.positionToRectangle(ranges[i].start)
                                var b = sendField.positionToRectangle(ranges[i].start + ranges[i].length)
                                var left = a.x, right = b.x
                                var y = a.y + a.height - 1
                                ctx.beginPath()
                                ctx.moveTo(left, y)
                                for (var px = left, up = true; px < right; px += 2, up = !up)
                                    ctx.lineTo(Math.min(px + 2, right), up ? y - 2 : y)
                                ctx.stroke()
                            }
                        }
                    }
                }
            }
            Button {
                id: sendButton
                objectName: "sendButton"
                text: root.loggedIn ? tr2("Senden", "Send") : tr2("Anmelden", "Log in")
                onClicked: root.loggedIn ? root.doSend() : loginPopup.open()
            }
        }
    }

    // ———————————————————————————————————————————— Menüs
    Menu {
        id: optionsMenu
        MenuItem {
            text: tr2("Auto-Kanal (aus laufendem Kick-Stream)", "Auto channel (from playing Kick stream)")
            checkable: true
            checked: controller ? controller.autoChannel : false
            onTriggered: controller.autoChannel = checked
        }
        MenuItem {
            text: tr2("Auto-Scroll", "Auto scroll")
            checkable: true
            checked: controller ? controller.autoScroll : true
            onTriggered: controller.autoScroll = checked
        }
        MenuItem {
            text: tr2("Uhrzeit anzeigen", "Show timestamps")
            checkable: true
            checked: controller ? controller.showTimestamps : false
            onTriggered: controller.showTimestamps = checked
        }
        MenuSeparator {}
        MenuItem {
            text: tr2("Rechtschreibung: Deutsch", "Spelling: German")
            checkable: true
            checked: controller ? controller.spellGerman : true
            onTriggered: controller.spellGerman = checked
        }
        MenuItem {
            text: tr2("Rechtschreibung: Englisch", "Spelling: English")
            checkable: true
            checked: controller ? controller.spellEnglish : true
            onTriggered: controller.spellEnglish = checked
        }
        MenuItem {
            text: tr2("Wortvorschläge beim Tippen", "Word suggestions while typing")
            checkable: true
            checked: controller ? controller.predictText : true
            onTriggered: controller.predictText = checked
        }
        MenuItem {
            text: tr2("Satzbau prüfen (lokal)", "Check grammar (local)")
            checkable: true
            checked: controller ? controller.grammarCheck : true
            onTriggered: controller.grammarCheck = checked
        }
        MenuItem {
            text: tr2("Satzbau online mit LanguageTool (sendet Text)", "Grammar online via LanguageTool (sends text)")
            checkable: true
            enabled: controller && controller.grammarCheck
            checked: controller ? controller.grammarOnline : false
            onTriggered: controller.grammarOnline = checked
        }
        MenuSeparator {}
        MenuItem {
            text: tr2("Benachrichtigungen & Boxen …", "Notifications & boxes …")
            onTriggered: notifyPopup.open()
        }
        MenuSeparator {}
        MenuItem {
            text: root.loggedIn ? tr2("Angemeldet als ", "Logged in as ") + controller.user + " …" : tr2("Bei Kick anmelden …", "Log in to Kick …")
            onTriggered: loginPopup.open()
        }
        MenuItem {
            text: tr2("Neu verbinden", "Reconnect")
            onTriggered: controller.reconnect()
        }
        MenuItem {
            text: tr2("Chat leeren (nur Anzeige)", "Clear chat (display only)")
            onTriggered: controller.clearHistory()
        }
        MenuItem {
            enabled: controller && controller.channel !== ""
            text: tr2("Kanal auf kick.com öffnen", "Open channel on kick.com")
            onTriggered: controller.openInBrowser("https://kick.com/" + controller.channel)
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
            visible: spellMenu.target !== null && spellMenu.suggestions.length === 0
            height: visible ? implicitHeight : 0
            enabled: false
            text: spellMenu.loading ? tr2("Suche Vorschläge …", "Looking for suggestions …") : tr2("Keine Vorschläge", "No suggestions")
        }
        MenuItem {
            objectName: "spellAddWord"
            visible: spellMenu.target !== null && spellMenu.target.kind !== "grammar"
            height: visible ? implicitHeight : 0
            text: tr2("Zum Wörterbuch hinzufügen", "Add to dictionary")
            onTriggered: if (controller && spellMenu.target) controller.addSpellWord(spellMenu.target.word)
        }
        MenuSeparator { visible: spellMenu.target !== null; height: visible ? implicitHeight : 0 }
        MenuItem { text: tr2("Ausschneiden", "Cut"); enabled: sendField.selectedText.length > 0; onTriggered: sendField.cut() }
        MenuItem { text: tr2("Kopieren", "Copy"); enabled: sendField.selectedText.length > 0; onTriggered: sendField.copy() }
        MenuItem { text: tr2("Einfügen", "Paste"); enabled: sendField.canPaste; onTriggered: sendField.paste() }
        MenuSeparator {}
        MenuItem {
            checkable: true
            checked: controller ? controller.spellGerman : false
            text: tr2("Deutsch prüfen", "Check German")
            onTriggered: if (controller) controller.spellGerman = checked
        }
        MenuItem {
            checkable: true
            checked: controller ? controller.spellEnglish : false
            text: tr2("Englisch prüfen (US + britisch)", "Check English (US + British)")
            onTriggered: if (controller) controller.spellEnglish = checked
        }
        MenuItem {
            checkable: true
            checked: controller ? controller.predictText : false
            text: tr2("Wortvorschläge", "Word suggestions")
            onTriggered: if (controller) controller.predictText = checked
        }
        MenuItem {
            checkable: true
            checked: controller ? controller.grammarCheck : false
            text: tr2("Satzbau prüfen", "Check grammar")
            onTriggered: if (controller) controller.grammarCheck = checked
        }
        MenuItem {
            checkable: true
            enabled: controller && controller.grammarCheck
            checked: controller ? controller.grammarOnline : false
            text: tr2("Satzbau online (LanguageTool – sendet den Text)", "Grammar online (LanguageTool – sends the text)")
            onTriggered: if (controller) controller.grammarOnline = checked
        }
    }

    Menu {
        id: chatMenu
        property var msg: null
        function openFor(m, item) { msg = m; popup() }
        MenuItem {
            text: tr2("Antworten", "Reply")
            enabled: chatMenu.msg && chatMenu.msg.id !== "" && chatMenu.msg.type !== "system" && root.loggedIn
            onTriggered: { chattersPopup.close(); root.startReply(chatMenu.msg) }
        }
        MenuItem {
            text: tr2("Nutzer-Menü …", "User menu …")
            enabled: chatMenu.msg && chatMenu.msg.name !== "" && chatMenu.msg.type !== "system"
            onTriggered: userCard.openFor(chatMenu.msg)
        }
        MenuItem {
            text: tr2("Nachricht löschen (Mod)", "Delete message (mod)")
            visible: root.isMod
            height: visible ? implicitHeight : 0
            enabled: chatMenu.msg && chatMenu.msg.id !== "" && chatMenu.msg.type !== "system" && !chatMenu.msg.deleted
            onTriggered: controller.userAction("delete", { name: chatMenu.msg.name, messageId: chatMenu.msg.id })
        }
        MenuItem {
            objectName: "copySelection"
            text: tr2("Markierung kopieren", "Copy selection")
            visible: root.selectedChatText !== "" && chatMenu.msg && chatMenu.msg.id === root.selectingKey
            height: visible ? implicitHeight : 0
            onTriggered: root.copyText(root.selectedChatText)
        }
        MenuItem {
            text: tr2("Alles markieren", "Select all")
            enabled: chatMenu.msg && chatMenu.msg.raw !== ""
            onTriggered: root.setSelection(chatMenu.msg.id, chatMenu.msg.raw, 0, chatMenu.msg.raw.length)
        }
        MenuItem {
            text: tr2("Nachricht kopieren", "Copy message")
            enabled: chatMenu.msg && chatMenu.msg.id !== ""
            onTriggered: root.copyText(chatMenu.msg.raw)
        }
        MenuItem {
            text: tr2("Name kopieren", "Copy name")
            enabled: chatMenu.msg && chatMenu.msg.name !== ""
            onTriggered: root.copyText(chatMenu.msg.name)
        }
        MenuItem {
            text: tr2("@Erwähnen", "@Mention")
            enabled: chatMenu.msg && chatMenu.msg.name !== "" && root.loggedIn
            onTriggered: { chattersPopup.close(); root.insertText("@" + chatMenu.msg.name) }
        }
        MenuItem {
            text: tr2("Kanal der Person auf kick.com", "Open user's channel on kick.com")
            enabled: chatMenu.msg && chatMenu.msg.user !== ""
            onTriggered: controller.openInBrowser("https://kick.com/" + chatMenu.msg.user)
        }
    }

    // ———————————————————————————————————————————— Leute im Chat
    Popup {
        id: chattersPopup
        objectName: "chattersPopup"
        parent: root
        x: 0; y: 0
        width: root.width
        height: root.height
        modal: true
        focus: true
        padding: 10
        property var list: []
        function refresh() { list = controller ? controller.chatters(chatterSearch.text) : [] }
        function number(n) { return Number(n).toLocaleString(Qt.locale(tr2("de_DE", "en_US")), "f", 0) }
        function ago(ts) {
            var s = Math.max(0, Math.round((Date.now() - ts) / 1000))
            if (s < 60) return tr2("gerade eben", "just now")
            var m = Math.round(s / 60)
            if (m < 60) return tr2("vor %1 Min.", "%1 min ago").arg(m)
            return tr2("vor %1 Std.", "%1 h ago").arg(Math.round(m / 60))
        }
        onOpened: { chatterSearch.text = ""; refresh() }
        background: Rectangle { color: "#181b1d"; radius: 6; border.color: root.kickGreen }
        Connections {
            target: controller
            ignoreUnknownSignals: true
            function onChattersChanged() { if (chattersPopup.opened) chattersPopup.refresh() }
        }

        contentItem: ColumnLayout {
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Label {
                    objectName: "chattersTitle"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.bold: true
                    font.pixelSize: root.chatFontSize + 3
                    color: textColor()
                    text: !controller || controller.viewerCount < 0
                          ? (controller && !controller.streamLive && controller.channel !== "" ? tr2("Stream offline", "Stream offline") : tr2("Zuschauer", "Viewers"))
                          : tr2("%1 Zuschauer", "%1 viewers").arg(chattersPopup.number(controller.viewerCount))
                }
                ToolButton { text: "✕"; implicitWidth: 30; onClicked: chattersPopup.close() }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: muted()
                font.pixelSize: Math.max(9, root.chatFontSize - 2)
                text: tr2("%1 Leute im Chat – alle, die seit dem Verbinden geschrieben haben. Eine Liste aller Zuschauer gibt Kick nicht heraus.",
                          "%1 people in chat – everyone who wrote since connecting. Kick doesn't publish a list of all viewers.")
                      .arg(controller ? chattersPopup.number(controller.chatterCount) : "0")
            }
            TextField {
                id: chatterSearch
                objectName: "chatterSearch"
                Layout.fillWidth: true
                placeholderText: tr2("Suchen …", "Search …")
                selectByMouse: true
                onTextChanged: chattersPopup.refresh()
            }
            ListView {
                id: chatterList
                objectName: "chatterList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: chattersPopup.list
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    width: chatterList.width - 8
                    height: chatterRow.implicitHeight + 6
                    color: rowHover.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                    radius: 4
                    HoverHandler { id: rowHover }
                    RowLayout {
                        id: chatterRow
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 4; rightMargin: 4 }
                        spacing: 6
                        Rectangle {
                            visible: modelData.roleLabel !== ""
                            radius: 3
                            color: modelData.role === "broadcaster" ? "#e9113c" : modelData.role === "moderator" ? "#00c73c"
                                 : modelData.role === "vip" ? "#e005b9" : modelData.role === "og" ? "#f0a500"
                                 : modelData.role === "founder" ? "#d4af37" : root.kickGreen
                            implicitWidth: roleText.implicitWidth + 6
                            implicitHeight: roleText.implicitHeight + 2
                            Text { id: roleText; anchors.centerIn: parent; text: modelData.roleLabel; color: "#0b0e0f"; font.bold: true; font.pixelSize: Math.max(8, root.chatFontSize - 4) }
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: modelData.self ? tr2("%1 (du)", "%1 (you)").arg(modelData.name) : modelData.name
                            color: modelData.color && modelData.color.length === 7 ? modelData.color : textColor()
                            font.bold: true
                            font.pixelSize: root.chatFontSize
                        }
                        Label {
                            visible: modelData.count > 0
                            text: modelData.count + " · " + chattersPopup.ago(modelData.lastTs)
                            color: muted()
                            font.pixelSize: Math.max(8, root.chatFontSize - 3)
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: userCard.openFor({ id: "", type: "chat", name: modelData.name, user: modelData.user,
                                                      userId: "", color: modelData.color, raw: "" })
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                visible: chattersPopup.list.length === 0
                wrapMode: Text.WordWrap
                color: muted()
                text: chatterSearch.text !== "" ? tr2("Niemand gefunden.", "Nobody found.")
                                                : tr2("Noch hat niemand geschrieben.", "Nobody has written yet.")
            }
        }
    }

    // ———————————————————————————————————————————— Nutzer-Karte (Klick auf einen Namen)
    component SmallButton: Rectangle {
        property string label
        property color tone: textColor()
        property bool danger: false
        signal activated()
        width: sbText.implicitWidth + 14
        height: sbText.implicitHeight + 8
        radius: 4
        color: sbHover.hovered ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.04)
        border.color: danger ? errorColor() : Qt.rgba(1, 1, 1, 0.18)
        Text {
            id: sbText
            anchors.centerIn: parent
            text: parent.label
            color: parent.danger ? errorColor() : parent.tone
            font.pixelSize: 12
        }
        HoverHandler { id: sbHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: parent.activated() }
    }

    Popup {
        id: userCard
        objectName: "userCard"
        parent: root
        x: 8
        y: Math.max(8, (root.height - height) / 4)
        width: root.width - 16
        modal: true
        focus: true
        padding: 12
        property var msg: ({})
        property var info: ({})
        property bool loading: false
        property string confirm: ""            // ban | block | unban
        readonly property string targetId: (msg && msg.userId) ? msg.userId : (info.id || "")
        readonly property bool isSelf: root.loggedIn && msg && msg.name && controller && msg.name.toLowerCase() === controller.user.toLowerCase()
        readonly property bool targetProtected: info.role === "broadcaster" || (info.role === "moderator" && controller && controller.myRole !== "broadcaster")
        readonly property bool canModerate: root.isMod && !isSelf && !targetProtected
        background: Rectangle { color: "#181b1d"; radius: 8; border.color: root.kickGreen }

        function openFor(m) {
            msg = m
            info = ({})
            confirm = ""
            loading = true
            banReason.text = ""
            controller.requestUserInfo(m.name)
            open()
        }
        function fmtDate(ms) {
            if (!ms) return "–"
            return new Date(ms).toLocaleDateString(Qt.locale(tr2("de_DE", "en_US")), Locale.ShortFormat)
        }
        function age(ms) {
            if (!ms) return ""
            var days = Math.floor((Date.now() - ms) / 86400000)
            if (days < 1) return tr2("heute", "today")
            if (days < 60) return tr2("%1 Tage", "%1 days").arg(days)
            var months = Math.floor(days / 30.4)
            if (months < 24) return tr2("%1 Monate", "%1 months").arg(months)
            return tr2("%1 Jahre", "%1 years").arg(Math.floor(days / 365.25))
        }
        function act(action, extra) {
            var t = { name: msg.name, userId: targetId, messageId: msg.id || "" }
            for (var k in extra) t[k] = extra[k]
            controller.userAction(action, t)
            close()
        }
        Connections {
            target: controller
            ignoreUnknownSignals: true
            function onUserInfo(name, info) {
                if (userCard.msg && name === userCard.msg.name) { userCard.info = info; userCard.loading = false }
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Rectangle {
                    width: 48; height: 48; radius: 24
                    color: Qt.rgba(1, 1, 1, 0.08)
                    clip: true
                    Image {
                        anchors.fill: parent
                        source: userCard.info.avatar || ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        visible: status === Image.Ready
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: !userCard.info.avatar
                        text: userCard.msg && userCard.msg.name ? userCard.msg.name.charAt(0).toUpperCase() : ""
                        color: textColor(); font.pixelSize: 20; font.bold: true
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        text: userCard.msg ? (userCard.msg.name || "") : ""
                        color: userCard.msg && userCard.msg.color ? userCard.msg.color : root.kickGreen
                        font.bold: true; font.pixelSize: 16
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                    Label {
                        Layout.fillWidth: true
                        color: muted(); font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        text: {
                            var r = userCard.info.role
                            var role = r === "broadcaster" ? "Streamer" : r === "moderator" ? tr2("Moderator", "Moderator")
                                     : r === "vip" ? "VIP" : ""
                            var list = []
                            var all = userCard.info.badges || []
                            for (var i = 0; i < all.length; ++i)
                                if (all[i] !== role && list.indexOf(all[i]) < 0) list.push(all[i])   // „Moderator“ nicht doppelt
                            var b = list.join(" · ")
                            if (userCard.loading) return tr2("Lade …", "Loading …")
                            if (userCard.info.error) return tr2("Konto-Infos nicht abrufbar", "Account info unavailable")
                            return [role, b].filter(function (x) { return x !== "" }).join(" · ")
                        }
                    }
                }
                ToolButton { text: "✕"; implicitWidth: 28; Layout.alignment: Qt.AlignTop; onClicked: userCard.close() }
            }
            GridLayout {
                Layout.fillWidth: true
                visible: !userCard.loading && !userCard.info.error
                columns: 2
                columnSpacing: 10
                rowSpacing: 2
                Label { text: tr2("Konto erstellt:", "Account created:"); color: muted(); font.pixelSize: 12 }
                Label { text: userCard.fmtDate(userCard.info.createdAt) + (userCard.info.createdAt ? " (" + userCard.age(userCard.info.createdAt) + ")" : ""); color: textColor(); font.pixelSize: 12 }
                Label { text: tr2("Folgt seit:", "Following since:"); color: muted(); font.pixelSize: 12 }
                Label { text: userCard.info.followingSince ? userCard.fmtDate(userCard.info.followingSince) + " (" + userCard.age(userCard.info.followingSince) + ")" : tr2("folgt nicht", "not following"); color: textColor(); font.pixelSize: 12 }
                Label { text: tr2("Abo:", "Subscription:"); color: muted(); font.pixelSize: 12 }
                Label { text: userCard.info.subscribedFor > 0 ? tr2("%1 Monat(e)", "%1 month(s)").arg(userCard.info.subscribedFor) : tr2("kein Abo", "none"); color: textColor(); font.pixelSize: 12 }
                Label { visible: !!userCard.info.banned; text: tr2("Status:", "Status:"); color: muted(); font.pixelSize: 12 }
                Label {
                    visible: !!userCard.info.banned
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: errorColor(); font.pixelSize: 12
                    text: userCard.info.bannedPermanent ? tr2("gebannt", "banned") + (userCard.info.bannedReason ? " – " + userCard.info.bannedReason : "")
                          : tr2("Timeout bis ", "Timed out until ") + (userCard.info.bannedUntil ? new Date(userCard.info.bannedUntil).toLocaleTimeString(Qt.locale(), Locale.ShortFormat) : "")
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: 6
                SmallButton {
                    label: tr2("@Erwähnen", "@Mention")
                    visible: root.loggedIn && !userCard.isSelf
                    onActivated: { root.insertText("@" + userCard.msg.name); userCard.close() }
                }
                SmallButton {
                    label: tr2("Antworten", "Reply")
                    visible: root.loggedIn && userCard.msg && !!userCard.msg.id && userCard.msg.type !== "system"
                    onActivated: { root.startReply(userCard.msg); userCard.close() }
                }
                SmallButton {
                    label: tr2("Name kopieren", "Copy name")
                    onActivated: { root.copyText(userCard.msg.name); userCard.close() }
                }
                SmallButton {
                    label: tr2("Kanal auf kick.com", "Channel on kick.com")
                    onActivated: controller.openInBrowser("https://kick.com/" + (userCard.info.slug || userCard.msg.user || userCard.msg.name))
                }
                SmallButton {
                    visible: !userCard.isSelf
                    label: controller && userCard.msg && controller.blocked.indexOf((userCard.msg.name || "").toLowerCase()) >= 0
                           ? tr2("Blockierung aufheben", "Unblock") : tr2("Blockieren", "Block")
                    danger: label === tr2("Blockieren", "Block")
                    onActivated: {
                        if (label === tr2("Blockieren", "Block")) userCard.confirm = "block"
                        else userCard.act("unblock", {})
                    }
                }
            }

            // Moderation (nur Mods/Streamer, nicht gegen Streamer/Mods)
            Label {
                visible: userCard.canModerate
                text: tr2("Moderation", "Moderation")
                color: muted(); font.pixelSize: 11; font.bold: true
            }
            Flow {
                Layout.fillWidth: true
                visible: userCard.canModerate
                spacing: 6
                SmallButton {
                    label: tr2("Nachricht löschen", "Delete message")
                    visible: userCard.msg && !!userCard.msg.id && !userCard.msg.deleted && userCard.msg.type !== "system"
                    onActivated: userCard.act("delete", {})
                }
                SmallButton { label: "Timeout 1 Min"; onActivated: userCard.act("timeout", { duration: 1 }) }
                SmallButton { label: "10 Min"; onActivated: userCard.act("timeout", { duration: 10 }) }
                SmallButton { label: tr2("1 Std", "1 h"); onActivated: userCard.act("timeout", { duration: 60 }) }
                SmallButton { label: tr2("24 Std", "24 h"); onActivated: userCard.act("timeout", { duration: 1440 }) }
                SmallButton { label: tr2("7 Tage", "7 days"); onActivated: userCard.act("timeout", { duration: 10080 }) }
                SmallButton { label: tr2("Bannen …", "Ban …"); danger: true; onActivated: userCard.confirm = "ban" }
                SmallButton {
                    label: tr2("Bann/Timeout aufheben", "Unban / remove timeout")
                    onActivated: userCard.act("unban", {})
                }
            }

            // Rückfrage
            Rectangle {
                Layout.fillWidth: true
                visible: userCard.confirm !== ""
                implicitHeight: confirmCol.implicitHeight + 12
                radius: 6
                color: Qt.rgba(1, 0.36, 0.36, 0.10)
                border.color: errorColor()
                ColumnLayout {
                    id: confirmCol
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6 }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: textColor()
                        text: userCard.confirm === "ban" ? tr2("%1 dauerhaft bannen?", "Ban %1 permanently?").arg(userCard.msg.name || "")
                              : tr2("%1 blockieren? Nachrichten werden dann ausgeblendet.", "Block %1? Their messages will be hidden.").arg(userCard.msg.name || "")
                    }
                    TextField {
                        id: banReason
                        Layout.fillWidth: true
                        visible: userCard.confirm === "ban"
                        maximumLength: 100
                        placeholderText: tr2("Grund (freiwillig)", "Reason (optional)")
                    }
                    RowLayout {
                        Item { Layout.fillWidth: true }
                        Button { text: tr2("Abbrechen", "Cancel"); onClicked: userCard.confirm = "" }
                        Button {
                            text: userCard.confirm === "ban" ? tr2("Bannen", "Ban") : tr2("Blockieren", "Block")
                            onClicked: userCard.act(userCard.confirm, { reason: banReason.text })
                        }
                    }
                }
            }
        }
    }

    // Farbwahl: Palette-Punkte plus eigenes Feld für #RRGGBB (Enter oder ✓ übernimmt)
    component ColorPicker: Column {
        id: picker
        property string current: ""
        property bool allowDefault: false        // „Standard“-Punkt (leere Farbe)
        signal picked(string color)
        spacing: 4
        Flow {
            width: picker.width
            spacing: 4
            Rectangle {
                visible: picker.allowDefault
                width: 20; height: 20; radius: 10
                color: "transparent"
                border.width: picker.current === "" ? 3 : 1
                border.color: picker.current === "" ? "white" : muted()
                Text { anchors.centerIn: parent; text: "–"; color: muted(); font.pixelSize: 11 }
                TapHandler { onTapped: picker.picked("") }
                HoverHandler { id: defHover }
                ToolTip.visible: defHover.hovered
                ToolTip.text: tr2("Standardfarbe der Bot-Box", "Default bot box colour")
            }
            Repeater {
                model: notifyPopup.colorChoices
                delegate: Rectangle {
                    width: 20; height: 20; radius: 10
                    color: modelData
                    border.width: picker.current.toLowerCase() === modelData.toLowerCase() ? 3 : 1
                    border.color: picker.current.toLowerCase() === modelData.toLowerCase() ? "white" : Qt.rgba(0, 0, 0, 0.4)
                    TapHandler { onTapped: picker.picked(modelData) }
                }
            }
        }
        // eigene Farbe als #RRGGBB
        Row {
            spacing: 6
            Label { text: tr2("Eigene:", "Custom:"); color: muted(); font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 20; height: 20; radius: 10
                color: /^#?[0-9a-fA-F]{6}$/.test(hexField.text) ? (hexField.text.charAt(0) === "#" ? hexField.text : "#" + hexField.text) : "transparent"
                border.width: 1
                border.color: muted()
            }
            TextField {
                id: hexField
                objectName: "hexField"
                width: 92
                font.pixelSize: 12
                placeholderText: "#RRGGBB"
                maximumLength: 7
                text: picker.current
                selectByMouse: true
                validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{0,6}/ }
                function apply() {
                    var t = text.trim()
                    if (t.length && t.charAt(0) !== "#") t = "#" + t
                    if (/^#[0-9a-fA-F]{6}$/.test(t)) picker.picked(t.toLowerCase())
                }
                onAccepted: apply()
            }
            Button {
                text: tr2("Übernehmen", "Apply")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: hexField.apply()
            }
        }
    }

    // ———————————————————————————————————————————— Benachrichtigungen & Boxen
    Popup {
        id: notifyPopup
        objectName: "notifyPopup"
        parent: root
        x: 0; y: 0
        width: root.width
        height: root.height
        modal: true
        focus: true
        padding: 10
        background: Rectangle { color: "#181b1d"; radius: 6; border.color: root.kickGreen }
        readonly property var kinds: [
            { k: "mention", de: "Erwähnung / Antwort an dich", en: "Mention / reply to you" },
            { k: "system", de: "Abos, Geschenke, Hosts, Live", en: "Subs, gifts, hosts, live" },
            { k: "follow", de: "Neue Follower", en: "New followers" },
            { k: "kicks", de: "Kicks (Geschenke)", en: "Kicks (gifts)" },
            { k: "bot", de: "Event-Bots", en: "Event bots" },
            { k: "mod", de: "Moderation (Bann, Timeout, Chat geleert)", en: "Moderation (ban, timeout, clear)" }
        ]
        property string editingBot: ""
        onClosed: editingBot = ""
        readonly property var colorChoices: ["#53fc18", "#f0a500", "#4a9eff", "#ff7ac6", "#a970ff", "#ff5c5c", "#00d1b2", "#ffd400", "#9a9a9a"]

        contentItem: ColumnLayout {
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: tr2("Benachrichtigungen & Boxen", "Notifications & boxes")
                    font.bold: true; font.pixelSize: 15; color: root.kickGreen
                }
                ToolButton {
                    objectName: "soundToggle"
                    readonly property bool on: controller ? controller.notifySound : true
                    text: on ? "🔔" : "🔕"
                    implicitWidth: 30
                    opacity: on ? 1 : 0.6
                    Accessible.name: on ? tr2("Ton ausschalten", "Turn sound off") : tr2("Ton einschalten", "Turn sound on")
                    ToolTip.visible: hovered
                    ToolTip.text: on ? tr2("Ton bei Erwähnungen: an (klicken zum Ausschalten)", "Sound on mentions: on (click to turn off)")
                                     : tr2("Ton bei Erwähnungen: aus (klicken zum Einschalten)", "Sound on mentions: off (click to turn on)")
                    onClicked: if (controller) controller.notifySound = !on
                }
                ToolButton { text: "✕"; implicitWidth: 30; onClicked: notifyPopup.close() }
            }
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: width
                contentHeight: notifyCol.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: notifyCol
                    width: parent.width - 12
                    spacing: 8

                    Label { text: tr2("Ton", "Sound"); color: muted(); font.bold: true; font.pixelSize: 12 }
                    CheckBox {
                        objectName: "soundSwitch"
                        text: tr2("Ton bei Erwähnung oder Antwort an dich", "Sound on mention or reply to you")
                        checked: controller ? controller.notifySound : false
                        onToggled: controller.notifySound = checked
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: tr2("Lautstärke", "Volume"); color: textColor(); font.pixelSize: 12 }
                        Slider {
                            id: volumeSlider
                            Layout.fillWidth: true
                            from: 0; to: 100; stepSize: 5
                            value: controller ? controller.notifyVolume : 30
                            onMoved: controller.notifyVolume = value
                            onPressedChanged: if (!pressed) controller.previewNotifySound()
                        }
                        Label { text: Math.round(volumeSlider.value) + " %"; color: muted(); font.pixelSize: 12 }
                        Button { text: "▶"; implicitWidth: 34; onClicked: controller.previewNotifySound() }
                    }
                    CheckBox {
                        text: tr2("Neue Follower im Chat anzeigen", "Show new followers in chat")
                        checked: controller ? controller.showFollows : true
                        onToggled: controller.showFollows = checked
                    }

                    Label { text: tr2("Farben der Boxen", "Box colours"); color: muted(); font.bold: true; font.pixelSize: 12 }
                    Repeater {
                        model: notifyPopup.kinds
                        delegate: ColumnLayout {
                            id: kindRow
                            Layout.fillWidth: true
                            spacing: 2
                            readonly property string kind: modelData.k
                            readonly property string current: controller ? controller.boxColors[kind] : "#f0a500"
                            Label { text: tr2(modelData.de, modelData.en); color: textColor(); font.pixelSize: 12 }
                            ColorPicker {
                                Layout.fillWidth: true
                                current: kindRow.current
                                onPicked: function (color) { controller.setBoxColor(kindRow.kind, color) }
                            }
                        }
                    }
                    Button { text: tr2("Farben zurücksetzen", "Reset colours"); onClicked: controller.resetBoxColors() }

                    Label { text: tr2("Event-Bots", "Event bots"); color: muted(); font.bold: true; font.pixelSize: 12 }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: muted(); font.pixelSize: 11
                        text: tr2("Nachrichten dieser Konten stehen in einer eigenen Box mit dem Etikett.", "Messages from these accounts get their own box with the label.")
                    }
                    Repeater {
                        model: controller ? controller.eventBots : []
                        delegate: ColumnLayout {
                            id: botRow
                            objectName: "botRow"
                            Layout.fillWidth: true
                            spacing: 4
                            property bool editing: notifyPopup.editingBot === modelData.name
                            property string draftColor: modelData.color
                            RowLayout {
                                Layout.fillWidth: true
                                Rectangle {
                                    width: botPillText.implicitWidth + 8; height: botPillText.implicitHeight + 2; radius: 3
                                    color: modelData.color ? modelData.color : (controller ? controller.boxColors["bot"] : "#a970ff")
                                    Text { id: botPillText; anchors.centerIn: parent; text: modelData.label; color: "#0b0e0f"; font.bold: true; font.pixelSize: 10 }
                                }
                                Label { Layout.fillWidth: true; text: modelData.name; color: textColor(); font.pixelSize: 12; elide: Text.ElideRight }
                                ToolButton {
                                    objectName: "botEdit"
                                    text: "✎"; implicitWidth: 26
                                    onClicked: {
                                        if (botRow.editing) { notifyPopup.editingBot = ""; return }
                                        notifyPopup.editingBot = modelData.name
                                        botRow.draftColor = modelData.color
                                        editName.text = modelData.name
                                        editLabel.text = modelData.label
                                    }
                                    ToolTip.visible: hovered; ToolTip.text: tr2("Bearbeiten", "Edit")
                                }
                                ToolButton { text: "✕"; implicitWidth: 26; onClicked: controller.removeEventBot(modelData.name) }
                            }
                            // Bearbeiten: Name, Etikett, Farbe (Palette oder eigene)
                            Rectangle {
                                Layout.fillWidth: true
                                visible: botRow.editing
                                implicitHeight: visible ? editCol.implicitHeight + 12 : 0
                                radius: 6
                                color: Qt.rgba(1, 1, 1, 0.05)
                                border.color: Qt.rgba(1, 1, 1, 0.15)
                                ColumnLayout {
                                    id: editCol
                                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6 }
                                    spacing: 4
                                    RowLayout {
                                        Layout.fillWidth: true
                                        TextField { id: editName; Layout.fillWidth: true; placeholderText: tr2("Kick-Name", "Kick name") }
                                        TextField { id: editLabel; Layout.preferredWidth: 100; placeholderText: tr2("Etikett", "Label") }
                                    }
                                    Label { text: tr2("Farbe der Box", "Box colour"); color: muted(); font.pixelSize: 11 }
                                    ColorPicker {
                                        Layout.fillWidth: true
                                        allowDefault: true
                                        current: botRow.draftColor
                                        onPicked: function (color) { botRow.draftColor = color }
                                    }
                                    RowLayout {
                                        Item { Layout.fillWidth: true }
                                        Button { text: tr2("Abbrechen", "Cancel"); onClicked: notifyPopup.editingBot = "" }
                                        Button {
                                            text: tr2("Speichern", "Save")
                                            highlighted: true
                                            onClicked: {
                                                if (controller.setEventBot(editName.text, editLabel.text, botRow.draftColor, modelData.name))
                                                    notifyPopup.editingBot = ""
                                                else root.showAction(false, tr2("✗ Ungültiger oder doppelter Name", "✗ Invalid or duplicate name"))
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Label { text: tr2("Bot hinzufügen", "Add bot"); color: muted(); font.pixelSize: 11 }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: botName; Layout.fillWidth: true; placeholderText: tr2("Kick-Name", "Kick name") }
                        TextField { id: botLabel; Layout.preferredWidth: 90; placeholderText: tr2("Etikett", "Label") }
                    }
                    ColorPicker {
                        id: newBotColor
                        Layout.fillWidth: true
                        allowDefault: true
                        property string chosen: ""
                        current: chosen
                        onPicked: function (color) { chosen = color }
                    }
                    Button {
                        text: tr2("+ Hinzufügen", "+ Add")
                        onClicked: if (controller.setEventBot(botName.text, botLabel.text, newBotColor.chosen)) {
                            botName.text = ""; botLabel.text = ""; newBotColor.chosen = ""
                        }
                    }
                    Button { text: tr2("Standard-Bots wiederherstellen", "Restore default bots"); onClicked: controller.resetEventBots() }

                    Label { text: tr2("Blockierte Nutzer", "Blocked users"); color: muted(); font.bold: true; font.pixelSize: 12 }
                    Label {
                        visible: !controller || controller.blocked.length === 0
                        text: tr2("Niemand blockiert. (Klick auf einen Namen → Blockieren)", "Nobody blocked. (Click a name → Block)")
                        color: muted(); font.pixelSize: 11
                    }
                    Repeater {
                        model: controller ? controller.blocked : []
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: modelData; color: textColor(); font.pixelSize: 12 }
                            Button { text: tr2("Aufheben", "Unblock"); onClicked: controller.userAction("unblock", { name: modelData }) }
                        }
                    }
                    Item { height: 6 }
                }
            }
        }
    }

    // ———————————————————————————————————————————— Anmeldung
    Popup {
        id: loginPopup
        objectName: "loginPopup"
        parent: root
        x: 8
        y: Math.max(8, (root.height - height) / 3)
        width: root.width - 16
        modal: true
        focus: true
        padding: 12
        background: Rectangle { color: "#181b1d"; radius: 8; border.color: root.kickGreen }

        ColumnLayout {
            width: parent.width
            spacing: 8
            Label {
                text: tr2("Bei Kick anmelden", "Log in to Kick")
                font.bold: true; font.pixelSize: 15; color: root.kickGreen
            }
            Label {
                visible: root.loggedIn
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: textColor()
                text: tr2("Angemeldet als ", "Logged in as ") + (controller ? controller.user : "")
            }
            Label {
                visible: !root.loggedIn
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: textColor()
                text: tr2("Mitlesen geht ohne Anmeldung. Zum Schreiben auf „Anmelden“ klicken – kick.com öffnet sich im Browser, dort bestätigen, fertig.",
                          "Reading works without login. To chat, click \"Log in\" – kick.com opens in your browser, confirm there, done.")
            }
            Label {
                Layout.fillWidth: true
                visible: controller && controller.loginState !== "idle"
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                color: controller && controller.loginState === "error" ? errorColor() : textColor()
                text: {
                    if (!controller) return ""
                    if (controller.loginState === "waiting")
                        return tr2("Browser geöffnet – bitte dort bei Kick bestätigen …", "Browser opened – please confirm on Kick …")
                    if (controller.loginError === "credentials")
                        return tr2("Client-ID und Client-Secret fehlen.", "Client ID and Client Secret are missing.")
                    if (controller.loginError.indexOf("Port") === 0)
                        return tr2("Lokaler Anmelde-Port belegt: ", "Local login port in use: ") + controller.loginError
                    return tr2("Anmeldung fehlgeschlagen: ", "Login failed: ") + controller.loginError
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    visible: controller && controller.loginState === "waiting"
                    text: tr2("Abbrechen", "Cancel")
                    onClicked: controller.cancelLogin()
                }
                Button {
                    visible: root.loggedIn
                    text: tr2("Abmelden", "Log out")
                    onClicked: controller.logout()
                }
                Button {
                    visible: !root.loggedIn && controller && controller.loginState !== "waiting"
                    text: tr2("Anmelden", "Log in")
                    highlighted: true
                    onClicked: {
                        controller.startLogin()
                    }
                }
                Button { text: tr2("Schließen", "Close"); onClicked: loginPopup.close() }
            }
        }
        Connections {
            target: controller
            ignoreUnknownSignals: true
            function onAuthChanged() { if (root.loggedIn && loginPopup.opened) loginPopup.close() }
        }
    }
}
