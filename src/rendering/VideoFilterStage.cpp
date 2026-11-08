#include "VideoFilterStage.h"
#include "../core/Logger.h"
#include "../player/MpvController.h"

#include <QRegularExpression>

namespace uve {

namespace {
const QString kLabel = QStringLiteral("uvefilter");
const QString kCopyHwdec = QStringLiteral("auto-copy-safe");

bool isCopyOrSoftware(const QString &hwdec)
{
    return hwdec.isEmpty() || hwdec == QLatin1String("no") || hwdec.contains(QLatin1String("-copy"));
}
} // namespace

VideoFilterStage::VideoFilterStage(MpvController *mpv, QObject *parent) : QObject(parent), m_mpv(mpv)
{
    connect(m_mpv, &MpvController::logMessage, this, &VideoFilterStage::onLog);
}

bool VideoFilterStage::isValidGraph(const QString &graph)
{
    // Nur zeitliche Entrauscher mit Zahlenoptionen: keine Quellen (movie=…) und keine Dateizugriffe
    static const QRegularExpression re(QStringLiteral("^(atadenoise|hqdn3d)(=[A-Za-z0-9_.:=-]*)?$"));
    return re.match(graph).hasMatch();
}

bool VideoFilterStage::set(const QString &owner, const QString &graph, QString *error)
{
    if (!m_owner.isEmpty() && m_owner != owner) {
        if (error) *error = QStringLiteral("video filter stage is in use by ") + m_owner;
        return false;
    }
    if (graph.isEmpty()) { clear(owner); return true; }
    if (!isValidGraph(graph)) {
        if (error) *error = QStringLiteral("invalid filter graph");
        return false;
    }
    m_error.clear();
    m_lastFilterLog.clear();
    m_sinceSet.start();
    if (m_owner.isEmpty()) {
        const QString hwdec = hwdecSetting();
        if (!isCopyOrSoftware(hwdec)) {
            m_savedHwdec = hwdec;
            m_mpv->setPropertyAsync(QStringLiteral("hwdec"), kCopyHwdec);
        }
    }
    m_owner = owner;
    m_graph = graph;
    // "vf add" mit vorhandenem Label ersetzt den Eintrag, die übrige Liste bleibt
    m_mpv->command({QStringLiteral("vf"), QStringLiteral("add"), QStringLiteral("@%1:lavfi=[%2]").arg(kLabel, graph)});
    return true;
}

void VideoFilterStage::clear(const QString &owner)
{
    if (m_owner.isEmpty() || m_owner != owner) return;
    m_error.clear();
    remove();
}

void VideoFilterStage::remove()
{
    m_mpv->command({QStringLiteral("vf"), QStringLiteral("remove"), QStringLiteral("@") + kLabel});
    // Die vom Benutzer inzwischen gewählte Einstellung hat Vorrang
    if (!m_savedHwdec.isEmpty() && hwdecSetting() == kCopyHwdec)
        m_mpv->setPropertyAsync(QStringLiteral("hwdec"), m_savedHwdec);
    m_savedHwdec.clear();
    m_owner.clear();
    m_graph.clear();
}

QString VideoFilterStage::hwdecSetting() const
{
    // Neuere mpv-Versionen führen hwdec als Liste, ältere als Text
    const QVariant v = m_mpv->getProperty(QStringLiteral("hwdec"));
    return v.typeId() == QMetaType::QString ? v.toString() : v.toStringList().join(QLatin1Char(','));
}

QVariantMap VideoFilterStage::status()
{
    // Ungültige Graphen lehnt mpv schon beim Einfügen ab, dann gibt es keine "Disabling filter"-Meldung.
    // Kurz warten: "vf add" läuft asynchron.
    if (!m_owner.isEmpty() && m_sinceSet.elapsed() > 500) {
        bool found = false;
        for (const auto &f : m_mpv->getProperty(QStringLiteral("vf")).toList())
            if (f.toMap().value(QStringLiteral("label")).toString() == kLabel) found = true;
        if (!found) {
            const QString reason = m_lastFilterLog.isEmpty() ? QStringLiteral("filter was not accepted") : m_lastFilterLog;
            Logger::write(QStringLiteral("video-filter"), m_graph + QStringLiteral(": ") + reason);
            remove();
            m_error = reason;
        }
    }
    return {{"active", !m_owner.isEmpty()}, {"graph", m_graph}, {"error", m_error},
            {"hwdec", m_mpv->getProperty(QStringLiteral("hwdec-current"))},
            {"dropped", m_mpv->getProperty(QStringLiteral("frame-drop-count")).toLongLong() +
                            m_mpv->getProperty(QStringLiteral("decoder-frame-drop-count")).toLongLong()},
            {"fps", m_mpv->getProperty(QStringLiteral("estimated-vf-fps"))}};
}

void VideoFilterStage::onLog(const QString &prefix, const QString &level, const QString &text)
{
    if (m_owner.isEmpty()) return;
    if ((level == QLatin1String("error") || level == QLatin1String("fatal")) &&
        (prefix == QLatin1String("lavfi") || prefix == QLatin1String("ffmpeg")))
        m_lastFilterLog = text.left(200);
    if (prefix != QLatin1String("vf")) return;
    if (!text.contains(QLatin1String("Disabling filter")) || !text.contains(kLabel)) return;
    // mpv hat den Filter bereits verworfen, die Wiedergabe läuft weiter: nur noch aufräumen
    Logger::write(QStringLiteral("video-filter"), m_graph + QStringLiteral(": ") + text);
    remove();
    m_error = text;
}

} // namespace uve
