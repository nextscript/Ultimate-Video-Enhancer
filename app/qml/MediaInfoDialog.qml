import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dlg
    property var theme
    property var info: ({})
    title: qsTr("Medieninformationen")
    modal: true
    width: Math.min(720, parent ? parent.width - 40 : 720)
    height: Math.min(560, parent ? parent.height - 40 : 560)
    anchors.centerIn: parent
    standardButtons: Dialog.Close
    Component.onCompleted: { var b = standardButton(Dialog.Close); if (b) b.text = qsTr("Schließen") }
    onAboutToShow: info = app.mediaInfo()

    ScrollView {
        anchors.fill: parent
        clip: true
        GridLayout {
            width: dlg.availableWidth
            columns: 2
            columnSpacing: 16
            rowSpacing: 6
            Repeater {
                model: Object.keys(dlg.info)
                delegate: Label {
                    required property string modelData
                    required property int index
                    Layout.row: index; Layout.column: 0
                    text: modelData; color: theme.muted
                }
            }
            Repeater {
                model: Object.keys(dlg.info)
                delegate: Label {
                    required property string modelData
                    required property int index
                    Layout.row: index; Layout.column: 1
                    Layout.fillWidth: true
                    text: String(dlg.info[modelData] === undefined || dlg.info[modelData] === "" ? "–" : dlg.info[modelData])
                    color: theme.text
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }
}
