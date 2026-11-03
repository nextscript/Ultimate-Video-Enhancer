#pragma once
// Expert Settings – Benutzer-Overrides der Grading-Blöcke GVF_*_SETTINGS
// (Ultimate Video Enhancer 1.17.5, uve.user.js Z. 4954–5242 und GVF_EXPERT_UI Z. 15877–16020).
// Gleiche Blöcke, Schlüssel, Standardwerte, Bereiche und Exportformat ("gvf-expert-settings", nur geänderte Werte).

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVector>

namespace uve::fx {

struct ExpertSettings
{
    struct Contrast { bool higherIsMore = true; double perStep = 0.04, min = 0.60, max = 1.60, pivot = 0.5; } contrast;
    struct Black { bool higherIsDarker = true; double perStep = 0.012, maxOffset = 0.12, softness = 0.05; } black;
    struct White { bool higherIsBrighter = true; double perStep = 0.012, maxOffset = 0.12, softness = 0.05; } white;
    struct Highlights { bool higherIsBrighter = true; double perStep = 0.010, power = 3, threshold = 0.0, maxDarken = 0.40; } highlights;
    struct Shadows { bool higherIsDarker = true; double perStep = 0.010, power = 3, threshold = 1.0, maxBrighten = 0.40; } shadows;
    struct Saturation { bool higherIsMore = true; double perStep = 0.05, min = 0.40, max = 1.80, power = 1; bool desaturateEvenly = true; } saturation;
    struct Vibrance { bool higherIsMore = true; double perStep = 0.05, min = 0.50, max = 1.50, power = 3, skinProtect = 0.6; } vibrance;
    struct Gamma { bool higherIsBrighter = true; double perStep = 0.025, min = 0.60, max = 1.60, maxShadowSlope = 2.0; } gamma;
    struct Sharpen {
        bool higherIsSharper = true;
        double maxAmount = 1.5, lightHalo = 0.6, darkHalo = 1.0, skinProtect = 0.5, noiseThreshold = 0.01, maxChange = 0.15;
        double svgRadius = 1.0, svgMaxSoften = 1.2, webglPerStep = 0.1;
    } sharpen;
    struct Grain { bool higherIsMore = true; double perStep = 0.01, power = 1, peak = 0.35, colorAmount = 0.3, svgStrength = 1.5, svgFrequency = 0.9; } grain;
    struct Hue { bool reverse = false; double perStep = 3.0, maxDegrees = 30, skinProtect = 0.7; } hue;
};

// Aktive Expert-Werte (vom Plugin gesetzt; Standard = Werte des Userscripts)
const ExpertSettings &expert();
void setExpert(const ExpertSettings &e);

// Beschreibung eines Feldes (für Oberfläche, Laden/Speichern, Import/Export)
struct ExpertField
{
    const char *block;      // z. B. "GVF_CONTRAST_SETTINGS"
    const char *key;        // z. B. "perStep"
    bool isBool;
    double min, max, step;
    bool svgOnly;           // nur im SVG-Modus des Userscripts – im Player ohne Wirkung, bleibt für den Roundtrip erhalten
};
const QVector<ExpertField> &expertFields();
QVariant expertValue(const ExpertSettings &e, const char *block, const char *key);
bool setExpertValue(ExpertSettings &e, const char *block, const char *key, const QVariant &v);

// { "GVF_..._SETTINGS": { key: value } } – nur Abweichungen vom Standard (wie expertSettingsDiff)
QJsonObject expertDiff(const ExpertSettings &e);
// Werte aus { "GVF_...": {...} } übernehmen (wie expertMerge: nur bekannte Schlüssel mit passendem Typ)
void expertMerge(ExpertSettings &e, const QJsonObject &blocks);
// Datei-Export / -Import im Userscript-Format. Import: Anzahl Blöcke oder −1, wenn kein Expert-Export.
QJsonObject expertExport(const ExpertSettings &e, const QString &version);
int expertImport(ExpertSettings &e, const QJsonObject &obj);

} // namespace uve::fx
