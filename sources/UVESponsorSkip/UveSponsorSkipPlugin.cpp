#include "UveSponsorSkipPlugin.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <functional>

namespace uve::sponsor {

static const QString kId = QStringLiteral("org.uveplayer.uvesponsorskip");
static constexpr int kTickMs = 200;
static constexpr int kTimeoutMs = 15000;
static constexpr auto kUserAgent = "UVESponsorSkip/1.0.2 (UVE Player)";
static const QString kLayerName = QStringLiteral("uveSponsorSkipMarkers");

// Ebene über der Zeitleiste: ein farbiger Balken je Segment. Reines QML ohne Plugin-Code, darum auch nach dem
// Entladen der DLL harmlos; die Länge kommt direkt vom Player-Objekt der SeekBar.
static const QByteArray kLayerQml = R"(
import QtQuick
Item {
    id: markers
    property var player
    property var segments: []
    readonly property real dur: player && player.duration > 0 ? player.duration : 0
    anchors.fill: parent
    z: 1
    visible: dur > 0 && segments.length > 0
    Repeater {
        model: markers.segments
        Rectangle {
            required property var modelData
            readonly property real a: Math.max(0, modelData.start)
            readonly property real b: Math.min(markers.dur, modelData.end)
            x: markers.dur > 0 ? markers.width * a / markers.dur : 0
            width: markers.dur > 0 ? Math.max(2, markers.width * (b - a) / markers.dur) : 0
            height: markers.height
            radius: Math.min(2, height / 2)
            color: modelData.color
            opacity: 0.9
        }
    }
}
)";

// ---------------------------------------------------------------------------------------------------------------
// Controller: Einstellungen und Zustand für die Seite

UveSponsorSkipController::UveSponsorSkipController(uve::IPlayerHost *host, SkipEngine *engine, QObject *parent)
    : QObject(parent), m_host(host), m_engine(engine)
{
    const QString lang = host->call(kId, QStringLiteral("app.info")).toMap().value("language").toString();
    if (!lang.isEmpty()) m_language = lang;

    const QJsonObject s = host->loadPluginSettings(kId);
    m_enabled = s.value("enabled").toBool(true);
    m_countViews = s.value("countViews").toBool(true);
    const QString server = s.value("server").toString().trimmed();
    if (server.startsWith(QLatin1String("http"))) m_server = server;
    const QJsonObject modes = s.value("modes").toObject();
    for (auto it = modes.begin(); it != modes.end(); ++it)
        if (category(it.key())) m_engine->setMode(it.key(), Mode(qBound(0, it.value().toInt(), 2)));
}

void UveSponsorSkipController::save()
{
    QJsonObject modes;
    for (const Category &c : categories()) modes.insert(QLatin1String(c.id), int(m_engine->mode(QLatin1String(c.id))));
    QJsonObject s{{"enabled", m_enabled}, {"countViews", m_countViews}, {"modes", modes}};
    if (m_server != QLatin1String("https://sponsor.ajay.app")) s.insert(QStringLiteral("server"), m_server);
    m_host->savePluginSettings(kId, s);
}

void UveSponsorSkipController::setEnabled(bool enabled)
{
    if (enabled == m_enabled) return;
    m_enabled = enabled;
    save();
    emit changed();
    emit enabledChanged();
}

void UveSponsorSkipController::setCountViews(bool count)
{
    if (count == m_countViews) return;
    m_countViews = count;
    save();
    emit changed();
}

void UveSponsorSkipController::setMode(const QString &cat, int mode)
{
    if (!category(cat)) return;
    m_engine->setMode(cat, Mode(qBound(0, mode, 2)));
    save();
    emit changed();
}

QVariantList UveSponsorSkipController::categoryList() const
{
    QVariantList out;
    for (const Category &c : categories())
        out << QVariantMap{{"id", QLatin1String(c.id)}, {"label", categoryLabel(QLatin1String(c.id))},
                           {"mode", int(m_engine->mode(QLatin1String(c.id)))}, {"color", QLatin1String(c.color)}};
    return out;
}

void UveSponsorSkipController::setStatus(const QString &status)
{
    m_status = status;
    emit changed();
}

void UveSponsorSkipController::setCanUndo(bool can)
{
    if (can == m_canUndo) return;
    m_canUndo = can;
    emit changed();
}

void UveSponsorSkipController::setLanguage(const QString &language)
{
    if (language.isEmpty() || language == m_language) return;
    m_language = language;
    emit changed();
}

QString UveSponsorSkipController::text(const char *de, const char *en) const
{
    return QString::fromUtf8(m_language.startsWith(QLatin1String("de")) ? de : en);
}

QString UveSponsorSkipController::categoryLabel(const QString &id) const
{
    const Category *c = category(id);
    return c ? text(c->de, c->en) : id;
}

// ---------------------------------------------------------------------------------------------------------------
// Plugin

bool UveSponsorSkipPlugin::initialize(uve::IPlayerHost *host, QString *error)
{
    if (!host) { if (error) *error = QStringLiteral("no host"); return false; }
    m_host = host;
    m_controller = new UveSponsorSkipController(host, &m_engine, this);
    m_nam = new QNetworkAccessManager(this);
    m_timer = new QTimer(this);
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &UveSponsorSkipPlugin::tick);
    connect(m_controller, &UveSponsorSkipController::skipRequested, this, &UveSponsorSkipPlugin::skipNow);
    connect(m_controller, &UveSponsorSkipController::undoRequested, this, &UveSponsorSkipPlugin::undo);
    connect(m_controller, &UveSponsorSkipController::seekRequested, this, &UveSponsorSkipPlugin::seek);
    // Segmente, Kategorie-Einstellungen oder an/aus geändert → Markierungen nachziehen
    connect(m_controller, &UveSponsorSkipController::changed, this, &UveSponsorSkipPlugin::updateMarkers);
    connect(m_controller, &UveSponsorSkipController::enabledChanged, this, [this] {
        if (!m_controller->enabled()) setMuted(false);
        updateTimer();
        if (m_controller->enabled()) {
            // Eingeschaltet, während schon ein Video läuft: Segmente jetzt holen
            m_videoId.clear();
            mediaLoaded(m_host->call(kId, QStringLiteral("player.state")).toMap());
        }
    });
    m_controller->setStatus(m_controller->text("Kein YouTube-Video geöffnet.", "No YouTube video open."));
    return true;
}

void UveSponsorSkipPlugin::activate()
{
    m_active = true;
    updateTimer();
    updateMarkers();
}

void UveSponsorSkipPlugin::suspend()
{
    m_active = false;
    setMuted(false);
    updateTimer();
    updateMarkers();
}

void UveSponsorSkipPlugin::deactivate()
{
    m_active = false;
    reset();
    removeMarkers();
}

void UveSponsorSkipPlugin::onEvent(const QString &name, const QVariantMap &data)
{
    if (name == QLatin1String("media.loading") || name == QLatin1String("player.stopped")) {
        reset();
    } else if (name == QLatin1String("media.loaded")) {
        mediaLoaded(data);
    } else if (name == QLatin1String("playback.paused")) {
        updateTimer();
    } else if (name == QLatin1String("action.triggered")) {
        const QString id = data.value("id").toString();
        if (id == QLatin1String("skip")) skipNow();
        else if (id == QLatin1String("undo")) undo();
        else if (id == QLatin1String("toggle")) {
            m_controller->setEnabled(!m_controller->enabled());
            m_host->showOsd(m_controller->enabled() ? m_controller->text("SponsorSkip an", "SponsorSkip on")
                                                    : m_controller->text("SponsorSkip aus", "SponsorSkip off"), 1500);
        }
    } else if (name == QLatin1String("language.changed")) {
        m_controller->setLanguage(data.value("code").toString());
        publishSegments();
    }
}

void UveSponsorSkipPlugin::mediaLoaded(const QVariantMap &data)
{
    // "source" ist die gewählte Adresse; bei Plugins wie UVESpotify steht die YouTube-Adresse erst in "path"
    QString id = videoIdFromUrl(data.value("source").toString());
    if (id.isEmpty()) id = videoIdFromUrl(data.value("path").toString());
    if (id.isEmpty()) id = videoIdFromUrl(m_host->call(kId, QStringLiteral("mpv.get"), {{"name", QStringLiteral("path")}}).toString());
    if (id == m_videoId && !id.isEmpty()) return;   // gleiche Adresse neu geladen (z. B. andere Auflösung)
    reset();
    if (id.isEmpty()) return;
    m_videoId = id;
    if (m_controller->enabled()) fetch(id);
}

void UveSponsorSkipPlugin::reset()
{
    if (m_reply) m_reply->abort();
    setMuted(false);
    m_videoId.clear();
    m_engine.clear();
    m_last = {};
    m_guardUntil = 0;
    m_controller->setCanUndo(false);
    m_controller->setSegments({});
    m_controller->setStatus(m_controller->text("Kein YouTube-Video geöffnet.", "No YouTube video open."));
    updateTimer();
}

void UveSponsorSkipPlugin::fetch(const QString &videoId)
{
    if (m_reply) m_reply->abort();
    m_controller->setStatus(m_controller->text("Frage SponsorBlock …", "Asking SponsorBlock …"));
    QNetworkRequest req(QUrl(skipSegmentsUrl(m_controller->server(), hashPrefix(videoId))));
    req.setTransferTimeout(kTimeoutMs);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = m_nam->get(req);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, videoId] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError || videoId != m_videoId) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {   // kein Video mit diesem Präfix hat Segmente
            m_engine.setSegments({});
        } else if (status == 200) {
            m_engine.setSegments(segmentsFromJson(reply->readAll(), videoId));
        } else {
            const QString err = status ? QStringLiteral("HTTP %1").arg(status) : reply->errorString();
            m_host->log(kId, QStringLiteral("warning"), QStringLiteral("SponsorBlock %1: %2").arg(videoId, err));
            m_controller->setStatus(m_controller->text("SponsorBlock nicht erreichbar (%1).", "SponsorBlock unreachable (%1).").arg(err));
            return;
        }
        publishSegments();
        updateTimer();
    });
}

void UveSponsorSkipPlugin::publishSegments()
{
    if (m_videoId.isEmpty()) return;
    QVariantList list;
    for (const Segment &s : m_engine.segments()) {
        const Category *c = category(s.category);
        list << QVariantMap{{"label", m_controller->categoryLabel(s.category)},
                            {"start", s.start}, {"end", s.end},
                            {"time", QStringLiteral("%1 – %2").arg(formatTime(s.start), formatTime(s.end))},
                            {"mute", s.actionType == QLatin1String("mute")},
                            {"category", s.category},
                            {"color", c ? QLatin1String(c->color) : QLatin1String("#888")}};
    }
    m_controller->setSegments(list);
    m_controller->setStatus(list.isEmpty()
        ? m_controller->text("Keine Segmente für dieses Video (%1).", "No segments for this video (%1).").arg(m_videoId)
        : m_controller->text("%1 Segment(e) für %2:", "%1 segment(s) for %2:").arg(list.size()).arg(m_videoId));
}

void UveSponsorSkipPlugin::updateTimer()
{
    const bool run = m_active && m_controller->enabled() && !m_engine.segments().isEmpty() && !m_host->isPaused();
    if (run && !m_timer->isActive()) m_timer->start();
    else if (!run && m_timer->isActive()) m_timer->stop();
}

void UveSponsorSkipPlugin::tick()
{
    // Zeitleiste noch nicht gefunden (Oberfläche später aufgebaut): alle 2 s erneut versuchen
    m_markerLayers.removeIf([](const QPointer<QQuickItem> &p) { return p.isNull(); });
    if (m_markerLayers.isEmpty() && ++m_attachRetry % 10 == 0) updateMarkers();

    const double pos = m_host->currentPosition();
    if (m_guardUntil) {
        if (QDateTime::currentMSecsSinceEpoch() < m_guardUntil && qAbs(pos - m_guardTarget) > 1.0) return;
        m_guardUntil = 0;
    }
    const SkipEngine::Tick t = m_engine.tick(pos);
    if (t.jump.valid) {
        jump(t.jump, true);
        return;
    }
    setMuted(t.mute);
    if (!t.prompt.isEmpty())
        m_host->showOsd(m_controller->text("%1 – überspringen mit Strg+Alt+S", "%1 – skip with Ctrl+Alt+S")
                            .arg(m_controller->categoryLabel(t.prompt)), 4000);
}

void UveSponsorSkipPlugin::jump(const SkipEngine::Jump &j, bool automatic)
{
    const double from = m_host->currentPosition();
    m_last = {true, from, j.uuids, j.category};
    m_controller->setCanUndo(true);
    setMuted(false);
    const double dur = duration();
    // Segment bis zum Ende (Outro): ans Ende springen, dann geht es mit dem nächsten Titel weiter
    const double target = dur > 0 ? qMin(j.to, dur) : j.to;
    seek(target);
    const QString label = m_controller->categoryLabel(j.category);
    m_host->showOsd(automatic ? m_controller->text("⏭ %1 übersprungen – Strg+Alt+Z: rückgängig", "⏭ Skipped %1 – Ctrl+Alt+Z: undo").arg(label)
                              : m_controller->text("⏭ %1 übersprungen", "⏭ Skipped %1").arg(label),
                    2500);
    reportViews(j.uuids);
}

void UveSponsorSkipPlugin::skipNow()
{
    if (!m_controller->enabled() || m_engine.segments().isEmpty()) {
        m_host->showOsd(m_controller->text("Kein Segment zum Überspringen", "No segment to skip"), 1500);
        return;
    }
    const SkipEngine::Jump j = m_engine.manualJump(m_host->currentPosition());
    if (!j.valid) {
        m_host->showOsd(m_controller->text("Kein Segment an dieser Stelle", "No segment at this position"), 1500);
        return;
    }
    jump(j, false);
}

void UveSponsorSkipPlugin::undo()
{
    if (!m_last.valid) {
        m_host->showOsd(m_controller->text("Kein Sprung zum Rückgängigmachen", "No skip to undo"), 1500);
        return;
    }
    // Zurück an die Stelle vor dem Sprung; diese Segmente in diesem Video nicht mehr automatisch überspringen
    m_engine.suppress(m_last.uuids);
    seek(m_last.from);
    m_host->showOsd(m_controller->text("↩ Sprung rückgängig – %1 läuft", "↩ Skip undone – playing %1")
                        .arg(m_controller->categoryLabel(m_last.category)), 2000);
    m_last = {};
    m_controller->setCanUndo(false);
}

void UveSponsorSkipPlugin::seek(double seconds)
{
    m_guardTarget = seconds;
    m_guardUntil = QDateTime::currentMSecsSinceEpoch() + 1500;
    m_host->call(kId, QStringLiteral("mpv.command"),
                 {{"args", QStringList{QStringLiteral("seek"), QString::number(qMax(0.0, seconds), 'f', 3), QStringLiteral("absolute+exact")}}});
}

void UveSponsorSkipPlugin::setMuted(bool mute)
{
    if (mute == m_muting || !m_host) return;
    if (mute) {
        // Nur stummschalten (und später wieder einschalten), wenn der Ton gerade an ist
        if (m_host->call(kId, QStringLiteral("mpv.get"), {{"name", QStringLiteral("mute")}}).toBool()) return;
        m_host->call(kId, QStringLiteral("mpv.set"), {{"name", QStringLiteral("mute")}, {"value", true}});
        m_host->showOsd(m_controller->text("🔇 Stummgeschaltet (SponsorBlock)", "🔇 Muted (SponsorBlock)"), 1500);
    } else {
        m_host->call(kId, QStringLiteral("mpv.set"), {{"name", QStringLiteral("mute")}, {"value", false}});
    }
    m_muting = mute;
}

void UveSponsorSkipPlugin::reportViews(const QStringList &uuids)
{
    if (!m_controller->countViews()) return;
    // Zählt nur "übersprungen" für das Segment (wie die Browser-Erweiterung); keine Video-ID, kein Konto
    for (const QString &uuid : uuids) {
        QNetworkRequest req(QUrl(viewedUrl(m_controller->server(), uuid)));
        req.setTransferTimeout(kTimeoutMs);
        req.setRawHeader("User-Agent", kUserAgent);
        QNetworkReply *reply = m_nam->post(req, QByteArray());
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
}

double UveSponsorSkipPlugin::duration() const
{
    return m_host->call(kId, QStringLiteral("mpv.get"), {{"name", QStringLiteral("duration")}}).toDouble();
}

// ---------------------------------------------------------------------------------------------------------------
// Markierungen in der Zeitleiste

QVariantList UveSponsorSkipPlugin::markerData() const
{
    QVariantList out;
    if (!m_active || !m_controller->enabled()) return out;
    for (const Segment &s : m_engine.segments()) {
        if (m_engine.mode(s.category) == Mode::Off) continue;   // ignorierte Kategorien nicht markieren
        const Category *c = category(s.category);
        out << QVariantMap{{"start", s.start}, {"end", s.end}, {"color", c ? QLatin1String(c->color) : QLatin1String("#888")}};
    }
    return out;
}

void UveSponsorSkipPlugin::updateMarkers()
{
    if (!m_host) return;
    const QVariantList data = markerData();
    m_markerLayers.removeIf([](const QPointer<QQuickItem> &p) { return p.isNull(); });
    if (m_markerLayers.isEmpty() && !data.isEmpty()) attachMarkers();
    for (const QPointer<QQuickItem> &layer : std::as_const(m_markerLayers)) layer->setProperty("segments", data);
}

void UveSponsorSkipPlugin::attachMarkers()
{
    // Die SeekBar des Players erkennen: QML-Typ "SeekBar" bzw. ihre Eigenschaften dragFraction/live/player
    QList<QQuickItem *> bars;
    const std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        const bool isBar = QByteArray(item->metaObject()->className()).startsWith("SeekBar")
                           || (item->property("dragFraction").isValid() && item->property("live").isValid()
                               && item->property("player").isValid());
        if (isBar) { bars << item; return; }
        for (QQuickItem *child : item->childItems()) walk(child);
    };
    for (QWindow *w : QGuiApplication::topLevelWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w)) walk(qw->contentItem());

    for (QQuickItem *bar : std::as_const(bars)) {
        QQmlEngine *engine = qmlEngine(bar);
        if (!engine) continue;
        // In die Leiste selbst ("track", erstes Kind) – so wächst die Markierung beim Darüberfahren mit
        QQuickItem *track = bar;
        const QList<QQuickItem *> kids = bar->childItems();
        if (!kids.isEmpty() && QByteArray(kids.first()->metaObject()->className()).startsWith("QQuickRectangle")) track = kids.first();
        bool present = false;
        for (QQuickItem *child : track->childItems())
            if (child->objectName() == kLayerName) { m_markerLayers << QPointer<QQuickItem>(child); present = true; }
        if (present) continue;

        QQmlComponent component(engine);
        component.setData(kLayerQml, QUrl(QStringLiteral("qrc:/uvesponsorskip/SegmentMarkers.qml")));
        QObject *obj = component.beginCreate(qmlContext(bar) ? qmlContext(bar) : engine->rootContext());
        auto *layer = qobject_cast<QQuickItem *>(obj);
        if (!layer) {
            delete obj;
            m_host->log(kId, QStringLiteral("warning"), QStringLiteral("timeline markers: %1").arg(component.errorString()));
            continue;
        }
        layer->setObjectName(kLayerName);
        layer->setParent(track);
        layer->setParentItem(track);
        layer->setProperty("player", bar->property("player"));
        component.completeCreate();
        m_markerLayers << QPointer<QQuickItem>(layer);
    }
}

void UveSponsorSkipPlugin::removeMarkers()
{
    for (const QPointer<QQuickItem> &layer : std::as_const(m_markerLayers))
        if (layer) { layer->setParentItem(nullptr); layer->deleteLater(); }
    m_markerLayers.clear();
}

} // namespace uve::sponsor
