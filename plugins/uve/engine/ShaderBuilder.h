#pragma once
#include "CubeLut.h"
#include "UveParams.h"

#include <QString>
#include <QVector>

namespace uve::fx {

// Erzeugt die mpv-Benutzer-Shader-Datei (//!HOOK MAIN) für die aktuellen Parameter.
// Pässe (in dieser Reihenfolge):
//   1. Original sichern (UVEORIG) – nur für den Vorher/Nachher-Vergleich
//   2. Unschärfemaske SL/SR (separierbarer Gauß, 2 Pässe)             [nur wenn SL ≠ 0]
//   3. Farbkorrektur (Grading, HDR-like, Korn) – Formeln aus dem UVE-WebGL-Shader
//   4. Detail-Schärfe, Kanten, 3D-LUT, Vorher/Nachher-Split
// Konstanten werden direkt eingesetzt (keine //!PARAM-Abhängigkeit vom Renderer).
struct ShaderBuildInput
{
    Params params;
    const CubeLut *lut = nullptr;   // nullptr = kein LUT
    const QVector<double> *lutMatrix = nullptr; // UVE-LUT-Profil (20 Werte, 4x5), nullptr = keins
    bool originalSaved = false;     // UVEORIG liefert schon die Denoising-Stufe (läuft davor): nicht überschreiben
};

// Pässe ohne LUT-Textur. Ist in.lut gesetzt, bindet der letzte Pass die Textur UVELUT,
// die aus einer separaten Datei kommt (buildLutShader) – mpv-Benutzertexturen sind dateiübergreifend sichtbar.
QString buildShader(const ShaderBuildInput &in);

// Nur die 3D-LUT-Textur (//!TEXTURE UVELUT). Ändert sich nur bei einem neuen LUT – wichtig, weil mpv
// Shader-Dateien nach Pfad zwischenspeichert und jede Regleränderung eine neue Datei erzeugt.
QString buildLutShader(const CubeLut &lut);

// Prefix aller //!DESC-Zeilen; ShaderChain ordnet Fehler darüber dem Plugin zu
inline constexpr const char *kShaderDescPrefix = "org.nextscript.ultimate-video-enhancer:";

} // namespace uve::fx
