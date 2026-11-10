import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Datei → Disc öffnen… / Disc-Image/Disc-Ordner öffnen…
// Laufwerke (mit erkanntem Medium) oder ISO/VIDEO_TS/BDMV; Typ-Badges nur nach tatsächlicher Erkennung.
Dialog {
    id: dlg
    property var theme
    property int tab: 0                  // 0 = Laufwerk, 1 = ISO/Ordner
    property int driveIndex: -1
    property string sourcePath: ""
    property var sourceInfo: ({})
    property bool inspecting: false
    property bool showInfo: false
    readonly property var disc: app.disc
    readonly property var caps: disc.capabilities
    readonly property var drive: driveIndex >= 0 && driveIndex < disc.drives.length ? disc.drives[driveIndex] : null
    readonly property string kind: tab === 0 ? (drive ? drive.kind : "") : (sourceInfo.kind || "")
    readonly property bool menusAvailable: (kind === "dvd" && caps.dvdMenus === true) || (kind === "bluray" && caps.blurayMenus === true)
    readonly property bool canOpen: tab === 0 ? (!!drive && drive.kind !== "" && !disc.scanning)
                                              : (sourcePath !== "" && !inspecting && sourceInfo.kind !== "" && sourceInfo.kind !== undefined && !sourceInfo.errorText)

    title: qsTr("Disc öffnen")
    modal: true
    width: Math.min(620, parent ? parent.width - 40 : 620)
    anchors.centerIn: parent

    function openTab(t) { tab = t; open() }
    function kindText(k) { return k ? disc.kindLabel(k) : "" }
    function driveStatus(d) {
        if (d.kind === "cdda") return d.audioTracks > 0 ? qsTr("%1 Spuren").arg(d.audioTracks) : ""
        if (d.kind !== "") return d.encrypted ? qsTr("verschlüsselt (AACS)") : ""
        return disc.errorText(d.error)
    }
    function openSelected() {
        if (!canOpen) return
        var menu = menuSwitch.visible && menuSwitch.checked
        app.setSetting("discStartInMenu", menuSwitch.checked)
        if (tab === 0) disc.openDrive(drive.device, menu)
        else disc.openSource(sourcePath, menu)
        close()
    }
    function chooseSource(path) {
        sourcePath = path
        sourceInfo = ({})
        inspecting = true
        disc.inspect(path)
    }

    onOpened: {
        showInfo = false
        if (tab === 0) disc.refreshDrives()
    }

    Connections {
        target: dlg.disc
        function onDrivesChanged() {
            if (dlg.disc.scanning) return
            // erstes Laufwerk mit Medium vorwählen
            var idx = dlg.disc.drives.findIndex(d => d.kind !== "")
            dlg.driveIndex = idx >= 0 ? idx : (dlg.disc.drives.length > 0 ? 0 : -1)
        }
        function onInspected(path, info) {
            if (path !== dlg.sourcePath) return
            dlg.sourceInfo = info
            dlg.inspecting = false
        }
    }

    FileDialog {
        id: imageDialog
        title: qsTr("Disc-Image wählen")
        nameFilters: [qsTr("Disc-Images (*.iso *.img *.udf)"), qsTr("Alle Dateien (*)")]
        onAccepted: dlg.chooseSource(app.toLocalFile(selectedFile))
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Disc-Ordner wählen (enthält VIDEO_TS oder BDMV)")
        onAccepted: dlg.chooseSource(app.toLocalFile(selectedFolder))
    }

    component Badge: Rectangle {
        property string kind
        visible: kind !== ""
        radius: 4
        color: kind === "bluray" ? "#2a5db0" : kind === "dvd" ? "#8a4fd0" : "#2f8f5b"
        implicitWidth: badgeLabel.implicitWidth + 12
        implicitHeight: badgeLabel.implicitHeight + 4
        Label { id: badgeLabel; anchors.centerIn: parent; text: dlg.kindText(parent.kind); color: "white"; font.pixelSize: 11; font.bold: true }
    }

    footer: DialogButtonBox {
        Button { text: dlg.showInfo ? qsTr("Informationen ausblenden") : qsTr("Disc-Informationen"); onClicked: dlg.showInfo = !dlg.showInfo; DialogButtonBox.buttonRole: DialogButtonBox.HelpRole }
        Button { text: qsTr("Abbrechen"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button { text: qsTr("Öffnen"); enabled: dlg.canOpen; highlighted: true; onClicked: dlg.openSelected() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            currentIndex: dlg.tab
            onCurrentIndexChanged: { dlg.tab = currentIndex; if (currentIndex === 0 && dlg.disc.drives.length === 0) dlg.disc.refreshDrives() }
            TabButton { text: qsTr("Laufwerk") }
            TabButton { text: qsTr("ISO/Ordner") }
        }

        // ---------------------------------------------------------------- Laufwerke
        ColumnLayout {
            visible: dlg.tab === 0
            Layout.fillWidth: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Label { text: dlg.disc.scanning ? qsTr("Laufwerke werden abgefragt …") : qsTr("Optische Laufwerke"); color: theme.text; font.bold: true; Layout.fillWidth: true }
                BusyIndicator { running: dlg.disc.scanning; visible: running; implicitWidth: 22; implicitHeight: 22 }
                Button { text: qsTr("Aktualisieren"); enabled: !dlg.disc.scanning; onClicked: dlg.disc.refreshDrives() }
            }
            Label {
                visible: !dlg.disc.scanning && dlg.disc.drives.length === 0
                text: dlg.disc.errorText(1)
                color: theme.muted
            }
            ListView {
                id: driveList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 200)
                clip: true
                model: dlg.disc.drives
                currentIndex: dlg.driveIndex
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    highlighted: index === dlg.driveIndex
                    onClicked: dlg.driveIndex = index
                    onDoubleClicked: { dlg.driveIndex = index; dlg.openSelected() }
                    contentItem: RowLayout {
                        spacing: 10
                        Label { text: modelData.name; color: theme.text; font.bold: true; font.family: "Consolas" }
                        Badge { kind: modelData.kind }
                        Label { text: modelData.label; color: theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { text: dlg.driveStatus(modelData); color: modelData.kind !== "" ? theme.muted : theme.error }
                    }
                }
            }
        }

        // ---------------------------------------------------------------- ISO/Ordner
        ColumnLayout {
            visible: dlg.tab === 1
            Layout.fillWidth: true
            spacing: 6
            RowLayout {
                Button { text: qsTr("Image wählen …"); onClicked: imageDialog.open() }
                Button { text: qsTr("Ordner wählen …"); onClicked: folderDialog.open() }
            }
            RowLayout {
                visible: dlg.sourcePath !== ""
                Layout.fillWidth: true
                spacing: 8
                Badge { kind: dlg.sourceInfo.kind || "" }
                Label { text: dlg.sourcePath; color: theme.text; elide: Text.ElideMiddle; Layout.fillWidth: true }
                BusyIndicator { running: dlg.inspecting; visible: running; implicitWidth: 22; implicitHeight: 22 }
            }
            Label {
                visible: !!dlg.sourceInfo.errorText
                text: dlg.sourceInfo.errorText || ""
                color: theme.error; Layout.fillWidth: true; wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.muted
                font.pixelSize: 12
                text: qsTr("Unterstützt: DVD-Video (ISO oder Ordner mit VIDEO_TS) und unverschlüsselte Blu-ray (ISO oder Ordner mit BDMV). Der Typ wird am Inhalt erkannt, nicht an der Dateiendung.")
            }
        }

        // ---------------------------------------------------------------- Startart
        CheckBox {
            id: menuSwitch
            visible: dlg.kind === "dvd" || dlg.kind === "bluray"
            enabled: dlg.menusAvailable
            checked: dlg.menusAvailable && app.setting("discStartInMenu", true)
            text: dlg.menusAvailable ? qsTr("Im Disc-Menü starten") : qsTr("Im Disc-Menü starten (von diesem Build nicht unterstützt – Haupttitel)")
        }
        Label {
            visible: menuSwitch.visible && !menuSwitch.checked
            text: qsTr("Der Haupttitel wird direkt abgespielt.")
            color: theme.muted; font.pixelSize: 12
        }

        // ---------------------------------------------------------------- Disc-Informationen
        GridLayout {
            visible: dlg.showInfo
            columns: 2
            columnSpacing: 14
            rowSpacing: 2
            Layout.fillWidth: true
            Repeater {
                model: dlg.showInfo ? dlg.disc.info() : []
                delegate: Label {
                    required property var modelData
                    required property int index
                    Layout.row: index; Layout.column: 0
                    text: modelData.label; color: theme.muted
                }
            }
            Repeater {
                model: dlg.showInfo ? dlg.disc.info() : []
                delegate: Label {
                    required property var modelData
                    required property int index
                    Layout.row: index; Layout.column: 1; Layout.fillWidth: true
                    text: modelData.value; color: theme.text; wrapMode: Text.WrapAnywhere
                }
            }
        }
    }
}
