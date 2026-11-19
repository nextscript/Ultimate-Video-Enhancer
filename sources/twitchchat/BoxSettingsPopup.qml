import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Benachrichtigungs-Boxen einstellen (Glocke in der Statuszeile): Farbe jeder Box-Art und die
// Liste der Event-Bots (Login, Etikett, eigene Farbe). Änderungen gelten sofort, auch für
// Nachrichten, die schon im Chat stehen, und werden in den Plugin-Einstellungen gespeichert.
Popup {
    id: boxSettings
    objectName: "boxSettingsPopup"
    property var panel
    property var controller
    property string editLogin: ""        // Bot, der gerade bearbeitet wird ("" = neuer Bot)
    property string formColor: ""        // Farbe im Bot-Formular ("" = Bot-Standardfarbe)
    property string formError: ""

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
    readonly property var kinds: [
        { kind: "mention", de: "Erwähnung (@du)", en: "Mention (@you)" },
        { kind: "system",  de: "System (Sub, Raid …)", en: "System (sub, raid …)" },
        { kind: "follow",  de: "Follow", en: "Follow" },
        { kind: "bits",    de: "Bits", en: "Bits" },
        { kind: "bot",     de: "Event-Bots (Standard)", en: "Event bots (default)" },
        { kind: "twitch",  de: "tmi.twitch.tv", en: "tmi.twitch.tv" },
        { kind: "automod", de: "AutoMod", en: "AutoMod" }
    ]
    readonly property var colors: controller ? controller.boxColors : ({})
    readonly property var defaults: controller ? controller.defaultBoxColors() : ({})
    readonly property var bots: controller ? controller.eventBots : []

    function kindColor(kind) {
        var c = colors[kind]
        return c ? c : (kind === "mention" ? panel.accent() : "#888888")
    }
    function botColor(bot) { return bot && bot.color ? bot.color : kindColor("bot") }

    function resetForm() {
        editLogin = ""
        formColor = ""
        formError = ""
        botLogin.text = ""
        botLabel.text = ""
    }
    function editBot(bot) {
        editLogin = bot.login
        formColor = bot.color || ""
        formError = ""
        botLogin.text = bot.login
        botLabel.text = bot.label
        flick.contentY = Math.max(0, botForm.y - 4)
        botLogin.forceActiveFocus()
    }
    function saveBot() {
        if (!controller) return
        if (!controller.setEventBot(botLogin.text, botLabel.text, formColor, editLogin)) {
            formError = tr("Ungültiger Twitch-Name (nur Buchstaben, Ziffern, _).", "Invalid Twitch name (letters, digits, _ only).")
            return
        }
        resetForm()
    }

    // Farbauswahl für eine Box-Art (target = {kind}) oder das Bot-Formular (target = {form: true})
    function chooseColor(target, current, allowDefault) {
        picker.target = target
        picker.allowDefault = allowDefault
        picker.value = current
        hexField.text = current
        picker.open()
    }
    function applyColor(value) {
        var t = picker.target
        if (t.form) formColor = value
        else if (controller) controller.setBoxColor(t.kind, value)
        picker.close()
    }

    onOpened: resetForm()

    component Swatch: Rectangle {
        property color swatch: "#888"
        signal picked()
        implicitWidth: 26
        implicitHeight: 22
        radius: 4
        color: Qt.rgba(swatch.r, swatch.g, swatch.b, 0.25)
        border.color: swatch
        border.width: 2
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.picked()
        }
    }

    contentItem: ColumnLayout {
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Label {
                objectName: "boxSettingsTitle"
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: boxSettings.tr("Benachrichtigungs-Boxen", "Notification boxes")
                font.bold: true
                font.pixelSize: panel.chatFontSize + 3
                color: panel.textColor()
            }
            ToolButton {
                objectName: "boxSettingsClose"
                text: "✕"
                implicitWidth: 30
                onClicked: boxSettings.close()
            }
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: content.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: content
                width: flick.width - 8
                spacing: 6

                // ——— Farben je Box-Art ———
                Label {
                    text: boxSettings.tr("Farben", "Colours")
                    font.bold: true
                    color: panel.textColor()
                    font.pixelSize: panel.chatFontSize
                }
                Repeater {
                    model: boxSettings.kinds
                    RowLayout {
                        objectName: "boxKindRow"
                        property string kind: modelData.kind
                        Layout.fillWidth: true
                        spacing: 6
                        Swatch {
                            objectName: "kindSwatch"
                            swatch: boxSettings.kindColor(modelData.kind)
                            onPicked: boxSettings.chooseColor({ kind: modelData.kind }, boxSettings.colors[modelData.kind] || "", true)
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: boxSettings.tr(modelData.de, modelData.en)
                            color: panel.textColor()
                            font.pixelSize: panel.chatFontSize
                        }
                        ToolButton {
                            objectName: "kindReset"
                            text: "↺"
                            implicitWidth: 28
                            visible: (boxSettings.colors[modelData.kind] || "") !== (boxSettings.defaults[modelData.kind] || "")
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: boxSettings.tr("Standardfarbe", "Default colour")
                            onClicked: if (controller) controller.setBoxColor(modelData.kind, "")
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.topMargin: 4; height: 1; color: panel.muted(); opacity: 0.4 }

                // ——— Event-Bots ———
                Label {
                    text: boxSettings.tr("Event-Bots", "Event bots")
                    font.bold: true
                    color: panel.textColor()
                    font.pixelSize: panel.chatFontSize
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: panel.muted()
                    font.pixelSize: Math.max(9, panel.chatFontSize - 2)
                    text: boxSettings.tr("Nachrichten dieser Konten stehen in einer Box mit dem Etikett.",
                                         "Messages from these accounts are shown in a box with the label.")
                }
                ColumnLayout {
                    id: botForm
                    Layout.fillWidth: true
                    spacing: 4
                    TextField {
                        id: botLogin
                        objectName: "botLoginField"
                        Layout.fillWidth: true
                        maximumLength: 26
                        font.pixelSize: panel.chatFontSize
                        placeholderText: boxSettings.tr("Twitch-Name des Bots", "Bot's Twitch name")
                        onTextChanged: boxSettings.formError = ""
                        onAccepted: botLabel.forceActiveFocus()
                    }
                    TextField {
                        id: botLabel
                        objectName: "botLabelField"
                        Layout.fillWidth: true
                        maximumLength: 40
                        font.pixelSize: panel.chatFontSize
                        placeholderText: boxSettings.tr("Etikett (frei lassen = Name)", "Label (empty = name)")
                        onAccepted: boxSettings.saveBot()
                    }
                    Label {
                        visible: boxSettings.formError !== ""
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: boxSettings.formError
                        color: panel.errorColor()
                        font.pixelSize: Math.max(9, panel.chatFontSize - 2)
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Swatch {
                            objectName: "botFormSwatch"
                            y: 4
                            swatch: boxSettings.formColor || boxSettings.kindColor("bot")
                            onPicked: boxSettings.chooseColor({ form: true }, boxSettings.formColor, true)
                        }
                        Button {
                            objectName: "botSave"
                            text: boxSettings.editLogin ? boxSettings.tr("Speichern", "Save") : boxSettings.tr("Hinzufügen", "Add")
                            enabled: botLogin.text.trim() !== ""
                            onClicked: boxSettings.saveBot()
                        }
                        Button {
                            objectName: "botCancel"
                            visible: boxSettings.editLogin !== ""
                            text: boxSettings.tr("Abbrechen", "Cancel")
                            onClicked: boxSettings.resetForm()
                        }
                    }
                }
                Repeater {
                    model: boxSettings.bots
                    RowLayout {
                        objectName: "botRow"
                        property string login: modelData.login
                        Layout.fillWidth: true
                        spacing: 4
                        Swatch {
                            objectName: "botSwatch"
                            swatch: boxSettings.botColor(modelData)
                            onPicked: boxSettings.chooseColor({ bot: modelData }, modelData.color || "", true)
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: modelData.label
                            font.bold: boxSettings.editLogin === modelData.login
                            color: panel.textColor()
                            font.pixelSize: panel.chatFontSize
                        }
                        Label {
                            Layout.maximumWidth: 90
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            visible: modelData.label.toLowerCase() !== modelData.login
                            text: modelData.login
                            color: panel.muted()
                            font.pixelSize: Math.max(9, panel.chatFontSize - 2)
                        }
                        ToolButton {
                            objectName: "botEdit"
                            text: "✎"
                            implicitWidth: 28
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: boxSettings.tr("Bearbeiten", "Edit")
                            onClicked: boxSettings.editBot(modelData)
                        }
                        ToolButton {
                            objectName: "botRemove"
                            text: "🗑"
                            implicitWidth: 28
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: boxSettings.tr("Entfernen", "Remove")
                            onClicked: {
                                if (boxSettings.editLogin === modelData.login) boxSettings.resetForm()
                                if (controller) controller.removeEventBot(modelData.login)
                            }
                        }
                    }
                }
                Label {
                    visible: boxSettings.bots.length === 0
                    text: boxSettings.tr("Keine Event-Bots eingetragen.", "No event bots.")
                    color: panel.muted()
                    font.pixelSize: panel.chatFontSize
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    spacing: 6
                    Button {
                        objectName: "resetBoxColors"
                        text: boxSettings.tr("Farben zurücksetzen", "Reset colours")
                        onClicked: if (controller) controller.resetBoxColors()
                    }
                    Button {
                        objectName: "resetEventBots"
                        text: boxSettings.tr("Bot-Liste zurücksetzen", "Reset bot list")
                        onClicked: { boxSettings.resetForm(); if (controller) controller.resetEventBots() }
                    }
                }
            }
        }
    }

    // ——— Farbauswahl ———
    Popup {
        id: picker
        objectName: "boxColorPicker"
        property var target: ({})
        property bool allowDefault: true
        property string value: ""
        readonly property var palette: ["#f1c40f", "#f39c12", "#e67e22", "#e74c3c", "#ff4f9a", "#c0392b",
                                        "#9147ff", "#8e44ad", "#4a9eff", "#3498db", "#1abc9c", "#00c8af",
                                        "#2ecc71", "#27ae60", "#a3e635", "#ecf0f1", "#95a5a6", "#7f8c8d"]
        parent: boxSettings.contentItem
        anchors.centerIn: parent
        width: Math.min(boxSettings.width - 20, 260)
        modal: true
        dim: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 10
        background: Rectangle {
            color: Qt.color(panel.textColor()).hslLightness > 0.5 ? "#232329" : "#f4f4f6"
            radius: 6
            border.color: panel.accent()
        }
        function commit(v) {
            var t = picker.target
            if (t.bot) {
                if (controller) controller.setEventBot(t.bot.login, t.bot.label, v, t.bot.login)
                picker.close()
            } else {
                boxSettings.applyColor(v)
            }
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: boxSettings.tr("Farbe wählen", "Choose colour")
                font.bold: true
                color: panel.textColor()
                font.pixelSize: panel.chatFontSize
            }
            Grid {
                columns: 6
                spacing: 6
                Repeater {
                    model: picker.palette
                    Rectangle {
                        objectName: "paletteColor"
                        property string hex: modelData
                        width: 30
                        height: 24
                        radius: 4
                        color: modelData
                        border.width: picker.value.toLowerCase() === modelData ? 3 : 1
                        border.color: picker.value.toLowerCase() === modelData ? panel.textColor() : Qt.darker(modelData, 1.4)
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: picker.commit(modelData)
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Rectangle {
                    width: 24
                    height: 24
                    radius: 4
                    color: /^#[0-9a-fA-F]{6}$/.test(hexField.text) ? hexField.text : "transparent"
                    border.color: panel.muted()
                }
                TextField {
                    id: hexField
                    objectName: "boxColorHex"
                    Layout.fillWidth: true
                    font.pixelSize: panel.chatFontSize
                    placeholderText: "#rrggbb"
                    maximumLength: 7
                    validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{0,6}/ }
                    onAccepted: if (/^#?[0-9a-fA-F]{6}$/.test(text)) picker.commit(text.charAt(0) === "#" ? text : "#" + text)
                }
                Button {
                    objectName: "boxColorOk"
                    text: "OK"
                    implicitWidth: 50
                    enabled: /^#?[0-9a-fA-F]{6}$/.test(hexField.text)
                    onClicked: picker.commit(hexField.text.charAt(0) === "#" ? hexField.text : "#" + hexField.text)
                }
            }
            Button {
                objectName: "boxColorDefault"
                visible: picker.allowDefault
                Layout.fillWidth: true
                text: boxSettings.tr("Standardfarbe", "Default colour")
                onClicked: picker.commit("")
            }
        }
    }
}
