#include "ShaderBuilder.h"

#include <QStringList>
#include <cmath>

namespace uve::fx {

namespace {

QString f(double v) { return QString::number(std::isfinite(v) ? v : 0.0, 'f', 6); }
bool active(double v, double neutral = 0.0) { return std::fabs(v - neutral) > 0.0001; }

QString header(const QString &desc, const QStringList &binds, const QString &save = {})
{
    QString h = QStringLiteral("//!HOOK MAIN\n");
    for (const auto &b : binds) h += QStringLiteral("//!BIND %1\n").arg(b);
    if (!save.isEmpty()) h += QStringLiteral("//!SAVE %1\n").arg(save);
    h += QStringLiteral("//!DESC %1 %2\n").arg(QLatin1String(kShaderDescPrefix), desc);
    return h;
}

const char *kCommon = R"GLSL(
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);
float uveLuma(vec3 c) { return dot(c, LUMA); }
)GLSL";

QString passOriginal()
{
    return header("Original sichern", {"HOOKED"}, "UVEORIG") + "vec4 hook() { return HOOKED_tex(HOOKED_pos); }\n\n";
}

// Bilateral-Rauschfilter + CAS (Konstanten aus dem UVE-WebGL-Shader).
// Abweichung: läuft hier VOR dem Grading auf der Quelle (UVE sampelt die ungegradete Quelle,
// mischt dabei aber das bereits gegradete Zentrum hinein – siehe Parity-Matrix).
QString passDenoiseCas()
{
    return header("Rauschfilter + CAS", {"HOOKED"}) + QStringLiteral(R"GLSL(
vec4 hook() {
    const float SIGMA_S = 1.2;
    const float SIGMA_R = 0.12;
    const float CAS_STR = 0.8;
    vec4 tex = HOOKED_tex(HOOKED_pos);
    vec3 center = tex.rgb;
    vec3 bsum = vec3(0.0);
    float bwSum = 0.0;
    for (int bdx = -1; bdx <= 1; bdx++) {
        for (int bdy = -1; bdy <= 1; bdy++) {
            vec3 bn = HOOKED_texOff(vec2(float(bdx), float(bdy))).rgb;
            float bsw = exp(-float(bdx * bdx + bdy * bdy) / (2.0 * SIGMA_S * SIGMA_S));
            float bcd = length(bn - center);
            float bcw = exp(-(bcd * bcd) / (2.0 * SIGMA_R * SIGMA_R));
            bsum += bn * (bsw * bcw);
            bwSum += bsw * bcw;
        }
    }
    vec3 denoised = bsum / bwSum;
    vec3 cn = HOOKED_texOff(vec2(0.0, -1.0)).rgb;
    vec3 cs = HOOKED_texOff(vec2(0.0, 1.0)).rgb;
    vec3 ce = HOOKED_texOff(vec2(1.0, 0.0)).rgb;
    vec3 cw = HOOKED_texOff(vec2(-1.0, 0.0)).rgb;
    vec3 minRGB = min(denoised, min(min(cn, cs), min(ce, cw)));
    vec3 maxRGB = max(denoised, max(max(cn, cs), max(ce, cw)));
    vec3 rcp = -1.0 / (sqrt(minRGB / (maxRGB + 1e-4)) + 1.0);
    vec3 amp = clamp(min(minRGB, 2.0 - maxRGB) * rcp, -0.125, 0.0) * CAS_STR;
    float rcpW = 1.0 / (1.0 + 4.0 * amp.x);
    return vec4(clamp((denoised + (cn + cs + ce + cw) * amp) * rcpW, 0.0, 1.0), tex.a);
}

)GLSL");
}

QString passGrading(const ShaderValues &s, const CurveConstants &k)
{
    QString b = header("Farbkorrektur", {"HOOKED"}) + kCommon;
    b += R"GLSL(
vec3 srgbToLinear(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    vec3 low = c / 12.92;
    vec3 high = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), c));
}
vec3 linearToSrgb(vec3 c) {
    c = max(c, vec3(0.0));
    vec3 low = c * 12.92;
    vec3 high = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return clamp(mix(low, high, step(vec3(0.0031308), c)), 0.0, 1.0);
}
vec3 RRTAndODTFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}
vec3 tonemapACES(vec3 color) {
    const mat3 ACESInputMat = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 ACESOutputMat = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    color = ACESInputMat * color;
    color = RRTAndODTFit(color);
    color = ACESOutputMat * color;
    return clamp(color, 0.0, 1.0);
}
vec3 applyHueRotate(vec3 color, float cosHue, float sinHue) {
    float y = dot(color, LUMA);
    float cb = (color.b - y) / 1.8556;
    float cr = (color.r - y) / 1.5748;
    float cb2 = cosHue * cb - sinHue * cr;
    float cr2 = sinHue * cb + cosHue * cr;
    float r = y + 1.5748 * cr2;
    float bb = y + 1.8556 * cb2;
    return vec3(r, (y - LUMA.r * r - LUMA.b * bb) / LUMA.g, bb);
}
float uveSkin(vec3 c) { return clamp((c.r - c.g) * 8.0, 0.0, 1.0) * clamp((c.g - c.b) * 8.0, 0.0, 1.0); }

vec4 hook() {
    vec4 tex = HOOKED_tex(HOOKED_pos);
    vec3 color = tex.rgb;
)GLSL";
    // UVE SVG-Kette: BL (Versatz) und WL (Lichter-Tabelle) direkt nach der Schärfung, vor dem Grading
    if (active(s.levelBlackOffset))
        b += QStringLiteral("    color = clamp(color + vec3(%1), 0.0, 1.0);\n").arg(f(s.levelBlackOffset));
    if (active(s.levelWhiteAdj)) {
        QStringList t;
        for (double v : whiteLevelTable(s.levelWhiteAdj)) t << f(v);
        b += QStringLiteral(R"GLSL(    {
        const float WLT[17] = float[17](%1);
        vec3 wx = clamp(color, 0.0, 1.0) * 16.0;
        vec3 wi = min(floor(wx), vec3(15.0));
        vec3 wf = wx - wi;
        color = vec3(mix(WLT[int(wi.r)], WLT[int(wi.r) + 1], wf.r),
                     mix(WLT[int(wi.g)], WLT[int(wi.g) + 1], wf.g),
                     mix(WLT[int(wi.b)], WLT[int(wi.b) + 1], wf.b));
    }
)GLSL").arg(t.join(", "));
    }
    if (active(s.rGain, 1) || active(s.gGain, 1) || active(s.bGain, 1))
        b += QStringLiteral("    color *= vec3(%1, %2, %3);\n").arg(f(s.rGain), f(s.gGain), f(s.bGain));
    b += "    color = pow(max(color, vec3(0.0001)), vec3(1.0 / 2.2));\n";
    if (active(s.hueDeg))
        b += QStringLiteral("    { float hsk = %1 * uveSkin(color); color = mix(applyHueRotate(color, %2, %3), color, hsk); }\n")
                 .arg(f(k.hueSkin), f(s.cosHue), f(s.sinHue));
    if (active(s.vibrance))
        b += QStringLiteral(R"GLSL(    {
        float lv = dot(color, LUMA);
        float mv = (color.r + color.g + color.b) / 3.0;
        float sv = clamp((abs(color.r - mv) + abs(color.g - mv) + abs(color.b - mv)) * 0.75, 0.0, 1.0);
        float vg = 1.0 + %1 * pow(max(1.0 - sv, 0.0001), %2) * (1.0 - %3 * uveSkin(color));
        color = lv + (color - lv) * vg;
    }
)GLSL").arg(f(s.vibrance), f(k.vibrancePower), f(k.vibranceSkin));
    if (active(s.saturation, 1))
        b += QStringLiteral("    { float luma2 = dot(color, LUMA); color = luma2 + %1 * (color - luma2); }\n").arg(f(s.saturation));
    if (active(s.satCurve)) {
        const QString sw = k.saturationPower == 0 ? QStringLiteral("1.0") : QStringLiteral("pow(max(1.0 - sm, 0.0001), %1)").arg(f(k.saturationPower));
        const QString sg = k.desaturateEvenly ? (s.satCurve > 0 ? QStringLiteral("1.0 + %1 * sw").arg(f(s.satCurve)) : QStringLiteral("1.0 + %1").arg(f(s.satCurve)))
                                              : QStringLiteral("1.0 + %1 * sw").arg(f(s.satCurve));
        b += QStringLiteral(R"GLSL(    {
        float ls = dot(color, LUMA);
        float mn = (color.r + color.g + color.b) / 3.0;
        float sm = clamp((abs(color.r - mn) + abs(color.g - mn) + abs(color.b - mn)) * 0.75, 0.0, 1.0);
        float sw = %1;
        color = ls + (color - ls) * (%2);
    }
)GLSL").arg(sw, sg);
    }
    if (active(s.contrast, 1)) b += QStringLiteral("    color = (color - 0.5) * %1 + 0.5;\n").arg(f(s.contrast));
    if (active(s.brightness, 1)) b += QStringLiteral("    color *= %1;\n").arg(f(s.brightness));
    if (s.inBlack > 0.0001 || s.inWhite < 0.9999 || s.outBlack > 0.0001 || s.outWhite < 0.9999) {
        b += QStringLiteral("    {\n        vec3 lt = (color - %1) / max(%2 - %1, 0.0001);\n").arg(f(s.inBlack), f(s.inWhite));
        if (k.blackSoftness > 0 && s.inBlack > 0.0001)
            b += QStringLiteral("        { float kb = %1; vec3 tb = clamp(lt + kb, 0.0, 2.0 * kb); lt = mix(tb * tb / (4.0 * kb), lt, step(vec3(kb), lt)); }\n").arg(f(k.blackSoftness));
        if (k.whiteSoftness > 0 && s.inWhite < 0.9999)
            b += QStringLiteral("        { float kw = %1; vec3 uw = 1.0 - lt; vec3 tw = clamp(uw + kw, 0.0, 2.0 * kw); lt = 1.0 - mix(tw * tw / (4.0 * kw), uw, step(vec3(kw), uw)); }\n").arg(f(k.whiteSoftness));
        b += QStringLiteral("        lt = clamp(lt, 0.0, 1.0);\n        color = %1 + (%2 - %1) * lt;\n    }\n").arg(f(s.outBlack), f(s.outWhite));
    }
    if (active(s.contrastCurve)) {
        b += "    {\n        vec3 cc = clamp(color, 0.0, 1.0);\n";
        if (active(k.contrastPivotExp, 1)) b += QStringLiteral("        cc = pow(max(cc, vec3(0.0)), vec3(%1));\n").arg(f(k.contrastPivotExp));
        b += QStringLiteral("        cc = clamp(cc - %1 * sin(6.2831853 * cc) * 0.1591549, 0.0, 1.0);\n").arg(f(s.contrastCurve));
        if (active(k.contrastPivotExp, 1)) b += QStringLiteral("        cc = pow(max(cc, vec3(0.0)), vec3(%1));\n").arg(f(1.0 / k.contrastPivotExp));
        b += "        color = cc;\n    }\n";
    }
    if (active(s.shadows))
        b += QStringLiteral("    { vec3 sc = clamp(clamp(color, 0.0, 1.0) / %1, 0.0, 1.0); color += %2 * %3 * sc * pow(max(1.0 - sc, vec3(0.0001)), vec3(%4)); }\n")
                 .arg(f(k.shadowsThreshold), f(s.shadows), f(1.0 / k.shadowsPeak), f(k.shadowsPower));
    if (active(s.highlights))
        b += QStringLiteral("    { vec3 hc = clamp((clamp(color, 0.0, 1.0) - %1) / %2, 0.0, 1.0); color += %3 * %4 * pow(max(hc, vec3(0.0001)), vec3(%5)) * (1.0 - hc); }\n")
                 .arg(f(k.highlightsThreshold), f(1.0 - k.highlightsThreshold), f(s.highlights), f(1.0 / k.highlightsPeak), f(k.highlightsPower));
    // Gamma (Mitteltöne; Schwarz/Weiß fix) mit Begrenzung der Schattensteilheit
    b += QStringLiteral("    { vec3 gIn = max(color, vec3(0.0)); color = pow(max(gIn, vec3(0.0001)), vec3(%1));%2 }\n")
             .arg(f(1.0 / std::fmin(std::fmax(s.gamma, 0.5), 2.0)),
                  k.gammaShadowSlope > 0 ? QStringLiteral(" color = min(color, gIn * %1);").arg(f(k.gammaShadowSlope)) : QString());
    b += "    color = pow(max(color, vec3(0.0001)), vec3(2.2));\n";
    if (s.hdr > 0.0001) {
        b += QStringLiteral(R"GLSL(    {
        float hdr = clamp(%1, 0.0, 1.0);
        vec3 lin = srgbToLinear(color);
        float sceneLuma = dot(lin, LUMA);
        float hdrCurve = pow(hdr, 1.85);
        float exposure = pow(2.0, hdrCurve * 1.10);
        float shadowMask = 1.0 - clamp(sceneLuma * 2.25, 0.0, 1.0);
        float shadowLift = 1.0 + hdrCurve * 0.85 * shadowMask;
        lin *= exposure * shadowLift;
        lin = tonemapACES(lin);
        color = linearToSrgb(lin);
        color = clamp(color * (1.0 + hdrCurve * 0.14), 0.0, 1.0);
    }
)GLSL").arg(f(s.hdr));
    }
    const bool grainOn = s.grain > 0.0001, grainCurveOn = s.grainCurve > 0.0001;
    if (grainOn || grainCurveOn) b += "    float noise = fract(sin(HOOKED_pos.x * 12.9898 + HOOKED_pos.y * 78.233) * 43758.5453);\n";
    if (grainOn) b += QStringLiteral("    color += vec3((noise - 0.5) * %1);\n").arg(f(s.grain));
    if (grainCurveOn) {
        b += "    {\n        float glum = clamp(dot(color, LUMA), 0.0, 1.0);\n";
        if (active(k.grainPeakExp, 1)) b += QStringLiteral("        glum = pow(glum, %1);\n").arg(f(k.grainPeakExp));
        b += k.grainPower == 0 ? QStringLiteral("        float gw = 1.0;\n")
                               : QStringLiteral("        float gw = pow(clamp(4.0 * glum * (1.0 - glum), 0.0, 1.0), %1);\n").arg(f(k.grainPower));
        b += "        vec3 gn = vec3(noise);\n";
        if (k.grainColorAmount > 0)
            b += QStringLiteral("        gn = mix(gn, vec3(noise, fract(sin(dot(HOOKED_pos, vec2(39.3468, 11.1351))) * 24634.6345), fract(sin(dot(HOOKED_pos, vec2(73.1562, 52.2351))) * 14375.8453)), %1);\n").arg(f(k.grainColorAmount));
        b += QStringLiteral("        color += (gn - 0.5) * %1 * gw;\n    }\n").arg(f(s.grainCurve));
    }
    b += "    return vec4(clamp(color, 0.0, 1.0), tex.a);\n}\n\n";
    return b;
}

QString passDetailLutCompare(const ShaderValues &s, const CurveConstants &k, const Params &p, const CubeLut *lut, const QVector<double> *mx)
{
    QStringList binds{"HOOKED"};
    if (p.compare) binds << "UVEORIG";
    if (lut) binds << "UVELUT";
    QString b = header("Detail, LUT, Vergleich", binds) + kCommon;
    b += "float uveSkin(vec3 c) { return clamp((c.r - c.g) * 8.0, 0.0, 1.0) * clamp((c.g - c.b) * 8.0, 0.0, 1.0); }\n";
    b += "vec4 hook() {\n    vec4 tex = HOOKED_tex(HOOKED_pos);\n    vec3 color = tex.rgb;\n";
    if (active(s.sharpen)) {
        // Luma-Detailkurve (SL + Grading-Schärfe): Rauschschwelle, weiche Begrenzung, Halo-Balance, Hautschutz
        const double maxChange = std::fmax(0.001, k.sharpenMaxChange);
        const QString gate = k.sharpenNoiseThreshold > 0 ? QStringLiteral("smoothstep(0.0, %1, abs(sd))").arg(f(2 * k.sharpenNoiseThreshold)) : QStringLiteral("1.0");
        b += QStringLiteral(R"GLSL(    {
        float lc = uveLuma(color);
        float sr = %8; // Radius (SR)
        float lavg = (uveLuma(HOOKED_texOff(vec2(0.0, -sr)).rgb) + uveLuma(HOOKED_texOff(vec2(0.0, sr)).rgb)
                    + uveLuma(HOOKED_texOff(vec2(-sr, 0.0)).rgb) + uveLuma(HOOKED_texOff(vec2(sr, 0.0)).rgb)) * 0.25;
        float sd = lc - lavg;
        float sgate = %1;
        float sy = sd * sgate * %2 * %3 / %4;
        float lumaDelta = %4 * sy / sqrt(1.0 + sy * sy);
        lumaDelta *= lumaDelta > 0.0 ? %5 : %6;
        lumaDelta *= 1.0 - %7 * uveSkin(color);
        color = clamp(color + vec3(lumaDelta), 0.0, 1.0);
    }
)GLSL").arg(gate, f(s.sharpen), f(k.sharpenMaxAmount), f(maxChange), f(k.sharpenLightHalo), f(k.sharpenDarkHalo), f(k.sharpenSkin), f(s.sharpenRadius));
    }
    if (s.edge > 0.0001) {
        b += QStringLiteral(R"GLSL(    {
        float edgeStrength = pow(clamp(%1, 0.0, 1.0), 2.2);
        float tl = uveLuma(HOOKED_texOff(vec2(-1.0, -1.0)).rgb);
        float t  = uveLuma(HOOKED_texOff(vec2( 0.0, -1.0)).rgb);
        float tr = uveLuma(HOOKED_texOff(vec2( 1.0, -1.0)).rgb);
        float l  = uveLuma(HOOKED_texOff(vec2(-1.0,  0.0)).rgb);
        float r  = uveLuma(HOOKED_texOff(vec2( 1.0,  0.0)).rgb);
        float bl = uveLuma(HOOKED_texOff(vec2(-1.0,  1.0)).rgb);
        float bm = uveLuma(HOOKED_texOff(vec2( 0.0,  1.0)).rgb);
        float br = uveLuma(HOOKED_texOff(vec2( 1.0,  1.0)).rgb);
        float gx = -tl + tr - 2.0 * l + 2.0 * r - bl + br;
        float gy = -tl - 2.0 * t - tr + bl + 2.0 * bm + br;
        float edgeMask = smoothstep(0.18, 0.60, length(vec2(gx, gy))) * edgeStrength;
        color *= 1.0 - edgeMask * 0.92;
    }
)GLSL").arg(f(s.edge));
    }
    if (lut) {
        const double n = lut->size;
        b += QStringLiteral(R"GLSL(    {
        vec3 dmin = vec3(%1, %2, %3);
        vec3 dmax = vec3(%4, %5, %6);
        vec3 lc = clamp((color - dmin) / (dmax - dmin), 0.0, 1.0);
        vec3 coord = lc * %7 + %8;
        vec3 graded = texture(UVELUT, coord).rgb;
        color = mix(color, clamp(graded, 0.0, 1.0), %9);
    }
)GLSL").arg(f(lut->domainMin[0]), f(lut->domainMin[1]), f(lut->domainMin[2]),
               f(lut->domainMax[0]), f(lut->domainMax[1]), f(lut->domainMax[2]))
                 .arg(f((n - 1) / n), f(0.5 / n), f(std::fmin(std::fmax(p.lutStrength, 0.0), 1.0)));
    }
    if (mx) {
        // UVE-LUT-Profil: 4×5-Farbmatrix, zuletzt im sRGB-Raum (uve.user.js Z. 10341, applyColorMatrix Z. 10108)
        const auto &m = *mx;
        const double st = std::fmin(std::fmax(p.lutStrength, 0.0), 1.0);
        b += QStringLiteral("    {\n        vec3 lm = vec3(%1 * color.r + %2 * color.g + %3 * color.b + %4,\n")
                 .arg(f(m[0]), f(m[1]), f(m[2]), f(m[4]));
        b += QStringLiteral("                       %1 * color.r + %2 * color.g + %3 * color.b + %4,\n")
                 .arg(f(m[5]), f(m[6]), f(m[7]), f(m[9]));
        b += QStringLiteral("                       %1 * color.r + %2 * color.g + %3 * color.b + %4);\n")
                 .arg(f(m[10]), f(m[11]), f(m[12]), f(m[14]));
        b += QStringLiteral("        color = mix(color, clamp(lm, 0.0, 1.0), %1);\n    }\n").arg(f(st));
    }
    b += "    vec4 outc = vec4(clamp(color, 0.0, 1.0), tex.a);\n";
    if (p.compare) {
        const double split = std::fmin(std::fmax(p.compareSplit, 0.0), 1.0);
        b += QStringLiteral(R"GLSL(    if (HOOKED_pos.x < %1) outc = UVEORIG_tex(HOOKED_pos);
    if (abs(HOOKED_pos.x - %1) < 1.5 * HOOKED_pt.x) outc = vec4(1.0, 0.54, 0.12, 1.0);
)GLSL").arg(f(split));
    }
    b += "    return outc;\n}\n\n";
    return b;
}

QString lutTexture(const CubeLut &lut)
{
    // 3D-Textur im mpv-Benutzer-Shader: //!TEXTURE, //!SIZE w h d, Daten als Hex (rgba16f, little endian)
    QString t = QStringLiteral("//!TEXTURE UVELUT\n//!SIZE %1 %1 %1\n//!FORMAT rgba16f\n//!FILTER LINEAR\n//!BORDER CLAMP\n").arg(lut.size);
    const int count = lut.size * lut.size * lut.size;
    QByteArray bytes;
    bytes.reserve(count * 8);
    auto put = [&bytes](float v) { const quint16 h = floatToHalf(v); bytes.append(char(h & 0xFF)); bytes.append(char(h >> 8)); };
    for (int i = 0; i < count; ++i) {
        put(lut.rgb[i * 3]); put(lut.rgb[i * 3 + 1]); put(lut.rgb[i * 3 + 2]); put(1.0f);
    }
    t += QString::fromLatin1(bytes.toHex()) + "\n\n";
    return t;
}

} // namespace

QString buildShader(const ShaderBuildInput &in)
{
    const Params &p = in.params;
    const ShaderValues s = computeShaderValues(p);
    const CurveConstants &k = curveConstants();
    QString out = QStringLiteral("// UVE Player – Ultimate Video Enhancer (nativer Port), automatisch erzeugt\n\n");
    const CubeLut *lut = (in.lut && in.lut->isValid()) ? in.lut : nullptr;
    if (p.compare) out += passOriginal();
    if (p.gpuBase) out += passDenoiseCas();
    out += passGrading(s, k);
    const QVector<double> *mx = (in.lutMatrix && in.lutMatrix->size() == 20) ? in.lutMatrix : nullptr;
    out += passDetailLutCompare(s, k, p, lut, mx);
    return out;
}

QString buildLutShader(const CubeLut &lut)
{
    return QStringLiteral("// UVE Player – 3D-LUT \"%1\" (%2³)\n").arg(lut.title).arg(lut.size) + lutTexture(lut);
}

} // namespace uve::fx
