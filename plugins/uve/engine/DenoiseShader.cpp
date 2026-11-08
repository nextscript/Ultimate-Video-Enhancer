#include "DenoiseShader.h"

#include <QtGlobal>
#include <cmath>

namespace uve::fx {

namespace {

QString num(double v) { return QString::number(v, 'f', 6); }
double mixd(double a, double b, double t) { return a + (b - a) * t; }
double unit(int v) { return qBound(0, v, 100) / 100.0; }

QString header(const QString &desc, const QString &save = {})
{
    QString h = QStringLiteral("//!HOOK MAIN\n//!BIND HOOKED\n");
    if (!save.isEmpty()) h += QStringLiteral("//!SAVE %1\n").arg(save);
    return h + QStringLiteral("//!DESC %1: %2\n").arg(QLatin1String(kDenoiseStageOwner), desc);
}

// Helligkeit und Farbdifferenzen (BT.709): Rauschen wirkt vor allem auf Y, Chroma wird nur behutsam mitgeführt
const char *kYcc = R"GLSL(
const vec3 DN_KY = vec3(0.2126, 0.7152, 0.0722);
vec3 dnToYcc(vec3 c) { float y = dot(c, DN_KY); return vec3(y, (c.b - y) / 1.8556, (c.r - y) / 1.5748); }
vec3 dnToRgb(vec3 v) {
    float r = v.x + 1.5748 * v.z;
    float b = v.x + 1.8556 * v.y;
    return vec3(r, (v.x - DN_KY.r * r - DN_KY.b * b) / DN_KY.g, b);
}
)GLSL";

// Wertebereich: Signal 0 … 1. Bei Stärke 100 entspricht sigmaR etwa 19 von 255 Stufen – kein pauschaler Weichzeichner.
double rangeSigma(double s, double d) { return mixd(0.012, 0.075, s) * mixd(1.25, 0.6, d); }
double spatialSigma(double s) { return mixd(0.7, 1.6, s); }

QString passBilateral(const DenoiseParams &p)
{
    const double s = unit(p.strength), d = unit(p.detail);
    const double sr = rangeSigma(s, d), ss = spatialSigma(s), cr = sr * 2.0;
    QString b = header(QStringLiteral("Bilateral")) + kYcc;
    b += QStringLiteral(R"GLSL(
vec4 hook() {
    const float IS = %1, IR = %2, ICR = %3, KEEP = %4;
    vec4 tex = HOOKED_tex(HOOKED_pos);
    vec3 c = dnToYcc(tex.rgb);
    float sy = c.x, wy = 1.0, wc = 1.0;
    vec2 sc = c.yz;
    for (int j = -2; j <= 2; j++) {
        for (int i = -2; i <= 2; i++) {
            if (i == 0 && j == 0) continue;
            vec3 n = dnToYcc(HOOKED_texOff(vec2(float(i), float(j))).rgb);
            vec3 dv = n - c;
            float gs = exp(-float(i * i + j * j) * IS);
            float ry = dv.x * dv.x * IR;
            float w = gs * exp(-ry);
            sy += n.x * w; wy += w;
            float wn = gs * exp(-ry * 0.5 - dot(dv.yz, dv.yz) * ICR);
            sc += n.yz * wn; wc += wn;
        }
    }
    vec3 o = mix(vec3(sy / wy, sc / wc), c, KEEP);
    return vec4(dnToRgb(o), tex.a);
}

)GLSL").arg(num(1.0 / (2.0 * ss * ss)), num(1.0 / (2.0 * sr * sr)), num(1.0 / (2.0 * cr * cr)), num(0.25 * d));
    return b;
}

// Lokaler Kontrast steuert die Stärke: flache Flächen stärker, Kanten und Texturen schwächer.
// Die acht äußeren Abtastungen im Abstand 3 erfassen auch Blockränder und Mosquito-Rauschen um Kanten.
QString passAdaptive(const DenoiseParams &p)
{
    const double s = unit(p.strength), d = unit(p.detail);
    const double sr = rangeSigma(s, d) * 1.1, ss = spatialSigma(s) * 1.25, cr = sr * 2.0;
    QString b = header(QStringLiteral("Adaptive Bilateral")) + kYcc;
    b += QStringLiteral(R"GLSL(
vec4 hook() {
    const float IS = %1, IR = %2, ICR = %3, KEEP = %4, EDGE_LOW = %5;
    const vec2 FAR[8] = vec2[8](vec2(3.0, 0.0), vec2(-3.0, 0.0), vec2(0.0, 3.0), vec2(0.0, -3.0),
                                vec2(3.0, 3.0), vec2(-3.0, 3.0), vec2(3.0, -3.0), vec2(-3.0, -3.0));
    vec4 tex = HOOKED_tex(HOOKED_pos);
    vec3 c = dnToYcc(tex.rgb);
    // Gerichteter Anstieg über 5×5: Rauschen hebt sich dabei auf, echte Kanten nicht. Ein Min/Max-Kontrast würde
    // starkes Rauschen als Kante werten und es gerade dort stehen lassen. Wert ≈ Kantenhöhe.
    vec2 slope = vec2(0.0);
    for (int j = -2; j <= 2; j++) {
        for (int i = -2; i <= 2; i++) {
            if (i == 0 && j == 0) continue;
            slope += vec2(float(i), float(j)) * dot(HOOKED_texOff(vec2(float(i), float(j))).rgb, DN_KY);
        }
    }
    float act = smoothstep(0.035, 0.12, length(slope) * 0.08);
    float scale = mix(1.5, EDGE_LOW, act);
    float ir = IR / (scale * scale);
    float icr = ICR / (scale * scale);
    float sy = c.x, wy = 1.0, wc = 1.0;
    vec2 sc = c.yz;
    for (int k = 0; k < 32; k++) {
        vec2 off;
        if (k < 24) {
            int q = k < 12 ? k : k + 1;     // 5×5 ohne die Mitte
            off = vec2(float(q % 5 - 2), float(q / 5 - 2));
        } else {
            off = FAR[k - 24];
        }
        vec3 n = dnToYcc(HOOKED_texOff(off).rgb);
        vec3 dv = n - c;
        float gs = exp(-dot(off, off) * IS);
        float ry = dv.x * dv.x * ir;
        float w = gs * exp(-ry);
        sy += n.x * w; wy += w;
        float wn = gs * exp(-ry * 0.5 - dot(dv.yz, dv.yz) * icr);
        sc += n.yz * wn; wc += wn;
    }
    vec3 o = mix(vec3(sy / wy, sc / wc), c, KEEP * act);
    return vec4(dnToRgb(o), tex.a);
}

)GLSL").arg(num(1.0 / (2.0 * ss * ss)), num(1.0 / (2.0 * sr * sr)), num(1.0 / (2.0 * cr * cr)), num(0.25 * d), num(mixd(0.6, 0.3, d)));
    return b;
}

// Non-Local Means: gewichtet Nachbarpixel danach, wie ähnlich ihre Umgebung (Patch) der Umgebung des Pixels ist – nicht
// nach dem einzelnen Pixelwert. Wiederkehrende Strukturen bleiben dadurch besser erhalten als beim Bilateralfilter.
// Für Echtzeit begrenzt: 32 Suchpositionen (5×5 und acht äußere), Patch als Kreuz aus 5 Abtastungen, Vergleich auf Luma.
QString passNlMeans(const DenoiseParams &p)
{
    const double s = unit(p.strength), d = unit(p.detail);
    const double h = mixd(0.012, 0.06, s) * mixd(1.2, 0.7, d);
    const double cr = h * 3.0;
    QString b = header(QStringLiteral("NLMeans")) + kYcc;
    b += QStringLiteral(R"GLSL(
vec4 hook() {
    const float IH = %1, NOISE = %2, ICR = %3, KEEP = %4;
    const vec2 FAR[8] = vec2[8](vec2(3.0, 0.0), vec2(-3.0, 0.0), vec2(0.0, 3.0), vec2(0.0, -3.0),
                                vec2(3.0, 3.0), vec2(-3.0, 3.0), vec2(3.0, -3.0), vec2(-3.0, -3.0));
    const vec2 PATCH[4] = vec2[4](vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0));
    vec4 tex = HOOKED_tex(HOOKED_pos);
    vec3 c = dnToYcc(tex.rgb);
    float ref[4];
    for (int q = 0; q < 4; q++) ref[q] = dot(HOOKED_texOff(PATCH[q]).rgb, DN_KY);
    float sy = 0.0, wy = 0.0, wmax = 0.0, wc = 0.0;
    vec2 sc = vec2(0.0);
    for (int k = 0; k < 32; k++) {
        vec2 off;
        if (k < 24) {
            int q = k < 12 ? k : k + 1;     // 5×5 ohne die Mitte
            off = vec2(float(q % 5 - 2), float(q / 5 - 2));
        } else {
            off = FAR[k - 24];
        }
        vec3 n = dnToYcc(HOOKED_texOff(off).rgb);
        float e = n.x - c.x;
        float d2 = e * e;
        for (int q = 0; q < 4; q++) {
            e = dot(HOOKED_texOff(off + PATCH[q]).rgb, DN_KY) - ref[q];
            d2 += e * e;
        }
        float w = exp(-max(d2 * 0.2 - NOISE, 0.0) * IH);
        sy += n.x * w; wy += w;
        wmax = max(wmax, w);
        vec2 dc = n.yz - c.yz;
        float wn = w * exp(-dot(dc, dc) * ICR);
        sc += n.yz * wn; wc += wn;
    }
    // Das Pixel selbst zählt wie sein ähnlichster Nachbar, sonst dominiert es jede Mittelung
    wmax = max(wmax, 0.001);
    vec3 o = vec3((sy + c.x * wmax) / (wy + wmax), (sc + c.yz * wmax) / (wc + wmax));
    return vec4(dnToRgb(mix(o, c, KEEP)), tex.a);
}

)GLSL").arg(num(1.0 / (h * h)), num(0.25 * h * h), num(1.0 / (2.0 * cr * cr)), num(0.15 * d));
    return b;
}

// Eigener Pass: mittelt zufällige Punkte mit wachsendem Radius, übernimmt das Ergebnis aber nur, solange die Abweichung
// unter einer engen Schwelle bleibt. Kanten liegen über der Schwelle und bleiben erhalten; das feine Rauschen
// (nur wo geglättet wurde) verhindert neue Stufen. Verlorene Bildinformation kann das nicht zurückholen.
QString passDeband(const DenoiseParams &p)
{
    const double k = unit(p.debanding);
    QString b = header(QStringLiteral("Deband"));
    b += QStringLiteral(R"GLSL(
float dnRand(vec2 co, float seed) { return fract(sin(dot(co, vec2(12.9898, 78.233)) + seed) * 43758.5453); }
vec4 hook() {
    const float THRESHOLD = %1, RANGE = %2, GRAIN = %3;
    vec4 tex = HOOKED_tex(HOOKED_pos);
    vec2 px = HOOKED_pos * HOOKED_size;
    float seed = float(frame % 256) * 0.61803;
    vec3 c = tex.rgb;
    for (int it = 1; it <= 3; it++) {
        float fi = float(it);
        float r = RANGE * fi / 3.0 * dnRand(px, seed + fi);
        float a = 6.2831853 * dnRand(px, seed + fi + 7.0);
        vec2 o = r * vec2(cos(a), sin(a));
        vec3 avg = (HOOKED_texOff(o).rgb + HOOKED_texOff(-o).rgb
                  + HOOKED_texOff(vec2(-o.y, o.x)).rgb + HOOKED_texOff(vec2(o.y, -o.x)).rgb) * 0.25;
        vec3 weight = 1.0 - smoothstep(vec3(THRESHOLD * 0.5 / fi), vec3(THRESHOLD / fi), abs(avg - c));
        c = mix(c, avg, weight);
    }
    float changed = smoothstep(0.0, THRESHOLD * 0.25, length(c - tex.rgb));
    c += (dnRand(px, seed + 13.0) - 0.5) * GRAIN * changed;
    return vec4(c, tex.a);
}

)GLSL").arg(num(mixd(0.002, 0.012, k)), num(mixd(8.0, 24.0, k)), num(mixd(0.001, 0.004, k)));
    return b;
}

} // namespace

QString denoiseModeKey(DenoiseMode mode)
{
    switch (mode) {
    case DenoiseMode::Bilateral: return QStringLiteral("bilateral");
    case DenoiseMode::AdaptiveDeband: return QStringLiteral("adaptive");
    case DenoiseMode::Temporal: return QStringLiteral("temporal");
    case DenoiseMode::NLMeans: return QStringLiteral("nlmeans");
    case DenoiseMode::Off: break;
    }
    return QStringLiteral("off");
}

DenoiseMode denoiseModeFromKey(const QString &key)
{
    if (key == QLatin1String("bilateral")) return DenoiseMode::Bilateral;
    if (key == QLatin1String("adaptive")) return DenoiseMode::AdaptiveDeband;
    if (key == QLatin1String("temporal")) return DenoiseMode::Temporal;
    if (key == QLatin1String("nlmeans")) return DenoiseMode::NLMeans;
    return DenoiseMode::Off;
}

DenoiseParams denoiseFromJson(const QJsonObject &o)
{
    DenoiseParams p;
    p.mode = denoiseModeFromKey(o.value(QStringLiteral("mode")).toString());
    p.strength = qBound(0, o.value(QStringLiteral("strength")).toInt(p.strength), 100);
    p.detail = qBound(0, o.value(QStringLiteral("detailPreservation")).toInt(p.detail), 100);
    p.debanding = qBound(0, o.value(QStringLiteral("debanding")).toInt(p.debanding), 100);
    return p;
}

QJsonObject denoiseToJson(const DenoiseParams &p)
{
    return {{"schemaVersion", kDenoiseSchemaVersion}, {"mode", denoiseModeKey(p.mode)}, {"strength", p.strength},
            {"detailPreservation", p.detail}, {"debanding", p.debanding}};
}

DenoiseParams denoisePreset(const DenoiseParams &current, const QString &resolution)
{
    struct Row { int strength, detail, debanding; };
    // Spalten: 480p, 720p (Standard), 1080p, 4K – je höher die Auflösung, desto feiner das Rauschen je Pixelfläche
    static const Row bilateral[] = {{50, 50, 35}, {35, 60, 35}, {25, 65, 35}, {15, 70, 35}};
    static const Row adaptive[] = {{50, 50, 45}, {40, 60, 35}, {30, 65, 30}, {20, 70, 25}};
    static const Row temporal[] = {{55, 50, 35}, {45, 55, 35}, {35, 60, 35}, {25, 65, 35}};
    static const Row nlmeans[] = {{50, 50, 35}, {40, 60, 35}, {30, 65, 35}, {20, 70, 35}};
    const int i = resolution == QLatin1String("480p") ? 0 : resolution == QLatin1String("1080p") ? 2 : resolution == QLatin1String("4k") ? 3 : 1;
    DenoiseParams p = current;
    const Row *table = nullptr;
    switch (current.mode) {
    case DenoiseMode::Bilateral: table = bilateral; break;
    case DenoiseMode::AdaptiveDeband: table = adaptive; break;
    case DenoiseMode::Temporal: table = temporal; break;
    case DenoiseMode::NLMeans: table = nlmeans; break;
    case DenoiseMode::Off: return p;
    }
    p.strength = table[i].strength;
    p.detail = table[i].detail;
    if (current.mode == DenoiseMode::AdaptiveDeband) p.debanding = table[i].debanding;
    return p;
}

bool denoiseUsesShader(const DenoiseParams &p)
{
    if (p.mode == DenoiseMode::Bilateral || p.mode == DenoiseMode::NLMeans) return p.strength > 0;
    if (p.mode == DenoiseMode::AdaptiveDeband) return p.strength > 0 || p.debanding > 0;
    return false;
}

QString buildDenoiseShader(const DenoiseParams &p, bool saveOriginal)
{
    if (!denoiseUsesShader(p)) return {};
    QString out = QStringLiteral("// UVE Player – Denoising, automatisch erzeugt\n\n");
    if (saveOriginal) out += header(QStringLiteral("Original sichern"), QStringLiteral("UVEORIG")) + QStringLiteral("vec4 hook() { return HOOKED_tex(HOOKED_pos); }\n\n");
    if (p.mode == DenoiseMode::Bilateral) {
        out += passBilateral(p);
    } else if (p.mode == DenoiseMode::NLMeans) {
        out += passNlMeans(p);
    } else {
        if (p.strength > 0) out += passAdaptive(p);
        if (p.debanding > 0) out += passDeband(p);
    }
    return out;
}

QString buildTemporalGraph(const DenoiseParams &p)
{
    if (p.mode != DenoiseMode::Temporal || p.strength <= 0) return {};
    const double s = unit(p.strength), d = unit(p.detail);
    // Schwellen je Ebene: a begrenzt die Abweichung eines Nachbarframes, b die Summe über das Fenster. Bewegung und
    // Szenenschnitte über a beenden das Fenster je Pixel. Bewegung mit weniger Kontrast als a wird dagegen mitgemittelt
    // und zieht einen Schleier hinter sich her (gemessen: Spur bis 60 px, 6 Stufen bei a = 14 und Kontrast 12).
    // Daher höchstens etwa 10 von 255 Stufen: Temporal ist für leichtes Rauschen, starkes gehört zu NLMeans.
    // b muss deutlich über a liegen, sonst bricht das Fenster schon am Rauschen ab. Fenster fest und klein (Speicher begrenzt).
    const double a = mixd(3.0, 10.0, s) / 255.0 * mixd(1.15, 0.75, d);
    const int frames = s < 0.5 ? 5 : 7;
    const QString y = num(a), yb = num(a * 6.0), c = num(a * 1.3), cb = num(a * 7.8);
    return QStringLiteral("atadenoise=0a=%1:0b=%2:1a=%3:1b=%4:2a=%3:2b=%4:s=%5").arg(y, yb, c, cb).arg(frames);
}

} // namespace uve::fx
