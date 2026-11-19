// LumaScale tab in the sidebar. The host sets controller and theme.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var controller
    property var theme

    readonly property color fg: theme ? theme.text : "#e6e8eb"
    readonly property color muted: theme ? theme.muted : "#8b93a1"
    readonly property color accent: theme ? theme.accent : "#ff8a1f"
    readonly property color errorColor: theme && theme.error ? theme.error : "#ff5c5c"
    readonly property var values: controller ? controller.values : ({})

    ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroller.availableWidth
            spacing: 8

            Item { Layout.preferredHeight: 2 }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                CheckBox {
                    objectName: "lumascaleEnabled"
                    text: "LumaScale aktiv"
                    checked: root.controller ? root.controller.enabled : false
                    onToggled: root.controller.enabled = checked
                }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "lumascaleReset"
                    text: "Standard"
                    onClicked: root.controller.resetDefaults()
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                wrapMode: Text.Wrap
                visible: text.length > 0
                color: root.errorColor
                text: root.controller ? root.controller.error : ""
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                wrapMode: Text.Wrap
                color: root.muted
                font.pixelSize: 11
                text: "FSR wirkt nur, wenn das Video hochskaliert wird, also das Fenster größer als das Video ist."
            }

            Label {
                Layout.leftMargin: 12
                Layout.topMargin: 4
                text: "Shader Parameters"
                font.bold: true
                color: root.accent
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.bottomMargin: 12
                spacing: 4
                enabled: root.controller ? root.controller.enabled : false
                opacity: enabled ? 1 : 0.45

                Repeater {
                    model: root.controller ? root.controller.parameters : []
                    delegate: ColumnLayout {
                        id: row
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: row.modelData.label
                                color: root.fg
                                ToolTip.text: row.modelData.note
                                ToolTip.visible: hover.hovered
                                ToolTip.delay: 300
                                HoverHandler { id: hover }
                            }
                            Label {
                                text: Number(slider.value).toFixed(2)
                                color: root.muted
                                font.family: "Consolas"
                            }
                        }
                        Slider {
                            id: slider
                            objectName: "lumascale_" + row.modelData.key
                            Layout.fillWidth: true
                            from: row.modelData.from
                            to: row.modelData.to
                            stepSize: row.modelData.step
                            snapMode: Slider.SnapAlways
                            value: root.values[row.modelData.key] !== undefined ? root.values[row.modelData.key] : row.modelData.def
                            onMoved: root.controller.setParameter(row.modelData.key, value)
                        }
                    }
                }
            }
        }
    }
}
