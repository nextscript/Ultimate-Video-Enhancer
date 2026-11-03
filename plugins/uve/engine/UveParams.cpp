#include "UveParams.h"
#include "ExpertSettings.h"
#include <QCoreApplication>

#include <QJsonValue>
#include <QStringList>
#include <cmath>

namespace uve::fx {

namespace {

// --- Standardwerte aus uve.user.js (GVF_*_SETTINGS) ---
struct { double brightness = 1.02, contrast = 1.05, saturation = 1.15; } constexpr BASE;
struct { double gpuBrightness = 0.96, gpuSaturation = 0.92; } constexpr MOODY;
constexpr double VIBRANT_SATURATION = 1.35;
constexpr double HDR_GPU_MAX_SINGLE = 0.65;
// Grading-Blöcke (Kontrast, Schwarz/Weiß, Lichter, Schatten, Sättigung, Dynamik, Gamma, Schärfe, Korn, Farbton)
// kommen aus den Expert Settings (ExpertSettings.h, Standard = GVF_*_SETTINGS des Userscripts).
struct Factor { double perStep, min, max; };
struct Offset { double perStep, maxOffset; };
// Detail-Schärfe des Players: webglPerStep × 1.5 (Standard 0.1 → 0.15 je Schritt, ±10 → ±1.5)
constexpr double SHARPEN_PLAYER_FACTOR = 1.5;
double dir(bool higherIsMore) { return higherIsMore ? 1.0 : -1.0; }

constexpr double kPi = 3.14159265358979323846;
double clamp(double x, double a, double b) { return std::fmin(std::fmax(x, a), b); }
// JavaScript Math.round: .5 rundet Richtung +∞
double jsRound(double x) { return std::floor(x + 0.5); }
double roundTo(double x, double step) { return jsRound(x / step) * step; }
double snap0(double x, double eps) { return std::fabs(x) <= eps ? 0.0 : x; } // wie UVE: <=
double num(double v) { return std::isfinite(v) ? v : 0.0; }

double curvePeak(double p) { return std::pow(p / (p + 1), p) / (p + 1); }

double userFactor(double d, const Factor &f) { return clamp(1.0 + d * f.perStep, f.min, f.max); }
double userOffset(double d, const Offset &o) { return clamp(d * o.perStep, -o.maxOffset, o.maxOffset); }

} // namespace

const CurveConstants &curveConstants()
{
    // Aus den aktiven Expert Settings abgeleitet; Begrenzungen wie im Userscript (getFragmentShader)
    static CurveConstants k{};
    const ExpertSettings &e = expert();
    k.contrastPivotExp = std::log(0.5) / std::log(clamp(e.contrast.pivot, 0.2, 0.8));
    k.saturationPower = std::fmax(0.0, e.saturation.power);
    k.desaturateEvenly = e.saturation.desaturateEvenly;
    k.vibrancePower = std::fmax(0.0, e.vibrance.power);
    k.vibranceSkin = clamp(e.vibrance.skinProtect, 0, 1);
    k.hueSkin = clamp(e.hue.skinProtect, 0, 1);
    k.blackSoftness = clamp(e.black.softness, 0, 0.3);
    k.whiteSoftness = clamp(e.white.softness, 0, 0.3);
    k.shadowsThreshold = clamp(e.shadows.threshold, 0.1, 1);
    k.shadowsPower = std::fmax(1, e.shadows.power);
    k.shadowsPeak = curvePeak(k.shadowsPower);
    k.highlightsThreshold = clamp(e.highlights.threshold, 0, 0.9);
    k.highlightsPower = std::fmax(1, e.highlights.power);
    k.highlightsPeak = curvePeak(k.highlightsPower);
    k.gammaShadowSlope = e.gamma.maxShadowSlope > 0 ? std::fmax(1.0, e.gamma.maxShadowSlope) : 0.0;   // 0 = aus
    k.sharpenNoiseThreshold = std::fmax(0.0, e.sharpen.noiseThreshold);
    k.sharpenMaxAmount = std::fmax(0.0, e.sharpen.maxAmount);
    k.sharpenMaxChange = std::fmax(0.001, e.sharpen.maxChange);
    k.sharpenLightHalo = clamp(e.sharpen.lightHalo, 0, 1);
    k.sharpenDarkHalo = clamp(e.sharpen.darkHalo, 0, 1);
    k.sharpenSkin = clamp(e.sharpen.skinProtect, 0, 1);
    k.grainPower = std::fmax(0.0, e.grain.power);
    k.grainPeakExp = std::log(0.5) / std::log(clamp(e.grain.peak, 0.2, 0.8));
    k.grainColorAmount = clamp(e.grain.colorAmount, 0, 1);
    return k;
}

double normU(double v) { return roundTo(clamp(num(v), -10, 10), 0.1); }
double normSL(double v) { return snap0(roundTo(clamp(num(v), -2, 2), 0.01), 0.005); }
double normSR(double v) { return snap0(roundTo(clamp(num(v), -2, 2), 0.01), 0.005); }
double normBL(double v) { return snap0(roundTo(clamp(num(v), -2, 2), 0.01), 0.005); }
double normWL(double v) { return snap0(roundTo(clamp(num(v), -2, 2), 0.01), 0.005); }
double blackToOffset(double bl) { return -clamp(bl, -2, 2) * 0.04; }
double whiteToHiAdj(double wl) { return clamp(wl, -2, 2) * 0.06; }
QVector<double> whiteLevelTable(double hiAdj)
{
    QVector<double> v;
    const double knee = 0.78;
    for (int i = 0; i < 17; ++i) {
        const double x = i / 16.0;
        double y = x;
        if (x > knee) y = x + hiAdj * ((x - knee) / (1 - knee));
        y = clamp(y, 0, 1);
        v << jsRound(y * 10000) / 10000; // toFixed(4)
    }
    return v;
}
double normDN(double v) { return snap0(roundTo(clamp(num(v), -1.5, 1.5), 0.01), 0.005); }
double normHDR(double v) { return snap0(roundTo(clamp(num(v), -1.0, 2.0), 0.01), 0.005); }
double normEDGE(double v) { return snap0(roundTo(clamp(num(v), 0, 1.0), 0.01), 0.005); }

double userGammaValue(double v)
{
    const auto &g = expert().gamma;
    return clamp(1.0 + normU(v) * dir(g.higherIsBrighter) * g.perStep, g.min, g.max);
}
double userContrastAmount(double v)
{
    const auto &c = expert().contrast;
    return clamp(userFactor(normU(v) * dir(c.higherIsMore), {c.perStep, c.min, c.max}) - 1.0, -0.95, 0.95);
}
double userSaturationAmount(double v)
{
    const auto &s = expert().saturation;
    return clamp(userFactor(normU(v) * dir(s.higherIsMore), {s.perStep, s.min, s.max}) - 1.0, -1.0, 1.0);
}
double userVibranceAmount(double v)
{
    const auto &s = expert().vibrance;
    return clamp(userFactor(normU(v) * dir(s.higherIsMore), {s.perStep, s.min, s.max}) - 1.0, -1.0, 1.0);
}
double userHueDegrees(double v)
{
    const auto &h = expert().hue;
    const double m = std::fabs(h.maxDegrees);
    return clamp(normU(v) * dir(!h.reverse) * h.perStep, -m, m);
}
double userGrainAmount(double v)
{
    const auto &g = expert().grain;
    return std::fmax(0.0, normU(v) * dir(g.higherIsMore) * g.perStep);
}

double userHighlightsAmount(double v)
{
    const auto &k = curveConstants();
    const auto &hs = expert().highlights;
    const double d = normU(v) * dir(hs.higherIsBrighter);
    const double p = k.highlightsPower, range = 1 - k.highlightsThreshold, peak = k.highlightsPeak;
    double maxRise = 0; // steilster Anstieg von t^p * (1 - t)
    for (int i = 1; i < 200; ++i) {
        const double t = i / 200.0;
        maxRise = std::fmax(maxRise, p * std::pow(t, p - 1) - (p + 1) * std::pow(t, p));
    }
    const double darkCap = std::fmin(std::fmax(0.0, hs.maxDarken), 0.95 * peak * range / std::fmax(maxRise, 0.0001));
    return clamp(d * hs.perStep, -darkCap, peak * range * 0.95);
}

double userShadowsAmount(double v)
{
    const auto &k = curveConstants();
    const auto &ss = expert().shadows;
    const double d = -normU(v) * dir(ss.higherIsDarker);
    const double p = k.shadowsPower, range = k.shadowsThreshold, peak = k.shadowsPeak;
    double maxFall = 0; // steilster Abfall von t * (1 - t)^p
    for (int i = 1; i < 200; ++i) {
        const double t = i / 200.0;
        maxFall = std::fmax(maxFall, -(std::pow(1 - t, p) - p * t * std::pow(1 - t, p - 1)));
    }
    const double brightCap = std::fmin(std::fmax(0.0, ss.maxBrighten), 0.95 * peak * range / std::fmax(maxFall, 0.0001));
    return clamp(d * ss.perStep, -peak * range * 0.95, brightCap);
}

double userBlackOffset(double v)
{
    const auto &b = expert().black;
    return userOffset(normU(v) * dir(b.higherIsDarker), {b.perStep, b.maxOffset});
}
double userWhiteOffset(double v)
{
    const auto &w = expert().white;
    return userOffset(normU(v) * dir(w.higherIsBrighter), {w.perStep, w.maxOffset});
}

ShaderValues computeShaderValues(const Params &p)
{
    ShaderValues s;
    double contrast = 1.0, saturation = 1.0, brightness = 1.0;
    if (p.baseTone) { contrast *= BASE.contrast; saturation *= BASE.saturation; brightness *= BASE.brightness; }
    if (p.profile == "film") { contrast *= 1.08; saturation *= 1.08; }
    else if (p.profile == "anime") { contrast *= 1.10; saturation *= 1.16; brightness *= 1.03; }
    else if (p.profile == "gaming") { contrast *= 1.12; saturation *= 1.06; }
    else if (p.profile == "eyecare") { saturation *= 0.85; brightness *= 1.06; }
    if (p.darkMoody) { saturation *= MOODY.gpuSaturation; brightness *= MOODY.gpuBrightness; }
    if (p.vibrantSat) saturation *= VIBRANT_SATURATION;

    const auto &es = expert().sharpen;
    const double sharpen = normU(p.sharpen) * dir(es.higherIsSharper) * es.webglPerStep * SHARPEN_PLAYER_FACTOR;
    const double hue = userHueDegrees(p.hue);
    double hdr = clamp(normHDR(p.hdr), -1.0, 2.0);
    if (hdr > 0) hdr = std::fmin(hdr, HDR_GPU_MAX_SINGLE);

    s.contrast = clamp(contrast, 0.5, 2.0);
    s.saturation = clamp(saturation, 0.0, 3.0);
    s.brightness = clamp(brightness, 0.5, 2.0);
    s.sharpen = clamp(sharpen, -1.5, 1.5);
    s.gamma = clamp(userGammaValue(p.gamma), 0.5, 2.0);
    s.grain = 0;   // DN (Korn) entfernt
    s.vibrance = clamp(userVibranceAmount(p.vibrance), -1.0, 1.0);
    s.hdr = hdr;
    s.edge = clamp(normEDGE(p.edge), 0.0, 1.0);
    s.rGain = clamp(std::round(clamp(p.rGain, 0, 255)) / 128.0, 0.0, 2.0);
    s.gGain = clamp(std::round(clamp(p.gGain, 0, 255)) / 128.0, 0.0, 2.0);
    s.bGain = clamp(std::round(clamp(p.bGain, 0, 255)) / 128.0, 0.0, 2.0);
    s.highlights = userHighlightsAmount(p.highlights);
    s.shadows = userShadowsAmount(p.shadows);
    const double b = userBlackOffset(p.black), w = userWhiteOffset(p.white);
    s.inBlack = std::fmax(0.0, b);
    s.inWhite = 1 - std::fmax(0.0, w);
    s.outBlack = std::fmax(0.0, -b);
    s.outWhite = 1 + std::fmin(0.0, w);
    s.contrastCurve = userContrastAmount(p.contrast);
    s.satCurve = userSaturationAmount(p.saturation);
    s.grainCurve = userGrainAmount(p.grain);
    s.hueDeg = hue;
    // SVG-Kette: Werte wie in ensureSvgFilter() auf eine Nachkommastelle gerundet (toFixed(1))
    const double BL = jsRound(normBL(p.bl) * 10) / 10, WL = jsRound(normWL(p.wl) * 10) / 10;
    s.levelBlackOffset = blackToOffset(BL);
    s.levelWhiteAdj = whiteToHiAdj(WL);
    // SL/SR: Unschärfemaske wie im UVE-SVG-Pfad (feGaussianBlur + feComposite k2 = 1 + A, k3 = −A).
    // Abweichung: Sigma mindestens 0.7 px, damit SL auch bei SR = 0 sichtbar wirkt (UVE: 0.1 px ≈ wirkungslos).
    const double SL = normSL(p.sl), SR = std::fabs(normSR(p.sr));
    s.usmAmount = std::fmax(0.0, SL);
    s.usmBlur = clamp(-SL, 0.0, 1.0);
    s.usmSigma = 0.7 + SR * 0.65;
    s.sharpenRadius = 1.0 + SR * 0.5;
    s.tealOrange = p.tealOrange;
    s.cosHue = std::cos(hue * kPi / 180.0);
    s.sinHue = std::sin(hue * kPi / 180.0);
    return s;
}

// ---------------------------------------------------------------- Profile
static const QStringList kKnownTop{"schema", "ver", "baseOtp", "darkMoody", "tealOrange", "vibrantSat", "sl", "sr", "bl",
                                   "wl", "dn", "edge", "hdr", "profile", "user", "uvePlayer", "lutProfile",
                                   "autoOn", "autoStrength", "autoLockWB"};
static const QStringList kProfiles{"off", "film", "anime", "gaming", "eyecare", "user"};

static bool readNumber(const QJsonObject &o, const char *key, double lo, double hi, double *out, QStringList *errors)
{
    const auto v = o.value(QLatin1String(key));
    if (v.isUndefined()) return false;
    if (!v.isDouble() || !std::isfinite(v.toDouble())) { *errors << QCoreApplication::translate("uve", "'%1' ist keine Zahl").arg(QLatin1String(key)); return false; }
    *out = clamp(v.toDouble(), lo, hi);
    return true;
}

ProfileResult paramsFromProfile(const QJsonObject &profile, const Params &base)
{
    ProfileResult r;
    r.params = base;
    const QString schema = profile.value("schema").toString();
    if (schema != QLatin1String("gvf-settings")) {
        r.error = QStringLiteral("Unbekanntes Profilformat (erwartet schema \"gvf-settings\", gefunden \"%1\")").arg(schema);
        return r;
    }
    QStringList errors;
    Params &p = r.params;
    auto flag = [&](const char *k, bool &dst) { const auto v = profile.value(QLatin1String(k)); if (v.isBool()) dst = v.toBool(); };
    flag("baseOtp", p.baseTone);
    flag("darkMoody", p.darkMoody);
    flag("tealOrange", p.tealOrange);
    flag("vibrantSat", p.vibrantSat);
    readNumber(profile, "sl", -2, 2, &p.sl, &errors);
    readNumber(profile, "sr", -2, 2, &p.sr, &errors);
    readNumber(profile, "bl", -2, 2, &p.bl, &errors);
    readNumber(profile, "wl", -2, 2, &p.wl, &errors);
    readNumber(profile, "dn", -1.5, 1.5, &p.dn, &errors);
    readNumber(profile, "edge", 0, 1, &p.edge, &errors);
    readNumber(profile, "hdr", -1, 2, &p.hdr, &errors);
    flag("autoOn", p.autoOn);
    flag("autoLockWB", p.autoLockWB);
    readNumber(profile, "autoStrength", 0, 1, &p.autoStrength, &errors);
    const QString prof = profile.value("profile").toString();
    if (!prof.isEmpty()) p.profile = kProfiles.contains(prof) ? prof : QStringLiteral("off");
    const auto u = profile.value("user").toObject();
    readNumber(u, "contrast", -10, 10, &p.contrast, &errors);
    readNumber(u, "black", -10, 10, &p.black, &errors);
    readNumber(u, "white", -10, 10, &p.white, &errors);
    readNumber(u, "highlights", -10, 10, &p.highlights, &errors);
    readNumber(u, "shadows", -10, 10, &p.shadows, &errors);
    readNumber(u, "saturation", -10, 10, &p.saturation, &errors);
    readNumber(u, "vibrance", -10, 10, &p.vibrance, &errors);
    readNumber(u, "sharpen", -10, 10, &p.sharpen, &errors);
    readNumber(u, "gamma", -10, 10, &p.gamma, &errors);
    readNumber(u, "grain", -10, 10, &p.grain, &errors);
    readNumber(u, "hue", -10, 10, &p.hue, &errors);
    double g = 0;
    if (readNumber(u, "r_gain", 0, 255, &g, &errors)) p.rGain = int(std::lround(g));
    if (readNumber(u, "g_gain", 0, 255, &g, &errors)) p.gGain = int(std::lround(g));
    if (readNumber(u, "b_gain", 0, 255, &g, &errors)) p.bGain = int(std::lround(g));
    if (profile.value("lutProfile").isString()) {
        const QString lp = profile.value("lutProfile").toString().trimmed();
        p.lutProfile = (lp == QLatin1String("none")) ? QString() : lp;
    }
    const auto ext = profile.value("uvePlayer").toObject();
    if (ext.value("gpuBase").isBool()) p.gpuBase = ext.value("gpuBase").toBool();
    if (ext.value("lutFile").isString()) p.lutFile = ext.value("lutFile").toString();
    readNumber(ext, "lutStrength", 0, 1, &p.lutStrength, &errors);
    if (!errors.isEmpty()) { r.error = errors.join("; "); return r; }
    // Unbekannte/nicht portierte Felder unverändert für den Export aufbewahren (Roundtrip)
    for (auto it = profile.begin(); it != profile.end(); ++it)
        if (!kKnownTop.contains(it.key())) r.passthrough.insert(it.key(), it.value());
    r.ok = true;
    return r;
}

static double fix(double v, int digits) { const double m = std::pow(10.0, digits); return jsRound(v * m) / m; }

QJsonObject profileFromParams(const Params &p, const QJsonObject &passthrough)
{
    QJsonObject o = passthrough;
    o.insert("schema", "gvf-settings");
    o.insert("ver", "1.10");
    o.insert("baseOtp", p.baseTone);
    o.insert("darkMoody", p.darkMoody);
    o.insert("tealOrange", p.tealOrange);
    o.insert("vibrantSat", p.vibrantSat);
    o.insert("sl", fix(normSL(p.sl), 1));
    o.insert("sr", fix(p.sr, 1));
    o.insert("bl", fix(p.bl, 1));
    o.insert("wl", fix(p.wl, 1));
    o.insert("dn", fix(normDN(p.dn), 1));
    o.insert("edge", fix(normEDGE(p.edge), 2));
    o.insert("hdr", fix(normHDR(p.hdr), 2));
    o.insert("profile", p.profile);
    o.insert("autoOn", p.autoOn);
    o.insert("autoStrength", fix(p.autoStrength, 2));
    o.insert("autoLockWB", p.autoLockWB);
    o.insert("lutProfile", p.lutProfile.isEmpty() ? QStringLiteral("none") : p.lutProfile);
    o.insert("user", QJsonObject{
        {"contrast", fix(normU(p.contrast), 1)}, {"black", fix(normU(p.black), 1)}, {"white", fix(normU(p.white), 1)},
        {"highlights", fix(normU(p.highlights), 1)}, {"shadows", fix(normU(p.shadows), 1)},
        {"saturation", fix(normU(p.saturation), 1)}, {"vibrance", fix(normU(p.vibrance), 1)},
        {"sharpen", fix(normU(p.sharpen), 1)}, {"gamma", fix(normU(p.gamma), 1)}, {"grain", fix(normU(p.grain), 1)},
        {"hue", fix(normU(p.hue), 1)}, {"r_gain", p.rGain}, {"g_gain", p.gGain}, {"b_gain", p.bGain}});
    o.insert("uvePlayer", QJsonObject{{"gpuBase", p.gpuBase}, {"lutFile", p.lutFile}, {"lutStrength", fix(p.lutStrength, 2)}});
    return o;
}

QJsonObject paramsToJson(const Params &p)
{
    QJsonObject o = profileFromParams(p);
    o.insert("enabled", p.enabled);
    return o;
}

Params paramsFromJson(const QJsonObject &o)
{
    auto r = paramsFromProfile(o);
    Params p = r.ok ? r.params : Params();
    if (o.value("enabled").isBool()) p.enabled = o.value("enabled").toBool();
    return p;
}

} // namespace uve::fx
