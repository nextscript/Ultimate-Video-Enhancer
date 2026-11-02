#include "UveParams.h"
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
constexpr double TEAL_GPU_HUE_SHIFT = -5.0;
constexpr double HDR_GPU_MAX_SINGLE = 0.65;
struct Factor { double perStep, min, max; };
constexpr Factor CONTRAST{0.04, 0.60, 1.60};
constexpr Factor SATURATION{0.05, 0.40, 1.80};
constexpr Factor VIBRANCE{0.05, 0.50, 1.50};
constexpr Factor GAMMA{0.025, 0.60, 1.60};
struct Offset { double perStep, maxOffset; };
constexpr Offset BLACK{0.012, 0.12};
constexpr Offset WHITE{0.012, 0.12};
struct Curve { double perStep, power, threshold, cap; };
constexpr Curve HIGHLIGHTS{0.010, 3, 0.0, 0.40};  // cap = maxDarken
constexpr Curve SHADOWS{0.010, 3, 1.0, 0.40};     // cap = maxBrighten
constexpr double SHARPEN_WEBGL_PER_STEP = 0.1;
constexpr double GRAIN_PER_STEP = 0.01;
constexpr double HUE_PER_STEP = 3.0, HUE_MAX_DEGREES = 30.0;

constexpr double kPi = 3.14159265358979323846;
double clamp(double x, double a, double b) { return std::fmin(std::fmax(x, a), b); }
// JavaScript Math.round: .5 rundet Richtung +∞
double jsRound(double x) { return std::floor(x + 0.5); }
double roundTo(double x, double step) { return jsRound(x / step) * step; }
double snap0(double x, double eps) { return std::fabs(x) <= eps ? 0.0 : x; } // wie UVE: <=
double num(double v) { return std::isfinite(v) ? v : 0.0; }

double curvePeak(double p) { return std::pow(p / (p + 1), p) / (p + 1); }

double userFactor(double v, const Factor &f) { return clamp(1.0 + normU(v) * f.perStep, f.min, f.max); }
double userOffset(double v, const Offset &o) { return clamp(normU(v) * o.perStep, -o.maxOffset, o.maxOffset); }

} // namespace

const CurveConstants &curveConstants()
{
    static const CurveConstants c = [] {
        CurveConstants k{};
        k.contrastPivotExp = std::log(0.5) / std::log(0.5);   // pivot 0.5 → 1
        k.saturationPower = 1;
        k.desaturateEvenly = true;
        k.vibrancePower = 3;
        k.vibranceSkin = 0.6;
        k.hueSkin = 0.7;
        k.blackSoftness = 0.05;
        k.whiteSoftness = 0.05;
        k.shadowsThreshold = clamp(SHADOWS.threshold, 0.1, 1);
        k.shadowsPower = std::fmax(1, SHADOWS.power);
        k.shadowsPeak = curvePeak(k.shadowsPower);
        k.highlightsThreshold = clamp(HIGHLIGHTS.threshold, 0, 0.9);
        k.highlightsPower = std::fmax(1, HIGHLIGHTS.power);
        k.highlightsPeak = curvePeak(k.highlightsPower);
        k.gammaShadowSlope = std::fmax(1.0, 2.0);
        k.sharpenNoiseThreshold = 0.01;
        k.sharpenMaxAmount = 1.5;
        k.sharpenMaxChange = 0.15;
        k.sharpenLightHalo = 0.6;
        k.sharpenDarkHalo = 1.0;
        k.sharpenSkin = 0.5;
        k.grainPower = 1;
        k.grainPeakExp = std::log(0.5) / std::log(clamp(0.35, 0.2, 0.8));
        k.grainColorAmount = 0.3;
        return k;
    }();
    return c;
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

double userGammaValue(double v) { return clamp(1.0 + normU(v) * GAMMA.perStep, GAMMA.min, GAMMA.max); }
double userContrastAmount(double v) { return clamp(userFactor(v, CONTRAST) - 1.0, -0.95, 0.95); }
double userSaturationAmount(double v) { return clamp(userFactor(v, SATURATION) - 1.0, -1.0, 1.0); }
double userVibranceAmount(double v) { return clamp(userFactor(v, VIBRANCE) - 1.0, -1.0, 1.0); }
double userHueDegrees(double v) { return clamp(normU(v) * HUE_PER_STEP, -HUE_MAX_DEGREES, HUE_MAX_DEGREES); }
double userGrainAmount(double v) { return std::fmax(0.0, normU(v) * GRAIN_PER_STEP); }

double userHighlightsAmount(double v)
{
    const auto &k = curveConstants();
    const double d = normU(v); // higherIsBrighter: true
    const double p = k.highlightsPower, range = 1 - k.highlightsThreshold, peak = k.highlightsPeak;
    double maxRise = 0; // steilster Anstieg von t^p * (1 - t)
    for (int i = 1; i < 200; ++i) {
        const double t = i / 200.0;
        maxRise = std::fmax(maxRise, p * std::pow(t, p - 1) - (p + 1) * std::pow(t, p));
    }
    const double darkCap = std::fmin(std::fmax(0.0, HIGHLIGHTS.cap), 0.95 * peak * range / std::fmax(maxRise, 0.0001));
    return clamp(d * HIGHLIGHTS.perStep, -darkCap, peak * range * 0.95);
}

double userShadowsAmount(double v)
{
    const auto &k = curveConstants();
    const double d = -normU(v); // higherIsDarker: true
    const double p = k.shadowsPower, range = k.shadowsThreshold, peak = k.shadowsPeak;
    double maxFall = 0; // steilster Abfall von t * (1 - t)^p
    for (int i = 1; i < 200; ++i) {
        const double t = i / 200.0;
        maxFall = std::fmax(maxFall, -(std::pow(1 - t, p) - p * t * std::pow(1 - t, p - 1)));
    }
    const double brightCap = std::fmin(std::fmax(0.0, SHADOWS.cap), 0.95 * peak * range / std::fmax(maxFall, 0.0001));
    return clamp(d * SHADOWS.perStep, -peak * range * 0.95, brightCap);
}

double userBlackOffset(double v) { return userOffset(v, BLACK); }  // higherIsDarker: true
double userWhiteOffset(double v) { return userOffset(v, WHITE); }  // higherIsBrighter: true

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

    const double sharpen = std::fmax(0.0, normSL(p.sl) * 0.3) + normU(p.sharpen) * SHARPEN_WEBGL_PER_STEP;
    const double grain = std::fmax(0.0, -normDN(p.dn) * 0.2);
    double hue = userHueDegrees(p.hue);
    if (p.tealOrange) hue += TEAL_GPU_HUE_SHIFT;
    double hdr = clamp(normHDR(p.hdr), -1.0, 2.0);
    if (hdr > 0) hdr = std::fmin(hdr, HDR_GPU_MAX_SINGLE);

    s.contrast = clamp(contrast, 0.5, 2.0);
    s.saturation = clamp(saturation, 0.0, 3.0);
    s.brightness = clamp(brightness, 0.5, 2.0);
    s.sharpen = clamp(sharpen, -1.0, 2.0);
    s.gamma = clamp(userGammaValue(p.gamma), 0.5, 2.0);
    s.grain = clamp(grain, 0.0, 0.5);
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
    s.sharpenRadius = std::fmax(1.0, std::fmax(0.1, std::fabs(jsRound(normSR(p.sr) * 10) / 10)));
    s.cosHue = std::cos(hue * kPi / 180.0);
    s.sinHue = std::sin(hue * kPi / 180.0);
    return s;
}

// ---------------------------------------------------------------- Profile
static const QStringList kKnownTop{"schema", "ver", "baseOtp", "darkMoody", "tealOrange", "vibrantSat", "sl", "sr", "bl",
                                   "wl", "dn", "edge", "hdr", "profile", "user", "uvePlayer", "lutProfile"};
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
