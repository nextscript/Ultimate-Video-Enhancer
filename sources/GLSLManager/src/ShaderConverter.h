#pragma once
// Wandelt Custom-Filter-Code des Ultimate Video Enhancer (WebGL/GLSL ES 3.00, Shadertoy, Hilfsfunktion, nackter
// Rumpf) in einen mpv-User-Shader um. mpv-Hook-Shader (//!HOOK) laufen unverändert mit allen Pässen.
// libmpv nutzt den vo_gpu-Renderer ohne //!PARAM und ohne Laufzeit-Uniforms: Reglerwerte werden deshalb als
// #define in den Shader geschrieben; jede Änderung ergibt eine neue Datei.
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace glslmgr {

struct ConvertInput
{
    QString code;
    QVariantMap uniforms;   // Name → Wert, wie entry.uniforms im UVE-Format
    QString blendMode = QStringLiteral("normal");
    QString label;
    double fps = 30.0;      // für u_time: mpv kennt nur einen Bildzähler
};

bool isMpvHook(const QString &code);        // enthält //!HOOK oder eine hook()-Funktion
QString detectType(const QString &code);    // webgl | svg
QString descriptionOf(const QString &code); // erstes //!DESC, sonst leer

// Regler: {name, def, min, max, step, label, group, kind: slider|toggle|select, options: [{value,label}]}
QVariantList parseUniformDefs(const QString &code);

// Leerer Text = in Ordnung, sonst eine kurze englische Fehlerkennung (empty | braces | parens)
QString staticCheck(const QString &code);

QString convertToMpv(const ConvertInput &in);

} // namespace glslmgr
