import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// UVEinetRadio – Seitenleisten-Panel. Öffnen, Suchen und Favoriten-Stern starten nichts;
// die Wiedergabe beginnt nur mit ▶ / Abspielen oder Doppelklick. Schließen des Panels lässt den Sender weiterlaufen.
Item {
    id: root
    property var controller
    property var theme
    implicitWidth: 360
    implicitHeight: 640
    RadioTexts { id: texts }

    readonly property color cText: theme ? theme.text : "#e6e8eb"
    readonly property color cMuted: theme ? theme.muted : "#8b93a1"
    readonly property color cError: theme ? theme.error : "#ff6b6b"
    readonly property color cLine: theme ? theme.line : "#262b34"
    readonly property color cAccent: theme ? theme.accent : "#ff8a1f"

    property string selectedId: ""

    function startSearch() {
        if (!controller) return
        controller.search(nameField.text, countryBox.value, languageBox.value, tagBox.value,
                          sortBox.currentIndex === 1 ? "name" : "popularity")
    }

    Component.onCompleted: {
        if (!controller) return
        var ls = controller.lastSearch || {}
        nameField.text = ls.name || ""
        countryBox.value = ls.country || ""
        languageBox.value = ls.language || ""
        tagBox.value = ls.tag || ""
        sortBox.currentIndex = ls.order === "name" ? 1 : 0
        if (controller.results.length === 0) startSearch()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        NowPlaying {
            Layout.fillWidth: true
            theme: root.theme
            controller: root.controller
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            background: Rectangle { color: "transparent"; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.cLine } }
            Repeater {
                model: [qsTr("Suchen"), qsTr("Favoriten"), qsTr("Zuletzt"), qsTr("Eigener Stream")]
                TabButton {
                    id: tb
                    required property string modelData
                    required property int index
                    text: modelData
                    font.pixelSize: 12
                    implicitWidth: 60   // kleiner als der Platz → TabBar verteilt gleichmäßig, kein Scrollen
                    leftPadding: 2
                    rightPadding: 2
                    contentItem: Text {
                        text: tb.text
                        font: tb.font
                        color: tb.checked ? root.cAccent : root.cMuted
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        color: "transparent"
                        Rectangle { visible: tb.checked; anchors.bottom: parent.bottom; width: parent.width; height: 2; color: root.cAccent }
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ------------------------------------------------------------- Sender suchen
            ColumnLayout {
                spacing: 6
                RField {
                    id: nameField
                    Layout.fillWidth: true
                    theme: root.theme
                    placeholderText: qsTr("Sendername suchen …")
                    onTextEdited: root.startSearch()
                    Keys.onReturnPressed: root.startSearch()
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width > 520 ? 4 : 2
                    columnSpacing: 6
                    rowSpacing: 6
                    RFilter {
                        id: countryBox
                        Layout.fillWidth: true
                        theme: root.theme
                        entries: controller ? controller.countries : []
                        allText: qsTr("Alle Länder")
                        onPicked: root.startSearch()
                        onPressedChanged: if (pressed && controller) controller.loadFacets()
                    }
                    RFilter {
                        id: languageBox
                        Layout.fillWidth: true
                        theme: root.theme
                        entries: controller ? controller.languages : []
                        allText: qsTr("Alle Sprachen")
                        onPicked: root.startSearch()
                        onPressedChanged: if (pressed && controller) controller.loadFacets()
                    }
                    RFilter {
                        id: tagBox
                        Layout.fillWidth: true
                        theme: root.theme
                        entries: controller ? controller.tags : []
                        allText: qsTr("Alle Genres")
                        onPicked: root.startSearch()
                        onPressedChanged: if (pressed && controller) controller.loadFacets()
                    }
                    ComboBox {
                        id: sortBox
                        Layout.fillWidth: true
                        implicitHeight: 30
                        font.pixelSize: 13
                        model: [qsTr("Beliebteste zuerst"), qsTr("Nach Name")]
                        onActivated: root.startSearch()
                        contentItem: Text {
                            leftPadding: 10
                            text: sortBox.displayText
                            font: sortBox.font
                            color: root.cText
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        background: Rectangle {
                            radius: 6
                            color: theme ? theme.panel2 : "#1b1f26"
                            border.color: sortBox.visualFocus ? (theme ? theme.focus : "#4da3ff") : root.cLine
                            border.width: sortBox.visualFocus ? 2 : 1
                        }
                    }
                }
                // Zustand der Suche
                RowLayout {
                    Layout.fillWidth: true
                    visible: controller && (controller.searchBusy || controller.facetsBusy || controller.searchError !== "")
                    spacing: 6
                    BusyIndicator {
                        visible: controller && (controller.searchBusy || controller.facetsBusy)
                        running: visible
                        implicitWidth: 20
                        implicitHeight: 20
                    }
                    Text {
                        Layout.fillWidth: true
                        text: !controller ? ""
                            : controller.searchBusy ? qsTr("Suche läuft …")
                            : controller.searchFromCache ? qsTr("Katalog offline – zeige gespeicherte Ergebnisse.")
                            : controller.searchError !== "" ? qsTr("Senderkatalog nicht erreichbar: %1").arg(controller.searchError)
                            : controller.facetsBusy ? qsTr("Lade Filter …") : ""
                        textFormat: Text.PlainText
                        color: controller && controller.searchError !== "" && !controller.searchBusy ? root.cError : root.cMuted
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                    RButton {
                        theme: root.theme
                        visible: controller && controller.searchError !== "" && !controller.searchBusy
                        text: qsTr("Erneut versuchen")
                        onClicked: controller.retrySearch()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: controller && controller.facetsError !== "" && !controller.facetsBusy
                    text: qsTr("Filterlisten konnten nicht geladen werden.")
                    color: root.cMuted
                    font.pixelSize: 11
                }
                ListView {
                    id: resultList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: controller ? controller.results : []
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: StationRow {
                        required property var modelData
                        width: resultList.width - 8
                        theme: root.theme
                        controller: root.controller
                        station: modelData
                        selected: root.selectedId === modelData.id
                        onClicked: root.selectedId = modelData.id
                        onPlayRequested: controller.play(modelData, "search")
                    }
                    // Seitenweise nachladen, sobald das Ende in Sicht kommt
                    onContentYChanged: if (controller && controller.hasMore && !controller.searchBusy
                                           && contentY + height > contentHeight - 200) controller.loadMore()
                    footer: Item {
                        width: resultList.width
                        height: controller && controller.hasMore ? 44 : 0
                        RButton {
                            anchors.centerIn: parent
                            visible: controller && controller.hasMore
                            theme: root.theme
                            text: controller && controller.searchBusy ? qsTr("Lade …") : qsTr("Mehr laden")
                            enabled: controller && !controller.searchBusy
                            onClicked: controller.loadMore()
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 20
                        visible: resultList.count === 0 && controller && controller.searchDone && !controller.searchBusy && controller.searchError === ""
                        text: qsTr("Keine Sender gefunden. Ändere den Suchbegriff oder die Filter.")
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: root.cMuted
                        font.pixelSize: 12
                    }
                }
            }

            // ------------------------------------------------------------- Favoriten
            ColumnLayout {
                spacing: 6
                ListView {
                    id: favList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: controller ? controller.favorites : []
                    ScrollBar.vertical: ScrollBar {}
                    delegate: StationRow {
                        required property var modelData
                        width: favList.width - 8
                        theme: root.theme
                        controller: root.controller
                        station: modelData
                        selected: root.selectedId === modelData.id
                        onClicked: root.selectedId = modelData.id
                        onPlayRequested: controller.play(modelData, "favorite")
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 20
                        visible: favList.count === 0
                        text: qsTr("Noch keine Favoriten. Markiere Sender mit ☆ – das geht auch, wenn der Katalog offline ist.")
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: root.cMuted
                        font.pixelSize: 12
                    }
                }
            }

            // ------------------------------------------------------------- Zuletzt gehört
            ColumnLayout {
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Die letzten %1 erfolgreich gestarteten Sender").arg(20)
                        color: root.cMuted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                    RButton {
                        theme: root.theme
                        danger: true
                        text: qsTr("Liste leeren")
                        enabled: controller && controller.history.length > 0
                        onClicked: controller.clearHistory()
                    }
                }
                ListView {
                    id: histList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: controller ? controller.history : []
                    ScrollBar.vertical: ScrollBar {}
                    delegate: StationRow {
                        required property var modelData
                        width: histList.width - 8
                        theme: root.theme
                        controller: root.controller
                        station: modelData
                        showRemove: true
                        removeTip: qsTr("Aus dem Verlauf entfernen")
                        selected: root.selectedId === modelData.id
                        onClicked: root.selectedId = modelData.id
                        onPlayRequested: controller.play(modelData, "history")
                        onRemoveRequested: controller.removeHistory(modelData.id)
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 20
                        visible: histList.count === 0
                        text: qsTr("Noch nichts gehört.")
                        horizontalAlignment: Text.AlignHCenter
                        color: root.cMuted
                        font.pixelSize: 12
                    }
                }
            }

            // ------------------------------------------------------------- Eigener Stream
            ColumnLayout {
                id: customTab
                spacing: 8
                readonly property string urlError: controller && urlField.text.trim() !== "" ? controller.customUrlError(urlField.text) : ""
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Direkte Stream-Adresse eines Senders (MP3, AAC, Ogg, HLS .m3u8 oder Playlist .m3u/.pls). Webseiten von Sendern funktionieren nicht.")
                    color: root.cMuted
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
                RField {
                    id: customName
                    Layout.fillWidth: true
                    theme: root.theme
                    placeholderText: qsTr("Sendername")
                    maximumLength: 200
                }
                RField {
                    id: urlField
                    Layout.fillWidth: true
                    theme: root.theme
                    placeholderText: "https://…"
                    invalid: customTab.urlError !== ""
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                    Keys.onReturnPressed: playCustomBtn.clicked()
                }
                Text {
                    Layout.fillWidth: true
                    visible: customTab.urlError !== ""
                    text: texts.errorText(customTab.urlError, "")
                    color: root.cError
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    visible: customTab.urlError === "" && urlField.text.trim().toLowerCase().indexOf("http://") === 0
                    text: qsTr("Hinweis: HTTP ist unverschlüsselt. Der Sender wird nur abgespielt, weil du ihn ausdrücklich gewählt hast.")
                    color: root.cMuted
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
                CheckBox {
                    id: saveFav
                    Layout.fillWidth: true
                    text: qsTr("Als Favorit speichern")
                    checked: true
                    contentItem: Text {
                        leftPadding: saveFav.indicator.width + 6
                        text: saveFav.text
                        color: root.cText
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                RowLayout {
                    RButton {
                        id: playCustomBtn
                        theme: root.theme
                        accent: true
                        text: qsTr("Abspielen")
                        enabled: urlField.text.trim() !== "" && customTab.urlError === ""
                        onClicked: {
                            if (!enabled) return
                            if (controller.playCustom(customName.text, urlField.text.trim(), saveFav.checked)) {
                                customName.text = ""
                                urlField.text = ""
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: controller && controller.storageError !== ""
            text: qsTr("Favoriten konnten nicht gespeichert/gelesen werden: %1").arg(controller ? controller.storageError : "")
            color: root.cError
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Katalog: Radio Browser") + (controller && controller.mirror !== "" ? " (" + controller.mirror + ")" : "")
            color: root.cMuted
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }
}
