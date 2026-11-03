import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Einstellungen → Expert Settings: Feinabstimmung der Grading-Kurven des Ultimate Video Enhancer
// (wie das "Expert Settings"-Fenster des UVE-Userscripts). Änderungen wirken sofort und werden automatisch gespeichert.
ColumnLayout {
    id: page
    property var theme
    property var effects      // UveController (null, wenn das Plugin nicht aktiv ist)
    property string filter: ""
    property string message: ""
    property bool messageError: false
    spacing: 6

    function decimals(step) { var s = String(step); var i = s.indexOf("."); return i < 0 ? 0 : s.length - i - 1 }
    function matches(block, field) {
        if (!filter) return true
        var f = filter.toLowerCase()
        return (block.title + " " + block.desc + " " + (field ? field.label + " " + field.desc + " " + field.key : "")).toLowerCase().indexOf(f) >= 0
    }
    function blockVisible(block) {
        if (!filter) return true
        if (matches(block, null)) return true
        for (var i = 0; i < block.fields.length; ++i) if (matches(block, block.fields[i])) return true
        return false
    }

    Connections {
        target: page.effects
        ignoreUnknownSignals: true
        function onExpertMessage(text, error) { page.message = text; page.messageError = error; msgTimer.restart() }
    }
    Timer { id: msgTimer; interval: 4000; onTriggered: page.message = "" }

    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Expert Settings (*.json)")]
        defaultSuffix: "json"
        onAccepted: page.effects.exportExpert(selectedFile)
    }
    FileDialog {
        id: importDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Expert Settings (*.json)"), qsTr("Alle Dateien (*)")]
        onAccepted: page.effects.importExpert(selectedFile)
    }

    // Kopf
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label { text: qsTr("🧪 Expert Settings"); font.pixelSize: 17; font.bold: true; color: theme.text }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.muted; font.pixelSize: 12
                text: qsTr("Feinabstimmung aller Kurven der Grading-Regler (Effekte → Basis/Farbe/Schärfe). Änderungen wirken sofort und werden automatisch gespeichert.")
            }
        }
        TextField {
            Layout.preferredWidth: 200
            placeholderText: qsTr("🔍 Einstellungen suchen …")
            onTextChanged: page.filter = text.trim()
            enabled: !!page.effects
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        enabled: !!page.effects
        Label {
            Layout.fillWidth: true; elide: Text.ElideRight
            color: page.message ? (page.messageError ? theme.error : theme.accent) : theme.muted
            text: page.message || (page.effects ? (page.effects.expertChangedCount > 0
                    ? qsTr("%1 Werte geändert").arg(page.effects.expertChangedCount) : qsTr("Alle Werte auf Standard")) : "")
        }
        Button { text: qsTr("Importieren …"); onClicked: importDialog.open() }
        Button { text: qsTr("Exportieren …"); onClicked: exportDialog.open() }
        Button {
            text: qsTr("Alle zurücksetzen")
            enabled: !!page.effects && page.effects.expertChangedCount > 0
            onClicked: page.effects.resetExpert("", "")
        }
    }

    Label {
        visible: !page.effects
        Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.muted
        text: qsTr("Das Ultimate-Video-Enhancer-Plugin ist nicht aktiv (Einstellungen → Plugins).")
    }

    ScrollView {
        id: scroll
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: !!page.effects
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 10

            Repeater {
                model: page.effects ? page.effects.expertBlocks : []
                delegate: Rectangle {
                    id: blk
                    required property var modelData
                    Layout.fillWidth: true
                    visible: page.blockVisible(modelData)
                    implicitHeight: blkCol.implicitHeight + 20
                    radius: 8
                    color: theme.panel2
                    border.color: modelData.changed ? theme.accent : theme.line
                    border.width: 1

                    ColumnLayout {
                        id: blkCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: blk.modelData.title; font.bold: true; font.pixelSize: 14; color: theme.text }
                            Label { text: blk.modelData.name; color: theme.muted; font.pixelSize: 10; font.family: "Consolas" }
                            Item { Layout.fillWidth: true }
                            ToolButton {
                                text: "↺"
                                enabled: blk.modelData.changed
                                onClicked: page.effects.resetExpert(blk.modelData.name, "")
                                ToolTip.text: qsTr("Block auf Standard zurücksetzen"); ToolTip.visible: hovered
                            }
                        }
                        Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: blk.modelData.desc; color: theme.muted; font.pixelSize: 12 }

                        Repeater {
                            model: blk.modelData.fields
                            delegate: RowLayout {
                                id: fld
                                required property var modelData
                                Layout.fillWidth: true
                                visible: page.matches(blk.modelData, modelData)
                                spacing: 8
                                opacity: modelData.svgOnly ? 0.6 : 1.0

                                ColumnLayout {
                                    Layout.preferredWidth: 230
                                    Layout.maximumWidth: 230
                                    spacing: 0
                                    Label {
                                        Layout.fillWidth: true; elide: Text.ElideRight
                                        text: fld.modelData.label + (fld.modelData.changed ? "  •" : "")
                                        color: fld.modelData.changed ? theme.accent : theme.text
                                    }
                                    Label {
                                        Layout.fillWidth: true; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight
                                        text: fld.modelData.desc; color: theme.muted; font.pixelSize: 11
                                    }
                                }
                                // Zahl: Regler + Feld
                                Slider {
                                    id: sl
                                    visible: fld.modelData.type === "num"
                                    Layout.fillWidth: true
                                    from: fld.modelData.min; to: fld.modelData.max; stepSize: fld.modelData.step
                                    snapMode: Slider.SnapAlways
                                    value: fld.modelData.type === "num" ? fld.modelData.value : 0
                                    onMoved: page.effects.setExpertValue(blk.modelData.name, fld.modelData.key, value)
                                }
                                TextField {
                                    visible: fld.modelData.type === "num"
                                    Layout.preferredWidth: 72
                                    horizontalAlignment: Text.AlignRight
                                    text: fld.modelData.type === "num" ? Number(fld.modelData.value).toFixed(page.decimals(fld.modelData.step)) : ""
                                    validator: DoubleValidator { bottom: fld.modelData.min; top: fld.modelData.max; locale: "C" }
                                    onEditingFinished: { var v = parseFloat(text.replace(",", ".")); if (!isNaN(v)) page.effects.setExpertValue(blk.modelData.name, fld.modelData.key, v) }
                                }
                                // Schalter
                                Switch {
                                    visible: fld.modelData.type === "bool"
                                    checked: fld.modelData.type === "bool" ? fld.modelData.value : false
                                    onToggled: page.effects.setExpertValue(blk.modelData.name, fld.modelData.key, checked)
                                }
                                Item { visible: fld.modelData.type === "bool"; Layout.fillWidth: true }
                                Label {
                                    text: qsTr("Std. %1").arg(fld.modelData.type === "bool" ? (fld.modelData.def ? qsTr("an") : qsTr("aus"))
                                                                                         : Number(fld.modelData.def).toFixed(page.decimals(fld.modelData.step)))
                                    color: theme.muted; font.pixelSize: 11
                                    Layout.preferredWidth: 64
                                }
                                ToolButton {
                                    text: "↺"
                                    enabled: fld.modelData.changed
                                    onClicked: page.effects.resetExpert(blk.modelData.name, fld.modelData.key)
                                    ToolTip.text: qsTr("Auf Standard zurücksetzen"); ToolTip.visible: hovered
                                }
                            }
                        }
                    }
                }
            }
            Item { implicitHeight: 4 }
        }
    }
}
