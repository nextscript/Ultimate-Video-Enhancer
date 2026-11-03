#include "ExpertSettings.h"

#include <QDateTime>
#include <QJsonValue>
#include <QTimeZone>
#include <cmath>
#include <cstring>
#include <variant>

namespace uve::fx {

namespace {

ExpertSettings g_expert;

struct Member
{
    const char *block, *key;
    std::variant<bool *(*)(ExpertSettings &), double *(*)(ExpertSettings &)> get;
};

#define EB(blk, grp, k) Member{blk, #k, +[](ExpertSettings &e) -> bool * { return &e.grp.k; }}
#define ED(blk, grp, k) Member{blk, #k, +[](ExpertSettings &e) -> double * { return &e.grp.k; }}

const QVector<Member> &members()
{
    static const QVector<Member> m{
        EB("GVF_CONTRAST_SETTINGS", contrast, higherIsMore), ED("GVF_CONTRAST_SETTINGS", contrast, perStep),
        ED("GVF_CONTRAST_SETTINGS", contrast, min), ED("GVF_CONTRAST_SETTINGS", contrast, max), ED("GVF_CONTRAST_SETTINGS", contrast, pivot),
        EB("GVF_BLACK_SETTINGS", black, higherIsDarker), ED("GVF_BLACK_SETTINGS", black, perStep),
        ED("GVF_BLACK_SETTINGS", black, maxOffset), ED("GVF_BLACK_SETTINGS", black, softness),
        EB("GVF_WHITE_SETTINGS", white, higherIsBrighter), ED("GVF_WHITE_SETTINGS", white, perStep),
        ED("GVF_WHITE_SETTINGS", white, maxOffset), ED("GVF_WHITE_SETTINGS", white, softness),
        EB("GVF_HIGHLIGHTS_SETTINGS", highlights, higherIsBrighter), ED("GVF_HIGHLIGHTS_SETTINGS", highlights, perStep),
        ED("GVF_HIGHLIGHTS_SETTINGS", highlights, power), ED("GVF_HIGHLIGHTS_SETTINGS", highlights, threshold),
        ED("GVF_HIGHLIGHTS_SETTINGS", highlights, maxDarken),
        EB("GVF_SHADOWS_SETTINGS", shadows, higherIsDarker), ED("GVF_SHADOWS_SETTINGS", shadows, perStep),
        ED("GVF_SHADOWS_SETTINGS", shadows, power), ED("GVF_SHADOWS_SETTINGS", shadows, threshold),
        ED("GVF_SHADOWS_SETTINGS", shadows, maxBrighten),
        EB("GVF_SATURATION_SETTINGS", saturation, higherIsMore), ED("GVF_SATURATION_SETTINGS", saturation, perStep),
        ED("GVF_SATURATION_SETTINGS", saturation, min), ED("GVF_SATURATION_SETTINGS", saturation, max),
        ED("GVF_SATURATION_SETTINGS", saturation, power), EB("GVF_SATURATION_SETTINGS", saturation, desaturateEvenly),
        EB("GVF_VIBRANCE_SETTINGS", vibrance, higherIsMore), ED("GVF_VIBRANCE_SETTINGS", vibrance, perStep),
        ED("GVF_VIBRANCE_SETTINGS", vibrance, min), ED("GVF_VIBRANCE_SETTINGS", vibrance, max),
        ED("GVF_VIBRANCE_SETTINGS", vibrance, power), ED("GVF_VIBRANCE_SETTINGS", vibrance, skinProtect),
        EB("GVF_GAMMA_SETTINGS", gamma, higherIsBrighter), ED("GVF_GAMMA_SETTINGS", gamma, perStep),
        ED("GVF_GAMMA_SETTINGS", gamma, min), ED("GVF_GAMMA_SETTINGS", gamma, max), ED("GVF_GAMMA_SETTINGS", gamma, maxShadowSlope),
        EB("GVF_SHARPEN_SETTINGS", sharpen, higherIsSharper), ED("GVF_SHARPEN_SETTINGS", sharpen, maxAmount),
        ED("GVF_SHARPEN_SETTINGS", sharpen, lightHalo), ED("GVF_SHARPEN_SETTINGS", sharpen, darkHalo),
        ED("GVF_SHARPEN_SETTINGS", sharpen, skinProtect), ED("GVF_SHARPEN_SETTINGS", sharpen, noiseThreshold),
        ED("GVF_SHARPEN_SETTINGS", sharpen, maxChange), ED("GVF_SHARPEN_SETTINGS", sharpen, svgRadius),
        ED("GVF_SHARPEN_SETTINGS", sharpen, svgMaxSoften), ED("GVF_SHARPEN_SETTINGS", sharpen, webglPerStep),
        EB("GVF_GRAIN_SETTINGS", grain, higherIsMore), ED("GVF_GRAIN_SETTINGS", grain, perStep),
        ED("GVF_GRAIN_SETTINGS", grain, power), ED("GVF_GRAIN_SETTINGS", grain, peak), ED("GVF_GRAIN_SETTINGS", grain, colorAmount),
        ED("GVF_GRAIN_SETTINGS", grain, svgStrength), ED("GVF_GRAIN_SETTINGS", grain, svgFrequency),
        EB("GVF_HUE_SETTINGS", hue, reverse), ED("GVF_HUE_SETTINGS", hue, perStep),
        ED("GVF_HUE_SETTINGS", hue, maxDegrees), ED("GVF_HUE_SETTINGS", hue, skinProtect),
    };
    return m;
}

const Member *findMember(const char *block, const char *key)
{
    for (const auto &m : members())
        if (!std::strcmp(m.block, block) && !std::strcmp(m.key, key)) return &m;
    return nullptr;
}

QJsonValue jsonOf(ExpertSettings &e, const Member &m)
{
    if (auto *b = std::get_if<0>(&m.get)) return *(*b)(e);
    return *std::get<1>(m.get)(e);
}

} // namespace

const ExpertSettings &expert() { return g_expert; }
void setExpert(const ExpertSettings &e) { g_expert = e; }

const QVector<ExpertField> &expertFields()
{
    // Bereiche und Schritte wie GVF_EXPERT_UI im Userscript
    static const QVector<ExpertField> f{
        {"GVF_CONTRAST_SETTINGS", "higherIsMore", true, 0, 0, 0, false},
        {"GVF_CONTRAST_SETTINGS", "perStep", false, 0.005, 0.09, 0.005, false},
        {"GVF_CONTRAST_SETTINGS", "min", false, 0.1, 1, 0.01, false},
        {"GVF_CONTRAST_SETTINGS", "max", false, 1, 1.95, 0.01, false},
        {"GVF_CONTRAST_SETTINGS", "pivot", false, 0.2, 0.8, 0.01, false},
        {"GVF_BLACK_SETTINGS", "higherIsDarker", true, 0, 0, 0, false},
        {"GVF_BLACK_SETTINGS", "perStep", false, 0.002, 0.03, 0.001, false},
        {"GVF_BLACK_SETTINGS", "maxOffset", false, 0.02, 0.3, 0.01, false},
        {"GVF_BLACK_SETTINGS", "softness", false, 0, 0.2, 0.01, false},
        {"GVF_WHITE_SETTINGS", "higherIsBrighter", true, 0, 0, 0, false},
        {"GVF_WHITE_SETTINGS", "perStep", false, 0.002, 0.03, 0.001, false},
        {"GVF_WHITE_SETTINGS", "maxOffset", false, 0.02, 0.3, 0.01, false},
        {"GVF_WHITE_SETTINGS", "softness", false, 0, 0.2, 0.01, false},
        {"GVF_HIGHLIGHTS_SETTINGS", "higherIsBrighter", true, 0, 0, 0, false},
        {"GVF_HIGHLIGHTS_SETTINGS", "perStep", false, 0.002, 0.03, 0.001, false},
        {"GVF_HIGHLIGHTS_SETTINGS", "power", false, 1, 8, 0.5, false},
        {"GVF_HIGHLIGHTS_SETTINGS", "threshold", false, 0, 0.9, 0.01, false},
        {"GVF_HIGHLIGHTS_SETTINGS", "maxDarken", false, 0, 0.6, 0.01, false},
        {"GVF_SHADOWS_SETTINGS", "higherIsDarker", true, 0, 0, 0, false},
        {"GVF_SHADOWS_SETTINGS", "perStep", false, 0.002, 0.03, 0.001, false},
        {"GVF_SHADOWS_SETTINGS", "power", false, 1, 8, 0.5, false},
        {"GVF_SHADOWS_SETTINGS", "threshold", false, 0.1, 1, 0.01, false},
        {"GVF_SHADOWS_SETTINGS", "maxBrighten", false, 0, 0.6, 0.01, false},
        {"GVF_SATURATION_SETTINGS", "higherIsMore", true, 0, 0, 0, false},
        {"GVF_SATURATION_SETTINGS", "perStep", false, 0.01, 0.1, 0.005, false},
        {"GVF_SATURATION_SETTINGS", "min", false, 0, 1, 0.01, false},
        {"GVF_SATURATION_SETTINGS", "max", false, 1, 2, 0.01, false},
        {"GVF_SATURATION_SETTINGS", "power", false, 0, 4, 0.1, false},
        {"GVF_SATURATION_SETTINGS", "desaturateEvenly", true, 0, 0, 0, false},
        {"GVF_VIBRANCE_SETTINGS", "higherIsMore", true, 0, 0, 0, false},
        {"GVF_VIBRANCE_SETTINGS", "perStep", false, 0.01, 0.1, 0.005, false},
        {"GVF_VIBRANCE_SETTINGS", "min", false, 0, 1, 0.01, false},
        {"GVF_VIBRANCE_SETTINGS", "max", false, 1, 2, 0.01, false},
        {"GVF_VIBRANCE_SETTINGS", "power", false, 0.5, 6, 0.1, false},
        {"GVF_VIBRANCE_SETTINGS", "skinProtect", false, 0, 1, 0.05, false},
        {"GVF_GAMMA_SETTINGS", "higherIsBrighter", true, 0, 0, 0, false},
        {"GVF_GAMMA_SETTINGS", "perStep", false, 0.005, 0.08, 0.005, false},
        {"GVF_GAMMA_SETTINGS", "min", false, 0.3, 1, 0.01, false},
        {"GVF_GAMMA_SETTINGS", "max", false, 1, 3, 0.01, false},
        {"GVF_GAMMA_SETTINGS", "maxShadowSlope", false, 0, 6, 0.1, false},
        {"GVF_SHARPEN_SETTINGS", "higherIsSharper", true, 0, 0, 0, false},
        {"GVF_SHARPEN_SETTINGS", "maxAmount", false, 0.2, 4, 0.1, false},
        {"GVF_SHARPEN_SETTINGS", "lightHalo", false, 0, 1, 0.05, false},
        {"GVF_SHARPEN_SETTINGS", "darkHalo", false, 0, 1, 0.05, false},
        {"GVF_SHARPEN_SETTINGS", "skinProtect", false, 0, 1, 0.05, false},
        {"GVF_SHARPEN_SETTINGS", "noiseThreshold", false, 0, 0.05, 0.001, false},
        {"GVF_SHARPEN_SETTINGS", "maxChange", false, 0.02, 0.4, 0.01, false},
        {"GVF_SHARPEN_SETTINGS", "svgRadius", false, 0.3, 3, 0.1, true},
        {"GVF_SHARPEN_SETTINGS", "svgMaxSoften", false, 0.2, 4, 0.1, true},
        {"GVF_SHARPEN_SETTINGS", "webglPerStep", false, 0.02, 0.3, 0.01, false},
        {"GVF_GRAIN_SETTINGS", "higherIsMore", true, 0, 0, 0, false},
        {"GVF_GRAIN_SETTINGS", "perStep", false, 0.002, 0.04, 0.001, false},
        {"GVF_GRAIN_SETTINGS", "power", false, 0, 4, 0.1, false},
        {"GVF_GRAIN_SETTINGS", "peak", false, 0.2, 0.8, 0.01, false},
        {"GVF_GRAIN_SETTINGS", "colorAmount", false, 0, 1, 0.05, false},
        {"GVF_GRAIN_SETTINGS", "svgStrength", false, 0.2, 4, 0.1, true},
        {"GVF_GRAIN_SETTINGS", "svgFrequency", false, 0.2, 2, 0.05, true},
        {"GVF_HUE_SETTINGS", "reverse", true, 0, 0, 0, false},
        {"GVF_HUE_SETTINGS", "perStep", false, 0.5, 10, 0.5, false},
        {"GVF_HUE_SETTINGS", "maxDegrees", false, 5, 180, 1, false},
        {"GVF_HUE_SETTINGS", "skinProtect", false, 0, 1, 0.05, false},
    };
    return f;
}

QVariant expertValue(const ExpertSettings &e, const char *block, const char *key)
{
    const Member *m = findMember(block, key);
    if (!m) return {};
    auto &ee = const_cast<ExpertSettings &>(e);
    if (auto *b = std::get_if<0>(&m->get)) return QVariant(*(*b)(ee));
    return QVariant(*std::get<1>(m->get)(ee));
}

bool setExpertValue(ExpertSettings &e, const char *block, const char *key, const QVariant &v)
{
    const Member *m = findMember(block, key);
    if (!m) return false;
    if (auto *b = std::get_if<0>(&m->get)) {
        if (v.typeId() != QMetaType::Bool) return false;
        *(*b)(e) = v.toBool();
        return true;
    }
    bool ok = false;
    const double d = v.toDouble(&ok);
    if (!ok || !std::isfinite(d)) return false;
    *std::get<1>(m->get)(e) = d;
    return true;
}

QJsonObject expertDiff(const ExpertSettings &e)
{
    ExpertSettings def;
    auto &ee = const_cast<ExpertSettings &>(e);
    QJsonObject out;
    for (const auto &m : members()) {
        const QJsonValue cur = jsonOf(ee, m), d = jsonOf(def, m);
        if (cur == d) continue;
        QJsonObject blk = out.value(QLatin1String(m.block)).toObject();
        blk.insert(QLatin1String(m.key), cur);
        out.insert(QLatin1String(m.block), blk);
    }
    return out;
}

void expertMerge(ExpertSettings &e, const QJsonObject &blocks)
{
    for (const auto &m : members()) {
        const QJsonValue blk = blocks.value(QLatin1String(m.block));
        if (!blk.isObject()) continue;
        const QJsonValue v = blk.toObject().value(QLatin1String(m.key));
        if (v.isUndefined()) continue;
        if (auto *b = std::get_if<0>(&m.get)) {
            if (v.isBool()) *(*b)(e) = v.toBool();
        } else if (v.isDouble() && std::isfinite(v.toDouble())) {
            *std::get<1>(m.get)(e) = v.toDouble();
        } else if (v.isString()) {   // Number("0.5") im Userscript
            bool ok = false;
            const double d = v.toString().toDouble(&ok);
            if (ok && std::isfinite(d)) *std::get<1>(m.get)(e) = d;
        }
    }
}

QJsonObject expertExport(const ExpertSettings &e, const QString &version)
{
    return QJsonObject{{"type", "gvf-expert-settings"}, {"version", 1}, {"script", version},
                       {"exported", QDateTime::currentDateTime().toTimeZone(QTimeZone::UTC).toString(Qt::ISODateWithMs)},
                       {"settings", expertDiff(e)}};
}

int expertImport(ExpertSettings &e, const QJsonObject &obj)
{
    if (obj.isEmpty()) return -1;
    const QJsonValue type = obj.value("type");
    if (!type.isUndefined() && type.toString() != QLatin1String("gvf-expert-settings")) return -1;
    const bool isExport = type.toString() == QLatin1String("gvf-expert-settings");
    const QJsonObject data = obj.value("settings").isObject() ? obj.value("settings").toObject() : obj;
    int names = 0;
    QStringList known;
    for (const auto &m : members()) if (!known.contains(QLatin1String(m.block))) known << QLatin1String(m.block);
    for (const auto &n : known) if (data.contains(n)) ++names;
    if (!names && !isExport) return -1;
    e = ExpertSettings();   // wie resetExpertSettings() vor dem Import
    expertMerge(e, data);
    return names;
}

} // namespace uve::fx
