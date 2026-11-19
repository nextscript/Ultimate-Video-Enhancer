#include "ShaderConverter.h"

#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace glslmgr {

namespace {

using RX = QRegularExpression;
constexpr auto kMulti = RX::MultilineOption;

const QString kNum = QStringLiteral(R"(-?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?)");

QString normalized(const QString &code)
{
    QString s = code;
    if (s.startsWith(QChar(0xFEFF))) s.remove(0, 1);
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    s.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return s;
}

// GL-Treiber lehnen Nicht-ASCII-Zeichen teils auch in Kommentaren ab
QString asciiOnly(QString s)
{
    for (auto &c : s)
        if (c.unicode() > 126 || (c.unicode() < 32 && c != QLatin1Char('\n') && c != QLatin1Char('\t'))) c = QLatin1Char('?');
    return s;
}

QString floatLiteral(double v)
{
    if (!std::isfinite(v)) v = 0.0;
    QString s = QString::number(v, 'g', 9);
    if (!s.contains(QLatin1Char('.')) && !s.contains(QLatin1Char('e'))) s += QStringLiteral(".0");
    return v < 0 ? QLatin1Char('(') + s + QLatin1Char(')') : s;
}

bool looksLikeInt(const QString &literal) { return !literal.contains(QLatin1Char('.')) && !literal.contains(QLatin1Char('e'), Qt::CaseInsensitive); }

bool excludedDefine(const QString &name)
{
    static const RX re(QStringLiteral("^(PI|M_PI|E|TAU|P[0-9]+)$"), RX::CaseInsensitiveOption);
    return re.match(name).hasMatch();
}

struct Param
{
    QString name, value, label;
    bool isInt = false;
    bool hasMin = false, hasMax = false;
    double min = 0, max = 1;
    qsizetype pos = 0;
};

// //!PARAM-Blöcke (vo_gpu_next) lesen und entfernen; vo_gpu kennt sie nicht
QList<Param> extractParams(const QString &src, QString *stripped)
{
    QList<Param> params;
    QStringList out;
    const QStringList lines = src.split(QLatin1Char('\n'));
    static const RX paramRe(QStringLiteral(R"(^\s*//!\s*PARAM\s+(\w+))"));
    static const RX cmdRe(QStringLiteral(R"(^\s*//!\s*(\w+)\s*(.*)$)"));
    qsizetype pos = 0;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const auto m = paramRe.match(lines[i]);
        if (!m.hasMatch()) { out << lines[i]; pos += lines[i].size() + 1; continue; }
        Param p;
        p.name = m.captured(1);
        p.label = p.name;
        p.pos = pos;
        qsizetype j = i + 1;
        for (; j < lines.size(); ++j) {
            const auto c = cmdRe.match(lines[j]);
            if (!c.hasMatch()) break;
            const QString cmd = c.captured(1).toUpper(), val = c.captured(2).trimmed();
            if (cmd == QLatin1String("DESC")) p.label = val;
            else if (cmd == QLatin1String("MINIMUM")) { p.min = val.toDouble(&p.hasMin); }
            else if (cmd == QLatin1String("MAXIMUM")) { p.max = val.toDouble(&p.hasMax); }
            else if (cmd == QLatin1String("TYPE")) p.isInt = val.contains(QLatin1String("int"));
            else break;   // nächster Block
        }
        for (; j < lines.size(); ++j) {
            const QString t = lines[j].trimmed();
            if (t.startsWith(QLatin1String("//!"))) break;
            if (!t.isEmpty() && p.value.isEmpty()) p.value = t;
            else if (t.isEmpty() && !p.value.isEmpty()) break;
        }
        params << p;
        i = j - 1;
    }
    if (stripped) *stripped = out.join(QLatin1Char('\n'));
    return params;
}

// Numerische #define-Werte durch die Reglerwerte ersetzen (Kommentar am Zeilenende bleibt erhalten)
QString applyDefineValues(const QString &src, const QVariantMap &uniforms)
{
    static const RX re(QStringLiteral(R"(^(\s*#define\s+)([A-Za-z_]\w*)(\s+))") + QLatin1Char('(') + kNum + QStringLiteral(R"()f?(\s*(?://.*)?)$)"), kMulti);
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(src);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString key = QStringLiteral("u_def_") + m.captured(2);
        if (excludedDefine(m.captured(2)) || !uniforms.contains(key)) continue;
        const double v = uniforms.value(key).toDouble();
        const QString lit = looksLikeInt(m.captured(4)) ? QString::number(qRound64(v)) : floatLiteral(v);
        out += src.mid(last, m.capturedStart() - last);
        out += m.captured(1) + m.captured(2) + m.captured(3) + lit + m.captured(5);
        last = m.capturedEnd();
    }
    return out + src.mid(last);
}

QString paramDefine(const Param &p, const QVariantMap &uniforms)
{
    QString value = p.value;
    if (uniforms.contains(p.name)) {
        const double v = uniforms.value(p.name).toDouble();
        value = p.isInt ? QString::number(qRound64(v)) : floatLiteral(v);
    }
    return QStringLiteral("#define %1 %2").arg(p.name, value);
}

QString convertHook(const QString &source, const ConvertInput &in)
{
    QString body;
    const QList<Param> params = extractParams(source, &body);
    body = applyDefineValues(body, in.uniforms);

    // Hook-Rumpf ohne Kopfzeilen: wie Pass auf dem Hauptbild behandeln
    static const RX hookCmd(QStringLiteral(R"(^\s*//!\s*HOOK\b)"), kMulti);
    if (!hookCmd.match(body).hasMatch()) {
        static const RX anyCmd(QStringLiteral(R"(^\s*//![^\n]*\n?)"), kMulti);
        body.remove(anyCmd);
        body = QStringLiteral("//!HOOK MAIN\n//!BIND HOOKED\n//!DESC GLSL Manager: %1\n").arg(in.label) + body;
    }
    if (params.isEmpty()) return body;

    // Die Werte der Parameter gehören in jeden Pass: nach den //!-Kopfzeilen eines HOOK-Blocks einfügen
    QStringList defines;
    for (const auto &p : params) defines << paramDefine(p, in.uniforms);
    QStringList out;
    bool inHeader = false;
    for (const auto &line : body.split(QLatin1Char('\n'))) {
        const bool cmd = line.trimmed().startsWith(QLatin1String("//!"));
        if (cmd && line.contains(QLatin1String("HOOK"))) inHeader = true;
        else if (cmd && line.contains(QLatin1String("TEXTURE"))) inHeader = false;
        if (inHeader && !cmd) { out << defines; inHeader = false; }
        out << line;
    }
    return out.join(QLatin1Char('\n'));
}

QString blendFunctions(const QString &mode)
{
    // Formeln wie CSS mix-blend-mode (das Userscript legt seine Filter so über das Video)
    static const QHash<QString, QString> sep{
        {QStringLiteral("multiply"), QStringLiteral("return b * s;")},
        {QStringLiteral("screen"), QStringLiteral("return b + s - b * s;")},
        {QStringLiteral("darken"), QStringLiteral("return min(b, s);")},
        {QStringLiteral("lighten"), QStringLiteral("return max(b, s);")},
        {QStringLiteral("overlay"), QStringLiteral("return mix(2.0*b*s, 1.0-2.0*(1.0-b)*(1.0-s), step(0.5, b));")},
        {QStringLiteral("hard-light"), QStringLiteral("return mix(2.0*b*s, 1.0-2.0*(1.0-b)*(1.0-s), step(0.5, s));")},
        {QStringLiteral("color-dodge"), QStringLiteral("vec3 r = min(vec3(1.0), b / max(1.0 - s, vec3(1e-5))); r = mix(r, vec3(1.0), step(1.0, s)); return mix(r, vec3(0.0), step(b, vec3(0.0)));")},
        {QStringLiteral("color-burn"), QStringLiteral("vec3 r = 1.0 - min(vec3(1.0), (1.0 - b) / max(s, vec3(1e-5))); r = mix(r, vec3(0.0), step(s, vec3(0.0))); return mix(r, vec3(1.0), step(1.0, b));")},
        {QStringLiteral("soft-light"), QStringLiteral("vec3 d = mix(((16.0*b-12.0)*b+4.0)*b, sqrt(b), step(0.25, b)); return mix(b-(1.0-2.0*s)*b*(1.0-b), b+(2.0*s-1.0)*(d-b), step(0.5, s));")},
        {QStringLiteral("difference"), QStringLiteral("return abs(b - s);")},
        {QStringLiteral("exclusion"), QStringLiteral("return b + s - 2.0 * b * s;")},
    };
    static const QString hsl = QStringLiteral(
        "float gvfLum(vec3 c){ return dot(c, vec3(0.3, 0.59, 0.11)); }\n"
        "float gvfSat(vec3 c){ return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }\n"
        "vec3 gvfClip(vec3 c){ float l = gvfLum(c), n = min(c.r, min(c.g, c.b)), x = max(c.r, max(c.g, c.b));\n"
        "  if (n < 0.0) c = l + (c - l) * l / (l - n);\n"
        "  if (x > 1.0) c = l + (c - l) * (1.0 - l) / (x - l);\n"
        "  return c; }\n"
        "vec3 gvfSetLum(vec3 c, float l){ return gvfClip(c + (l - gvfLum(c))); }\n"
        "vec3 gvfSetSat(vec3 c, float s){ float n = min(c.r, min(c.g, c.b)), x = max(c.r, max(c.g, c.b));\n"
        "  return x > n ? (c - n) * s / (x - n) : vec3(0.0); }\n");
    static const QHash<QString, QString> nonsep{
        {QStringLiteral("hue"), QStringLiteral("return gvfSetLum(gvfSetSat(s, gvfSat(b)), gvfLum(b));")},
        {QStringLiteral("saturation"), QStringLiteral("return gvfSetLum(gvfSetSat(b, gvfSat(s)), gvfLum(b));")},
        {QStringLiteral("color"), QStringLiteral("return gvfSetLum(s, gvfLum(b));")},
        {QStringLiteral("luminosity"), QStringLiteral("return gvfSetLum(b, gvfLum(s));")},
    };
    if (sep.contains(mode)) return QStringLiteral("vec3 gvfBlend(vec3 b, vec3 s){ %1 }\n").arg(sep.value(mode));
    if (nonsep.contains(mode)) return hsl + QStringLiteral("vec3 gvfBlend(vec3 b, vec3 s){ %1 }\n").arg(nonsep.value(mode));
    return {};
}

QString replaceAll(QString s, const QString &pattern, const QString &with, RX::PatternOptions opts = RX::NoPatternOption)
{
    s.replace(RX(pattern, opts), with);
    return s;
}

// Ablauf wie _normalizeUserFrag/_buildFragSrc des Userscripts, Ziel ist aber ein hook() für mpv
QString convertFragment(const QString &source, const ConvertInput &in)
{
    const QVariantList defs = parseUniformDefs(source);
    QString s = source;

    s = replaceAll(s, QStringLiteral(R"(^\s*//\s*@(?:uniform|select)[^\n]*)"), QString(), kMulti);
    static const RX anyCmd(QStringLiteral(R"(^\s*//![^\n]*$)"), kMulti);
    s.remove(anyCmd);

    // Reglerwerte: annotierte Namen werden zu #define, ihre eigenen Deklarationen fallen weg
    QString defines;
    for (const auto &v : defs) {
        const auto d = v.toMap();
        const QString name = d.value("name").toString();
        if (name.startsWith(QLatin1String("u_def_"))) continue;
        const QString e = RX::escape(name);
        s = replaceAll(s, QStringLiteral(R"(^\s*#define\s+%1\b[^\n]*)").arg(e), QString(), kMulti);
        s = replaceAll(s, QStringLiteral(R"(^\s*uniform\s+(?:\w+\s+)?float\s+%1\s*;[^\n]*)").arg(e), QString(), kMulti);
        s = replaceAll(s, QStringLiteral(R"(^\s*(?:const\s+)?float\s+%1\s*=[^;]+;[^\n]*)").arg(e), QString(), kMulti);
        const QVariant value = in.uniforms.contains(name) ? in.uniforms.value(name) : d.value("def");
        defines += QStringLiteral("#define %1 %2\n").arg(name, floatLiteral(value.toDouble()));
    }
    s = applyDefineValues(s, in.uniforms);

    s = replaceAll(s, QStringLiteral(R"(^\s*#version\s+[^\n]*)"), QString(), kMulti);
    s = replaceAll(s, QStringLiteral(R"(^\s*#extension\s+[^\n]*)"), QString(), kMulti);
    s = replaceAll(s, QStringLiteral(R"(^\s*precision\s+\w+\s+\w+\s*;)"), QString(), kMulti);

    // Vom Userscript bereitgestellte Eingänge
    s = replaceAll(s, QStringLiteral(R"(^\s*uniform\s+(?:\w+p\s+)?\w+\s+(?:u_video|u_video_raw|u_res|u_time|u_mouse|u_strength|u_layers|u_zoom|u_avg_lum|u_avg_r|u_avg_g|u_avg_b|u_contrast|u_gvf_mix|u_gvf_offset_scale|iChannel[01]|iResolution|iTime|iMouse|iFrame)\s*;)"), QString(), kMulti);

    // Eigene Namen für Ein-/Ausgang übernehmen
    QString aliases;
    {
        static const RX outRe(QStringLiteral(R"(^\s*(?:layout\s*\([^)]*\)\s*)?out\s+(?:\w+p\s+)?vec4\s+(\w+)\s*;)"), kMulti);
        for (auto it = outRe.globalMatch(s); it.hasNext();) {
            const QString n = it.next().captured(1);
            if (n != QLatin1String("fragColor")) aliases += QStringLiteral("#define %1 fragColor\n").arg(n);
        }
        s.remove(outRe);
        static const RX inRe(QStringLiteral(R"(^\s*(?:in|varying)\s+(?:\w+p\s+)?vec2\s+(\w+)\s*;)"), kMulti);
        for (auto it = inRe.globalMatch(s); it.hasNext();) {
            const QString n = it.next().captured(1);
            if (n != QLatin1String("v_uv")) aliases += QStringLiteral("#define %1 v_uv\n").arg(n);
        }
        s.remove(inRe);
        // Weitere Eingänge gibt es hier nicht: Texturen zeigen auf das Bild, Zahlen haben den Wert 0 wie in WebGL
        static const RX samplerRe(QStringLiteral(R"(^\s*uniform\s+(?:\w+p\s+)?sampler2D\s+(\w+)\s*;)"), kMulti);
        for (auto it = samplerRe.globalMatch(s); it.hasNext();) aliases += QStringLiteral("#define %1 HOOKED_raw\n").arg(it.next().captured(1));
        s.remove(samplerRe);
        s = replaceAll(s, QStringLiteral(R"(^\s*uniform\s+(?:\w+p\s+)?(float|int|bool|vec[234]|ivec[234])\s+(\w+)\s*;)"), QStringLiteral(R"(const \1 \2 = \1(0);)"), kMulti);
    }

    // Veraltete Schreibweisen; alle Texturzugriffe laufen über gvfTexture (Bild in WebGL steht auf dem Kopf)
    s = replaceAll(s, QStringLiteral(R"(\bgl_FragColor\b)"), QStringLiteral("fragColor"));
    s = replaceAll(s, QStringLiteral(R"(\bgl_FragData\s*\[\s*\d+\s*\])"), QStringLiteral("fragColor"));
    s = replaceAll(s, QStringLiteral(R"(\bgl_FragCoord\b)"), QStringLiteral("gvfFragCoord"));
    s = replaceAll(s, QStringLiteral(R"(\b(?:texture2DLod(?:EXT)?|textureLod)\s*\()"), QStringLiteral("gvfTextureLod("));
    s = replaceAll(s, QStringLiteral(R"(\b(?:texture2D|texture|shadow2D)\s*\()"), QStringLiteral("gvfTexture("));
    s = replaceAll(s, QStringLiteral(R"(\btexelFetch\s*\()"), QStringLiteral("gvfTexelFetch("));
    s = replaceAll(s, QStringLiteral(R"(\boutColor\b)"), QStringLiteral("fragColor"));

    // Einstieg finden: main, Shadertoy, letzte Hilfsfunktion oder nackter Rumpf
    static const RX mainRe(QStringLiteral(R"(\bvoid\s+main\s*\(\s*(?:void)?\s*\))"));
    QString tail;
    if (mainRe.match(s).hasMatch()) {
        s.replace(mainRe, QStringLiteral("void gvfUserMain()"));
    } else if (s.contains(RX(QStringLiteral(R"(\bmainImage\s*\()")))) {
        tail = QStringLiteral("void gvfUserMain(){ mainImage(fragColor, v_uv * u_res); }\n");
    } else {
        static const RX fnRe(QStringLiteral(R"(\b(vec4|vec3|vec2|float|void)\s+(\w+)\s*\(([^)]*)\))"));
        QString ret, name, params;
        for (auto it = fnRe.globalMatch(s); it.hasNext();) {
            const auto m = it.next();
            ret = m.captured(1); name = m.captured(2); params = m.captured(3);
        }
        if (!name.isEmpty() && ret != QLatin1String("void")) {
            QStringList args;
            int vec2Count = 0;
            for (const auto &raw : params.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                const QString type = raw.trimmed().split(RX(QStringLiteral(R"(\s+)"))).value(0);
                if (type == QLatin1String("vec2")) args << (vec2Count++ == 0 ? QStringLiteral("v_uv") : QStringLiteral("u_res"));
                else if (type == QLatin1String("sampler2D")) args << QStringLiteral("u_video");
                else if (type == QLatin1String("float")) args << QStringLiteral("1.0");
                else if (type == QLatin1String("vec3")) args << QStringLiteral("vec3(1.0)");
                else if (type == QLatin1String("vec4")) args << QStringLiteral("vec4(1.0)");
                else if (type == QLatin1String("int")) args << QStringLiteral("1");
                else if (type == QLatin1String("bool")) args << QStringLiteral("false");
                else args << QStringLiteral("0.0");
            }
            const QString call = name + QLatin1Char('(') + args.join(QStringLiteral(", ")) + QLatin1Char(')');
            const QString assign = ret == QLatin1String("vec4") ? QStringLiteral("fragColor = %1;").arg(call)
                                 : ret == QLatin1String("vec3") ? QStringLiteral("fragColor = vec4(%1, 1.0);").arg(call)
                                 : ret == QLatin1String("vec2") ? QStringLiteral("fragColor = vec4(%1, 0.0, 1.0);").arg(call)
                                                                : QStringLiteral("float _r = %1; fragColor = vec4(_r, _r, _r, 1.0);").arg(call);
            tail = QStringLiteral("void gvfUserMain(){ %1 }\n").arg(assign);
        } else {
            s = QStringLiteral("void gvfUserMain(){\n") + s + QStringLiteral("\n}\n");
        }
    }

    static const RX statsRe(QStringLiteral(R"(\b(?:u_avg_lum|u_avg_r|u_avg_g|u_avg_b|u_contrast)\b)"));
    static const RX timeRe(QStringLiteral(R"(\b(?:u_time|iTime)\b)"));
    const bool stats = statsRe.match(s).hasMatch();
    const bool time = timeRe.match(s).hasMatch();
    const QString statsTex = QStringLiteral("GVFSTATS") + QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha1).toHex().left(8)).toUpper();

    QString out;
    if (stats) {
        // Ersatz für den Frame Analyzer des Userscripts: Mittelwerte und Kontrast in eine 1×1-Textur
        out += QStringLiteral("//!HOOK MAIN\n//!BIND HOOKED\n//!SAVE %1\n//!WIDTH 1\n//!HEIGHT 1\n//!COMPONENTS 4\n//!DESC GLSL Manager: Frame-Statistik\n").arg(statsTex);
        out += QStringLiteral(
            "vec4 hook() {\n"
            "    vec3 sum = vec3(0.0); float lo = 1.0, hi = 0.0;\n"
            "    for (int y = 0; y < 9; y++) for (int x = 0; x < 16; x++) {\n"
            "        vec3 c = HOOKED_tex(vec2((float(x) + 0.5) / 16.0, (float(y) + 0.5) / 9.0)).rgb;\n"
            "        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
            "        sum += c; lo = min(lo, l); hi = max(hi, l);\n"
            "    }\n"
            "    return vec4(sum / 144.0, hi - lo);\n"
            "}\n\n");
    }
    out += QStringLiteral("//!HOOK MAIN\n//!BIND HOOKED\n");
    if (stats) out += QStringLiteral("//!BIND %1\n").arg(statsTex);
    out += QStringLiteral("//!DESC GLSL Manager: %1\n").arg(in.label);

    out += QStringLiteral(
        "#define u_video HOOKED_raw\n#define u_video_raw HOOKED_raw\n#define iChannel0 HOOKED_raw\n#define iChannel1 HOOKED_raw\n"
        "#define u_res HOOKED_size\n#define iResolution vec3(HOOKED_size, 1.0)\n"
        "#define v_uv vec2(HOOKED_pos.x, 1.0 - HOOKED_pos.y)\n"
        "#define u_mouse vec2(0.5)\n#define iMouse vec4(0.0)\n#define iFrame frame\n"
        "#define u_strength 1.0\n#define u_layers 1.0\n#define u_zoom 1.0\n#define u_gvf_mix 1.0\n#define u_gvf_offset_scale 1.0\n");
    if (time) out += QStringLiteral("#define u_time (float(frame) / %1)\n#define iTime u_time\n").arg(floatLiteral(in.fps > 1.0 ? in.fps : 30.0));
    if (stats) {
        out += QStringLiteral("#define u_avg_r %1_tex(vec2(0.5)).r\n#define u_avg_g %1_tex(vec2(0.5)).g\n#define u_avg_b %1_tex(vec2(0.5)).b\n#define u_contrast %1_tex(vec2(0.5)).a\n").arg(statsTex);
        out += QStringLiteral("#define u_avg_lum dot(%1_tex(vec2(0.5)).rgb, vec3(0.2126, 0.7152, 0.0722))\n").arg(statsTex);
    }
    out += defines;
    out += aliases;
    out += QStringLiteral(
        "vec4 fragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
        "vec4 gvfFragCoord = vec4(0.0);\n"
        "vec4 gvfTexture(sampler2D t, vec2 uv) { return HOOKED_mul * texture(t, vec2(uv.x, 1.0 - uv.y)); }\n"
        "vec4 gvfTexture(sampler2D t, vec2 uv, float bias) { return HOOKED_mul * texture(t, vec2(uv.x, 1.0 - uv.y), bias); }\n"
        "vec4 gvfTextureLod(sampler2D t, vec2 uv, float lod) { return HOOKED_mul * textureLod(t, vec2(uv.x, 1.0 - uv.y), lod); }\n"
        "vec4 gvfTexelFetch(sampler2D t, ivec2 p, int lod) { ivec2 z = textureSize(t, lod); return HOOKED_mul * texelFetch(t, ivec2(p.x, z.y - 1 - p.y), lod); }\n");
    out += blendFunctions(in.blendMode);
    out += s;
    out += QLatin1Char('\n');
    out += tail;

    const QString mixed = blendFunctions(in.blendMode).isEmpty() ? QStringLiteral("fragColor.rgb") : QStringLiteral("gvfBlend(clamp(src.rgb, 0.0, 1.0), clamp(fragColor.rgb, 0.0, 1.0))");
    out += QStringLiteral(
        "vec4 hook() {\n"
        "    gvfFragCoord = vec4(v_uv * u_res, 0.0, 1.0);\n"
        "    gvfUserMain();\n"
        "    vec4 src = HOOKED_tex(HOOKED_pos);\n"
        "    return vec4(mix(src.rgb, %1, clamp(fragColor.a, 0.0, 1.0)), src.a);\n"
        "}\n").arg(mixed);
    return out;
}

} // namespace

bool isMpvHook(const QString &code)
{
    static const RX re(QStringLiteral(R"((^\s*//!\s*HOOK\b)|(\bvec4\s+hook\s*\(\s*\))|(\bHOOKED_(?:tex|texOff|pos|pt|size)\b))"), kMulti);
    return re.match(code).hasMatch();
}

QString detectType(const QString &code)
{
    static const RX re(QStringLiteral(R"((^\s*#version\b)|(\bvoid\s+main\s*\()|(\bmainImage\s*\()|(\buniform\s+\w+)|(\bfragColor\b)|(\bgl_FragColor\b))"), kMulti);
    if (isMpvHook(code) || re.match(code).hasMatch()) return QStringLiteral("webgl");
    if (code.trimmed().startsWith(QLatin1Char('<'))) return QStringLiteral("svg");
    return QStringLiteral("webgl");
}

QString descriptionOf(const QString &code)
{
    static const RX re(QStringLiteral(R"(^\s*//!\s*DESC\s+([^\r\n]+))"), kMulti);
    return re.match(code).captured(1).trimmed();
}

QVariantList parseUniformDefs(const QString &codeIn)
{
    const QString src = normalized(codeIn);
    struct Def { QVariantMap map; qsizetype pos; };
    QList<Def> defs;
    QSet<QString> seen;
    const auto addSlider = [&](const QString &name, double def, double min, double max, const QString &label, qsizetype pos, double step) {
        if (name.isEmpty() || seen.contains(name) || !std::isfinite(def)) return;
        seen.insert(name);
        static const RX boolRe(QStringLiteral(R"((?:^|[_\s\-/])(enable|enabled|toggle|debug|use|active)(?:$|[_\s\-/]))"), RX::CaseInsensitiveOption);
        const bool toggle = min == 0.0 && max == 1.0 && (def == 0.0 || def == 1.0) && boolRe.match(name + QLatin1Char(' ') + label).hasMatch();
        QVariantMap m{{"name", name}, {"def", def}, {"min", min}, {"max", max}, {"label", label.isEmpty() ? name : label},
                      {"kind", toggle ? QStringLiteral("toggle") : QStringLiteral("slider")},
                      {"step", step > 0 ? step : (max - min) / 200.0}};
        defs.push_back({m, pos});
    };

    // // @uniform float NAME def min max "Label"
    static const RX uniformRe(QStringLiteral(R"rx(//\s*@uniform\s+float\s+(\w+)\s+([\d.eE+-]+)\s+([\d.eE+-]+)\s+([\d.eE+-]+)(?:\s+"([^"]*)")?)rx"));
    for (auto it = uniformRe.globalMatch(src); it.hasNext();) {
        const auto m = it.next();
        addSlider(m.captured(1), m.captured(2).toDouble(), m.captured(3).toDouble(), m.captured(4).toDouble(), m.captured(5), m.capturedStart(), 0);
    }

    // //!PARAM (mpv-Hook-Shader)
    for (const auto &p : extractParams(src, nullptr)) {
        bool ok = false;
        const double v = p.value.toDouble(&ok);
        if (!ok) continue;
        const double min = p.hasMin ? p.min : std::min(0.0, v * 2.0);
        const double max = p.hasMax ? p.max : std::max(2.0, std::abs(v) * 2.0);
        addSlider(p.name, v, min, max, p.label, p.pos, p.isInt ? 1.0 : 0.0);
    }

    // Numerische #define-Konstanten als Regler (Regeln wie im Userscript)
    static const RX defineRe(QStringLiteral(R"(^\s*#define\s+([A-Za-z_]\w*)\s+()") + kNum + QStringLiteral(R"()f?\s*(?://.*)?$)"), kMulti);
    static const RX strongRe(QStringLiteral("threshold|thresh|cutoff|limit|scale|strength|curve|amount|blend|mix|gain|sharp"), RX::CaseInsensitiveOption);
    for (auto it = defineRe.globalMatch(src); it.hasNext();) {
        const auto m = it.next();
        const QString raw = m.captured(1);
        if (excludedDefine(raw)) continue;
        const double v = m.captured(2).toDouble();
        double min = 0.0, max = std::max(2.0, std::abs(v) * 2.0);
        if (v < 0) { min = v * 2.0; max = std::abs(v) * 2.0; }
        if (v > 0 && v <= 1.0) { min = 0.0; max = 2.0; }
        if (strongRe.match(raw).hasMatch()) { min = 0.0; max = std::max(2.0, std::abs(v) * 2.0); }
        if (raw.compare(QLatin1String("pattern"), Qt::CaseInsensitive) == 0) { min = 1.0; max = 9.0; }
        QString label = raw;
        label.replace(QLatin1Char('_'), QLatin1Char(' '));
        addSlider(QStringLiteral("u_def_") + raw, v, min, max, label, m.capturedStart(), looksLikeInt(m.captured(2)) ? 1.0 : 0.0);
    }

    // // @select float NAME def "Label" 0:A,1:B
    static const RX selectRe(QStringLiteral(R"rx(//\s*@select\s+float\s+(\w+)\s+([\d.eE+-]+)\s+"([^"]*)"\s+([\d.,:\w \t()-]+))rx"));
    for (auto it = selectRe.globalMatch(src); it.hasNext();) {
        const auto m = it.next();
        QVariantList options;
        for (const auto &o : m.captured(4).trimmed().split(QLatin1Char(','))) {
            const qsizetype c = o.indexOf(QLatin1Char(':'));
            bool ok = false;
            const double value = o.left(c).trimmed().toDouble(&ok);
            if (ok) options << QVariantMap{{"value", value}, {"label", c >= 0 ? o.mid(c + 1).trimmed() : o.trimmed()}};
        }
        if (options.isEmpty() || seen.contains(m.captured(1))) continue;
        seen.insert(m.captured(1));
        defs.push_back({QVariantMap{{"name", m.captured(1)}, {"def", m.captured(2).toDouble()}, {"label", m.captured(3)}, {"kind", QStringLiteral("select")}, {"options", options}}, m.capturedStart()});
    }

    std::stable_sort(defs.begin(), defs.end(), [](const Def &a, const Def &b) { return a.pos < b.pos; });

    // "Gruppe / Name" wird im Editor als Überschrift mit Reglern darunter gezeigt
    QVariantList out;
    QString group;
    for (auto &d : defs) {
        const QStringList parts = d.map.value("label").toString().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString header;
        if (parts.size() >= 2) {
            const QString g = parts.first().trimmed();
            if (g != group) header = g;
            group = g;
            QStringList rest;
            for (qsizetype i = 1; i < parts.size(); ++i) rest << parts[i].trimmed();
            d.map.insert("label", rest.join(QStringLiteral(" / ")));
        } else {
            group.clear();
        }
        d.map.insert("groupHeader", header);
        out << d.map;
    }
    return out;
}

QString staticCheck(const QString &codeIn)
{
    QString s = normalized(codeIn);
    if (s.trimmed().isEmpty()) return QStringLiteral("empty");
    static const RX comments(QStringLiteral(R"(//[^\n]*|/\*.*?\*/)"), RX::DotMatchesEverythingOption);
    s.remove(comments);
    if (s.count(QLatin1Char('{')) != s.count(QLatin1Char('}'))) return QStringLiteral("braces");
    if (s.count(QLatin1Char('(')) != s.count(QLatin1Char(')'))) return QStringLiteral("parens");
    return {};
}

QString convertToMpv(const ConvertInput &inRaw)
{
    ConvertInput in = inRaw;
    in.label = in.label.simplified();
    if (in.label.isEmpty()) in.label = QStringLiteral("Filter");
    const QString src = normalized(in.code);
    return asciiOnly(isMpvHook(src) ? convertHook(src, in) : convertFragment(src, in));
}

} // namespace glslmgr
