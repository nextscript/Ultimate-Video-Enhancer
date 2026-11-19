import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var controller
    property var theme
    implicitHeight: content.implicitHeight + 24

    readonly property bool de: !controller || controller.language.indexOf("de") === 0
    function t(deText, enText) { return root.de ? deText : enText }
    readonly property color textColor: theme ? theme.text : "white"
    readonly property color mutedColor: theme ? theme.muted : "#999"

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Label { text: "UVESponsorSkip"; font.bold: true; color: root.textColor; Layout.fillWidth: true }
            Switch {
                checked: !!controller && controller.enabled
                onToggled: controller.enabled = checked
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: root.mutedColor
            text: root.t("Überspringt bei YouTube-Videos Abschnitte aus der SponsorBlock-Datenbank. Je Kategorie: automatisch, per Taste (Strg+Alt+S) oder ignorieren. Strg+Alt+Z macht den letzten Sprung rückgängig.",
                         "Skips sections of YouTube videos from the SponsorBlock database. Per category: automatically, by key (Ctrl+Alt+S) or ignore. Ctrl+Alt+Z undoes the last skip.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                Layout.fillWidth: true
                text: root.t("⏭ Überspringen", "⏭ Skip")
                enabled: !!controller && controller.enabled && controller.segments.length > 0
                onClicked: controller.skip()
            }
            Button {
                Layout.fillWidth: true
                text: root.t("↩ Sprung rückgängig", "↩ Undo skip")
                enabled: !!controller && controller.canUndo
                onClicked: controller.undo()
            }
        }

        Label {
            visible: !!controller && controller.status !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: root.textColor
            text: controller ? controller.status : ""
        }
        Repeater {
            model: controller ? controller.segments : []
            delegate: ItemDelegate {
                Layout.fillWidth: true
                padding: 4
                onClicked: controller.seekTo(modelData.start)
                ToolTip.visible: hovered
                ToolTip.text: root.t("Zum Anfang springen", "Jump to start")
                contentItem: RowLayout {
                    spacing: 8
                    Rectangle { width: 10; height: 10; radius: 2; color: modelData.color }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: root.textColor
                        text: modelData.label + (modelData.mute ? root.t(" (stumm)", " (mute)") : "")
                    }
                    Label { text: modelData.time; color: root.mutedColor; font.family: "Consolas" }
                }
            }
        }

        Label { text: root.t("Kategorien", "Categories"); font.bold: true; color: root.textColor; Layout.topMargin: 6 }
        Repeater {
            model: controller ? controller.categories : []
            delegate: RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle { width: 10; height: 10; radius: 2; color: modelData.color }
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 60
                    elide: Text.ElideRight
                    color: root.textColor
                    text: modelData.label
                }
                ComboBox {
                    Layout.preferredWidth: 150
                    model: [root.t("Ignorieren", "Ignore"), root.t("Per Taste", "By key"), root.t("Automatisch", "Automatic")]
                    currentIndex: modelData.mode
                    onActivated: (i) => controller.setMode(modelData.id, i)
                }
            }
        }

        CheckBox {
            Layout.topMargin: 6
            checked: !!controller && controller.countViews
            onToggled: controller.countViews = checked
            text: root.t("Übersprünge anonym an SponsorBlock melden", "Report skips anonymously to SponsorBlock")
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: root.mutedColor
            font.pixelSize: 11
            text: root.t("Daten: SponsorBlock (sponsor.ajay.app), CC BY-NC-SA 4.0. Abgefragt wird nur ein Teil des Hashwerts der Video-ID.",
                         "Data: SponsorBlock (sponsor.ajay.app), CC BY-NC-SA 4.0. Only a part of the video ID's hash is sent.")
        }
        Item { Layout.fillHeight: true }
    }
}
