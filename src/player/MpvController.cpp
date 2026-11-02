#include "MpvController.h"

#include <mpv/client.h>

#include <QRegularExpression>
#include <QDateTime>
#include <cstring>
#include <QDir>
#include <QMetaObject>
#include <QTimeZone>
#include <QTimer>

#include <memory>
#include <vector>

namespace uve {

// ---------------------------------------------------------------- mpv_node ⇄ QVariant
static QVariant nodeToVariant(const mpv_node *n)
{
    switch (n->format) {
    case MPV_FORMAT_STRING: return QString::fromUtf8(n->u.string);
    case MPV_FORMAT_FLAG: return bool(n->u.flag);
    case MPV_FORMAT_INT64: return qlonglong(n->u.int64);
    case MPV_FORMAT_DOUBLE: return n->u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList l;
        for (int i = 0; i < n->u.list->num; ++i) l << nodeToVariant(&n->u.list->values[i]);
        return l;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap m;
        for (int i = 0; i < n->u.list->num; ++i)
            m.insert(QString::fromUtf8(n->u.list->keys[i]), nodeToVariant(&n->u.list->values[i]));
        return m;
    }
    default: return {};
    }
}

// Hält die Speicherbelegung eines aus QVariant erzeugten mpv_node.
// Alle Puffer liegen in eigenen Heap-Blöcken (unique_ptr), damit Zeiger bei Rekursion stabil bleiben.
struct NodeBuilder
{
    mpv_node node{};
    std::vector<std::unique_ptr<QByteArray>> strings;
    std::vector<std::unique_ptr<std::vector<mpv_node>>> arrays;
    std::vector<std::unique_ptr<std::vector<char *>>> keyLists;
    std::vector<std::unique_ptr<mpv_node_list>> lists;

    explicit NodeBuilder(const QVariant &v) { build(&node, v); }

    char *keepString(const QString &s)
    {
        strings.push_back(std::make_unique<QByteArray>(s.toUtf8()));
        return strings.back()->data();
    }

    void build(mpv_node *dst, const QVariant &v)
    {
        switch (v.typeId()) {
        case QMetaType::Bool: dst->format = MPV_FORMAT_FLAG; dst->u.flag = v.toBool(); break;
        case QMetaType::Int: case QMetaType::LongLong: case QMetaType::UInt: case QMetaType::ULongLong:
            dst->format = MPV_FORMAT_INT64; dst->u.int64 = v.toLongLong(); break;
        case QMetaType::Double: case QMetaType::Float: dst->format = MPV_FORMAT_DOUBLE; dst->u.double_ = v.toDouble(); break;
        case QMetaType::QVariantList: case QMetaType::QStringList: {
            const auto l = v.toList();
            arrays.push_back(std::make_unique<std::vector<mpv_node>>(l.size()));
            auto *arr = arrays.back().get();
            for (qsizetype i = 0; i < l.size(); ++i) build(&(*arr)[size_t(i)], l[i]);
            lists.push_back(std::make_unique<mpv_node_list>());
            auto *list = lists.back().get();
            list->num = int(arr->size()); list->values = arr->data(); list->keys = nullptr;
            dst->format = MPV_FORMAT_NODE_ARRAY; dst->u.list = list;
            break;
        }
        case QMetaType::QVariantMap: {
            const auto m = v.toMap();
            arrays.push_back(std::make_unique<std::vector<mpv_node>>(m.size()));
            keyLists.push_back(std::make_unique<std::vector<char *>>());
            auto *arr = arrays.back().get();
            auto *ks = keyLists.back().get();
            size_t i = 0;
            for (auto it = m.begin(); it != m.end(); ++it, ++i) {
                ks->push_back(keepString(it.key()));
                build(&(*arr)[i], it.value());
            }
            lists.push_back(std::make_unique<mpv_node_list>());
            auto *list = lists.back().get();
            list->num = int(arr->size()); list->values = arr->data(); list->keys = ks->data();
            dst->format = MPV_FORMAT_NODE_MAP; dst->u.list = list;
            break;
        }
        default:
            dst->format = MPV_FORMAT_STRING; dst->u.string = keepString(v.toString());
        }
    }
};

// ---------------------------------------------------------------- Lebenszyklus
MpvController::MpvController(QObject *parent) : QObject(parent) {}

MpvController::~MpvController()
{
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

bool MpvController::initialize(const QVariantMap &options, QString *error)
{
    m_mpv = mpv_create();
    if (!m_mpv) { if (error) *error = QStringLiteral("mpv_create() fehlgeschlagen"); return false; }

    // Grundeinstellungen: keine mpv-eigene Oberfläche/Tasten, Ausgabe über die Render-API
    const QVariantMap base{
        {"vo", "libmpv"},
        {"hwdec", "auto-safe"},
        {"keep-open", "yes"},
        {"idle", "yes"},
        {"input-default-bindings", "no"},
        {"input-vo-keyboard", "no"},
        {"osc", "no"},
        {"osd-level", "1"},
        {"osd-font-size", "34"},
        {"osd-border-size", "2"},
        {"config", "no"},
        {"terminal", "no"},
        {"volume-max", "150"},
        {"sub-auto", "fuzzy"},
        {"ytdl", "yes"},
        {"cache", "yes"},
        {"demuxer-max-bytes", "150MiB"},
        {"screenshot-format", "png"},
        {"screenshot-template", "%F_%P"},
    };
    QVariantMap merged = base;
    for (auto it = options.begin(); it != options.end(); ++it) merged.insert(it.key(), it.value());
    for (auto it = merged.begin(); it != merged.end(); ++it) {
        const int r = mpv_set_option_string(m_mpv, it.key().toUtf8().constData(), it.value().toString().toUtf8().constData());
        if (r < 0) qWarning("mpv-Option %s=%s: %s", qPrintable(it.key()), qPrintable(it.value().toString()), mpv_error_string(r));
    }

    mpv_request_log_messages(m_mpv, "warn");

    if (const int r = mpv_initialize(m_mpv); r < 0) {
        if (error) *error = QString::fromUtf8(mpv_error_string(r));
        mpv_destroy(m_mpv); m_mpv = nullptr;
        return false;
    }

    struct Obs { const char *name; mpv_format fmt; };
    const Obs props[] = {
        {"time-pos", MPV_FORMAT_DOUBLE}, {"duration", MPV_FORMAT_DOUBLE}, {"pause", MPV_FORMAT_FLAG},
        {"volume", MPV_FORMAT_DOUBLE}, {"mute", MPV_FORMAT_FLAG}, {"speed", MPV_FORMAT_DOUBLE},
        {"media-title", MPV_FORMAT_STRING}, {"path", MPV_FORMAT_STRING}, {"track-list", MPV_FORMAT_NODE},
        {"seekable", MPV_FORMAT_FLAG}, {"paused-for-cache", MPV_FORMAT_FLAG}, {"core-idle", MPV_FORMAT_FLAG},
        {"demuxer-cache-time", MPV_FORMAT_DOUBLE}, {"sub-delay", MPV_FORMAT_DOUBLE},
        {"audio-delay", MPV_FORMAT_DOUBLE}, {"eof-reached", MPV_FORMAT_FLAG},
        {"ab-loop-a", MPV_FORMAT_NODE}, {"ab-loop-b", MPV_FORMAT_NODE},
    };
    for (const auto &p : props) mpv_observe_property(m_mpv, 0, p.name, p.fmt);

    mpv_set_wakeup_callback(m_mpv, &MpvController::wakeup, this);

    auto *diagTimer = new QTimer(this);
    diagTimer->setInterval(1000);
    connect(diagTimer, &QTimer::timeout, this, &MpvController::pollDiagnostics);
    diagTimer->start();
    return true;
}

bool MpvController::loadfileTakesIndex() const
{
    // "mpv 0.37.0", "mpv v0.41.0-1386-g2137d21b2" → Neben-Version ≥ 38; unbekannt → aktuelle Syntax
    static const QRegularExpression re(QStringLiteral("(\\d+)\\.(\\d+)"));
    const auto m = re.match(mpvVersion());
    if (!m.hasMatch()) return true;
    const int major = m.captured(1).toInt(), minor = m.captured(2).toInt();
    return major > 0 || minor >= 38;
}

QString MpvController::mpvVersion() const
{
    return getProperty(QStringLiteral("mpv-version")).toString();
}

void MpvController::wakeup(void *ctx)
{
    // Wird aus einem mpv-Thread aufgerufen: nur in die Qt-Eventloop einreihen
    auto *self = static_cast<MpvController *>(ctx);
    QMetaObject::invokeMethod(self, &MpvController::processEvents, Qt::QueuedConnection);
}

void MpvController::processEvents()
{
    if (!m_mpv) return;
    for (int guard = 0; guard < 1000; ++guard) {
        mpv_event *e = mpv_wait_event(m_mpv, 0); // timeout 0 = nicht blockierend
        if (e->event_id == MPV_EVENT_NONE) break;
        handleEvent(e);
    }
}

void MpvController::handleEvent(mpv_event *e)
{
    switch (e->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto *p = static_cast<mpv_event_property *>(e->data);
        QVariant v;
        if (p->format == MPV_FORMAT_DOUBLE) v = *static_cast<double *>(p->data);
        else if (p->format == MPV_FORMAT_FLAG) v = bool(*static_cast<int *>(p->data));
        else if (p->format == MPV_FORMAT_STRING) v = QString::fromUtf8(*static_cast<char **>(p->data));
        else if (p->format == MPV_FORMAT_NODE) v = nodeToVariant(static_cast<mpv_node *>(p->data));
        handlePropertyChange(QString::fromUtf8(p->name), v);
        break;
    }
    case MPV_EVENT_START_FILE:
        m_loaded = false;
        setState(Opening);
        break;
    case MPV_EVENT_FILE_LOADED:
        m_loaded = true;
        m_error.clear();
        updatePlayState();
        emit fileLoaded();
        break;
    case MPV_EVENT_END_FILE: {
        auto *ef = static_cast<mpv_event_end_file *>(e->data);
        QString reason;
        switch (ef->reason) {
        case MPV_END_FILE_REASON_EOF: reason = "eof"; break;
        case MPV_END_FILE_REASON_STOP: reason = "stop"; break;
        case MPV_END_FILE_REASON_QUIT: reason = "quit"; break;
        case MPV_END_FILE_REASON_ERROR: reason = "error"; break;
        case MPV_END_FILE_REASON_REDIRECT: reason = "redirect"; break;
        default: reason = "unknown";
        }
        m_loaded = false;
        if (reason == "error") setState(Error, QString::fromUtf8(mpv_error_string(ef->error)));
        else if (reason == "eof") setState(Ended);
        else if (reason == "stop" || reason == "quit") setState(Idle);
        emit endOfFile(reason);
        break;
    }
    case MPV_EVENT_IDLE:
        if (m_state != Error && m_state != Ended) setState(Idle);
        break;
    case MPV_EVENT_LOG_MESSAGE: {
        auto *m = static_cast<mpv_event_log_message *>(e->data);
        const QString prefix = QString::fromUtf8(m->prefix), level = QString::fromUtf8(m->level);
        const QString text = QString::fromUtf8(m->text).trimmed();
        emit logMessage(prefix, level, text);
        // Shader-Kompilier-/Parserfehler (user shaders) für das Effekt-Plugin
        // Nur Fehler-/Fatal-Meldungen (Warnungen wie Shader-Cache-Hinweise sind harmlos)
        if ((prefix.startsWith("vo") || prefix.contains("libmpv") || prefix.contains("gpu")) &&
            (level == "error" || level == "fatal"))
            emit shaderError(text);
        break;
    }
    default: break;
    }
}

void MpvController::handlePropertyChange(const QString &name, const QVariant &v)
{
    if (name == "time-pos") {
        m_position = v.isValid() ? v.toDouble() : 0;
        emit positionChanged();
    } else if (name == "duration") {
        m_duration = v.isValid() ? v.toDouble() : 0;
        emit durationChanged();
    } else if (name == "pause") {
        m_paused = v.toBool();
        emit pausedChanged();
        updatePlayState();
    } else if (name == "volume") {
        m_volume = v.toDouble(); emit volumeChanged();
    } else if (name == "mute") {
        m_muted = v.toBool(); emit mutedChanged();
    } else if (name == "speed") {
        m_speed = v.toDouble(); emit speedChanged();
    } else if (name == "media-title") {
        m_title = v.toString(); emit mediaTitleChanged();
    } else if (name == "path") {
        m_path = v.toString(); emit pathChanged();
    } else if (name == "track-list") {
        updateTracks(v);
    } else if (name == "seekable") {
        m_seekable = v.toBool(); emit seekableChanged();
    } else if (name == "paused-for-cache") {
        m_buffering = v.toBool();
        updatePlayState();
    } else if (name == "demuxer-cache-time") {
        m_bufferedUntil = v.isValid() ? v.toDouble() : 0; emit bufferedChanged();
    } else if (name == "sub-delay") {
        m_subDelay = v.toDouble(); emit delaysChanged();
    } else if (name == "audio-delay") {
        m_audioDelay = v.toDouble(); emit delaysChanged();
    } else if (name == "eof-reached") {
        // Mit keep-open=yes meldet mpv das Dateiende nicht per END_FILE, sondern über eof-reached
        // (letztes Bild bleibt stehen). Für die Wiedergabeliste wie ein normales Ende behandeln.
        if (v.toBool() && m_loaded && m_state != Ended) {
            setState(Ended);
            emit endOfFile(QStringLiteral("eof"));
        }
    } else if (name == "ab-loop-a") {
        m_abA = v.typeId() == QMetaType::Double ? v.toDouble() : -1; emit abLoopChanged();
    } else if (name == "ab-loop-b") {
        m_abB = v.typeId() == QMetaType::Double ? v.toDouble() : -1; emit abLoopChanged();
    }
}

void MpvController::updatePlayState()
{
    if (!m_loaded) return;
    if (m_buffering) setState(Buffering);
    else setState(m_paused ? Paused : Playing);
}

void MpvController::setState(State s, const QString &error)
{
    if (s == Error) m_error = error;
    if (m_state == s && s != Error) return;
    m_state = s;
    emit stateChanged();
}

void MpvController::updateTracks(const QVariant &trackList)
{
    m_audio.clear(); m_subs.clear(); m_video.clear();
    for (const auto &t : trackList.toList()) {
        const auto m = t.toMap();
        QVariantMap item{
            {"id", m.value("id")}, {"type", m.value("type")}, {"selected", m.value("selected")},
            {"lang", m.value("lang")}, {"title", m.value("title")}, {"codec", m.value("codec")},
            {"external", m.value("external")}, {"default", m.value("default")}, {"forced", m.value("forced")},
            {"channels", m.value("demux-channel-count")}, {"width", m.value("demux-w")}, {"height", m.value("demux-h")},
            {"albumart", m.value("albumart")},
        };
        // Lesbare Bezeichnung (Titel · Sprache · Codec Kanäle)
        QStringList label;
        if (!m.value("title").toString().isEmpty()) label << m.value("title").toString();
        if (!m.value("lang").toString().isEmpty()) label << m.value("lang").toString().toUpper();
        QString codec = m.value("codec").toString().toUpper();
        const int ch = m.value("demux-channel-count").toInt();
        if (ch == 6) codec += " 5.1"; else if (ch == 8) codec += " 7.1"; else if (ch == 2) codec += " Stereo"; else if (ch == 1) codec += " Mono";
        if (!codec.trimmed().isEmpty()) label << codec.trimmed();
        if (m.value("forced").toBool()) label << QStringLiteral("erzwungen");
        if (m.value("external").toBool()) label << QStringLiteral("extern");
        item.insert("label", label.isEmpty() ? QStringLiteral("Spur %1").arg(m.value("id").toInt()) : label.join(" · "));
        const auto type = m.value("type").toString();
        if (type == "audio") m_audio << item;
        else if (type == "sub") m_subs << item;
        else if (type == "video" && !m.value("albumart").toBool()) m_video << item;
    }
    emit tracksChanged();
}

void MpvController::pollDiagnostics()
{
    if (!m_mpv || !m_loaded) return;
    QVariantMap d;
    const char *names[] = {"hwdec-current", "current-vo", "video-codec", "audio-codec-name", "estimated-vf-fps",
                           "container-fps", "frame-drop-count", "decoder-frame-drop-count", "avsync",
                           "video-params/pixelformat", "video-params/w", "video-params/h", "video-params/gamma",
                           "video-params/primaries", "video-bitrate", "audio-bitrate", "glsl-shaders",
                           "audio-params/channel-count", "audio-params/samplerate", "file-format", "cache-buffering-state"};
    for (const char *n : names) d.insert(QString::fromUtf8(n), getProperty(QString::fromUtf8(n)));
    m_diag = d;
    emit diagnosticsChanged();
}

// ---------------------------------------------------------------- Eigenschaften/Befehle
QVariant MpvController::getProperty(const QString &name) const
{
    if (!m_mpv) return {};
    mpv_node n{};
    if (mpv_get_property(m_mpv, name.toUtf8().constData(), MPV_FORMAT_NODE, &n) < 0) return {};
    const auto v = nodeToVariant(&n);
    mpv_free_node_contents(&n);
    return v;
}

bool MpvController::setProperty(const QString &name, const QVariant &value)
{
    if (!m_mpv) return false;
    NodeBuilder nb(value);
    return mpv_set_property(m_mpv, name.toUtf8().constData(), MPV_FORMAT_NODE, &nb.node) >= 0;
}

bool MpvController::command(const QStringList &args)
{
    if (!m_mpv) return false;
    QList<QByteArray> bytes;
    QList<const char *> ptrs;
    for (const auto &a : args) { bytes << a.toUtf8(); }
    for (const auto &b : bytes) ptrs << b.constData();
    ptrs << nullptr;
    const int r = mpv_command_async(m_mpv, 0, ptrs.data());
    return r >= 0;
}

void MpvController::load(const QString &url, double startSeconds, bool startPaused)
{
    QStringList opts;
    if (startSeconds > 1) opts << QStringLiteral("start=%1").arg(startSeconds, 0, 'f', 3);
    opts << QStringLiteral("pause=%1").arg(startPaused ? "yes" : "no");
    m_abA = m_abB = -1;
    setProperty("ab-loop-a", "no");
    setProperty("ab-loop-b", "no");
    // mpv ≥ 0.38: loadfile <url> replace <index> <options>; ältere Versionen (z. B. Linux-Distributionen
    // mit mpv 0.37) kennen den Index nicht: loadfile <url> replace <options>
    const QStringList cmd = loadfileTakesIndex()
        ? QStringList{"loadfile", url, "replace", "-1", opts.join(',')}
        : QStringList{"loadfile", url, "replace", opts.join(',')};
    if (!m_renderReady) {
        // Ohne Render-Kontext würde mpv die Videospur abschalten → bis zur Videoausgabe zurückstellen
        m_pendingLoad = cmd;
        m_path = url; emit pathChanged();
        setState(Opening);
        // Rückfallebene: ohne sichtbares Fenster (z. B. minimiert gestartet) nach 5 s trotzdem laden
        QTimer::singleShot(5000, this, [this] { if (!m_pendingLoad.isEmpty()) markRenderReady(); });
        return;
    }
    command(cmd);
}

void MpvController::markRenderReady()
{
    m_renderReady = true;
    if (!m_pendingLoad.isEmpty()) {
        const auto cmd = m_pendingLoad;
        m_pendingLoad.clear();
        command(cmd);
    }
}

void MpvController::stop() { command({"stop"}); }
void MpvController::setPaused(bool p) { setProperty("pause", p); }
void MpvController::togglePause() { command({"cycle", "pause"}); }

void MpvController::seek(double seconds, bool exact)
{
    command({"seek", QString::number(qMax(0.0, seconds), 'f', 3), exact ? "absolute+exact" : "absolute"});
}
void MpvController::seekRelative(double seconds) { command({"seek", QString::number(seconds, 'f', 3), "relative"}); }
void MpvController::seekPercent(double percent) { command({"seek", QString::number(percent, 'f', 2), "absolute-percent"}); }
void MpvController::frameStep() { command({"frame-step"}); }
void MpvController::frameBackStep() { command({"frame-back-step"}); }
void MpvController::setVolume(double v) { setProperty("volume", qBound(0.0, v, 150.0)); }
void MpvController::setMuted(bool m) { setProperty("mute", m); }
void MpvController::setSpeed(double s) { setProperty("speed", qBound(0.1, s, 8.0)); }
void MpvController::setAudioTrack(int id) { setProperty("aid", id <= 0 ? QVariant("no") : QVariant(qlonglong(id))); }
void MpvController::setSubtitleTrack(int id) { setProperty("sid", id <= 0 ? QVariant("no") : QVariant(qlonglong(id))); }
void MpvController::cycleAudio() { command({"cycle", "audio"}); }
void MpvController::cycleSubtitle() { command({"cycle", "sub"}); }
void MpvController::addSubtitle(const QString &file) { command({"sub-add", file, "select"}); }
void MpvController::setSubDelay(double s) { setProperty("sub-delay", s); }
void MpvController::setAudioDelay(double s) { setProperty("audio-delay", s); }

void MpvController::cycleAbLoop() { command({"ab-loop"}); }

bool MpvController::abLoopActive() const { return m_abA >= 0; }

QString MpvController::abLoopText() const
{
    auto fmt = [](double t) { return QDateTime::fromSecsSinceEpoch(qint64(t), QTimeZone::UTC).toString(t >= 3600 ? "h:mm:ss" : "mm:ss"); };
    if (m_abA < 0) return {};
    if (m_abB < 0) return QStringLiteral("A %1").arg(fmt(m_abA));
    return tr("A-B %1 – %2").arg(fmt(m_abA), fmt(m_abB));
}

void MpvController::setAspect(const QString &mode)
{
    // fit: Seitenverhältnis halten · fill: Füllen (Beschnitt) · stretch: Verzerren · feste Verhältnisse
    if (mode == "fill") { setProperty("keepaspect", true); setProperty("panscan", 1.0); setProperty("video-aspect-override", "-1"); }
    else if (mode == "stretch") { setProperty("keepaspect", false); setProperty("panscan", 0.0); setProperty("video-aspect-override", "-1"); }
    else if (mode.contains(':')) { setProperty("keepaspect", true); setProperty("panscan", 0.0); setProperty("video-aspect-override", mode); }
    else { setProperty("keepaspect", true); setProperty("panscan", 0.0); setProperty("video-aspect-override", "-1"); }
}

QImage MpvController::grabFrame(int maxWidth) const
{
    if (!m_mpv) return {};
    const char *args[] = {"screenshot-raw", "video", "bgra", nullptr};
    mpv_node result{};
    if (mpv_command_ret(m_mpv, args, &result) < 0) return {};
    QImage img;
    if (result.format == MPV_FORMAT_NODE_MAP) {
        qint64 w = 0, h = 0, stride = 0;
        QByteArray fmt;
        const mpv_byte_array *data = nullptr;
        for (int i = 0; i < result.u.list->num; ++i) {
            const char *k = result.u.list->keys[i];
            const mpv_node &v = result.u.list->values[i];
            if (!strcmp(k, "w") && v.format == MPV_FORMAT_INT64) w = v.u.int64;
            else if (!strcmp(k, "h") && v.format == MPV_FORMAT_INT64) h = v.u.int64;
            else if (!strcmp(k, "stride") && v.format == MPV_FORMAT_INT64) stride = v.u.int64;
            else if (!strcmp(k, "format") && v.format == MPV_FORMAT_STRING) fmt = v.u.string;
            else if (!strcmp(k, "data") && v.format == MPV_FORMAT_BYTE_ARRAY) data = v.u.ba;
        }
        if (data && w > 0 && h > 0 && stride >= w * 4 && qint64(data->size) >= stride * h && fmt == "bgra") {
            // Kopie, da mpv den Puffer mit mpv_free_node_contents freigibt
            img = QImage(static_cast<const uchar *>(data->data), int(w), int(h), int(stride), QImage::Format_ARGB32).copy();
            if (maxWidth > 0 && img.width() > maxWidth) img = img.scaledToWidth(maxWidth, Qt::SmoothTransformation);
            img = img.convertToFormat(QImage::Format_RGB32);
        }
    }
    mpv_free_node_contents(&result);
    return img;
}

QString MpvController::screenshot(const QString &directory)
{
    QDir().mkpath(directory);
    const QString file = QDir(directory).filePath(QStringLiteral("UVE_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")));
    // "video": Bild ohne Untertitel/OSD in Quellauflösung (ohne Benutzer-Shader)
    command({"screenshot-to-file", file, "video"});
    return file;
}

void MpvController::showText(const QString &text, int ms) { command({"show-text", text, QString::number(ms)}); }

} // namespace uve
