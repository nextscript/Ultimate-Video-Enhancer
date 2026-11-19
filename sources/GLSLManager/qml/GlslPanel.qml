// Tab "GLSL" in der Seitenleiste: Liste der Filter mit allen Funktionen der Custom Filter Codes.
// Der Editor öffnet sich als Fenster in diesem Tab (modal.open mit scope "sidebar").
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    property var theme
    property var controller
    property var plugin
    property var host

    readonly property color fg: theme ? theme.text : "#e6e8eb"
    readonly property color muted: theme ? theme.muted : "#8b93a1"
    readonly property color accent: theme ? theme.accent : "#ff8a1f"
    readonly property color line: theme ? theme.line : "#262b34"
    readonly property color panel: theme ? theme.panel : "#14171c"
    readonly property color panel2: theme ? theme.panel2 : "#1b1f26"
    readonly property color errorColor: theme ? theme.error : "#ff6b6b"

    readonly property var typeNames: ({ webgl: "GLSL", svg: "SVG", canvas2d: "2D", audio: "Audio" })
    property string searchText: ""
    property string typeFilter: ""
    property string message: ""
    property bool messageIsError: false
    property int editorToken: 0
    property string pendingImport: ""
    property var pendingDupes: []
    property string pendingDeleteId: ""
    property string pendingDeleteLabel: ""
    property string captureLabel: ""
    property bool examplesLoading: false

    readonly property var allEntries: controller ? controller.entries : []
    readonly property var visibleEntries: {
        var q = searchText.trim().toLowerCase(), t = typeFilter
        var out = []
        for (var i = 0; i < allEntries.length; ++i) {
            var e = allEntries[i]
            if (t !== "" && e.type !== t) continue
            if (q !== "") {
                var hay = [e.label || "", (e.tags || []).join(" "), e.category || "", e.description || ""].join(" ").toLowerCase()
                if (hay.indexOf(q) < 0) continue
            }
            out.push(Object.assign({ sourceIndex: i }, e))
        }
        return out
    }
    readonly property bool canDrag: searchText.trim() === "" && typeFilter === ""

    function say(text, isError) { message = text; messageIsError = isError === true; messageTimer.restart() }

    function openEditor(id, preset) {
        editorToken++
        var result = plugin.call("modal.open", { panel: "editor", data: { token: editorToken, id: id || "", preset: preset || null } })
        if (result !== true) say(plugin.lastError(), true)
    }

    function handleImport(text, policy) {
        var r = controller.importText(text, policy)
        if (r.error) { say(r.error, true); return }
        if (r.dupes) {
            pendingImport = text
            pendingDupes = r.dupes
            duplicatePopup.open()
            return
        }
        say(qsTr("Import: %1 neu, %2 ersetzt, %3 übersprungen").arg(r.added).arg(r.replaced).arg(r.skipped))
    }

    function importClipboard() {
        var text = controller.clipboardText()
        if (text.trim() === "") { say(qsTr("Die Zwischenablage enthält keinen Text."), true); return }
        var first = text.trim().charAt(0)
        if (first === "{" || first === "[") handleImport(text, "")
        else openEditor("", { code: text })
    }

    function loadExamples() {
        examplesLoading = true
        var xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE) return
            examplesLoading = false
            if (xhr.status >= 200 && xhr.status < 300) handleImport(xhr.responseText, "")
            else say(qsTr("Beispiele konnten nicht geladen werden (HTTP %1).").arg(xhr.status), true)
        }
        xhr.open("GET", "https://raw.githubusercontent.com/nextscript/Ultimate-Video-Enhancer/refs/heads/main/gvf_filter_codes_example.json")
        xhr.send()
    }

    component SmallButton: Button {
        id: sb
        property string tip: ""
        implicitHeight: 28
        leftPadding: 8; rightPadding: 8; topPadding: 2; bottomPadding: 2
        focusPolicy: Qt.NoFocus
        ToolTip.text: tip
        ToolTip.visible: tip.length > 0 && hovered
        ToolTip.delay: 500
    }

    component RowButton: ToolButton {
        property string tip: ""
        property color tint: root.fg
        implicitWidth: 26; implicitHeight: 26
        padding: 0
        focusPolicy: Qt.NoFocus
        font.pixelSize: 14
        contentItem: Text {
            text: parent.text
            font: parent.font
            color: parent.tint
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        ToolTip.text: tip
        ToolTip.visible: tip.length > 0 && hovered
        ToolTip.delay: 500
    }

    component Badge: Rectangle {
        property alias text: badgeText.text
        property color tint: root.muted
        implicitWidth: badgeText.implicitWidth + 8
        implicitHeight: 15
        radius: 3
        color: Qt.rgba(tint.r, tint.g, tint.b, 0.14)
        border.color: Qt.rgba(tint.r, tint.g, tint.b, 0.45)
        Text { id: badgeText; anchors.centerIn: parent; color: parent.tint; font.pixelSize: 9; font.bold: true }
    }

    Timer { id: messageTimer; interval: 7000; onTriggered: root.message = "" }

    FileDialog {
        id: importDialog
        title: qsTr("Filter importieren")
        nameFilters: [qsTr("Filter und Shader (*.json *.glsl *.hook *.frag *.txt)"), qsTr("Alle Dateien (*)")]
        onAccepted: {
            var r = controller.readImportFile(selectedFile.toString())
            if (r.error) root.say(r.error, true)
            else if (r.isJson) root.handleImport(r.text, "")
            else root.openEditor("", { code: r.text, label: r.label })
        }
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Filter exportieren")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["JSON (*.json)"]
        onAccepted: {
            var r = controller.exportFile(selectedFile.toString())
            if (r.error) root.say(r.error, true)
            else root.say(qsTr("%1 Filter exportiert.").arg(r.count))
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Switch {
                id: masterSwitch
                objectName: "masterSwitch"
                focusPolicy: Qt.NoFocus
                checked: controller ? controller.masterEnabled : false
                onToggled: controller.masterEnabled = checked
                ToolTip.text: qsTr("Alle Filter gemeinsam ein- oder ausschalten")
                ToolTip.visible: hovered
                ToolTip.delay: 500
            }
            Label {
                Layout.fillWidth: true
                color: root.fg
                font.bold: true
                elide: Text.ElideRight
                text: controller ? qsTr("%1 von %2 aktiv").arg(controller.activeCount).arg(root.allEntries.length) : ""
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 4
            SmallButton {
                objectName: "newButton"
                text: qsTr("Neu")
                highlighted: true
                tip: qsTr("Neuen Filter anlegen")
                onClicked: root.openEditor("", null)
            }
            SmallButton {
                objectName: "pasteButton"
                text: qsTr("Einfügen")
                tip: qsTr("Shader-Code oder JSON aus der Zwischenablage übernehmen")
                onClicked: root.importClipboard()
            }
            SmallButton {
                text: qsTr("Import")
                tip: qsTr("JSON-Export des Userscripts oder Shader-Datei laden")
                onClicked: importDialog.open()
            }
            SmallButton {
                text: qsTr("Export")
                tip: qsTr("Alle Filter als JSON speichern (mit dem Userscript kompatibel)")
                onClicked: {
                    exportDialog.currentFile = "file:///" + controller.suggestedExportName()
                    exportDialog.open()
                }
            }
            SmallButton {
                text: root.examplesLoading ? qsTr("Lädt …") : qsTr("Beispiele")
                enabled: !root.examplesLoading
                tip: qsTr("Beispielfilter des Userscripts von GitHub laden")
                onClicked: root.loadExamples()
            }
            SmallButton {
                text: qsTr("Bibliothek")
                tip: qsTr("Filter-Bibliothek des Userscripts im Browser öffnen")
                onClicked: Qt.openUrlExternally("https://svg.ts3x.cc/")
            }
        }

        TextField {
            id: searchField
            objectName: "searchField"
            Layout.fillWidth: true
            placeholderText: qsTr("Suche in Name, Tags, Kategorie …")
            onTextChanged: root.searchText = text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 3
            Repeater {
                model: [{ key: "", label: qsTr("Alle") }, { key: "webgl", label: "GLSL" }, { key: "canvas2d", label: "2D" }, { key: "svg", label: "SVG" }, { key: "audio", label: "Audio" }]
                delegate: Button {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 24
                    padding: 2
                    focusPolicy: Qt.NoFocus
                    text: modelData.label
                    font.pixelSize: 11
                    checkable: true
                    checked: root.typeFilter === modelData.key
                    onClicked: root.typeFilter = modelData.key
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: root.panel2
            radius: 4
            border.color: root.line
            clip: true

            Label {
                anchors.centerIn: parent
                width: parent.width - 24
                visible: list.count === 0
                color: root.muted
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: root.allEntries.length === 0 ? qsTr("Noch keine Filter. Lege einen an, füge Code ein oder importiere eine Datei.")
                                                   : qsTr("Kein Filter passt zur Suche.")
            }

            ListView {
                id: list
                objectName: "filterList"
                anchors.fill: parent
                anchors.margins: 3
                model: root.visibleEntries
                spacing: 3
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                property int dragFrom: -1
                property int dropTo: -1

                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    width: ListView.view.width - 4
                    height: rowColumn.implicitHeight + 8
                    radius: 4
                    color: root.panel
                    opacity: list.dragFrom === index ? 0.5 : 1
                    border.color: list.dropTo === index && list.dragFrom !== index ? root.accent : (modelData.error ? root.errorColor : root.line)

                    ColumnLayout {
                        id: rowColumn
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 4 }
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: "⠿"
                                color: root.canDrag ? root.muted : root.line
                                font.pixelSize: 14
                                Layout.preferredWidth: 14
                                horizontalAlignment: Text.AlignHCenter
                                MouseArea {
                                    objectName: "dragHandle"
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    enabled: root.canDrag
                                    cursorShape: enabled ? Qt.OpenHandCursor : Qt.ArrowCursor
                                    preventStealing: true
                                    onPressed: list.dragFrom = row.index
                                    onPositionChanged: (mouse) => {
                                        var p = mapToItem(list.contentItem, mouse.x, mouse.y)
                                        var i = list.indexAt(10, p.y)
                                        if (i >= 0) list.dropTo = i
                                    }
                                    onReleased: {
                                        // Erst aufräumen: Das Verschieben baut diese Zeile neu auf
                                        var from = list.dragFrom, to = list.dropTo
                                        list.dragFrom = -1
                                        list.dropTo = -1
                                        if (to >= 0 && from >= 0 && to !== from) controller.moveEntry(from, to)
                                    }
                                    onCanceled: { list.dragFrom = -1; list.dropTo = -1 }
                                }
                            }
                            CheckBox {
                                objectName: "enableBox"
                                padding: 0
                                focusPolicy: Qt.NoFocus
                                checked: row.modelData.enabled === true
                                onToggled: controller.setEnabled(row.modelData.id, checked)
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: 3
                                text: row.modelData.label || ""
                                color: row.modelData.playable ? root.fg : root.muted
                                font.bold: true
                                elide: Text.ElideRight
                                ToolTip.text: row.modelData.description || row.modelData.label || ""
                                ToolTip.visible: nameHover.hovered
                                ToolTip.delay: 600
                                HoverHandler { id: nameHover }
                            }
                            RowButton {
                                objectName: "hotkeyButton"
                                text: "⌨"
                                tint: row.modelData.hotkey ? root.accent : root.muted
                                tip: row.modelData.hotkey ? qsTr("Taste: %1 (ändern oder entfernen)").arg(row.modelData.hotkey.toUpperCase()) : qsTr("Taste festlegen")
                                onClicked: {
                                    root.captureLabel = row.modelData.label
                                    hotkeyPopup.entryId = row.modelData.id
                                    hotkeyPopup.existing = row.modelData.hotkey || ""
                                    controller.beginCapture(row.modelData.id)
                                    hotkeyPopup.open()
                                }
                            }
                            RowButton { objectName: "editButton"; text: "✎"; tip: qsTr("Bearbeiten"); onClicked: root.openEditor(row.modelData.id, null) }
                            RowButton { objectName: "copyButton"; text: "⧉"; tip: qsTr("Duplizieren"); onClicked: controller.duplicateEntry(row.modelData.id) }
                            RowButton {
                                objectName: "deleteButton"
                                text: "✕"
                                tint: root.errorColor
                                tip: qsTr("Löschen")
                                onClicked: {
                                    root.pendingDeleteId = row.modelData.id
                                    root.pendingDeleteLabel = row.modelData.label
                                    deletePopup.open()
                                }
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.leftMargin: 17
                            spacing: 3
                            Badge {
                                text: row.modelData.isHook ? "mpv" : (root.typeNames[row.modelData.type] || "?")
                                tint: row.modelData.playable ? "#b28cff" : root.muted
                            }
                            Badge { visible: !!row.modelData.hotkey; text: (row.modelData.hotkey || "").toUpperCase(); tint: "#ffc850" }
                            Badge { visible: !!row.modelData.category; text: row.modelData.category || ""; tint: "#8fa8ff" }
                            Repeater {
                                model: (row.modelData.tags || []).slice(0, 3)
                                Badge { required property var modelData; text: modelData.length > 12 ? modelData.substring(0, 11) + "…" : modelData; tint: "#c8a040" }
                            }
                        }
                        Label {
                            visible: !row.modelData.playable
                            Layout.fillWidth: true
                            Layout.leftMargin: 17
                            color: root.muted
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                            text: qsTr("Nur im Browser möglich, wird hier nicht abgespielt.")
                        }
                        RowLayout {
                            visible: !!row.modelData.error
                            Layout.fillWidth: true
                            Layout.leftMargin: 17
                            Label {
                                Layout.fillWidth: true
                                color: root.errorColor
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                                maximumLineCount: 4
                                elide: Text.ElideRight
                                text: qsTr("Shaderfehler, Filter pausiert: ") + (row.modelData.error || "")
                            }
                            SmallButton { text: qsTr("Erneut"); onClicked: controller.retry(row.modelData.id) }
                        }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.message !== ""
            text: root.message
            color: root.messageIsError ? root.errorColor : root.accent
            wrapMode: Text.Wrap
            font.pixelSize: 12
        }
        Label {
            Layout.fillWidth: true
            visible: root.message === "" && root.allEntries.length > 1 && root.canDrag
            text: qsTr("Reihenfolge: am Griff ⠿ ziehen. Oben läuft zuerst.")
            color: root.muted
            elide: Text.ElideRight
            font.pixelSize: 11
        }
    }

    // ------------------------------------------------------------- Rückfragen
    component AskPopup: Popup {
        parent: root
        anchors.centerIn: parent
        width: Math.min(root.width - 24, 300)
        modal: true
        dim: true
        focus: true
        popupType: Popup.Item
        padding: 14
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: root.panel; border.color: root.accent; radius: 8 }
    }

    AskPopup {
        id: duplicatePopup
        objectName: "duplicatePopup"
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 8
            Label { Layout.fillWidth: true; text: qsTr("Doppelte Filter"); font.bold: true; color: root.fg }
            Label {
                Layout.fillWidth: true
                color: root.fg
                wrapMode: Text.Wrap
                text: qsTr("%1 Filter mit gleichem Namen gibt es schon:").arg(root.pendingDupes.length) + "\n• "
                      + root.pendingDupes.slice(0, 8).join("\n• ") + (root.pendingDupes.length > 8 ? "\n…" : "")
            }
            RowLayout {
                Layout.fillWidth: true
                Button { text: qsTr("Abbrechen"); Layout.fillWidth: true; onClicked: duplicatePopup.close() }
                Button { text: qsTr("Überspringen"); Layout.fillWidth: true; onClicked: { duplicatePopup.close(); root.handleImport(root.pendingImport, "skip") } }
                Button { objectName: "overwriteButton"; text: qsTr("Ersetzen"); highlighted: true; Layout.fillWidth: true; onClicked: { duplicatePopup.close(); root.handleImport(root.pendingImport, "overwrite") } }
            }
        }
    }

    AskPopup {
        id: deletePopup
        objectName: "deletePopup"
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 8
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.fg; text: qsTr("„%1“ wirklich löschen?").arg(root.pendingDeleteLabel) }
            RowLayout {
                Layout.fillWidth: true
                Button { text: qsTr("Abbrechen"); Layout.fillWidth: true; onClicked: deletePopup.close() }
                Button { objectName: "confirmDelete"; text: qsTr("Löschen"); highlighted: true; Layout.fillWidth: true; onClicked: { controller.removeEntry(root.pendingDeleteId); deletePopup.close() } }
            }
        }
    }

    AskPopup {
        id: hotkeyPopup
        objectName: "hotkeyPopup"
        property string entryId: ""
        property string existing: ""
        // Die Taste fängt der Controller ab: Danach schließt sich das Fenster von selbst
        readonly property string capturing: controller ? controller.captureId : ""
        onCapturingChanged: if (capturing === "" && opened) close()
        onClosed: if (controller && controller.captureId !== "") controller.cancelCapture()
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 8
            Label { Layout.fillWidth: true; text: qsTr("Taste festlegen"); font.bold: true; color: root.fg }
            Label { Layout.fillWidth: true; text: root.captureLabel; color: root.muted; elide: Text.ElideRight }
            Label {
                Layout.fillWidth: true
                color: root.fg
                wrapMode: Text.Wrap
                text: qsTr("Drücke eine Taste. Sie schaltet den Filter um und hat Vorrang vor einem gleichen Player-Kürzel.")
            }
            RowLayout {
                Layout.fillWidth: true
                Button { text: qsTr("Abbrechen"); Layout.fillWidth: true; onClicked: hotkeyPopup.close() }
                Button {
                    visible: hotkeyPopup.existing !== ""
                    text: qsTr("Entfernen (%1)").arg(hotkeyPopup.existing.toUpperCase())
                    Layout.fillWidth: true
                    onClicked: controller.clearHotkey(hotkeyPopup.entryId)
                }
            }
        }
    }
}
