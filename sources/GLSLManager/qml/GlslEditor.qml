// Editor eines Filters: Fenster im GLSL-Tab (modal.open). "Größer" verschiebt dasselbe Fenster über das Video.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var theme
    property var controller
    property var plugin
    property var modal

    readonly property color fg: theme ? theme.text : "#e6e8eb"
    readonly property color muted: theme ? theme.muted : "#8b93a1"
    readonly property color accent: theme ? theme.accent : "#ff8a1f"
    readonly property color line: theme ? theme.line : "#262b34"
    readonly property color panel2: theme ? theme.panel2 : "#1b1f26"
    readonly property color errorColor: theme ? theme.error : "#ff6b6b"

    property int token: -1
    property string entryId: ""
    property var uniforms: ({})
    property var defs: []
    property string errorText: ""
    property bool wide: false
    property bool forceSave: false

    readonly property bool editing: entryId !== ""
    readonly property string currentType: typeBox.currentValue || "webgl"
    readonly property bool glsl: currentType === "webgl"
    readonly property bool hook: glsl && controller ? controller.isHook(codeArea.text) : false

    readonly property var blendModes: [
        { value: "normal", text: "Normal" }, { value: "darken", text: "Darken" }, { value: "multiply", text: "Multiply" },
        { value: "color-burn", text: "Color Burn" }, { value: "lighten", text: "Lighten" }, { value: "screen", text: "Screen" },
        { value: "color-dodge", text: "Color Dodge" }, { value: "overlay", text: "Overlay" }, { value: "soft-light", text: "Soft Light" },
        { value: "hard-light", text: "Hard Light" }, { value: "difference", text: "Difference" }, { value: "exclusion", text: "Exclusion" },
        { value: "hue", text: "Hue" }, { value: "saturation", text: "Saturation" }, { value: "color", text: "Color" }, { value: "luminosity", text: "Luminosity" }
    ]

    function loadFrom(data) {
        if (!data || data.token === token) return
        token = data.token
        entryId = data.id || ""
        var e = entryId !== "" && controller ? controller.entry(entryId) : null
        var preset = data.preset || {}
        labelField.text = e ? (e.label || "") : (preset.label || "")
        codeArea.text = e ? (e.code || "") : (preset.code || "")
        tagsField.text = e ? (e.tags || []).join(", ") : ""
        categoryField.text = e ? (e.category || "") : ""
        typeBox.currentIndex = Math.max(0, typeBox.indexOfValue(e ? (e.type || "webgl") : (preset.code && controller ? controller.detect(preset.code) : "webgl")))
        blendBox.currentIndex = Math.max(0, blendBox.indexOfValue(e ? (e.blendMode || "normal") : "normal"))
        uniforms = e && e.uniforms ? Object.assign({}, e.uniforms) : ({})
        errorText = ""
        rebuildDefs()
    }

    function rebuildDefs() { defs = glsl && controller ? controller.uniformDefs(codeArea.text) : [] }

    function valueOf(def) {
        var v = uniforms[def.name]
        return v === undefined ? def.def : v
    }

    function fields() {
        return {
            label: labelField.text,
            code: codeArea.text,
            type: currentType,
            blendMode: blendBox.currentValue || "normal",
            tags: tagsField.text,
            category: categoryField.text,
            uniforms: currentUniforms(),
            noPrecheck: forceSave   // "Trotzdem": mpv entscheidet, nicht der Probelauf
        }
    }

    // Nur Werte behalten, die der Code noch kennt, und neue Regler mit ihrem Vorgabewert ergänzen
    function currentUniforms() {
        var out = {}
        for (var i = 0; i < defs.length; ++i) out[defs[i].name] = valueOf(defs[i])
        return out
    }

    function preview() {
        errorText = controller.check(codeArea.text)
        if (errorText !== "") return
        controller.setPreview(entryId, fields())
    }

    // Prüfen wie das Userscript vor dem Speichern: Meldung des GPU-Treibers statt erst beim Abspielen
    function validated() {
        errorText = controller.check(codeArea.text)
        if (errorText === "") {
            var message = controller.validate(fields())
            if (message !== "") errorText = qsTr("GLSL-Fehler: ") + message
        }
        return errorText === ""
    }

    function test() {
        if (validated()) preview()
    }

    function setValue(name, value) {
        var u = Object.assign({}, uniforms)
        u[name] = value
        uniforms = u
        if (controller.check(codeArea.text) === "") liveTimer.restart()
    }

    function save() {
        // Leerer Code und Klammerfehler sperren immer; eine Treibermeldung lässt sich beim zweiten Klick übergehen
        if (controller.check(codeArea.text) !== "") { errorText = controller.check(codeArea.text); return }
        if (!forceSave && !validated()) { forceSave = true; return }
        if (editing) controller.updateEntry(entryId, fields())
        else controller.addEntry(fields())
        controller.clearPreview()
        modal.close("saved")
    }

    function cancel() {
        controller.clearPreview()
        modal.close()
    }

    function toggleSize() {
        wide = !wide
        plugin.call("modal.open", wide ? { panel: "editor", scope: "video", mode: "movable", width: 760, height: 620, data: modal.data }
                                       : { panel: "editor", scope: "sidebar", mode: "fill", data: modal.data })
    }

    onModalChanged: if (modal) loadFrom(modal.data)
    onControllerChanged: if (modal && token < 0) loadFrom(modal.data)
    Connections {
        target: root.modal ? root.modal : null
        ignoreUnknownSignals: true
        function onDataChanged() { root.loadFrom(root.modal.data) }
    }
    Component.onDestruction: if (controller) controller.clearPreview()

    Timer { id: defsTimer; interval: 400; onTriggered: root.rebuildDefs() }
    // TextEdit zeichnet nur den zuletzt bekannten sichtbaren Bereich: Nach einer Größenänderung neu melden
    Timer {
        id: viewportTimer
        interval: 40
        onTriggered: {
            var f = codeScroll.contentItem
            f.contentY += 1
            f.contentY -= 1
        }
    }
    onWidthChanged: viewportTimer.restart()
    onHeightChanged: viewportTimer.restart()
    Timer { id: liveTimer; interval: 90; onTriggered: root.preview() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            TextField {
                id: labelField
                objectName: "labelField"
                Layout.fillWidth: true
                placeholderText: qsTr("Name, z. B. Sobel Sketch")
            }
            ComboBox {
                id: typeBox
                objectName: "typeBox"
                Layout.preferredWidth: 86
                textRole: "text"
                valueRole: "value"
                model: [{ value: "webgl", text: "GLSL" }, { value: "svg", text: "SVG" }, { value: "canvas2d", text: "2D" }, { value: "audio", text: "Audio" }]
                onActivated: root.rebuildDefs()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: root.glsl
            Label { text: qsTr("Mischmodus"); color: root.muted; font.pixelSize: 12 }
            ComboBox {
                id: blendBox
                objectName: "blendBox"
                Layout.fillWidth: true
                enabled: !root.hook
                textRole: "text"
                valueRole: "value"
                model: root.blendModes
                onActivated: root.preview()
                ToolTip.text: qsTr("mpv-Hook-Shader bestimmen ihr Bild selbst und nutzen keinen Mischmodus.")
                ToolTip.visible: root.hook && hovered
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            TextField { id: tagsField; Layout.fillWidth: true; placeholderText: qsTr("Tags, mit Komma getrennt") }
            TextField { id: categoryField; Layout.preferredWidth: 96; placeholderText: qsTr("Kategorie") }
        }

        Label {
            Layout.fillWidth: true
            visible: !root.glsl
            color: root.muted
            wrapMode: Text.Wrap
            font.pixelSize: 11
            text: qsTr("Dieser Typ läuft nur im Browser. Er bleibt gespeichert und wird mit exportiert, aber hier nicht abgespielt.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                Layout.fillWidth: true
                color: root.muted
                font.pixelSize: 11
                elide: Text.ElideRight
                text: !root.glsl ? "" : (root.hook ? qsTr("mpv-Hook") : qsTr("GLSL-Filter"))
                ToolTip.text: root.hook ? qsTr("mpv-Hook-Shader: läuft unverändert mit allen Pässen.") : qsTr("GLSL-Filter: wird für mpv umgewandelt.")
                ToolTip.visible: kindHover.hovered
                HoverHandler { id: kindHover }
            }
            Button {
                implicitHeight: 24; padding: 2; leftPadding: 6; rightPadding: 6
                text: qsTr("Einfügen")
                font.pixelSize: 11
                focusPolicy: Qt.NoFocus
                onClicked: {
                    codeArea.text = controller.clipboardText()
                    if (labelField.text === "") typeBox.currentIndex = Math.max(0, typeBox.indexOfValue(controller.detect(codeArea.text)))
                    root.rebuildDefs()
                }
            }
            Button {
                implicitHeight: 24; padding: 2; leftPadding: 6; rightPadding: 6
                text: qsTr("Kopieren")
                font.pixelSize: 11
                focusPolicy: Qt.NoFocus
                onClicked: controller.copyText(codeArea.text)
            }
            Button {
                objectName: "sizeButton"
                implicitHeight: 24; padding: 2; leftPadding: 6; rightPadding: 6
                text: root.wide ? qsTr("Im Tab") : qsTr("Größer")
                font.pixelSize: 11
                focusPolicy: Qt.NoFocus
                onClicked: root.toggleSize()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 110
            color: root.panel2
            border.color: codeArea.activeFocus ? root.accent : root.line
            radius: 4
            clip: true
            ScrollView {
                id: codeScroll
                anchors.fill: parent
                anchors.margins: 2
                TextArea {
                    id: codeArea
                    objectName: "codeArea"
                    wrapMode: TextEdit.NoWrap
                    selectByMouse: true
                    color: "#d0ffb0"
                    background: null
                    font.family: "Consolas"
                    font.pixelSize: 12
                    placeholderText: qsTr("GLSL-Fragment-Shader oder mpv-Hook-Shader.\n\nVerfügbar: u_video, u_res, u_time, v_uv, fragColor\n\nvoid main() {\n    fragColor = texture(u_video, v_uv);\n}\n\nRegler:\n// @uniform float STARK 1.0 0.0 2.0 \"Stärke\"")
                    onTextChanged: { root.forceSave = false; defsTimer.restart() }
                }
            }
        }

        ScrollView {
            id: paramsView
            objectName: "paramsView"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(paramsColumn.implicitHeight + 2, root.height * 0.32)
            visible: root.defs.length > 0
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: paramsColumn
                width: paramsView.availableWidth
                spacing: 2
                Label { text: qsTr("Shader-Parameter"); color: root.accent; font.bold: true; font.pixelSize: 12 }
                Repeater {
                    model: root.defs
                    delegate: ColumnLayout {
                        id: paramItem
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            visible: paramItem.modelData.groupHeader !== ""
                            text: paramItem.modelData.groupHeader
                            color: root.muted
                            font.bold: true
                            font.pixelSize: 11
                            Layout.topMargin: 4
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                Layout.preferredWidth: 104
                                text: paramItem.modelData.label
                                color: root.fg
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                ToolTip.text: paramItem.modelData.label + " (" + paramItem.modelData.name + ")"
                                ToolTip.visible: paramHover.hovered
                                ToolTip.delay: 500
                                HoverHandler { id: paramHover }
                            }
                            Slider {
                                objectName: "paramSlider"
                                visible: paramItem.modelData.kind === "slider"
                                Layout.fillWidth: true
                                from: paramItem.modelData.min === undefined ? 0 : paramItem.modelData.min
                                to: paramItem.modelData.max === undefined ? 1 : paramItem.modelData.max
                                stepSize: paramItem.modelData.step === undefined ? 0 : paramItem.modelData.step
                                value: paramItem.modelData.kind === "slider" ? root.valueOf(paramItem.modelData) : 0
                                focusPolicy: Qt.NoFocus
                                onMoved: root.setValue(paramItem.modelData.name, value)
                            }
                            Label {
                                visible: paramItem.modelData.kind === "slider"
                                Layout.preferredWidth: 42
                                horizontalAlignment: Text.AlignRight
                                text: paramItem.modelData.kind === "slider" ? Number(root.valueOf(paramItem.modelData)).toFixed(paramItem.modelData.step === 1 ? 0 : 2) : ""
                                color: root.muted
                                font.family: "Consolas"
                                font.pixelSize: 11
                            }
                            CheckBox {
                                visible: paramItem.modelData.kind === "toggle"
                                Layout.fillWidth: true
                                padding: 0
                                focusPolicy: Qt.NoFocus
                                checked: paramItem.modelData.kind === "toggle" && Number(root.valueOf(paramItem.modelData)) >= 0.5
                                onToggled: root.setValue(paramItem.modelData.name, checked ? 1.0 : 0.0)
                            }
                            ComboBox {
                                objectName: "paramSelect"
                                visible: paramItem.modelData.kind === "select"
                                Layout.fillWidth: true
                                implicitHeight: 26
                                font.pixelSize: 11
                                textRole: "label"
                                valueRole: "value"
                                model: paramItem.modelData.options || []
                                focusPolicy: Qt.NoFocus
                                Component.onCompleted: if (paramItem.modelData.kind === "select") currentIndex = Math.max(0, indexOfValue(root.valueOf(paramItem.modelData)))
                                onActivated: root.setValue(paramItem.modelData.name, currentValue)
                            }
                        }
                    }
                }
            }
        }

        Label {
            objectName: "statusLabel"
            Layout.fillWidth: true
            visible: text !== ""
            wrapMode: Text.Wrap
            maximumLineCount: 5
            elide: Text.ElideRight
            font.pixelSize: 11
            color: root.errorText !== "" || root.previewState === "error" ? root.errorColor : root.muted
            text: root.errorText !== "" ? root.errorText
                : root.previewState === "error" ? qsTr("Shaderfehler: ") + (controller ? controller.previewError : "")
                : root.previewState === "checking" ? qsTr("Vorschau wird geprüft …")
                : root.previewState === "ok" ? qsTr("Vorschau läuft ohne Fehler.")
                : root.previewState === "novideo" ? qsTr("Kein Video geladen: geprüft wird beim nächsten Abspielen.") : ""
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Button { objectName: "cancelButton"; text: qsTr("Abbrechen"); Layout.fillWidth: true; onClicked: root.cancel() }
            Button {
                objectName: "testButton"
                text: qsTr("Testen")
                Layout.fillWidth: true
                enabled: root.glsl
                onClicked: root.test()
            }
            Button {
                objectName: "saveButton"
                text: root.forceSave ? qsTr("Trotzdem") : (root.editing ? qsTr("Speichern") : qsTr("Hinzufügen"))
                highlighted: true
                Layout.fillWidth: true
                onClicked: root.save()
            }
        }
    }

    readonly property string previewState: controller ? controller.previewStatus : ""
}
