import QtQuick
import QtQuick.Effects

// Glaseffekt (Einstellung glassEffect): zeigt den Bereich sourceRect von sourceItem weichgezeichnet und leicht
// gesättigt, am Rand wie durch Glas gebrochen, getönt und mit Glanzkante (shaders/glass.frag).
// sourceItem darf dieses Element nicht enthalten (sonst nimmt es sich selbst auf).
Item {
    id: glass
    property Item sourceItem
    property rect sourceRect
    property color tint: "black"
    property real tintAlpha: 0.3
    property real radius: 0
    property real bezel: 22
    property real refraction: 16
    property real highlight: 1

    // Bereich hinter dem Glas, in halber Auflösung (der Weichzeichner braucht keine Details)
    ShaderEffectSource {
        id: grab
        anchors.fill: parent
        sourceItem: glass.sourceItem
        sourceRect: glass.sourceRect
        textureSize: Qt.size(Math.max(1, Math.round(width / 2)), Math.max(1, Math.round(height / 2)))
        wrapMode: ShaderEffectSource.ClampToEdge
        smooth: true
        visible: false
    }
    MultiEffect {
        id: blurred
        anchors.fill: parent
        source: grab
        autoPaddingEnabled: false
        blurEnabled: true
        blur: 1.0
        blurMax: 48
        blurMultiplier: 0.6
        saturation: 0.35
    }
    ShaderEffectSource {
        id: blurredTexture
        sourceItem: blurred
        hideSource: true
        visible: false
    }
    ShaderEffect {
        anchors.fill: parent
        property variant source: blurredTexture
        property size size: Qt.size(width, height)
        property real radius: glass.radius
        property real bezel: glass.bezel
        property real refraction: glass.refraction
        property real tintAlpha: glass.tintAlpha
        property real highlight: glass.highlight
        property color tint: glass.tint
        fragmentShader: "qrc:/uve/shaders/glass.frag.qsb"
    }
}
