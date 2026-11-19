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

    readonly property var heights: [0, 1080, 720, 540, 360]
    readonly property var formats: ["flv", "hls"]

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10
        Label { text: "UVETikTok"; font.bold: true; color: theme ? theme.text : "white" }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme ? theme.muted : "#999"
            text: root.t("Öffne einfach einen TikTok-Link: Video (auch vm.tiktok.com-Kurzlinks), LIVE (tiktok.com/@name/live) oder ein Profil, das gerade live ist. Alles läuft als Stream, es wird keine Datei gespeichert.",
                         "Just open a TikTok link: a video (vm.tiktok.com short links too), a LIVE (tiktok.com/@name/live) or a profile that is live right now. Everything plays as a stream, no file is saved.")
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: root.t("Höchste Qualität", "Maximum quality"); color: theme ? theme.text : "white" }
            ComboBox {
                Layout.fillWidth: true
                model: root.heights.map(h => h === 0 ? root.t("Beste", "Best") : h + "p")
                currentIndex: controller ? Math.max(0, root.heights.indexOf(controller.maxHeight)) : 0
                onActivated: (i) => controller.maxHeight = root.heights[i]
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: root.t("LIVE-Format", "LIVE format"); color: theme ? theme.text : "white" }
            ComboBox {
                Layout.fillWidth: true
                model: [root.t("FLV (geringe Verzögerung)", "FLV (low latency)"), root.t("HLS (stabiler)", "HLS (more stable)")]
                currentIndex: controller ? Math.max(0, root.formats.indexOf(controller.liveFormat)) : 0
                onActivated: (i) => controller.liveFormat = root.formats[i]
            }
        }
        Label {
            visible: !!controller && controller.status !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme ? theme.text : "white"
            text: controller ? controller.status : ""
        }
        Item { Layout.fillHeight: true }
    }
}
