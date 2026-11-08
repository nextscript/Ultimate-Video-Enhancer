#include "DenoiseController.h"

#include "PluginApi.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace uve::fx {

namespace {
constexpr int kShaderCommitMs = 150;     // Regler: höchstens so oft eine neue Shader-Datei
constexpr int kFilterCommitMs = 300;     // Temporal: erst nach dem Loslassen, jede Änderung leert die Historie
constexpr double kOverloadShare = 0.15;  // Anteil verworfener Frames je Sekunde …
constexpr int kOverloadSeconds = 5;      // … über diese Dauer → Rückfall auf den räumlichen Filter
constexpr int kGraceSeconds = 3;         // nach dem Umschalten (Decoder startet neu) nicht werten
} // namespace

DenoiseController::DenoiseController(IPlayerHost *host, const QString &pluginId, QObject *parent)
    : QObject(parent), m_host(host), m_id(pluginId)
{
    m_available = qEnvironmentVariableIntValue("UVE_DISABLE_DENOISING") == 0;
    m_commitTimer.setSingleShot(true);
    connect(&m_commitTimer, &QTimer::timeout, this, &DenoiseController::stageRequested);
    m_pollTimer.setInterval(1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &DenoiseController::pollFilter);
}

QVariantList DenoiseController::modes() const
{
    return {QVariantMap{{"value", "off"}, {"text", tr("Aus")}},
            QVariantMap{{"value", "bilateral"}, {"text", tr("1. Bilateral Filter")}},
            QVariantMap{{"value", "adaptive"}, {"text", tr("2. Adaptive Bilateral + Debanding")}},
            QVariantMap{{"value", "temporal"}, {"text", tr("3. Temporal Denoising")}},
            QVariantMap{{"value", "nlmeans"}, {"text", tr("4. NLMeans (GLSL-Shader)")}}};
}

QString DenoiseController::status() const
{
    if (!m_problem.isEmpty()) return m_problem;
    if (m_params.mode == DenoiseMode::Off) return tr("Aus");
    if (!m_running) return tr("Wartet – Effekte sind aus");
    if (m_params.strength == 0 && !(debandingEnabled() && m_params.debanding > 0)) return tr("Keine Wirkung – Stärke ist 0");
    if (m_params.mode == DenoiseMode::Temporal) return m_filterInfo.isEmpty() ? tr("Initialisiert") : m_filterInfo;
    return tr("Aktiv");
}

QString DenoiseController::pipelineLabel() const
{
    if (!m_available || !m_running || m_stageFile.isEmpty() == m_graph.isEmpty()) return {};   // keine Stufe aktiv
    switch (m_params.mode) {
    case DenoiseMode::Bilateral: return QStringLiteral("Bilateral");
    case DenoiseMode::AdaptiveDeband: return QStringLiteral("Adaptive + Deband");
    case DenoiseMode::Temporal: return QStringLiteral("Temporal");
    case DenoiseMode::NLMeans: return QStringLiteral("NLMeans");
    case DenoiseMode::Off: break;
    }
    return {};
}

void DenoiseController::load(const QJsonObject &o)
{
    m_params = denoiseFromJson(o);
    // Schalter für die schnelle vollständige Abschaltung (settings.json oder UVE_DISABLE_DENOISING=1)
    m_disabledBySetting = o.value(QStringLiteral("featureEnabled")).toBool(true) == false;
    if (m_disabledBySetting) m_available = false;
}

QJsonObject DenoiseController::toJson() const
{
    QJsonObject o = denoiseToJson(m_params);
    if (m_disabledBySetting) o.insert(QStringLiteral("featureEnabled"), false);
    return o;
}

void DenoiseController::registerStage()
{
    if (!m_available) return;
    // Alte Dateien einer früheren Sitzung gehören mpv nicht mehr
    removeOldStageFiles();
    m_host->setShaderStage(QString::fromLatin1(kDenoiseStageOwner), m_stageFile);
}

void DenoiseController::setRunning(bool running)
{
    if (m_running == running) return;
    m_running = running;
    emit statusChanged();
}

void DenoiseController::retranslate()
{
    emit languageChanged();
    emit statusChanged();
}

bool DenoiseController::writeStage(const QString &text)
{
    const QString owner = QString::fromLatin1(kDenoiseStageOwner);
    if (text.isEmpty()) {
        m_stageText.clear();
        m_stageFile.clear();
        m_host->setShaderStage(owner, QString());
        removeOldStageFiles();
        return true;
    }
    // mpv speichert Shader nach Pfad zwischen: geänderter Inhalt braucht einen neuen Namen
    if (m_session.isEmpty()) m_session = QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
    const QString file = QDir(m_host->pluginDataDir(m_id)).filePath(QStringLiteral("denoise-%1-%2.glsl").arg(m_session).arg(++m_seq));
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(text.toUtf8()) < 0 || !f.commit()) return false;
    m_stageText = text;
    m_stageFile = file;
    m_host->setShaderStage(owner, file);
    removeOldStageFiles();
    return true;
}

void DenoiseController::removeOldStageFiles()
{
    const QDir dir(m_host->pluginDataDir(m_id));
    for (const auto &fi : dir.entryInfoList({QStringLiteral("denoise-*.glsl")}, QDir::Files))
        if (QDir::cleanPath(fi.absoluteFilePath()) != QDir::cleanPath(m_stageFile)) QFile::remove(fi.absoluteFilePath());
}

void DenoiseController::apply(bool compare)
{
    if (!m_available) return;
    m_commitTimer.stop();
    const bool runIt = m_running;
    const QString text = runIt ? buildDenoiseShader(m_params, compare) : QString();
    if (text != m_stageText) {
        if (!writeStage(text)) {
            fallBack(DenoiseMode::Off, tr("Fallback auf Aus: Shaderdatei nicht schreibbar"));
            return;
        }
    }
    m_stageCompare = compare && !text.isEmpty();

    const QString graph = runIt ? buildTemporalGraph(m_params) : QString();
    if (graph != m_graph) {
        QString error;
        const bool known = m_host->hasCommand(QStringLiteral("video.filter.set"));
        const bool ok = known && m_host->call(m_id, QStringLiteral("video.filter.set"), {{"graph", graph}}, &error).toBool();
        if (!ok && !graph.isEmpty()) {
            m_graph.clear();
            fallBack(DenoiseMode::Bilateral, tr("Temporal nicht verfügbar, Fallback auf Bilateral: %1").arg(known ? error : tr("Player zu alt")));
            return;
        }
        m_graph = graph;
        m_filterInfo.clear();
        m_overloadSeconds = 0;
        m_graceSeconds = kGraceSeconds;
        m_lastDropped = -1;
    }
    if (m_graph.isEmpty()) m_pollTimer.stop();
    else if (!m_pollTimer.isActive()) m_pollTimer.start();
    emit statusChanged();
}

void DenoiseController::pollFilter()
{
    const QVariantMap s = m_host->call(m_id, QStringLiteral("video.filter.status")).toMap();
    if (!s.value("active").toBool()) {
        // mpv hat den Filter verworfen (z. B. Bildformat oder direkte Hardware-Decodierung, die der Benutzer gerade gewählt hat)
        m_graph.clear();
        fallBack(DenoiseMode::Bilateral, tr("Temporal nicht verfügbar, Fallback auf Bilateral: %1").arg(s.value("error").toString()));
        return;
    }
    const qint64 dropped = s.value("dropped").toLongLong();
    const double fps = s.value("fps").toDouble();
    const QString hw = s.value("hwdec").toString();
    QString info = tr("Aktiv – CPU-Filter, Decoder: %1").arg(hw.isEmpty() || hw == QLatin1String("no") ? tr("Software") : hw);
    if (m_host->isPaused() || m_host->currentMediaPath().isEmpty()) {
        m_lastDropped = -1;   // keine Wertung ohne laufendes Bild
    } else {
        if (m_graceSeconds > 0) --m_graceSeconds;
        else if (m_lastDropped >= 0 && fps > 1) {
            const bool over = double(dropped - m_lastDropped) > fps * kOverloadShare;
            m_overloadSeconds = over ? m_overloadSeconds + 1 : 0;
            if (m_overloadSeconds >= kOverloadSeconds) {
                fallBack(DenoiseMode::Bilateral, tr("Temporal überlastet (Frames verworfen), Fallback auf Bilateral"));
                return;
            }
            if (over) info = tr("Temporal überlastet – Frames werden verworfen");
        }
        m_lastDropped = dropped;
    }
    if (info != m_filterInfo) { m_filterInfo = info; emit statusChanged(); }
}

void DenoiseController::onShaderError(const QString &message)
{
    m_host->log(m_id, QStringLiteral("error"), QStringLiteral("denoising: ") + message);
    if (m_params.mode == DenoiseMode::Off) return;
    fallBack(DenoiseMode::Off, tr("Fallback auf Aus: GPU-Shader nicht unterstützt (%1)").arg(message.left(160)));
}

void DenoiseController::fallBack(DenoiseMode mode, const QString &problem)
{
    m_host->log(m_id, QStringLiteral("warning"), QStringLiteral("denoising: ") + problem);
    m_params.mode = mode;
    m_problem = problem;
    emit changed();
    emit statusChanged();
    emit persistRequested();
    // Aus dem Fehlerpfad heraus nicht direkt umbauen: mpv meldet Fehler während der Verarbeitung seiner Ereignisse
    m_commitTimer.start(0);
}

void DenoiseController::setParams(const DenoiseParams &next, bool immediately)
{
    if (next == m_params) return;
    const bool modeChanged = next.mode != m_params.mode;
    m_params = next;
    if (modeChanged) m_problem.clear();
    emit changed();
    emit statusChanged();
    emit persistRequested();
    if (immediately) { m_commitTimer.stop(); emit stageRequested(); return; }
    if (m_params.mode == DenoiseMode::Temporal) m_commitTimer.start(kFilterCommitMs);
    else if (!m_commitTimer.isActive()) m_commitTimer.start(kShaderCommitMs);
}

void DenoiseController::setMode(const QString &key)
{
    DenoiseParams n = m_params;
    n.mode = denoiseModeFromKey(key);
    if (n.mode == m_params.mode && !m_problem.isEmpty()) { m_problem.clear(); emit statusChanged(); }
    setParams(n, true);
}

void DenoiseController::setStrength(int v) { DenoiseParams n = m_params; n.strength = qBound(0, v, 100); setParams(n, false); }
void DenoiseController::setDetail(int v) { DenoiseParams n = m_params; n.detail = qBound(0, v, 100); setParams(n, false); }
void DenoiseController::setDebanding(int v) { DenoiseParams n = m_params; n.debanding = qBound(0, v, 100); setParams(n, false); }
void DenoiseController::applyPreset(const QString &resolution) { setParams(denoisePreset(m_params, resolution), true); }

void DenoiseController::reset()
{
    m_problem.clear();
    setParams(DenoiseParams(), true);
    emit statusChanged();
}

} // namespace uve::fx
