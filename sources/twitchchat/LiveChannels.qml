import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Fenster „Live auf Twitch“ (modal.open, über dem Bild): Kanäle mit Vorschaubild, Kategorie und Zuschauerzahl,
// dazu Kategorien mit Cover. Ein Klick auf einen Kanal spielt ihn im Player ab und der Chat wechselt dorthin.
// Farben kommen aus dem Player-Theme (hell/dunkel); ohne Theme dunkel.
Item {
    id: root
    objectName: "liveChannels"
    property var controller
    property var theme
    property var plugin
    property var modal
    implicitWidth: 820
    implicitHeight: 560

    // ——— Farben (Player-Theme, sonst dunkle Vorgaben) ———
    readonly property bool dark: !theme || theme.dark === undefined ? true : !!theme.dark
    readonly property color cBg: theme && theme.panel ? theme.panel : (dark ? "#14171c" : "#ffffff")
    readonly property color cSurface: theme && theme.panel2 ? theme.panel2 : (dark ? "#1b1f26" : "#eceef1")
    readonly property color cSurfaceHover: dark ? Qt.lighter(cSurface, 1.35) : Qt.darker(cSurface, 1.06)
    readonly property color cLine: theme && theme.line ? theme.line : (dark ? "#262b34" : "#d6d9de")
    readonly property color cText: theme && theme.text ? theme.text : (dark ? "#e6e8eb" : "#1d2026")
    readonly property color cMuted: theme && theme.muted ? theme.muted : (dark ? "#8b93a1" : "#5d6573")
    readonly property color cAccent: theme && theme.accent ? theme.accent : "#9147ff"
    readonly property color cError: theme && theme.error ? theme.error : (dark ? "#ff6b6b" : "#c62828")
    readonly property color cLive: "#eb0400"

    function tr2(de, en) { return plugin && plugin.language === "de" ? de : en }
    readonly property string language: plugin && plugin.language ? plugin.language : "en"
    function fullCount(n) { return Number(n).toLocaleString(Qt.locale(language === "de" ? "de_DE" : "en_US"), "f", 0) }
    function compact(n) {
        n = Number(n) || 0
        var de = language === "de"
        function one(v) { var s = (Math.round(v * 10) / 10).toString(); return de ? s.replace(".", ",") : s }
        if (n >= 1000000) return one(n / 1000000) + (de ? " Mio." : "M")
        if (n >= 1000) return one(n / 1000) + (de ? " Tsd." : "K")
        return String(n)
    }

    // Sprachfilter für „Beliebt“ und Kanäle einer Kategorie – frei wählbar, unabhängig von der Player-Sprache, gespeichert.
    // Codes so, wie Twitch sie im Filter annimmt (geprüft: he/fa lehnt Twitch ab).
    property string filterLanguage: ""
    property string filterSort: "relevance"     // relevance = Twitch „Für dich empfohlen“, viewers = Zuschauer viel → wenig
    function setFilterSort(code) {
        filterSort = code
        if (controller) controller.setLiveSort(code)
        load(tab, controller ? controller.liveCategory : "")
    }
    readonly property var twitchLanguages: [
        { code: "ar", name: "العربية" }, { code: "asl", name: "American Sign Language" }, { code: "bg", name: "Български" },
        { code: "ca", name: "Català" }, { code: "cs", name: "Čeština" }, { code: "da", name: "Dansk" },
        { code: "de", name: "Deutsch" }, { code: "el", name: "Ελληνικά" }, { code: "en", name: "English" },
        { code: "es", name: "Español" }, { code: "fi", name: "Suomi" }, { code: "fr", name: "Français" },
        { code: "hi", name: "हिन्दी" }, { code: "hu", name: "Magyar" }, { code: "id", name: "Bahasa Indonesia" },
        { code: "it", name: "Italiano" }, { code: "ja", name: "日本語" }, { code: "ko", name: "한국어" },
        { code: "ms", name: "Bahasa Melayu" }, { code: "nl", name: "Nederlands" }, { code: "no", name: "Norsk" },
        { code: "pl", name: "Polski" }, { code: "pt", name: "Português" }, { code: "ro", name: "Română" },
        { code: "ru", name: "Русский" }, { code: "sk", name: "Slovenčina" }, { code: "sv", name: "Svenska" },
        { code: "th", name: "ไทย" }, { code: "tl", name: "Tagalog" }, { code: "tr", name: "Türkçe" },
        { code: "uk", name: "Українська" }, { code: "vi", name: "Tiếng Việt" }, { code: "zh", name: "中文" },
        { code: "zh_hk", name: "中文（香港）" }, { code: "other", name: "Other / Andere" }]
    function setFilterLanguage(code) {
        filterLanguage = code
        if (controller) controller.setLiveLanguage(code)
        load(tab, controller ? controller.liveCategory : "")
    }
    readonly property string tab: controller ? controller.liveTab : "top"
    property bool hasProfile: false
    Connections {
        target: root.controller
        function onLiveStreamsChanged() { if (root.controller && !root.controller.liveBusy) root.hasProfile = root.controller.hasLiveProfile() }
    }
    readonly property bool busy: !!controller && controller.liveBusy
    readonly property var streams: controller ? controller.liveStreams : []
    readonly property var categories: controller ? controller.liveCategories : []

    function load(t, query) {
        if (!controller) return
        if (t !== "search") searchDelay.stop()     // eine noch wartende Suche darf den neuen Reiter nicht überschreiben
        // Sprache nicht in der Kategorienliste und nicht bei „Gefolgt“ (dort alle Live-Kanäle);
        // „Gefolgt“ ohne „Für dich empfohlen“, dort immer nach Zuschauern (gespeicherte Sortierung bleibt für die anderen Reiter)
        var lang = (t === "categories" || t === "follow") ? "" : filterLanguage
        var sort = t === "follow" ? "viewers" : filterSort
        if (t === "search") controller.loadLiveStreams("search", searchField.text, lang, sort)
        else controller.loadLiveStreams(t, query || "", lang, sort)
        scroller.contentItem.contentY = 0
    }
    function openCategory(name) {
        // Klick merken: „Für dich empfohlen“ zeigt die meistgeklickten Kategorien zuerst
        if (controller && typeof controller.noteLiveCategory === "function") controller.noteLiveCategory(name)
        load("category", name)
    }
    function play(login, game) {
        // Kategorie mitgeben: daraus lernt „Für dich empfohlen“
        if (!controller || !controller.playLiveChannel(login, game || "")) return
        if (modal) modal.close(login)
    }
    function errorText(code) {
        if (code === "login") return tr2("Melde dich im Chat mit Twitch an, um deine gefolgten Kanäle zu sehen.",
                                         "Sign in with Twitch in the chat to see the channels you follow.")
        if (code === "scope") return tr2("Deine Anmeldung erlaubt keine Follows – bitte im Chat neu anmelden.",
                                         "Your sign-in does not allow follows – please sign in again in the chat.")
        if (code === "expired") return tr2("Anmeldung abgelaufen – bitte im Chat neu anmelden.", "Sign-in expired – please sign in again in the chat.")
        return tr2("Twitch ist gerade nicht erreichbar (%1).", "Twitch is unreachable right now (%1).").arg(code)
    }

    // Der Player setzt controller oft erst nach dem Erzeugen: dann laden. Standard: „Gefolgt“, wenn angemeldet
    function start() {
        if (!controller) return
        filterLanguage = controller.liveLanguage()     // zuletzt gewählte Sprache und Sortierung
        filterSort = controller.liveSort()
        load(controller.canListFollowed ? "follow" : "top")
    }
    Component.onCompleted: start()
    onControllerChanged: start()

    // ——— Bausteine ———
    // Bild mit runden Ecken ohne Shader: ein Rahmen in Hintergrundfarbe deckt die Ecken ab (geht auch im Software-Renderer)
    // Alles bleibt innerhalb des Elements: Das Bild sitzt um b eingerückt, der Rahmen liegt genau auf dem Elementrand
    // (äußerer Radius R) und deckt die Bildecken ab. Bildecke liegt r·√2 vom Bogenmittelpunkt → b ≥ R·(1 − 1/√2).
    // Für Kreise: radius = halbe Breite. Sichtbar ist das Bild mit Radius R − b.
    component RoundImage: Item {
        id: ri
        property alias source: img.source
        property real radius: 8                       // äußerer Radius (Elementrand)
        property color backdrop: root.cBg
        property color placeholder: root.dark ? "#0e1014" : "#d9dce1"
        readonly property real inset: Math.ceil(radius * 0.293) + 1
        readonly property real innerRadius: Math.max(0, radius - inset)
        Rectangle { anchors.fill: parent; anchors.margins: ri.inset; radius: ri.innerRadius; color: ri.placeholder }
        Image {
            id: img
            anchors.fill: parent
            anchors.margins: ri.inset
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
        }
        Rectangle {
            anchors.fill: parent
            radius: ri.radius
            color: "transparent"
            border.width: ri.inset
            border.color: ri.backdrop
        }
    }
    // Pille (Reiter, Schalter, Kategorie-Chip)
    component Pill: Rectangle {
        id: pill
        property string label
        property bool active: false
        property color activeColor: root.cAccent
        property int fontSize: 12
        signal clicked()
        implicitWidth: pillText.implicitWidth + 24
        implicitHeight: 30
        radius: height / 2
        color: active ? activeColor : (pillMouse.containsMouse ? root.cSurfaceHover : "transparent")
        Behavior on color { ColorAnimation { duration: 120 } }
        Text {
            id: pillText
            anchors.centerIn: parent
            text: pill.label
            color: pill.active ? "white" : root.cText
            font.pixelSize: pill.fontSize
            font.bold: pill.active
        }
        MouseArea { id: pillMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: pill.clicked() }
    }

    // Dropdown im Pillen-Stil: options = [{code, name}], current = gewählter code, picked(code) beim Auswählen
    component DropMenu: Pill {
        id: dm
        property string icon
        property var options: []
        property string current: ""
        property bool highlightWhenSet: true      // farbig hervorheben, solange nicht der erste Eintrag gewählt ist
        property bool showCodes: false            // Kürzel rechts (z. B. DE, EN)
        property string itemPrefix: "liveOption_"
        property int menuWidth: 230
        signal picked(string code)
        function currentName() {
            for (var i = 0; i < options.length; ++i) if (options[i].code === current) return options[i].name
            return current
        }
        function currentIndex() {
            for (var i = 0; i < options.length; ++i) if (options[i].code === current) return i
            return 0
        }
        label: icon + "  " + currentName() + "   ▾"
        active: highlightWhenSet && options.length > 0 && current !== options[0].code
        border.color: active ? "transparent" : root.cLine
        onClicked: menu.opened ? menu.close() : menu.open()
        Popup {
            id: menu
            y: dm.height + 6
            x: dm.width - width
            width: dm.menuWidth
            height: Math.min(360, list.contentHeight + 12)
            padding: 6
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            onOpened: list.positionViewAtIndex(dm.currentIndex(), ListView.Contain)
            enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } }
            exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }
            background: Rectangle { radius: 12; color: root.cSurface; border.color: root.cLine }
            contentItem: ListView {
                id: list
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: dm.options
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    id: optRow
                    required property var modelData
                    objectName: dm.itemPrefix + (modelData.code || "all")
                    readonly property bool selected: modelData.code === dm.current
                    width: list.width - 4
                    height: 34
                    radius: 8
                    color: selected ? Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.22)
                                    : (optMouse.containsMouse ? root.cSurfaceHover : "transparent")
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 10
                        spacing: 8
                        Text { width: 16; text: optRow.selected ? "✓" : ""; color: root.cAccent; font.bold: true; font.pixelSize: 13 }
                        Text { text: optRow.modelData.name; color: root.cText; font.pixelSize: 13; font.bold: optRow.selected }
                    }
                    Text {
                        anchors.right: parent.right; anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        visible: dm.showCodes && optRow.modelData.code !== ""
                        text: optRow.modelData.code.toUpperCase().replace("_", "-")
                        color: root.cMuted
                        font.pixelSize: 10
                    }
                    MouseArea {
                        id: optMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { menu.close(); dm.picked(optRow.modelData.code) }
                    }
                }
            }
        }
    }
    Rectangle { anchors.fill: parent; color: root.cBg }

    // ——— Größe ändern: Griff unten rechts ———
    // Das Player-Fenster hat keinen eigenen Rahmen zum Ziehen. Ein erneutes modal.open mit width/height ändert die Größe
    // des offenen Fensters, ohne die Seite neu zu laden. Das Fenster bleibt mittig, darum zählt die Mausbewegung doppelt.
    property int pendingWidth: 0
    property int pendingHeight: 0
    readonly property bool canResize: !!plugin && typeof plugin.call === "function" && !!modal && modal.mode !== "fill"
    function applySize() {
        if (canResize && pendingWidth > 0)
            plugin.call("modal.open", { panel: "live", width: pendingWidth, height: pendingHeight })
    }
    Timer { id: resizeTimer; interval: 30; onTriggered: root.applySize() }
    Item {
        id: grip
        objectName: "liveResizeGrip"
        visible: root.canResize
        width: 20; height: 20
        anchors.right: parent.right; anchors.bottom: parent.bottom
        z: 20
        // drei schräge Striche wie bei üblichen Fenstergriffen
        Repeater {
            model: 3
            Rectangle {
                required property int index
                width: 4 + index * 5; height: 2; radius: 1
                color: gripMouse.containsMouse || gripMouse.pressed ? root.cAccent : root.cMuted
                rotation: -45
                x: grip.width - width / 2 - 3 - index * 2.5
                y: grip.height - 4 - index * 2.5
                transformOrigin: Item.Center
            }
        }
        MouseArea {
            id: gripMouse
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeFDiagCursor
            property point start
            property size startSize
            onPressed: (mouse) => {
                start = mapToGlobal(mouse.x, mouse.y)
                startSize = Qt.size(root.width + 2, root.height + 38)   // + Rand und Titelleiste des Player-Fensters
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                var p = mapToGlobal(mouse.x, mouse.y)
                root.pendingWidth = Math.max(520, Math.round(startSize.width + 2 * (p.x - start.x)))
                root.pendingHeight = Math.max(380, Math.round(startSize.height + 2 * (p.y - start.y)))
                if (!resizeTimer.running) resizeTimer.start()
            }
            onReleased: {
                if (root.pendingWidth <= 0) return
                root.applySize()
                // tatsächliche Größe merken (der Player begrenzt sie auf den Bildbereich)
                Qt.callLater(function() { if (controller) controller.setLiveWindowSize(Math.round(root.width + 2), Math.round(root.height + 38)) })
            }
        }
        ToolTip.visible: gripMouse.containsMouse && !gripMouse.pressed
        ToolTip.delay: 600
        ToolTip.text: root.tr2("Ziehen, um die Größe zu ändern", "Drag to resize")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12

        // ——— Kopf: Reiter, Sprache, Aktualisieren ———
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                implicitWidth: tabRow.implicitWidth + 8
                implicitHeight: 38
                radius: 19
                color: root.cSurface
                border.color: root.cLine
                Row {
                    id: tabRow
                    anchors.centerIn: parent
                    spacing: 2
                    Repeater {
                        model: [{ id: "follow", de: "Gefolgt", en: "Following" },
                                { id: "top", de: "Beliebt", en: "Popular" },
                                { id: "categories", de: "Kategorien", en: "Categories" },
                                { id: "search", de: "Suche", en: "Search" }]
                        delegate: Pill {
                            required property var modelData
                            objectName: "liveTab_" + modelData.id
                            label: root.tr2(modelData.de, modelData.en)
                            active: root.tab === modelData.id || (modelData.id === "categories" && root.tab === "category")
                            onClicked: {
                                if (modelData.id === "search") {
                                    controller.applyLiveResult("search", [], "", [], "")
                                    searchField.forceActiveFocus()
                                    if (searchField.text.trim() !== "") root.load("search")
                                } else root.load(modelData.id)
                            }
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            // Sortierung (alle Reiter außer „Gefolgt“) und Sprache (alle Kanal-Listen, nicht die Kategorienliste): Dropdowns
            DropMenu {
                objectName: "liveSort"
                visible: root.tab !== "follow"
                icon: "⇅"
                itemPrefix: "liveSort_"
                highlightWhenSet: false
                menuWidth: 240
                current: root.filterSort
                options: [{ code: "relevance", name: root.tr2("Für dich empfohlen", "Recommended for you") },
                          { code: "viewers", name: root.tr2("Zuschauer (viel → wenig)", "Viewers (high → low)") }]
                onPicked: (code) => root.setFilterSort(code)
            }
            DropMenu {
                objectName: "liveLanguage"
                visible: root.tab !== "categories" && root.tab !== "follow"
                icon: "🌐"
                itemPrefix: "liveLanguage_"
                showCodes: true
                current: root.filterLanguage
                options: [{ code: "", name: root.tr2("Alle Sprachen", "All languages") }].concat(root.twitchLanguages)
                onPicked: (code) => root.setFilterLanguage(code)
            }
            Rectangle {
                objectName: "liveRefresh"
                implicitWidth: 34; implicitHeight: 34; radius: 17
                color: refreshMouse.containsMouse ? root.cSurfaceHover : root.cSurface
                border.color: root.cLine
                Text {
                    id: refreshIcon
                    anchors.centerIn: parent
                    text: "⟳"; color: root.cText; font.pixelSize: 17
                    RotationAnimation on rotation { running: root.busy; from: 0; to: 360; duration: 900; loops: Animation.Infinite
                                                    onRunningChanged: if (!running) refreshIcon.rotation = 0 }
                }
                MouseArea { id: refreshMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: root.load(root.tab, controller ? controller.liveCategory : "") }
                ToolTip.visible: refreshMouse.containsMouse
                ToolTip.delay: 500
                ToolTip.text: root.tr2("Aktualisieren", "Refresh")
            }
        }

        // ——— Suchfeld ———
        Rectangle {
            Layout.fillWidth: true
            visible: root.tab === "search"
            implicitHeight: 40
            radius: 20
            color: root.cSurface
            border.color: searchField.activeFocus ? root.cAccent : root.cLine
            border.width: searchField.activeFocus ? 2 : 1
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14; anchors.rightMargin: 10
                spacing: 8
                Text { text: "🔍"; font.pixelSize: 14; color: root.cMuted }
                TextField {
                    id: searchField
                    objectName: "liveSearch"
                    Layout.fillWidth: true
                    background: null
                    color: root.cText
                    placeholderText: root.tr2("Kanal, Spiel oder Kategorie suchen …", "Search channel, game or category …")
                    placeholderTextColor: root.cMuted
                    selectByMouse: true
                    // Suche beim Tippen: kurz nach dem letzten Tastendruck, Enter sucht sofort
                    onTextChanged: if (root.tab === "search") searchDelay.restart()
                    onAccepted: { searchDelay.stop(); root.load("search") }
                    Timer {
                        id: searchDelay
                        interval: 350
                        onTriggered: {
                            if (!controller || root.tab !== "search") return    // inzwischen anderer Reiter
                            if (searchField.text.trim() === "") controller.applyLiveResult("search", [], "", [], "")
                            else root.load("search")
                        }
                    }
                }
            }
        }

        // ——— Hinweis: „Für dich empfohlen“ lernt erst mit der Zeit ———
        Rectangle {
            objectName: "liveRecommendHint"
            Layout.fillWidth: true
            visible: !!controller && root.filterSort === "relevance" && root.tab !== "follow" && !root.busy && !root.hasProfile
            implicitHeight: hintText.implicitHeight + 14
            radius: 10
            color: Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.12)
            border.color: Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.35)
            Text {
                id: hintText
                anchors.fill: parent
                anchors.margins: 7
                anchors.leftMargin: 12
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 12
                color: root.cText
                text: root.tr2("✨ „Für dich empfohlen“ lernt mit: Kanäle, die du hier öffnest, ihre Kategorien und – angemeldet – deine gefolgten Kanäle kommen nach oben. Noch fehlt dafür etwas, darum gilt vorerst die Zuschauerzahl.",
                               "✨ “Recommended for you” learns as you go: channels you open here, their categories and – when signed in – the channels you follow move to the top. Until then the viewer count decides.")
            }
        }

        // ——— Pfad in einer Kategorie ———
        RowLayout {
            Layout.fillWidth: true
            visible: root.tab === "category"
            spacing: 8
            Pill { label: "‹  " + root.tr2("Kategorien", "Categories"); border.color: root.cLine; onClicked: root.load("categories") }
            Text {
                Layout.fillWidth: true
                text: controller ? controller.liveCategory : ""
                color: root.cText
                font.pixelSize: 17
                font.bold: true
                elide: Text.ElideRight
            }
        }

        // ——— Inhalt ———
        ScrollView {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Column {
                id: content
                // etwas Rand, damit die beim Mouse-over vergrößerte Karte nicht abgeschnitten wird
                x: 6
                width: scroller.availableWidth - 12
                topPadding: 6
                spacing: 14
                readonly property int gap: 12
                readonly property int streamCols: Math.max(1, Math.floor((width + gap) / (250 + gap)))
                readonly property real streamW: Math.floor((width - gap * (streamCols - 1)) / streamCols)
                readonly property int catCols: Math.max(2, Math.floor((width + gap) / (138 + gap)))
                readonly property real catW: Math.floor((width - gap * (catCols - 1)) / catCols)

                // Platzhalter während des Ladens (pulsierende Karten)
                Flow {
                    width: parent.width
                    spacing: content.gap
                    visible: root.busy
                    Repeater {
                        model: 6
                        Rectangle {
                            width: content.streamW
                            height: content.streamW * 9 / 16 + 70
                            radius: 12
                            color: root.cSurface
                            SequentialAnimation on opacity {
                                running: root.busy; loops: Animation.Infinite
                                NumberAnimation { to: 0.45; duration: 650; easing.type: Easing.InOutQuad }
                                NumberAnimation { to: 1.0; duration: 650; easing.type: Easing.InOutQuad }
                            }
                        }
                    }
                }

                // Leer / Fehler
                Column {
                    width: parent.width
                    visible: !root.busy && !!controller && root.streams.length === 0 && root.categories.length === 0
                    topPadding: 50
                    spacing: 12
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: controller && controller.liveError !== "" ? "⚠" : (root.tab === "search" ? "🔍" : "📺")
                        font.pixelSize: 40
                        color: root.cMuted
                    }
                    Text {
                        objectName: "liveMessage"
                        width: Math.min(parent.width, 460)
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        font.pixelSize: 14
                        color: controller && controller.liveError !== "" ? root.cError : root.cMuted
                        text: !controller ? ""
                              : controller.liveError !== "" ? root.errorText(controller.liveError)
                              : root.tab === "search" ? root.tr2("Einfach lostippen – Ergebnisse erscheinen sofort.", "Just start typing – results appear right away.")
                              : root.tab === "follow" ? root.tr2("Keiner deiner Kanäle ist gerade live.", "None of the channels you follow is live right now.")
                              : root.tr2("Keine Live-Kanäle gefunden.", "No live channels found.")
                    }
                    Pill {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: !!controller && ["login", "scope", "expired"].indexOf(controller.liveError) >= 0
                        label: root.tr2("Mit Twitch anmelden", "Sign in with Twitch")
                        active: true
                        onClicked: { controller.startLogin(); if (root.modal) root.modal.close("login") }
                    }
                }

                // Kategorien (Reiter „Kategorien“ und Treffer der Suche)
                Text {
                    visible: !root.busy && root.tab === "search" && root.categories.length > 0
                    text: root.tr2("Kategorien", "Categories")
                    color: root.cText; font.pixelSize: 15; font.bold: true
                }
                Flow {
                    width: parent.width
                    spacing: content.gap
                    visible: !root.busy && root.categories.length > 0
                    Repeater {
                        model: root.tab === "search" ? root.categories.slice(0, content.catCols * 2) : root.categories
                        delegate: Item {
                            id: cat
                            required property var modelData
                            objectName: "liveCategory_" + modelData.name
                            width: content.catW
                            height: catCol.implicitHeight
                            Column {
                                id: catCol
                                width: parent.width
                                spacing: 6
                                RoundImage {
                                    width: parent.width
                                    height: Math.round(width * 4 / 3)
                                    radius: 10
                                    source: cat.modelData.boxArt
                                    scale: catMouse.containsMouse ? 1.03 : 1.0
                                    Behavior on scale { NumberAnimation { duration: 140 } }
                                    Rectangle {
                                        anchors.fill: parent; radius: 10; color: "#000000"
                                        opacity: catMouse.containsMouse ? 0.25 : 0
                                        Behavior on opacity { NumberAnimation { duration: 140 } }
                                    }
                                    Rectangle {
                                        anchors.fill: parent; radius: 10; color: "transparent"
                                        border.width: 3; border.color: catMouse.containsMouse ? root.cAccent : "transparent"
                                    }
                                    // „Für dich“: warum die Kategorie oben steht
                                    Rectangle {
                                        objectName: "liveCategoryReason"
                                        readonly property string reason: root.filterSort !== "relevance" ? ""
                                            : (cat.modelData.clicks || 0) > 0 ? "★ " + root.tr2("%1× geöffnet", "opened %1×").arg(cat.modelData.clicks)
                                            : (cat.modelData.plays || 0) > 0 ? "★ " + root.tr2("geschaut", "watched")
                                            : cat.modelData.followedGame ? "♥ " + root.tr2("Gefolgte live", "Followed live") : ""
                                        visible: reason !== ""
                                        anchors { left: parent.left; top: parent.top; margins: 7 }
                                        width: Math.min(reasonText.implicitWidth + 12, parent.width - 14)
                                        height: reasonText.implicitHeight + 6
                                        radius: height / 2
                                        color: root.cAccent
                                        Text {
                                            id: reasonText
                                            anchors.centerIn: parent
                                            width: Math.min(implicitWidth, parent.parent.width - 26)
                                            text: parent.reason
                                            color: "#ffffff"; font.pixelSize: 11; font.bold: true
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                                Text {
                                    width: parent.width
                                    text: cat.modelData.title
                                    color: root.cText; font.pixelSize: 13; font.bold: true
                                    elide: Text.ElideRight
                                }
                                Row {
                                    spacing: 5
                                    Rectangle { width: 7; height: 7; radius: 4; color: root.cLive; anchors.verticalCenter: parent.verticalCenter }
                                    Text {
                                        objectName: "liveCategoryViewers"
                                        text: root.compact(cat.modelData.viewers) + root.tr2(" Zuschauer", " viewers")
                                        color: root.cMuted; font.pixelSize: 12
                                    }
                                }
                            }
                            MouseArea { id: catMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: root.openCategory(cat.modelData.name) }
                        }
                    }
                }

                // Kanäle
                Text {
                    visible: !root.busy && root.tab === "search" && root.streams.length > 0
                    text: root.tr2("Live-Kanäle", "Live channels")
                    color: root.cText; font.pixelSize: 15; font.bold: true
                }
                Flow {
                    id: streamFlow
                    objectName: "liveGrid"
                    width: parent.width
                    spacing: content.gap
                    visible: !root.busy
                    Repeater {
                        model: root.streams
                        delegate: Rectangle {
                            id: card
                            required property var modelData
                            readonly property bool current: !!controller && controller.channel === modelData.login
                            readonly property bool hovered: cardMouse.containsMouse || gameMouse.containsMouse
                            objectName: "liveCard_" + modelData.login
                            width: content.streamW
                            height: cardCol.implicitHeight + 12
                            radius: 12
                            color: hovered ? root.cSurfaceHover : root.cSurface
                            border.width: current || hovered ? 2 : 1
                            border.color: current || hovered ? root.cAccent : root.cLine
                            // Mouse-over: Karte hebt sich leicht an und liegt über den Nachbarn
                            scale: hovered ? 1.025 : 1.0
                            z: hovered ? 2 : 0
                            Behavior on color { ColorAnimation { duration: 120 } }
                            Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

                            MouseArea { id: cardMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: root.play(card.modelData.login, card.modelData.game) }

                            Column {
                                id: cardCol
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 4 }
                                spacing: 10
                                RoundImage {
                                    width: parent.width
                                    height: Math.round(width * 9 / 16)
                                    radius: 11
                                    backdrop: card.color
                                    source: card.modelData.thumb
                                    // Mouse-over: Bild abdunkeln, runder Abspiel-Knopf in der Mitte
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 8
                                        color: "#000000"
                                        opacity: cardMouse.containsMouse ? 0.38 : 0
                                        Behavior on opacity { NumberAnimation { duration: 140 } }
                                    }
                                    Rectangle {
                                        objectName: "livePlayHint"
                                        anchors.centerIn: parent
                                        width: 52; height: 52; radius: 26
                                        color: root.cAccent
                                        opacity: cardMouse.containsMouse ? 0.95 : 0
                                        scale: cardMouse.containsMouse ? 1.0 : 0.7
                                        Behavior on opacity { NumberAnimation { duration: 140 } }
                                        Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } }
                                        Text { anchors.centerIn: parent; anchors.horizontalCenterOffset: 2; text: "▶"; color: "white"; font.pixelSize: 22 }
                                    }
                                    // LIVE oben links, Zuschauer unten links
                                    Rectangle {
                                        x: 10; y: 10
                                        radius: 4; color: root.cLive
                                        width: liveText.implicitWidth + 10; height: liveText.implicitHeight + 4
                                        Text { id: liveText; anchors.centerIn: parent; text: "LIVE"; color: "white"; font.pixelSize: 10; font.bold: true }
                                    }
                                    Rectangle {
                                        x: 10; y: parent.height - height - 10
                                        radius: 4; color: "#cc0e0e10"
                                        width: viewersRow.implicitWidth + 12; height: viewersRow.implicitHeight + 6
                                        Row {
                                            id: viewersRow
                                            anchors.centerIn: parent
                                            spacing: 5
                                            Rectangle { width: 7; height: 7; radius: 4; color: root.cLive; anchors.verticalCenter: parent.verticalCenter }
                                            Text {
                                                objectName: "liveViewers"
                                                text: root.fullCount(card.modelData.viewers) + root.tr2(" Zuschauer", " viewers")
                                                color: "white"; font.pixelSize: 11
                                            }
                                        }
                                    }
                                    Rectangle {
                                        visible: card.current
                                        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
                                        radius: 4; color: root.cAccent
                                        width: nowText.implicitWidth + 10; height: nowText.implicitHeight + 4
                                        Text { id: nowText; anchors.centerIn: parent; text: root.tr2("LÄUFT", "PLAYING"); color: "white"; font.pixelSize: 10; font.bold: true }
                                    }
                                }
                                Row {
                                    width: parent.width
                                    spacing: 10
                                    RoundImage {
                                        id: avatar
                                        width: 44; height: 44; radius: 22
                                        backdrop: card.color
                                        source: card.modelData.avatar
                                        Text {
                                            anchors.centerIn: parent
                                            visible: card.modelData.avatar === ""
                                            text: card.modelData.name.charAt(0).toUpperCase()
                                            color: root.cText; font.bold: true; font.pixelSize: 15
                                        }
                                    }
                                    Column {
                                        width: parent.width - avatar.width - 10
                                        spacing: 3
                                        Text {
                                            width: parent.width
                                            text: card.modelData.name
                                            color: root.cText; font.pixelSize: 14; font.bold: true
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            width: parent.width
                                            text: card.modelData.title
                                            color: root.cMuted; font.pixelSize: 12
                                            elide: Text.ElideRight
                                        }
                                        // Kategorie als Chip – Klick öffnet die Kategorie
                                        Rectangle {
                                            objectName: "liveGame"
                                            visible: card.modelData.game !== ""
                                            width: Math.min(parent.width, gameText.implicitWidth + 16)
                                            height: gameText.implicitHeight + 6
                                            radius: height / 2
                                            color: Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, gameMouse.containsMouse ? 0.32 : 0.18)
                                            Text {
                                                id: gameText
                                                anchors.centerIn: parent
                                                width: Math.min(implicitWidth, parent.width - 16)
                                                text: card.modelData.game
                                                color: root.dark ? Qt.lighter(root.cAccent, 1.25) : Qt.darker(root.cAccent, 1.25)
                                                font.pixelSize: 11; font.bold: true
                                                elide: Text.ElideRight
                                            }
                                            MouseArea { id: gameMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                                        onClicked: root.openCategory(card.modelData.game) }
                                            ToolTip.visible: gameMouse.containsMouse
                                            ToolTip.delay: 500
                                            ToolTip.text: root.tr2("Kategorie öffnen", "Open category")
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Item { width: 1; height: 4 }
            }
        }
    }
}
