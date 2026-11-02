// Effekt-Panel des Ultimate-Video-Enhancer-Plugins (wird vom Host per Loader geladen).
// Eigenschaften vom Host: controller (UveController), theme (Farben), host (Dateidialoge)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    property var controller
    property var theme
    property var host

    readonly property color fg: theme ? theme.text : "#e6e8eb"
    readonly property color muted: theme ? theme.muted : "#8b93a1"
    readonly property color accent: theme ? theme.accent : "#ff8a1f"
    readonly property color line: theme ? theme.line : "#262b34"
    readonly property color panel2: theme ? theme.panel2 : "#1b1f26"

    // Schlüssel (auch in C++ verwendet) – Anzeige über qsTr(modelData)
    readonly property var sections: [QT_TR_NOOP("Basis"), QT_TR_NOOP("Farbe"), QT_TR_NOOP("Schärfe"), QT_TR_NOOP("HDR-like"), QT_TR_NOOP("LUT")]
    property var collapsed: ({})

    component IconBtn: AbstractButton {
        id: ib
        property string kind: "download"   // download | gallery
        property string tip: ""
        implicitWidth: 30; implicitHeight: 30
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        ToolTip.text: tip; ToolTip.visible: tip.length > 0 && hovered; ToolTip.delay: 500
        background: Rectangle { radius: 6; color: ib.down ? root.line : (ib.hovered ? root.panel2 : "transparent") }
        contentItem: Canvas {
            id: cv
            readonly property color ink: ib.enabled ? root.fg : root.muted
            onInkChanged: requestPaint()
            Component.onCompleted: requestPaint()
            onPaint: {
                var ctx = getContext("2d"), w = 16, h = 16
                var ox = (width - w) / 2, oy = (height - h) / 2
                ctx.reset(); ctx.translate(ox, oy)
                ctx.strokeStyle = ink; ctx.fillStyle = ink
                ctx.lineWidth = 1.8; ctx.lineCap = "round"; ctx.lineJoin = "round"
                if (ib.kind === "download") {
                    ctx.beginPath(); ctx.moveTo(8, 2); ctx.lineTo(8, 10.5); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(4.5, 7.5); ctx.lineTo(8, 11); ctx.lineTo(11.5, 7.5); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(2.5, 11.5); ctx.lineTo(2.5, 14); ctx.lineTo(13.5, 14); ctx.lineTo(13.5, 11.5); ctx.stroke()
                } else if (ib.kind === "gallery") {
                    for (var i = 0; i < 2; ++i) for (var j = 0; j < 2; ++j) ctx.fillRect(2 + i * 6.5, 2 + j * 6.5, 5.5, 5.5)
                }
            }
        }
    }

    function controlsOf(section) {
        return controller ? controller.controls.filter(function (c) { return c.section === section }) : []
    }

    FileDialog {
        id: lutDialog
        title: qsTr("3D-LUT laden (.cube)")
        nameFilters: ["Cube-LUT (*.cube)", qsTr("Alle Dateien (*)")]
        onAccepted: controller.loadLut(selectedFile)
    }
    FileDialog {
        id: importDialog
        title: qsTr("UVE-Profil importieren")
        nameFilters: ["UVE-Profil (*.json)"]
        onAccepted: controller.importProfile(selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("UVE-Profil exportieren")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["UVE-Profil (*.json)"]
        onAccepted: controller.exportProfile(selectedFile)
    }
    FileDialog {
        id: lutProfileDialog
        title: qsTr("UVE-LUT-Profile importieren (.json / LUTsProfiles_v*.zip)")
        nameFilters: ["LUT-Profile (*.json *.zip)", qsTr("Alle Dateien (*)")]
        onAccepted: controller.importLutProfiles(selectedFile)
    }
    Dialog {
        id: saveDialog
        title: qsTr("Preset speichern")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        Component.onCompleted: { var c = standardButton(Dialog.Cancel); if (c) c.text = qsTr("Abbrechen") }
        TextField { id: presetName; width: 260; placeholderText: qsTr("Name des Presets") }
        onAccepted: if (presetName.text.trim().length) controller.savePreset(presetName.text)
        onOpened: { presetName.text = controller.activePreset; presetName.forceActiveFocus() }
    }

    // ---------- LUT-Galerie: aktueller Videoframe mit jedem LUT-Profil
    Popup {
        id: gallery
        parent: Overlay.overlay
        modal: true
        focus: true
        x: Math.round((parent.width - width) / 2); y: Math.round((parent.height - height) / 2)
        width: Math.min(parent.width - 40, 1180); height: parent.height - 60
        padding: 12
        property string filterGroup: ""
        property string search: ""
        readonly property var groups: {
            var g = []
            var items = controller ? controller.galleryItems : []
            for (var i = 0; i < items.length; ++i) if (g.indexOf(items[i].group) < 0) g.push(items[i].group)
            return g
        }
        readonly property var shown: (controller ? controller.galleryItems : []).filter(function (it) {
            return (!filterGroup || it.group === filterGroup) && (!search || it.name.toLowerCase().indexOf(search.toLowerCase()) >= 0)
        })
        onOpened: controller.buildGallery()
        background: Rectangle { color: theme ? theme.bg : "#0b0d10"; border.color: line; radius: 8 }

        Connections {   // nach dem Laden eines LUT-Pakets Galerie neu aufbauen
            target: root.controller
            ignoreUnknownSignals: true
            function onLibraryChanged() { if (gallery.visible) controller.buildGallery() }
            function onGalleryRequested() { gallery.open() }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("LUT-Galerie"); color: fg; font.pixelSize: 18; font.bold: true }
                Label { text: controller ? controller.galleryInfo : ""; color: muted; Layout.fillWidth: true; elide: Text.ElideRight; leftPadding: 10 }
                Button { text: qsTr("⟳ Frame aktualisieren"); onClicked: controller.buildGallery(); ToolTip.text: qsTr("Aktuelles Videobild neu übernehmen"); ToolTip.visible: hovered }
                IconBtn { kind: "download"; enabled: !!controller && !controller.libraryBusy; onClicked: controller.downloadLutPackage(); tip: qsTr("LUT-Paket von GitHub laden") }
                ToolButton { text: "✕"; onClicked: gallery.close() }
            }
            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    Layout.preferredWidth: 260
                    model: [qsTr("Alle Gruppen")].concat(gallery.groups)
                    onActivated: (i) => gallery.filterGroup = i === 0 ? "" : gallery.groups[i - 1]
                }
                TextField { Layout.preferredWidth: 240; placeholderText: qsTr("Suchen…"); onTextChanged: gallery.search = text }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("Aktiv: ") + (controller ? controller.lutProfileInfo : ""); color: accent; elide: Text.ElideRight; Layout.maximumWidth: 420 }
                Button { text: qsTr("Kein LUT"); onClicked: controller.setLutProfile("") }
            }
            GridView {
                id: grid
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true
                cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 230)))
                cellHeight: cellWidth * 9 / 16 + 40
                model: [{ key: "", name: qsTr("Original (ohne LUT)"), group: "", url: controller ? controller.galleryOriginal : "" }].concat(gallery.shown)
                ScrollBar.vertical: ScrollBar {}
                delegate: Item {
                    id: cell
                    required property var modelData
                    width: grid.cellWidth; height: grid.cellHeight
                    readonly property bool current: !!controller && controller.values.lutProfile === modelData.key
                    Rectangle {
                        anchors.fill: parent; anchors.margins: 5
                        radius: 6
                        color: cellMouse.containsMouse ? panel2 : "transparent"
                        border.width: cell.current ? 2 : 1
                        border.color: cell.current ? accent : line
                        Image {
                            id: thumb
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 4 }
                            height: width * 9 / 16
                            source: cell.modelData.url
                            cache: false; asynchronous: true
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 360
                        }
                        Column {
                            anchors { left: parent.left; right: parent.right; top: thumb.bottom; margins: 6; topMargin: 2 }
                            Label { width: parent.width; text: cell.modelData.name; color: cell.current ? accent : fg; elide: Text.ElideRight; font.pixelSize: 12 }
                            Label { width: parent.width; text: cell.modelData.group; color: muted; elide: Text.ElideRight; font.pixelSize: 10 }
                        }
                        MouseArea {
                            id: cellMouse
                            anchors.fill: parent; hoverEnabled: true
                            onClicked: controller.setLutProfile(cell.modelData.key)
                            onDoubleClicked: { controller.setLutProfile(cell.modelData.key); gallery.close() }
                        }
                        ToolTip.visible: cellMouse.containsMouse
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Klick: anwenden · Doppelklick: anwenden und schließen")
                    }
                }
            }
            Label {
                visible: !!controller && controller.galleryItems.length === 0
                Layout.fillWidth: true
                text: qsTr("Keine LUT-Profile vorhanden. Das Download-Symbol oben lädt das aktuelle LUTsProfiles-Paket aus dem UVE-Repository.")
                color: muted; wrapMode: Text.Wrap
            }
        }
    }

    Connections {
        target: root.controller
        ignoreUnknownSignals: true
        function onScrollRequested(where) {
            var f = panelScroll.contentItem
            f.contentY = where === "top" ? 0 : Math.max(0, f.contentHeight - f.height)
        }
    }

    ScrollView {
        id: panelScroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 6

            // ---------- Kopf: An/Aus, Vergleich, Undo
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 8
                Switch {
                    text: controller && controller.enabled ? qsTr("Effekte an") : qsTr("Effekte aus")
                    checked: controller ? controller.enabled : false
                    onToggled: controller.setEnabled(checked)
                    ToolTip.text: qsTr("Strg+Alt+E")
                    ToolTip.visible: hovered
                }
                Item { Layout.fillWidth: true }
                ToolButton { text: "↶"; enabled: !!controller && controller.canUndo; onClicked: controller.undo(); ToolTip.text: qsTr("Rückgängig"); ToolTip.visible: hovered }
                ToolButton { text: "↷"; enabled: !!controller && controller.canRedo; onClicked: controller.redo(); ToolTip.text: qsTr("Wiederholen"); ToolTip.visible: hovered }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 8; Layout.rightMargin: 8
                CheckBox {
                    text: qsTr("Vorher/Nachher")
                    checked: controller ? controller.compare : false
                    onToggled: controller.setCompare(checked)
                    ToolTip.text: qsTr("Strg+Alt+U – links Original, rechts mit Effekten")
                    ToolTip.visible: hovered
                }
                Slider {
                    Layout.fillWidth: true
                    enabled: !!controller && controller.compare
                    from: 0; to: 1
                    value: controller ? controller.values.compareSplit : 0.5
                    onMoved: controller.setCompareSplit(value)
                }
            }

            // ---------- Fehleranzeige (Shader/LUT/Profil)
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 8; Layout.rightMargin: 8
                visible: !!controller && controller.lastError.length > 0
                color: "#3a1d1d"; radius: 6
                implicitHeight: errCol.implicitHeight + 12
                ColumnLayout {
                    id: errCol
                    anchors.fill: parent; anchors.margins: 6
                    Label { Layout.fillWidth: true; text: controller ? controller.lastError : ""; color: "#ffb0b0"; wrapMode: Text.Wrap }
                    Button { text: qsTr("Erneut versuchen"); onClicked: controller.reenableShader() }
                }
            }

            // ---------- Presets
            Label { text: qsTr("PRESETS"); color: muted; font.pixelSize: 11; font.letterSpacing: 1; Layout.leftMargin: 10; Layout.topMargin: 4 }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 8; Layout.rightMargin: 8
                ComboBox {
                    id: presetBox
                    Layout.fillWidth: true
                    model: controller ? controller.presets : []
                    textRole: "name"
                    displayText: controller && controller.activePreset.length ? controller.activePreset : qsTr("(angepasst)")
                    onActivated: (i) => controller.applyPreset(model[i].name)
                }
                IconBtn {
                    kind: "download"
                    enabled: !!controller && !controller.libraryBusy
                    onClicked: controller.loadExampleProfile()
                    tip: qsTr("Beispielprofil von GitHub laden (My_Profile.json)")
                }
                Button { text: qsTr("Speichern…"); onClicked: saveDialog.open(); ToolTip.text: qsTr("Als Preset speichern"); ToolTip.visible: hovered }
                ToolButton {
                    text: "✕"
                    enabled: !!controller && controller.presets.some(p => p.name === controller.activePreset && !p.builtin)
                    onClicked: controller.deletePreset(controller.activePreset)
                    ToolTip.text: qsTr("Eigenes Preset löschen"); ToolTip.visible: hovered
                }
            }
            RowLayout {
                Layout.leftMargin: 8; Layout.rightMargin: 8
                CheckBox {
                    text: qsTr("Für diese Datei merken")
                    checked: controller ? controller.mediaPresetActive : false
                    onToggled: controller.setMediaPreset(checked)
                }
                CheckBox {
                    text: qsTr("Standard")
                    checked: !!controller && controller.defaultPreset.length > 0 && controller.defaultPreset === controller.activePreset
                    enabled: !!controller && controller.activePreset.length > 0
                    onToggled: controller.setDefaultPreset(checked ? controller.activePreset : "")
                    ToolTip.text: qsTr("Globales Standard-Preset für neue Dateien"); ToolTip.visible: hovered
                }
            }
            RowLayout {
                Layout.leftMargin: 8; Layout.rightMargin: 8
                Button { text: qsTr("UVE-Profil importieren…"); onClicked: importDialog.open() }
                Button { text: qsTr("Exportieren…"); onClicked: exportDialog.open() }
            }
            Label {
                Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 8
                visible: !!controller && controller.libraryStatus.length > 0
                text: controller ? controller.libraryStatus : ""
                color: muted; wrapMode: Text.Wrap; font.pixelSize: 11
            }

            // ---------- Effekt-Abschnitte (aus Controller-Metadaten)
            Repeater {
                model: root.sections
                delegate: ColumnLayout {
                    id: sec
                    required property string modelData
                    readonly property bool open: !root.collapsed[modelData]
                    Layout.fillWidth: true
                    spacing: 2

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        implicitHeight: 30
                        color: panel2
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 4
                            Label { text: (sec.open ? "▾ " : "▸ ") + qsTr(sec.modelData); color: fg; font.bold: true; Layout.fillWidth: true }
                            ToolButton { text: "↺"; onClicked: controller.resetSection(sec.modelData); ToolTip.text: qsTr("Abschnitt zurücksetzen"); ToolTip.visible: hovered }
                        }
                        MouseArea {
                            anchors.fill: parent; anchors.rightMargin: 40
                            onClicked: { var c = Object.assign({}, root.collapsed); c[sec.modelData] = sec.open; root.collapsed = c }
                        }
                    }

                    // LUT-Zeile
                    RowLayout {
                        visible: sec.open && sec.modelData === "LUT"
                        Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 8
                        Label { text: controller ? controller.lutInfo : ""; color: muted; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Button { text: qsTr("Laden…"); onClicked: lutDialog.open() }
                        Button { text: qsTr("Entfernen"); enabled: !!controller && controller.values.lutFile.length > 0; onClicked: controller.clearLut() }
                    }
                    // UVE-LUT-Profile (Farbmatrix) aus der GitHub-Bibliothek
                    ColumnLayout {
                        visible: sec.open && sec.modelData === "LUT"
                        Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 8
                        spacing: 4
                        Label { text: qsTr("UVE-LUT-Profil"); color: fg }
                        RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: lutProfileBox
                            Layout.fillWidth: true
                            readonly property var items: [{ key: "", text: qsTr("(keins)") }].concat(controller ? controller.lutProfiles : [])
                            model: items
                            textRole: "text"; valueRole: "key"
                            currentIndex: controller ? Math.max(0, items.findIndex(o => o.key === controller.values.lutProfile)) : 0
                            displayText: controller && controller.values.lutProfile && currentIndex <= 0 ? controller.lutProfileInfo : currentText
                            onActivated: (i) => controller.setLutProfile(items[i].key)
                        }
                        IconBtn {
                            kind: "download"
                            enabled: !!controller && !controller.libraryBusy
                            onClicked: controller.downloadLutPackage()
                            tip: qsTr("LUT-Paket von GitHub laden (neuestes LUTsProfiles_v*.zip)")
                        }
                        IconBtn {
                            kind: "gallery"
                            onClicked: gallery.open()
                            tip: qsTr("LUT-Galerie – aktuelles Bild mit allen LUT-Profilen")
                        }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: controller ? (controller.lutProfiles.length + qsTr(" Profile") + (controller.librarySource ? " · " + controller.librarySource : "") + " · " + controller.lutProfileInfo) : ""
                            color: muted; elide: Text.ElideRight; font.pixelSize: 11
                        }
                        RowLayout {
                            Button { text: qsTr("LUT-Profile importieren…"); onClicked: lutProfileDialog.open() }
                        }
                    }

                    Repeater {
                        model: sec.open ? root.controlsOf(sec.modelData) : []
                        delegate: Item {
                            id: row
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: modelData.type === "slider" ? 46 : 34
                            readonly property var current: controller ? controller.values[modelData.key] : undefined

                            // Regler
                            ColumnLayout {
                                visible: row.modelData.type === "slider"
                                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 8
                                spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: row.modelData.label; color: fg; Layout.fillWidth: true; elide: Text.ElideRight
                                            ToolTip.text: row.modelData.note || ""; ToolTip.visible: !!row.modelData.note && hov.hovered
                                            HoverHandler { id: hov } }
                                    Label {
                                        text: row.current === undefined ? "" : Number(row.current).toFixed(row.modelData.step < 0.1 ? 2 : (row.modelData.step < 1 ? 1 : 0))
                                        color: Math.abs(Number(row.current) - row.modelData.def) > 1e-6 ? accent : muted
                                        font.family: "Consolas"
                                    }
                                    ToolButton {
                                        text: "↺"; implicitWidth: 24; implicitHeight: 22
                                        enabled: Math.abs(Number(row.current) - row.modelData.def) > 1e-6
                                        onClicked: controller.resetValue(row.modelData.key)
                                    }
                                }
                                Slider {
                                    Layout.fillWidth: true
                                    from: row.modelData.min || 0; to: row.modelData.max || 1; stepSize: row.modelData.step || 0.01
                                    value: row.modelData.type !== "slider" ? 0 : (row.current === undefined ? row.modelData.def : row.current)
                                    onPressedChanged: pressed ? controller.beginGesture() : controller.endGesture()
                                    onMoved: controller.setValue(row.modelData.key, value)
                                }
                            }
                            // Schalter
                            Switch {
                                visible: row.modelData.type === "switch"
                                anchors.left: parent.left; anchors.leftMargin: 6
                                text: row.modelData.label
                                checked: row.current === true
                                onToggled: controller.setValue(row.modelData.key, checked)
                                ToolTip.text: row.modelData.note || ""; ToolTip.visible: !!row.modelData.note && hovered
                            }
                            // Auswahl
                            RowLayout {
                                visible: row.modelData.type === "choice"
                                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 8
                                Label { text: row.modelData.label; color: fg; Layout.fillWidth: true }
                                ComboBox {
                                    model: row.modelData.options || []
                                    textRole: "text"; valueRole: "value"
                                    currentIndex: (row.modelData.options || []).findIndex(o => o.value === row.current)
                                    onActivated: (i) => controller.setValue(row.modelData.key, row.modelData.options[i].value)
                                }
                            }
                        }
                    }
                }
            }

            // ---------- Fuß
            Label {
                Layout.fillWidth: true; Layout.margins: 10
                text: qsTr("Pipeline: ") + (controller ? controller.pipelineInfo : "")
                color: muted; wrapMode: Text.Wrap; font.pixelSize: 11
            }
            RowLayout {
                Layout.leftMargin: 8; Layout.bottomMargin: 10
                Button { text: qsTr("Alles zurücksetzen"); onClicked: controller.resetAll() }
            }
        }
    }
}
