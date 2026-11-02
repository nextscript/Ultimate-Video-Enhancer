import QtQuick
import QtQuick.Controls

// Diagnose: Decoder, Video-Ausgabe, FPS, verworfene Bilder, A/V-Abweichung, aktive Shader-Kette
Rectangle {
    id: o
    property var player
    property var shaders
    readonly property var d: player ? player.diagnostics : ({})
    color: "#c0000000"
    radius: 6
    width: txt.implicitWidth + 20
    height: txt.implicitHeight + 16
    function v(k) { var x = d[k]; return x === undefined || x === null || x === "" ? "–" : x }
    Text {
        id: txt
        x: 10; y: 8
        color: "#e8e8e8"
        font.family: "Consolas"
        font.pixelSize: 12
        text: [
            qsTr("Decoder (hwdec):   ") + (o.v("hwdec-current") === "–" ? qsTr("Software") : o.v("hwdec-current")),
            qsTr("Video-Ausgabe:     ") + o.v("current-vo") + qsTr(" (Render-API, OpenGL)"),
            qsTr("Video:             ") + o.v("video-codec"),
            qsTr("Auflösung:         ") + o.v("video-params/w") + "×" + o.v("video-params/h") + "  " + o.v("video-params/pixelformat"),
            qsTr("Farbraum:          ") + o.v("video-params/primaries") + " / " + o.v("video-params/gamma"),
            qsTr("FPS (Quelle/est.): ") + Number(o.d["container-fps"] || 0).toFixed(3) + " / " + Number(o.d["estimated-vf-fps"] || 0).toFixed(3),
            qsTr("Verworfen (VO/Dec):") + " " + o.v("frame-drop-count") + " / " + o.v("decoder-frame-drop-count"),
            qsTr("A/V-Abweichung:    ") + (o.d["avsync"] !== undefined ? (Number(o.d["avsync"]) * 1000).toFixed(1) + qsTr(" ms") : "–"),
            qsTr("Audio:             ") + o.v("audio-codec-name") + "  " + o.v("audio-params/channel-count") + qsTr(" ch  ") + o.v("audio-params/samplerate") + qsTr(" Hz"),
            qsTr("Bitrate V/A:       ") + Math.round((o.d["video-bitrate"] || 0) / 1000) + " / " + Math.round((o.d["audio-bitrate"] || 0) / 1000) + qsTr(" kbit/s"),
            qsTr("Puffer:            ") + o.v("cache-buffering-state") + " %",
            qsTr("Shader-Kette:      ") + (o.shaders && o.shaders.activeShaders.length ? o.shaders.activeShaders.map(s => s.split(/[\\/]/).pop()).join(" → ") : "keine"),
        ].join("\n")
    }
}
