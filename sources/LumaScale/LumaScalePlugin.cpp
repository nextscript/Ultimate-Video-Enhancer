#include "LumaScalePlugin.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>

namespace lumascale {

namespace {

const QString kId = QStringLiteral("org.lumascale.fsr");
// A sub-stage of its own: the host re-enables it when a new file arrives after a shader error.
const QString kStage = kId + QStringLiteral("#fsr");

struct Parameter {
    const char *key;     // placeholder in the template
    const char *label;
    const char *note;
    double from, to, step, def;
    bool integer;        // preprocessor switch: written as 0 / 1
};

// Defaults, order and labels follow defaultSettings.PNG.
const Parameter kParameters[] = {
    {"FSR_STRENGTH", "FSR Strength", "Mischung des EASU-Ergebnisses: 0 = bilinear, 1 = Original-EASU, 2 = verstärkt.", 0, 2, 0.01, 1.0, false},
    {"FSR_RADIUS", "FSR Radius", "Größe des EASU-Filterkerns. 1 = Original.", 0, 3, 0.01, 1.0, false},
    {"SHARPNESS", "RCAS Sharpness", "Wie im Script in Blendenstufen: 0 = maximale Schärfe, 2 = schwächste Schärfe.", 0, 2, 0.01, 0.70, false},
    {"EASU_EDGE", "EASU Edge", "Verstärkung der Kantenerkennung. 0,35 = Original.", 0, 1.5, 0.01, 0.35, false},
    {"FSR_RCAS_DENOISE", "RCAS Denoise", "Schont verrauschte Bereiche beim Schärfen: 0 = aus, 1 = Original-Stärke.", 0, 1, 0.01, 0.60, false},
    {"EFFECT_MIX", "Effect Mix", "Mischung mit dem unbearbeiteten Bild: 0 = aus, 1 = voller Effekt.", 0, 1, 0.01, 1.0, false},
    {"OFFSET_SCALE", "Offset Scale", "Abstand der RCAS-Nachbarpixel. 1 = Original.", 0, 2.5, 0.01, 1.0, false},
    {"FSR_PQ", "FSR PQ", "1, wenn die Quelle PQ-Gamma (HDR) nutzt.", 0, 1, 1, 0.0, true},
    {"FSR_EASU_DERING", "FSR EASU DERING", "Unterdrückt Überschwinger an Kanten. 0 spart etwas Leistung.", 0, 1, 1, 1.0, true},
    {"FSR_EASU_SIMPLE_ANALYSIS", "FSR EASU SIMPLE ANALYSIS", "1 = einfachere Richtungsanalyse, schneller.", 0, 1, 1, 0.0, true},
    {"FSR_EASU_QUIT_EARLY", "FSR EASU QUIT EARLY", "1 = bilinear abseits von Kanten, schneller.", 0, 1, 1, 0.0, true},
    {"FSR_EASU_DIR_THRESHOLD", "FSR EASU DIR THRESHOLD", "Ab wann ein Pixel als richtungslos gilt. Höher = mehr Pixel mit Kantenfilter.", 1, 128, 1, 64.0, false},
};

const Parameter *findParameter(const QString &key)
{
    for (const auto &p : kParameters)
        if (key == QLatin1String(p.key)) return &p;
    return nullptr;
}

double sanitized(const Parameter &p, double value)
{
    if (!std::isfinite(value)) return p.def;
    value = qBound(p.from, value, p.to);
    return p.integer ? std::round(value) : value;
}

QString literal(double value) { return QString::number(value, 'f', 6); }

} // namespace

LumaScaleController::LumaScaleController(uve::IPlayerHost *host, QObject *parent) : QObject(parent), m_host(host)
{
    QFile file(QStringLiteral(":/lumascale/lumascale.glsl.in"));
    if (file.open(QIODevice::ReadOnly)) m_template = QString::fromUtf8(file.readAll());
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(120);   // slider drags would otherwise recompile for every pixel of movement
    connect(&m_debounce, &QTimer::timeout, this, &LumaScaleController::flush);
    load();
}

QVariantList LumaScaleController::parameters() const
{
    QVariantList out;
    for (const auto &p : kParameters) {
        out << QVariantMap{{"key", QString::fromLatin1(p.key)}, {"label", QString::fromLatin1(p.label)},
                           {"note", QString::fromUtf8(p.note)}, {"from", p.from}, {"to", p.to},
                           {"step", p.step}, {"def", p.def}};
    }
    return out;
}

void LumaScaleController::load()
{
    const QJsonObject saved = m_host->loadPluginSettings(kId);
    m_enabled = saved.value(QStringLiteral("enabled")).toBool(true);
    const QJsonObject values = saved.value(QStringLiteral("values")).toObject();
    m_values.clear();
    for (const auto &p : kParameters)
        m_values.insert(QString::fromLatin1(p.key), sanitized(p, values.value(QLatin1String(p.key)).toDouble(p.def)));
}

void LumaScaleController::setEnabled(bool on)
{
    if (on == m_enabled) return;
    m_enabled = on;
    emit enabledChanged();
    flush();   // the toggle should act at once
}

void LumaScaleController::setParameter(const QString &key, double value)
{
    const Parameter *p = findParameter(key);
    if (!p) return;
    const double v = sanitized(*p, value);
    if (m_values.value(key).toDouble() == v) return;
    m_values.insert(key, v);
    emit valuesChanged();
    scheduleFlush();
}

void LumaScaleController::resetDefaults()
{
    for (const auto &p : kParameters) m_values.insert(QString::fromLatin1(p.key), p.def);
    emit valuesChanged();
    flush();
}

void LumaScaleController::setActive(bool active)
{
    if (active == m_active) return;
    if (!active && m_debounce.isActive()) flush();   // keep a change made just before closing
    m_active = active;
    applyShader();
}

void LumaScaleController::scheduleFlush() { m_debounce.start(); }

void LumaScaleController::flush()
{
    m_debounce.stop();
    QJsonObject values;
    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it) values.insert(it.key(), it.value().toDouble());
    m_host->savePluginSettings(kId, QJsonObject{{"enabled", m_enabled}, {"values", values}});
    applyShader();
}

QString LumaScaleController::buildShader() const
{
    QString out = m_template;
    for (const auto &p : kParameters) {
        const double v = m_values.value(QString::fromLatin1(p.key)).toDouble();
        out.replace(QStringLiteral("@%1@").arg(QLatin1String(p.key)), p.integer ? QString::number(int(v)) : literal(v));
    }
    // Switches for the preprocessor, which cannot compare float literals.
    out.replace(QStringLiteral("@FSR_RCAS_DENOISE_ON@"), m_values.value(QStringLiteral("FSR_RCAS_DENOISE")).toDouble() > 0.0 ? QStringLiteral("1") : QStringLiteral("0"));
    out.replace(QStringLiteral("@OFFSET_IS_ONE@"), m_values.value(QStringLiteral("OFFSET_SCALE")).toDouble() == 1.0 ? QStringLiteral("1") : QStringLiteral("0"));
    out.replace(QStringLiteral("@OWNER@"), kStage);
    return out;
}

void LumaScaleController::applyShader()
{
    QString next;
    if (m_active && m_enabled) {
        if (m_template.isEmpty()) {
            setError(QStringLiteral("Die Shader-Vorlage fehlt im Plugin."));
        } else {
            // mpv caches shaders by path: changed content has to get a new name.
            const QByteArray shader = buildShader().toUtf8();
            const QString hash = QString::fromLatin1(QCryptographicHash::hash(shader, QCryptographicHash::Sha1).toHex().left(16));
            const QDir dir(m_host->pluginDataDir(kId));
            next = dir.filePath(QStringLiteral("lumascale_%1.glsl").arg(hash));
            QSaveFile out(next);
            if (!out.open(QIODevice::WriteOnly) || out.write(shader) != shader.size() || !out.commit()) {
                setError(QStringLiteral("Shader-Datei konnte nicht geschrieben werden: %1").arg(next));
                next.clear();
            } else {
                setError({});
            }
        }
    } else {
        setError({});
    }

    m_currentFile = next;
    m_host->setShaderStage(kStage, next);

    // Remove older versions only now, once the player has been told to use the new file.
    const QDir dir(m_host->pluginDataDir(kId));
    const auto stale = dir.entryList({QStringLiteral("lumascale_*.glsl")}, QDir::Files);
    for (const auto &name : stale) {
        const QString path = dir.filePath(name);
        if (path != next) QFile::remove(path);
    }
}

void LumaScaleController::setError(const QString &message)
{
    if (message == m_error) return;
    m_error = message;
    emit errorChanged();
}

void LumaScaleController::onShaderError(const QString &message)
{
    setError(message);
    m_host->log(kId, QStringLiteral("error"), message);
}

bool LumaScalePlugin::initialize(uve::IPlayerHost *host, QString *error)
{
    if (!host || host->apiVersion() != uve::kPluginApiVersion) {
        if (error) *error = QStringLiteral("Host-API nicht kompatibel");
        return false;
    }
    m_controller = new LumaScaleController(host, this);
    return true;
}

void LumaScalePlugin::activate() { if (m_controller) m_controller->setActive(true); }
void LumaScalePlugin::suspend() { if (m_controller) m_controller->setActive(false); }
void LumaScalePlugin::deactivate() { if (m_controller) m_controller->setActive(false); }
void LumaScalePlugin::onShaderError(const QString &message) { if (m_controller) m_controller->onShaderError(message); }

} // namespace lumascale
