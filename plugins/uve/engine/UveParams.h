#pragma once
// UVE-Parameter und die 1:1 portierte Umrechnung Regler → Shader-Werte.
//
// Referenz (Ultimate Video Enhancer 1.17.5, uve.user.js):
//   - Standardwerte der Experteneinstellungen: GVF_*_SETTINGS (Zeilen 4898–5136)
//   - Umrechnungsfunktionen normU/userFactor/userOffset/user*Amount: Zeilen 9320–9530
//   - WebGL-Parameter (updateParams): Zeilen 10527–10611
//   - WebGL-Fragment-Shader (getFragmentShader): Zeilen 10024–10350
// Abweichungen sind in docs/UVE_PARITY_MATRIX.md dokumentiert.

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace uve::fx {

// Vom Benutzer einstellbare Werte (Semantik und Bereiche wie im UVE-Profil "gvf-settings")
struct Params
{
    bool enabled = true;       // Effekte an/aus (Plugin-Ebene)
    bool baseTone = true;      // "baseOtp": UVE-Grundton (Kontrast 1.05, Sättigung 1.15, Helligkeit 1.02)
    bool darkMoody = false;    // "darkMoody"
    bool tealOrange = false;   // "tealOrange"
    bool vibrantSat = false;   // "vibrantSat"
    QString profile = "off";   // off | film | anime | gaming | eyecare | user

    double sl = 0;    // Schärfe (−2 … 2): Unschärfemaske wie im UVE-SVG-Pfad (positiv = schärfer, negativ = weicher)
    double sr = 0;    // Schärfe-Radius (−2 … 2): Radius der Unschärfemaske und der Detailschärfe
    double bl = 0;    // Schwarzpegel (−2 … 2), UVE-SVG-Pfad: Versatz −BL·0.04 (höher = tiefere Schwarztöne)
    double wl = 0;    // Weißpegel (−2 … 2), UVE-SVG-Pfad: Lichterkurve ab Knie 0.78, Stärke WL·0.06
    double dn = 0;    // Denoise/Textur (−1.5 … 1.5); nur für den Profil-Roundtrip, ohne Wirkung im Player
    double hdr = 0;   // HDR-like (−1 … 2); im GPU-Pfad wirken nur Werte > 0
    double edge = 0;  // Kanten (0 … 1)

    // Grading ("user"), je −10 … 10
    double contrast = 0, black = 0, white = 0, highlights = 0, shadows = 0;
    double saturation = 0, vibrance = 0, sharpen = 0, gamma = 0, grain = 0, hue = 0;
    int rGain = 128, gGain = 128, bGain = 128; // 0 … 255

    // Auto-Scene-Match (UVE "autoOn"/"autoStrength"/"autoLockWB", Standard wie im Userscript)
    bool autoOn = true;
    double autoStrength = 0.65;   // 0 … 1
    bool autoLockWB = true;       // Weißabgleich per Farbton nachführen

    // Erweiterungen des nativen Players
    bool gpuBase = false;      // früher Bilateral-Rauschfilter + CAS; entfernt, nur noch für den Profil-Roundtrip
    QString lutFile;           // .cube-Datei
    QString lutProfile;        // UVE-LUT-Profil (Matrix) "Gruppe||Name", leer = keins
    double lutStrength = 1.0;  // 0 … 1
    bool compare = false;      // Vorher/Nachher (Split)
    double compareSplit = 0.5; // 0 … 1 (links = Original)

    bool operator==(const Params &) const = default;
};

// Werte, die der Shader erhält (entspricht this.params in UVE updateParams)
struct ShaderValues
{
    double contrast = 1, saturation = 1, brightness = 1, sharpen = 0, gamma = 1, grain = 0, vibrance = 0;
    double hdr = 0, edge = 0, rGain = 1, gGain = 1, bGain = 1, highlights = 0, shadows = 0;
    double inBlack = 0, inWhite = 1, outBlack = 0, outWhite = 1;
    double levelBlackOffset = 0;   // BL (SVG-Kette): additiver Versatz
    double levelWhiteAdj = 0;      // WL (SVG-Kette): Lichteranhebung
    double sharpenRadius = 1;      // SR: Abtastradius der Detailschärfe in Pixeln (1 … 2)
    double usmAmount = 0;          // SL > 0: Stärke der Unschärfemaske (out = src + A·(src − blur))
    double usmBlur = 0;            // SL < 0: Anteil des weichgezeichneten Bildes (0 … 1)
    double usmSigma = 0.7;         // SR: Gauß-Sigma der Unschärfemaske in Pixeln (0.7 … 2.0)
    bool tealOrange = false;       // Split-Toning: Schatten → Petrol, Lichter → Orange
    double contrastCurve = 0, satCurve = 0, grainCurve = 0, hueDeg = 0, cosHue = 1, sinHue = 0;
};

// Feste Konstanten der Shader-Kurven (aus den GVF_*_SETTINGS-Standards abgeleitet)
struct CurveConstants
{
    double contrastPivotExp;     // ln(0.5)/ln(pivot)
    double saturationPower;      // GVF_SATURATION_SETTINGS.power
    bool desaturateEvenly;
    double vibrancePower, vibranceSkin, hueSkin;
    double blackSoftness, whiteSoftness;
    double shadowsThreshold, shadowsPower, shadowsPeak;
    double highlightsThreshold, highlightsPower, highlightsPeak;
    double gammaShadowSlope;
    double sharpenNoiseThreshold, sharpenMaxAmount, sharpenMaxChange, sharpenLightHalo, sharpenDarkHalo, sharpenSkin;
    double grainPower, grainPeakExp, grainColorAmount;
};

const CurveConstants &curveConstants();

// Portierte Hilfsfunktionen (benannt wie im Original)
double normU(double v);
double normSL(double v);
double normSR(double v);
double normBL(double v);
double normWL(double v);
// UVE SVG-Kette (uve.user.js Z. 9530–9534, 18803–18840, 19397–19404)
double blackToOffset(double bl);         // −clamp(BL,−2,2)·0.04
double whiteToHiAdj(double wl);          // clamp(WL,−2,2)·0.06
QVector<double> whiteLevelTable(double hiAdj); // 17 Stützstellen (feFuncX type="table"), auf 4 Stellen gerundet
double normDN(double v);
double normHDR(double v);
double normEDGE(double v);
double userGammaValue(double v);
double userContrastAmount(double v);
double userSaturationAmount(double v);
double userVibranceAmount(double v);
double userHueDegrees(double v);
double userGrainAmount(double v);
double userHighlightsAmount(double v);
double userShadowsAmount(double v);
double userBlackOffset(double v);
double userWhiteOffset(double v);

ShaderValues computeShaderValues(const Params &p);

// Profil-Import/-Export im UVE-Format "gvf-settings" (ver 1.10) + Erweiterungsblock "uvePlayer"
struct ProfileResult { bool ok = false; QString error; Params params; QJsonObject passthrough; };
ProfileResult paramsFromProfile(const QJsonObject &profile, const Params &base = Params());
QJsonObject profileFromParams(const Params &p, const QJsonObject &passthrough = {});

QJsonObject paramsToJson(const Params &p);
Params paramsFromJson(const QJsonObject &o);

} // namespace uve::fx
