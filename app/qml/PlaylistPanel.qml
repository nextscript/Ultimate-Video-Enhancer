import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var playlist
    property var theme
    signal requestOpenFiles()
    signal requestOpenFolder()
    signal requestOpenUrl()
    signal requestSave()
    signal requestMediaInfo()

    property var selected: ({})       // Zeile → true
    property int anchorRow: -1
    property int cursorRow: -1        // Zeile, auf der die Pfeiltasten stehen
    property string filter: ""
    // Liste hat den Tastaturfokus: Pfeiltasten & Co. wählen Titel aus, statt zu spulen oder die Lautstärke zu ändern
    readonly property bool keyboardActive: list.activeFocus
    readonly property var navigationKeys: ["Up", "Down", "Shift+Up", "Shift+Down", "PgUp", "PgDown", "Home", "End",
                                           "Shift+PgUp", "Shift+PgDown", "Shift+Home", "Shift+End", "Return", "Enter", "Del"]

    function selectedRows() { return Object.keys(selected).map(Number).sort((a, b) => a - b) }
    function selectOnly(r) { var s = {}; s[r] = true; selected = s; anchorRow = r; cursorRow = r }
    function toggle(r) { var s = Object.assign({}, selected); if (s[r]) delete s[r]; else s[r] = true; selected = s; anchorRow = r; cursorRow = r }
    function selectRange(r) {
        if (anchorRow < 0) return selectOnly(r)
        var s = {}
        for (var i = Math.min(anchorRow, r); i <= Math.max(anchorRow, r); ++i) if (rowMatches(i)) s[i] = true
        selected = s; cursorRow = r
    }
    function clearSelection() { selected = ({}); anchorRow = -1; cursorRow = -1 }
    function rowMatches(i) { return filter.length === 0 || playlist.titleAt(i).toLowerCase().indexOf(filter) >= 0 }
    // Tastaturauswahl: delta in sichtbaren Zeilen (±1, ±Seite, ±Infinity = Anfang/Ende), extend = mit Umschalt erweitern
    function moveCursor(delta, extend) {
        var vis = []
        for (var i = 0; i < playlist.count; ++i) if (rowMatches(i)) vis.push(i)
        if (!vis.length) return
        var from = cursorRow
        var pos = vis.indexOf(from)
        var target
        // Noch keine Tastaturauswahl: beim laufenden Titel beginnen (sonst am Anfang bzw. Ende)
        if (pos < 0) target = vis.indexOf(playlist.currentIndex) >= 0 && isFinite(delta) ? playlist.currentIndex
                            : (delta > 0 ? vis[0] : vis[vis.length - 1])
        else target = vis[Math.max(0, Math.min(vis.length - 1, pos + (isFinite(delta) ? delta : (delta > 0 ? vis.length : -vis.length))))]
        if (extend) {
            if (anchorRow < 0) anchorRow = pos >= 0 ? from : target
            selectRange(target)
        } else {
            selectOnly(target)
        }
        list.positionViewAtIndex(target, ListView.Contain)
    }
    function pageRows() { return Math.max(1, Math.floor(list.height / 30) - 1) }
    // Fensterkoordinaten der Zeilenmitte (für den UI-Selbsttest)
    function rowCenter(i) {
        list.positionViewAtIndex(i, ListView.Contain)
        var it = list.itemAtIndex(i)
        return it ? it.mapToItem(null, it.width / 2, it.height / 2) : null
    }
    function moveSel(where) { playlist.moveRows(selectedRows(), where); clearSelection() }

    Connections { target: root.playlist; function onCountChanged() { root.clearSelection() } }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            Label { text: qsTr("Wiedergabeliste"); font.bold: true; color: theme.text; Layout.fillWidth: true }
            Label { text: playlist.count + qsTr(" Titel"); color: theme.muted; font.pixelSize: 12 }
        }
        TextField {
            id: search
            Layout.fillWidth: true
            Layout.leftMargin: 8; Layout.rightMargin: 8
            placeholderText: qsTr("Suchen…")
            onTextChanged: root.filter = text.toLowerCase()
            // Pfeil runter im Suchfeld: zu den Treffern wechseln
            Keys.onDownPressed: { list.forceActiveFocus(); root.moveCursor(1, false) }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.playlist
            activeFocusOnTab: true
            keyNavigationEnabled: false
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property int index
                required property string title
                required property string path
                required property double duration
                required property bool error
                required property bool isCurrent
                readonly property bool matches: root.filter.length === 0 || title.toLowerCase().indexOf(root.filter) >= 0
                width: list.width
                height: matches ? 30 : 0
                visible: matches
                // Laufender Eintrag: leicht orange getönt, damit man ihn in langen Listen sofort findet
                color: root.selected[index] ? (theme.dark ? "#22303f" : "#cfe3ff")
                     : hover.hovered ? theme.panel2
                     : row.isCurrent ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, theme.dark ? 0.14 : 0.12)
                     : "transparent"
                HoverHandler { id: hover }
                Rectangle {   // orangefarbener Balken am linken Rand des laufenden Eintrags
                    visible: row.isCurrent
                    width: 3; height: parent.height
                    color: theme.accent
                }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                    Label {
                        text: row.isCurrent ? "▶" : (row.index + 1)
                        color: row.isCurrent ? theme.accent : theme.muted
                        font.pixelSize: 11
                        Layout.preferredWidth: 24
                        horizontalAlignment: Text.AlignRight
                    }
                    Label {
                        text: row.title
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                        color: row.error ? theme.error : (row.isCurrent ? theme.accent : theme.text)
                        font.bold: row.isCurrent
                        ToolTip.text: row.path
                        ToolTip.visible: hover.hovered
                        ToolTip.delay: 900
                    }
                    Label {
                        text: row.duration > 0 ? app.formatTime(row.duration) : ""
                        color: row.isCurrent ? theme.accent : theme.muted; font.pixelSize: 11; font.family: "Consolas"
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (m) => {
                        list.forceActiveFocus()
                        if (m.button === Qt.RightButton) {
                            if (!root.selected[row.index]) root.selectOnly(row.index)
                            itemMenu.popup()
                            return
                        }
                        if (m.modifiers & Qt.ControlModifier) root.toggle(row.index)
                        else if (m.modifiers & Qt.ShiftModifier) root.selectRange(row.index)
                        else root.selectOnly(row.index)
                    }
                    onDoubleClicked: app.playIndex(row.index)
                }
            }
            MouseArea {
                anchors.fill: parent
                z: -1
                acceptedButtons: Qt.RightButton
                onClicked: { root.clearSelection(); itemMenu.popup() }
            }
            Keys.onPressed: (e) => {
                var ext = (e.modifiers & Qt.ShiftModifier) !== 0
                var plain = !(e.modifiers & (Qt.ControlModifier | Qt.AltModifier))
                if (plain && e.key === Qt.Key_Up) { root.moveCursor(-1, ext); e.accepted = true }
                else if (plain && e.key === Qt.Key_Down) { root.moveCursor(1, ext); e.accepted = true }
                else if (plain && e.key === Qt.Key_PageUp) { root.moveCursor(-root.pageRows(), ext); e.accepted = true }
                else if (plain && e.key === Qt.Key_PageDown) { root.moveCursor(root.pageRows(), ext); e.accepted = true }
                else if (plain && e.key === Qt.Key_Home) { root.moveCursor(-Infinity, ext); e.accepted = true }
                else if (plain && e.key === Qt.Key_End) { root.moveCursor(Infinity, ext); e.accepted = true }
                else if (e.key === Qt.Key_Delete && root.selectedRows().length) { playlist.remove(root.selectedRows()); e.accepted = true }
                else if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && root.selectedRows().length) {
                    app.playIndex(root.selected[root.cursorRow] ? root.cursorRow : root.selectedRows()[0]); e.accepted = true }
                else if (e.key === Qt.Key_A && (e.modifiers & Qt.ControlModifier)) { var s = {}; for (var i = 0; i < playlist.count; ++i) s[i] = true; root.selected = s; e.accepted = true }
                else if ((e.modifiers & Qt.AltModifier) && e.key === Qt.Key_Up) { root.moveSel("up"); e.accepted = true }
                else if ((e.modifiers & Qt.AltModifier) && e.key === Qt.Key_Down) { root.moveSel("down"); e.accepted = true }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 4
            spacing: 0
            IconButton { theme: root.theme; text: "＋"; tip: qsTr("Dateien hinzufügen"); onClicked: root.requestOpenFiles() }
            IconButton { theme: root.theme; text: "－"; tip: qsTr("Ausgewählte entfernen (Entf)"); enabled: root.selectedRows().length > 0; onClicked: playlist.remove(root.selectedRows()) }
            IconButton { theme: root.theme; text: "✕"; tip: qsTr("Liste leeren"); enabled: playlist.count > 0; onClicked: playlist.clear() }
            IconButton { theme: root.theme; text: "⇅"; tip: qsTr("Nach Name sortieren"); enabled: playlist.count > 1; onClicked: playlist.sortByName() }
            IconButton { theme: root.theme; text: "⤓"; tip: qsTr("Speichern (Strg+Y)"); enabled: playlist.count > 0; onClicked: root.requestSave() }
            Item { Layout.fillWidth: true }
            IconButton { theme: root.theme; text: "⇄"; tip: qsTr("Zufall (Z)"); highlighted2: playlist.shuffle; onClicked: playlist.shuffle = !playlist.shuffle }
            IconButton {
                theme: root.theme
                text: playlist.repeat === "one" ? "↻¹" : "↻"
                highlighted2: playlist.repeat !== "none"
                tip: playlist.repeat === "none" ? qsTr("Wiederholen: aus") : (playlist.repeat === "all" ? qsTr("Wiederholen: alle") : qsTr("Wiederholen: eins"))
                onClicked: playlist.cycleRepeat()
            }
        }
    }

    Menu {
        id: itemMenu
        readonly property var rows: root.selectedRows()
        readonly property int n: rows.length
        MenuItem { text: qsTr("Abspielen"); enabled: itemMenu.n === 1; onTriggered: app.playIndex(itemMenu.rows[0]) }
        MenuItem { text: itemMenu.n > 1 ? qsTr("Als Nächstes abspielen (") + itemMenu.n + ")" : qsTr("Als Nächstes abspielen"); enabled: itemMenu.n > 0; onTriggered: root.moveSel("next") }
        MenuSeparator {}
        MenuItem { text: itemMenu.n > 1 ? itemMenu.n + qsTr(" Einträge entfernen") : qsTr("Entfernen"); enabled: itemMenu.n > 0; onTriggered: playlist.remove(itemMenu.rows) }
        MenuItem { text: qsTr("Alle anderen entfernen"); enabled: itemMenu.n > 0 && itemMenu.n < playlist.count; onTriggered: playlist.removeOthers(itemMenu.rows) }
        MenuItem { text: qsTr("Fehlerhafte Einträge entfernen"); onTriggered: playlist.removeErrors() }
        MenuSeparator {}
        MenuItem { text: qsTr("Nach oben"); enabled: itemMenu.n > 0; onTriggered: root.moveSel("up") }
        MenuItem { text: qsTr("Nach unten"); enabled: itemMenu.n > 0; onTriggered: root.moveSel("down") }
        MenuItem { text: qsTr("An den Anfang"); enabled: itemMenu.n > 0; onTriggered: root.moveSel("top") }
        MenuItem { text: qsTr("Ans Ende"); enabled: itemMenu.n > 0; onTriggered: root.moveSel("bottom") }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Im Explorer zeigen")
            enabled: itemMenu.n === 1 && !/^[a-z][a-z0-9+.-]*:\/\//i.test(playlist.pathAt(itemMenu.rows[0]))
            onTriggered: app.showInExplorer(playlist.pathAt(itemMenu.rows[0]))
        }
        MenuItem {
            text: itemMenu.n > 1 ? qsTr("Pfade kopieren") : qsTr("Pfad / Link kopieren")
            enabled: itemMenu.n > 0
            onTriggered: app.copyText(itemMenu.rows.map(r => playlist.pathAt(r)).join("\r\n"))
        }
        MenuItem { text: qsTr("Medieninformationen"); enabled: itemMenu.n === 1 && itemMenu.rows[0] === playlist.currentIndex; onTriggered: root.requestMediaInfo() }
        MenuSeparator {}
        MenuItem { text: qsTr("Dateien hinzufügen…"); onTriggered: root.requestOpenFiles() }
        MenuItem { text: qsTr("Ordner hinzufügen…"); onTriggered: root.requestOpenFolder() }
        MenuItem { text: qsTr("Netzwerkstream hinzufügen…"); onTriggered: root.requestOpenUrl() }
        MenuSeparator {}
        MenuItem { text: qsTr("Nach Name sortieren"); enabled: playlist.count > 1; onTriggered: playlist.sortByName() }
        MenuItem { text: qsTr("Wiedergabeliste speichern…"); enabled: playlist.count > 0; onTriggered: root.requestSave() }
        MenuItem { text: qsTr("Liste leeren"); enabled: playlist.count > 0; onTriggered: playlist.clear() }
    }
}
