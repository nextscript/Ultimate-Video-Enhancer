#include "UvePreviewPlugin.h"

#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <cmath>
#include <functional>

namespace uve::preview {

static const QString kId = QStringLiteral("org.uveplayer.uvepreview");
static const QString kLayerName = QStringLiteral("uvePreviewLayer");
static constexpr int kThrottleMs = 100;      // Anfrageabstand
static constexpr int kCacheFrames = 120;     // Speicher-Cache pro Medium (≈ 120 × 60 KB)
static constexpr int kBoxW = 240;            // Vorschau-Box, unabhängig von der Fenstergröße
static constexpr int kBoxH = 180;
static constexpr double kExactUpTo = 900;    // bis 15 min exakt springen, darüber Keyframes (schneller)

// ---------------------------------------------------------------------------------------------------------------
// Controller

UvePreviewController::UvePreviewController(uve::IPlayerHost *host, QObject *parent)
    : QObject(parent), m_host(host), m_worker(new PreviewWorker(this)), m_throttle(new QTimer(this))
{
    const QString lang = host->call(kId, QStringLiteral("app.info")).toMap().value("language").toString();
    if (!lang.isEmpty()) m_language = lang;
    m_enabled = host->loadPluginSettings(kId).value("enabled").toBool(true);

    m_throttle->setSingleShot(true);
    m_throttle->setInterval(kThrottleMs);
    connect(m_throttle, &QTimer::timeout, this, &UvePreviewController::dispatch);
    connect(m_worker, &PreviewWorker::frameReady, this, &UvePreviewController::onFrame, Qt::QueuedConnection);
    connect(m_worker, &PreviewWorker::frameFailed, this, &UvePreviewController::onFailed, Qt::QueuedConnection);
    connect(m_worker, &PreviewWorker::mediaUnsupported, this, &UvePreviewController::onUnsupported, Qt::QueuedConnection);

    QString err;
    if (!PreviewWorker::mpvAvailable(&err)) {
        m_host->log(kId, QStringLiteral("warning"), QStringLiteral("libmpv: %1").arg(err));
        setStatus(text("libmpv nicht gefunden – keine Bildvorschau.", "libmpv not found – no picture preview."));
    } else {
        setStatus(text("Kein lokales Video geöffnet.", "No local video open."));
    }
}

UvePreviewController::~UvePreviewController() { shutdown(); }

QString UvePreviewController::text(const char *de, const char *en) const
{
    return QString::fromUtf8(m_language.startsWith(QLatin1String("de")) ? de : en);
}

void UvePreviewController::setLanguage(const QString &code)
{
    if (code.isEmpty() || code == m_language) return;
    m_language = code;
    emit languageChanged();
    updateAvailable();
}

void UvePreviewController::setStatus(const QString &s)
{
    if (s == m_status) return;
    m_status = s;
    emit statusChanged();
}

void UvePreviewController::setEnabled(bool on)
{
    if (on == m_enabled) return;
    m_enabled = on;
    QJsonObject s = m_host->loadPluginSettings(kId);
    s.insert(QStringLiteral("enabled"), on);
    m_host->savePluginSettings(kId, s);
    if (!on) { invalidate(); m_worker->closeMedia(); m_cache.clear(); }
    emit enabledChanged();
    updateAvailable();
}

void UvePreviewController::setActive(bool active)
{
    if (active == m_active) return;
    m_active = active;
    if (!active) { invalidate(); m_worker->closeMedia(); m_cache.clear(); }
    updateAvailable();
}

void UvePreviewController::shutdown()
{
    invalidate();
    m_cache.clear();
    m_worker->shutdown();   // beendet den Thread und räumt die mpv-Instanz auf
}

static bool isLocalVideoPath(const QString &path, QString *local)
{
    if (path.isEmpty()) return false;
    QString p = path;
    const QUrl url(path);
    // Laufwerksbuchstabe "C:/…" ist kein URL-Schema; file:// in einen Pfad wandeln; alle anderen Schemata
    // (http, tcp, dvd, bd, cdda, edl, memory …) sind Netzwerk/Disc → keine Bildvorschau in 1.0.0
    if (url.isLocalFile()) p = url.toLocalFile();
    else if (!url.scheme().isEmpty() && url.scheme().size() > 1) return false;
    const QFileInfo fi(p);
    if (!fi.exists() || !fi.isFile()) return false;
    *local = fi.absoluteFilePath();
    return true;
}

void UvePreviewController::mediaChanged(const QString &path, double duration)
{
    invalidate();
    // Laufende Aufträge beenden und die Datei sicher freigeben, bevor der Player (neu) lädt
    m_worker->closeMedia(path.isEmpty() ? 1000 : 0);
    m_cache.clear();
    m_failed.clear();
    ++m_media;
    m_unsupported = false;
    m_path.clear();
    m_duration = duration;
    QString local;
    if (isLocalVideoPath(path, &local)) {
        m_path = local;
        // Hauptwiedergabe schon bekannt als ohne Video (Audiodatei, nur Cover) → gar nicht erst laden
        const QVariant albumart = m_host->call(kId, QStringLiteral("mpv.get"), {{"name", QStringLiteral("current-tracks/video/albumart")}});
        const QVariant vid = m_host->call(kId, QStringLiteral("mpv.get"), {{"name", QStringLiteral("current-tracks/video/id")}});
        if (albumart.toBool() || (vid.isValid() && !vid.isNull() && vid.toInt() <= 0)) m_unsupported = true;
    }
    m_step = previewStep(m_duration);
    updateAvailable();
}

void UvePreviewController::updateAvailable()
{
    QString err;
    const bool mpvOk = PreviewWorker::mpvAvailable(&err);
    const bool avail = mpvOk && m_active && m_enabled && !m_path.isEmpty() && !m_unsupported;
    if (avail != m_available) {
        m_available = avail;
        emit availableChanged();
    }
    if (!avail) clearImage();
    if (!mpvOk) setStatus(text("libmpv nicht gefunden – keine Bildvorschau.", "libmpv not found – no picture preview."));
    else if (!m_enabled) setStatus(text("Aus – die Zeitleiste zeigt nur die Zeit.", "Off – the timeline only shows the time."));
    else if (m_path.isEmpty()) setStatus(text("Kein lokales Video geöffnet – die Zeitleiste zeigt nur die Zeit.",
                                              "No local video open – the timeline only shows the time."));
    else if (m_unsupported) setStatus(text("Keine Videospur – die Zeitleiste zeigt nur die Zeit.",
                                           "No video track – the timeline only shows the time."));
    else setStatus(text("Bereit: %1", "Ready: %1").arg(QFileInfo(m_path).fileName()));
}

void UvePreviewController::hover(double seconds)
{
    if (!m_available) return;
    if (m_duration <= 0) {   // Länge erst jetzt bekannt
        m_duration = m_host->call(kId, QStringLiteral("mpv.get"), {{"name", QStringLiteral("duration")}}).toDouble();
        m_step = previewStep(m_duration);
        if (m_duration <= 0) return;
    }
    const int last = qMax(0, int(std::floor((m_duration - 0.001) / m_step)));
    const int index = std::clamp(int(std::lround(std::clamp(seconds, 0.0, m_duration) / m_step)), 0, last);
    if (index == m_wanted) return;
    m_wanted = index;
    if (m_cache.contains(index)) {
        publish(index);
        return;
    }
    // Neueste Position merken; höchstens alle kThrottleMs ein Auftrag
    if (!m_throttle->isActive()) dispatch();
}

void UvePreviewController::dispatch()
{
    if (!m_available || m_wanted < 0 || m_wanted == m_sentIndex || m_cache.contains(m_wanted) || m_failed.contains(m_wanted)) return;
    PreviewJob job;
    job.id = ++m_seq;
    job.media = m_media;
    job.path = m_path;
    job.index = m_wanted;
    // Letzter Rasterplatz: kurz vor das Ende, damit es sicher ein Bild gibt
    job.time = qMin(m_wanted * m_step, qMax(0.0, m_duration - qMin(0.25, m_duration / 50.0)));
    job.boxWidth = kBoxW;
    job.boxHeight = kBoxH;
    job.duration = m_duration;
    job.exact = m_duration <= kExactUpTo;
    m_lastSent = job.id;
    m_sentIndex = m_wanted;
    m_worker->submit(job);
    m_throttle->start();
}

void UvePreviewController::onFrame(quint64 id, quint64 media, int index, const QImage &img, const QString &url)
{
    if (media != m_media || !m_available) return;   // altes Medium oder inzwischen aus
    Frame f;
    f.url = url;   // PNG, im Decoder-Thread kodiert
    f.w = img.width();
    f.h = img.height();
    f.used = ++m_useClock;
    if (m_cache.size() >= kCacheFrames) {   // ältesten Eintrag verwerfen
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            if (it->used < oldest->used) oldest = it;
        m_cache.erase(oldest);
    }
    m_cache.insert(index, f);
    if (id == m_lastSent) m_sentIndex = -1;
    // Nur zeigen, was noch zur aktuellen Anfrage gehört (oder die neueste ist, solange die gewünschte noch lädt)
    if (m_wanted >= 0 && (index == m_wanted || id == m_lastSent)) publish(index);
    if (m_wanted >= 0 && !m_cache.contains(m_wanted) && !m_throttle->isActive()) dispatch();
}

void UvePreviewController::onFailed(quint64 id, quint64 media, int index, const QString &reason)
{
    if (media != m_media) return;
    if (id == m_lastSent) m_sentIndex = -1;
    // Vorübergehend (abgebrochen, Datei gesperrt): beim nächsten Überfahren neu versuchen
    if (reason != QLatin1String("cancelled") && reason != QLatin1String("file busy")) m_failed.insert(index);
    if (reason != QLatin1String("cancelled"))
        m_host->log(kId, QStringLiteral("debug"), QStringLiteral("preview %1: %2").arg(index).arg(reason));
    // Bei Fehler bleibt der Tooltip mit der Zeit (kein Bild für diese Stelle)
}

void UvePreviewController::onUnsupported(quint64 media, const QString &reason)
{
    if (media != m_media) return;
    m_host->log(kId, QStringLiteral("info"), QStringLiteral("no preview for %1: %2").arg(QFileInfo(m_path).fileName(), reason));
    m_unsupported = true;
    m_worker->closeMedia();
    updateAvailable();
}

void UvePreviewController::publish(int index)
{
    const auto it = m_cache.find(index);
    if (it == m_cache.end()) return;
    it->used = ++m_useClock;
    m_shownIndex = index;
    if (m_image == it->url) return;
    m_image = it->url;
    m_imageW = it->w;
    m_imageH = it->h;
    emit imageChanged();
}

void UvePreviewController::clearImage()
{
    m_shownIndex = -1;
    if (m_image.isEmpty()) return;
    m_image.clear();
    emit imageChanged();
}

void UvePreviewController::invalidate()
{
    m_throttle->stop();
    m_worker->cancelPending();
    m_wanted = -1;
    m_sentIndex = -1;
    m_lastSent = 0;   // spätere Ergebnisse gehören zu keiner aktuellen Anfrage mehr
    clearImage();
}

void UvePreviewController::leave()
{
    invalidate();
}

// ---------------------------------------------------------------------------------------------------------------
// Plugin

bool UvePreviewPlugin::initialize(uve::IPlayerHost *host, QString *error)
{
    if (!host) { if (error) *error = QStringLiteral("no host"); return false; }
    m_host = host;
    m_controller = new UvePreviewController(host, this);
    m_attachTimer = new QTimer(this);
    m_attachTimer->setInterval(1500);   // Oberfläche entsteht evtl. erst nach dem Plugin
    connect(m_attachTimer, &QTimer::timeout, this, &UvePreviewPlugin::ensureLayers);
    return true;
}

void UvePreviewPlugin::activate()
{
    m_controller->setActive(true);
    // Schon ein Medium geladen (Plugin eingeschaltet während der Wiedergabe)
    const QVariantMap st = m_host->call(kId, QStringLiteral("player.state")).toMap();
    m_controller->mediaChanged(st.value("path").toString(), st.value("duration").toDouble());
    ensureLayers();
    m_attachTimer->start();
}

void UvePreviewPlugin::suspend()
{
    m_attachTimer->stop();
    m_controller->setActive(false);
    removeLayers();
}

void UvePreviewPlugin::deactivate()
{
    m_attachTimer->stop();
    removeLayers();
    m_controller->setActive(false);
    m_controller->shutdown();
}

void UvePreviewPlugin::onEvent(const QString &name, const QVariantMap &data)
{
    if (name == QLatin1String("media.loading") || name == QLatin1String("player.stopped")) {
        m_controller->mediaChanged(QString(), 0);
    } else if (name == QLatin1String("media.loaded")) {
        mediaLoaded(data);
    } else if (name == QLatin1String("language.changed")) {
        m_controller->setLanguage(data.value("code").toString());
    } else if (name == QLatin1String("app.quit")) {
        m_attachTimer->stop();
        removeLayers();
        m_controller->shutdown();
    }
}

void UvePreviewPlugin::mediaLoaded(const QVariantMap &data)
{
    // "path" ist das tatsächlich geladene Medium (bei Plugins mit urlHandlers evtl. eine andere Adresse als "source")
    m_controller->mediaChanged(data.value("path").toString(), data.value("duration").toDouble());
}

void UvePreviewPlugin::ensureLayers()
{
    m_layers.removeIf([](const QPointer<QQuickItem> &p) { return p.isNull(); });
    if (m_layers.isEmpty()) attachLayers();
}

static QQuickItem *childItem(QQuickItem *parent, const std::function<bool(QQuickItem *)> &match)
{
    for (QQuickItem *c : parent->childItems())
        if (match(c)) return c;
    return nullptr;
}

static bool isClass(QQuickItem *item, const char *prefix)
{
    return QByteArray(item->metaObject()->className()).startsWith(prefix);
}

void UvePreviewPlugin::attachLayers()
{
    // SeekBar des Players erkennen: QML-Typ "SeekBar" bzw. ihre Eigenschaften dragFraction/live/player
    QList<QQuickItem *> bars;
    const std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        const bool isBar = isClass(item, "SeekBar")
                           || (item->property("dragFraction").isValid() && item->property("live").isValid()
                               && item->property("player").isValid());
        if (isBar) { bars << item; return; }
        for (QQuickItem *child : item->childItems()) walk(child);
    };
    for (QWindow *w : QGuiApplication::topLevelWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w)) walk(qw->contentItem());
    if (bars.isEmpty()) return;

    QFile f(QStringLiteral(":/uvepreview/PreviewLayer.qml"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray qml = f.readAll();

    for (QQuickItem *bar : std::as_const(bars)) {
        if (QQuickItem *present = childItem(bar, [](QQuickItem *c) { return c->objectName() == kLayerName; })) {
            m_layers << QPointer<QQuickItem>(present);
            continue;
        }
        QQmlEngine *engine = qmlEngine(bar);
        if (!engine) continue;
        // Bestandteile der SeekBar: Leiste (erstes Rechteck), MouseArea (Hover/Ziehen) und der Zeit-Tooltip
        // (Rechteck über der Leiste mit Text, ohne eigene MouseArea – im Unterschied zum LIVE-Badge)
        const QList<QQuickItem *> kids = bar->childItems();
        QQuickItem *track = !kids.isEmpty() && isClass(kids.first(), "QQuickRectangle") ? kids.first() : nullptr;
        QQuickItem *mouse = childItem(bar, [](QQuickItem *c) { return isClass(c, "QQuickMouseArea"); });
        QQuickItem *tip = childItem(bar, [](QQuickItem *c) {
            if (!isClass(c, "QQuickRectangle") || !c->objectName().isEmpty()) return false;
            const QList<QQuickItem *> ch = c->childItems();
            return ch.size() == 1 && isClass(ch.first(), "QQuickText");
        });
        if (!track || !mouse) {
            m_host->log(kId, QStringLiteral("warning"), QStringLiteral("timeline layout not recognised"));
            continue;
        }
        QQmlComponent component(engine);
        component.setData(qml, QUrl(QStringLiteral("qrc:/uvepreview/PreviewLayer.qml")));
        QObject *obj = component.beginCreate(qmlContext(bar) ? qmlContext(bar) : engine->rootContext());
        auto *layer = qobject_cast<QQuickItem *>(obj);
        if (!layer) {
            delete obj;
            m_host->log(kId, QStringLiteral("warning"), QStringLiteral("preview layer: %1").arg(component.errorString()));
            continue;
        }
        layer->setObjectName(kLayerName);
        layer->setParent(bar);
        layer->setParentItem(bar);
        layer->setProperty("player", bar->property("player"));
        layer->setProperty("controller", QVariant::fromValue<QObject *>(m_controller));
        layer->setProperty("bar", QVariant::fromValue<QObject *>(bar));
        layer->setProperty("mouse", QVariant::fromValue<QObject *>(mouse));
        layer->setProperty("track", QVariant::fromValue<QObject *>(track));
        layer->setProperty("tip", QVariant::fromValue<QObject *>(tip));
        component.completeCreate();
        m_layers << QPointer<QQuickItem>(layer);
    }
}

void UvePreviewPlugin::removeLayers()
{
    for (const QPointer<QQuickItem> &layer : std::as_const(m_layers))
        if (layer) {
            layer->setProperty("controller", QVariant::fromValue<QObject *>(nullptr));
            layer->setParentItem(nullptr);
            delete layer.data();   // sofort: Popup verschwindet, Tooltip-Deckkraft wird wiederhergestellt
        }
    m_layers.clear();
}

} // namespace uve::preview
