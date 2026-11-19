#pragma once
// Kompiliert einen mpv-Shader probeweise im GPU-Treiber, in einem eigenen unsichtbaren OpenGL-Kontext.
// So gibt es die genaue Fehlermeldung auch ohne laufendes Video; mpv meldet dem Plugin nur "fehlgeschlagen".
#include <QString>
#include <QStringList>

namespace glslmgr {

struct ValidationResult
{
    bool checked = false;   // false: nicht prüfbar (kein OpenGL, //!TEXTURE, //!COMPUTE)
    QString error;          // Meldung des Treibers, leer = in Ordnung
};

// Nur auf dem GUI-Thread aufrufen.
ValidationResult validateHookShader(const QString &hookText);

// Quelltext eines Passes wie ihn mpv erzeugt; für Tests
QStringList fragmentSources(const QString &hookText, int glslVersion);

} // namespace glslmgr
