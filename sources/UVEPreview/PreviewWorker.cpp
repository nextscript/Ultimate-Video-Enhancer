#include "PreviewWorker.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTransform>
#include <QUrl>

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/stream_cb.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace uve::preview {

namespace {

constexpr int kOpenLimitMs = 12000;   // Datei öffnen + erstes Bild
constexpr int kSeekLimitMs = 6000;    // ein Sprung + Bild
constexpr int kIdleCloseMs = 2500;    // ohne Aufträge die Datei wieder freigeben

// ------------------------------------------------------------------ libmpv des Players finden
// Kein eigenes libmpv im Paket: der Player hat seine libmpv schon geladen (Windows: libmpv-2.dll, Linux/macOS:
// gegen libmpv gelinkt). Dieselbe Auslieferung wird für die zweite, unabhängige Instanz benutzt.
struct MpvApi {
    decltype(&mpv_create) create = nullptr;
    decltype(&mpv_initialize) initialize = nullptr;
    decltype(&mpv_terminate_destroy) terminate_destroy = nullptr;
    decltype(&mpv_set_option_string) set_option_string = nullptr;
    decltype(&mpv_command) command = nullptr;
    decltype(&mpv_get_property) get_property = nullptr;
    decltype(&mpv_get_property_string) get_property_string = nullptr;
    decltype(&mpv_free) free = nullptr;
    decltype(&mpv_wait_event) wait_event = nullptr;
    decltype(&mpv_set_wakeup_callback) set_wakeup_callback = nullptr;
    decltype(&mpv_command_ret) command_ret = nullptr;
    decltype(&mpv_free_node_contents) free_node_contents = nullptr;
    decltype(&mpv_stream_cb_add_ro) stream_cb_add_ro = nullptr;
    // Render-API (Software-Renderer), nur für den Rückfallweg
    decltype(&mpv_render_context_create) rc_create = nullptr;
    decltype(&mpv_render_context_set_update_callback) rc_set_update_callback = nullptr;
    decltype(&mpv_render_context_update) rc_update = nullptr;
    decltype(&mpv_render_context_render) rc_render = nullptr;
    decltype(&mpv_render_context_free) rc_free = nullptr;
    bool renderOk = false;
    bool ok = false;
    QString error;
};

void *findSymbol(void *module, const char *name)
{
#ifdef Q_OS_WIN
    return module ? reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(module), name)) : nullptr;
#else
    return dlsym(module ? module : RTLD_DEFAULT, name);
#endif
}

void *findMpvModule()
{
#ifdef Q_OS_WIN
    // Schon geladene libmpv des Players (main.cpp lädt die neueste libmpv-2.dll explizit)
    for (const wchar_t *name : {L"libmpv-2.dll", L"mpv-2.dll", L"libmpv-1.dll", L"mpv-1.dll"})
        if (HMODULE h = GetModuleHandleW(name)) return h;
    // Sonst (z. B. Test ohne Player): Programmordner\mpv\libmpv-2.dll, wie beim Player
    const QString dir = QCoreApplication::applicationDirPath();
    for (const QString &rel : {QStringLiteral("mpv/libmpv-2.dll"), QStringLiteral("libmpv-2.dll")}) {
        const QString p = QDir::toNativeSeparators(QDir(dir).filePath(rel));
        if (HMODULE h = LoadLibraryExW(reinterpret_cast<const wchar_t *>(p.utf16()), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) return h;
    }
    return nullptr;
#else
    if (dlsym(RTLD_DEFAULT, "mpv_create")) return nullptr;   // im Prozess vorhanden (Player ist gegen libmpv gelinkt)
#ifdef Q_OS_MACOS
    const char *names[] = {"libmpv.2.dylib", "libmpv.dylib", "@executable_path/../Frameworks/libmpv.2.dylib"};
#else
    const char *names[] = {"libmpv.so.2", "libmpv.so.1", "libmpv.so"};
#endif
    for (const char *n : names)
        if (void *h = dlopen(n, RTLD_NOW | RTLD_GLOBAL)) return h;
    return nullptr;
#endif
}

MpvApi &mpvApi()
{
    static MpvApi api;
    static std::once_flag once;
    std::call_once(once, [] {
        void *h = findMpvModule();
#ifdef Q_OS_WIN
        if (!h) { api.error = QStringLiteral("libmpv-2.dll not found"); return; }
#endif
        auto get = [h](auto &fn, const char *name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(findSymbol(h, name));
            return fn != nullptr;
        };
        api.ok = get(api.create, "mpv_create") && get(api.initialize, "mpv_initialize")
              && get(api.terminate_destroy, "mpv_terminate_destroy") && get(api.set_option_string, "mpv_set_option_string")
              && get(api.command, "mpv_command") && get(api.get_property, "mpv_get_property")
              && get(api.get_property_string, "mpv_get_property_string") && get(api.free, "mpv_free")
              && get(api.wait_event, "mpv_wait_event") && get(api.set_wakeup_callback, "mpv_set_wakeup_callback")
              && get(api.command_ret, "mpv_command_ret") && get(api.free_node_contents, "mpv_free_node_contents")
              && get(api.stream_cb_add_ro, "mpv_stream_cb_add_ro");
        if (!api.ok) api.error = QStringLiteral("libmpv found, but the client API is incomplete");
        api.renderOk = get(api.rc_create, "mpv_render_context_create")
                    && get(api.rc_set_update_callback, "mpv_render_context_set_update_callback")
                    && get(api.rc_update, "mpv_render_context_update") && get(api.rc_render, "mpv_render_context_render")
                    && get(api.rc_free, "mpv_render_context_free");
    });
    return api;
}

// Wecker für mpv-Callbacks (kommen auf mpv-Threads)
struct Wake {
    std::mutex lock;
    std::condition_variable cv;
    bool flag = false;
    void notify()
    {
        { std::lock_guard l(lock); flag = true; }
        cv.notify_all();
    }
    void wait(int ms)
    {
        std::unique_lock l(lock);
        cv.wait_for(l, std::chrono::milliseconds(ms), [this] { return flag; });
        flag = false;
    }
};

void onWakeup(void *p) { static_cast<Wake *>(p)->notify(); }

// Bildquelle: 0 = "screenshot-raw" mit vo=null (schnell, neuere libmpv), 1 = Software-Renderer der Render-API
// mit vo=libmpv (z. B. libmpv 0.37 unter Ubuntu 24.04, wo vo=null kein Bild für Screenshots behält).
// Wird beim ersten Fehlschlag für den ganzen Prozess umgestellt.
std::atomic<int> g_source{0};

// ------------------------------------------------------------------ Datei über eigene Callbacks lesen
// libmpv öffnet die Datei nicht selbst: auf Netzlaufwerken wie RaiDrive (WinFsp) scheitert libmpvs eigenes Öffnen,
// solange der Player die Datei offen hat – und macht dabei den Dateizugriff des Players ungültig (Wiedergabe
// bleibt stehen). Ein normales QFile-Öffnen stört nicht. mpv liest daher über mpv_stream_cb (uvepreview://).
struct StreamSource {
    QString path;
};

int64_t streamRead(void *cookie, char *buf, uint64_t nbytes)
{
    auto *f = static_cast<QFile *>(cookie);
    const qint64 n = f->read(buf, qint64(qMin<uint64_t>(nbytes, 1u << 24)));
    return n < 0 ? -1 : n;
}

int64_t streamSeek(void *cookie, int64_t offset)
{
    auto *f = static_cast<QFile *>(cookie);
    return f->seek(offset) ? offset : int64_t(MPV_ERROR_GENERIC);
}

int64_t streamSize(void *cookie)
{
    const qint64 s = static_cast<QFile *>(cookie)->size();
    return s >= 0 ? s : int64_t(MPV_ERROR_UNSUPPORTED);
}

void streamClose(void *cookie) { delete static_cast<QFile *>(cookie); }

int streamOpen(void *userData, char *, mpv_stream_cb_info *info)
{
    auto *src = static_cast<StreamSource *>(userData);
    auto *f = new QFile(src->path);
    if (!f->open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
        delete f;
        return MPV_ERROR_LOADING_FAILED;
    }
    info->cookie = f;
    info->read_fn = streamRead;
    info->seek_fn = streamSeek;
    info->size_fn = streamSize;
    info->close_fn = streamClose;
    return 0;
}

QString propString(MpvApi &m, mpv_handle *h, const char *name)
{
    char *s = m.get_property_string(h, name);
    if (!s) return {};
    const QString out = QString::fromUtf8(s);
    m.free(s);
    return out;
}

} // namespace

struct PreviewWorker::Instance {
    mpv_handle *handle = nullptr;
    mpv_render_context *render = nullptr;   // nur Rückfallweg
    std::unique_ptr<Wake> wake;
    std::unique_ptr<StreamSource> source;   // lebt bis nach mpv_terminate_destroy
    quint64 media = 0;
    QString path;
    qint64 pixels = 0;       // Videogröße (Breite × Höhe) für die Wahl exakt/Keyframe
    bool transient = false;  // Öffnen vorübergehend fehlgeschlagen (Datei gesperrt) → beim nächsten Auftrag neu versuchen
    bool failed = false;     // Medium hat keine nutzbare Videospur → alle weiteren Aufträge sofort ablehnen
    QString failReason;
};

PreviewWorker::PreviewWorker(QObject *parent) : QThread(parent) {}

PreviewWorker::~PreviewWorker() { shutdown(); }

bool PreviewWorker::mpvAvailable(QString *error)
{
    MpvApi &m = mpvApi();
    if (!m.ok && error) *error = m.error;
    return m.ok;
}

void PreviewWorker::submit(const PreviewJob &job)
{
    {
        std::lock_guard l(m_lock);
        m_job = job;
        m_hasJob = true;
        m_close = false;
    }
    m_cv.notify_all();
    if (!isRunning()) start(QThread::LowPriority);
}

void PreviewWorker::cancelPending()
{
    std::lock_guard l(m_lock);
    m_hasJob = false;
}

void PreviewWorker::closeMedia(int waitMs)
{
    std::unique_lock l(m_lock);
    m_hasJob = false;
    m_close = true;
    m_cv.notify_all();
    if (waitMs > 0 && isRunning())
        m_closedCv.wait_for(l, std::chrono::milliseconds(waitMs), [this] { return !m_fileOpen; });
}

void PreviewWorker::shutdown()
{
    {
        std::lock_guard l(m_lock);
        m_stop = true;
        m_hasJob = false;
    }
    m_cv.notify_all();
    if (isRunning()) wait();
    std::lock_guard l(m_lock);
    m_stop = false;   // kann später neu gestartet werden
    m_close = false;
}

bool PreviewWorker::interrupted()
{
    std::lock_guard l(m_lock);
    return m_stop || m_close;
}

void PreviewWorker::run()
{
    Instance inst;
    for (;;) {
        PreviewJob job;
        {
            std::unique_lock l(m_lock);
            // Leerlauf: Instanz bleibt kurz offen (schnelle nächste Vorschau), dann Datei freigeben
            const auto ready = [this] { return m_stop || m_close || m_hasJob; };
            if (inst.handle) {
                if (!m_cv.wait_for(l, std::chrono::milliseconds(kIdleCloseMs), ready)) {
                    l.unlock();
                    destroyInstance(inst);
                    continue;
                }
            } else {
                m_cv.wait(l, ready);
            }
            if (m_stop) break;
            if (m_close) {
                m_close = false;
                l.unlock();
                destroyInstance(inst);
                continue;
            }
            job = m_job;
            m_hasJob = false;
        }
        if (inst.handle && (inst.media != job.media || inst.path != job.path)) destroyInstance(inst);
        if (inst.failed && inst.media == job.media) {
            emit frameFailed(job.id, job.media, job.index, inst.failReason);
            continue;
        }
        QString reason;
        if (!inst.handle && !openInstance(inst, job, &reason)) {
            if (interrupted()) continue;
            const bool transient = inst.transient;
            destroyInstance(inst);
            if (transient) {   // z. B. Netzlaufwerk sperrt die Datei kurz: kein Urteil über das Medium
                emit frameFailed(job.id, job.media, job.index, reason);
                continue;
            }
            inst.media = job.media;
            inst.path = job.path;
            inst.failed = true;
            inst.failReason = reason;
            emit mediaUnsupported(job.media, reason);
            emit frameFailed(job.id, job.media, job.index, reason);
            continue;
        }
        QImage img = renderAt(inst, job, &reason);
        if (img.isNull() && !inst.render && reason == QLatin1String("screenshot failed") && mpvApi().renderOk) {
            // Diese libmpv liefert mit vo=null kein Bild: auf den Software-Renderer umstellen und neu versuchen
            g_source = 1;
            destroyInstance(inst);
            reason.clear();
            if (openInstance(inst, job, &reason)) img = renderAt(inst, job, &reason);
        }
        if (img.isNull()) {
            emit frameFailed(job.id, job.media, job.index, reason);
            continue;
        }
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG", 80);
        emit frameReady(job.id, job.media, job.index, img, QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64()));
    }
    destroyInstance(inst);
}

// Kleinere Bilder schon im Decoder-Thread: Zwischengröße (doppelte Box), Seitenverhältnis/SAR bleibt erhalten
static QByteArray scaleGraph(const PreviewJob &job)
{
    return QByteArray("scale=w=") + QByteArray::number(qMax(job.boxWidth, job.boxHeight) * 2) + ":h=-2:flags=fast_bilinear";
}

// Bild der Instanz holen: "screenshot-raw video" liefert das aktuelle Videobild (nach Filtern, ohne OSD/Untertitel,
// mit Anzeige-Seitenverhältnis), dann in die Box einpassen. Nie über die Videogröße hinaus vergrößern.
static QImage grabCurrent(MpvApi &m, mpv_handle *h, int boxW, int boxH, QString *reason)
{
    const char *args[] = {"screenshot-raw", "video", "bgra", nullptr};
    mpv_node result{};
    if (m.command_ret(h, args, &result) < 0) { *reason = QStringLiteral("screenshot failed"); return {}; }
    QImage img;
    if (result.format == MPV_FORMAT_NODE_MAP) {
        qint64 w = 0, hh = 0, stride = 0;
        QByteArray fmt;
        const mpv_byte_array *data = nullptr;
        for (int i = 0; i < result.u.list->num; ++i) {
            const char *k = result.u.list->keys[i];
            const mpv_node &v = result.u.list->values[i];
            if (!std::strcmp(k, "w") && v.format == MPV_FORMAT_INT64) w = v.u.int64;
            else if (!std::strcmp(k, "h") && v.format == MPV_FORMAT_INT64) hh = v.u.int64;
            else if (!std::strcmp(k, "stride") && v.format == MPV_FORMAT_INT64) stride = v.u.int64;
            else if (!std::strcmp(k, "format") && v.format == MPV_FORMAT_STRING) fmt = v.u.string;
            else if (!std::strcmp(k, "data") && v.format == MPV_FORMAT_BYTE_ARRAY) data = v.u.ba;
        }
        if (data && w > 0 && hh > 0 && stride >= w * 4 && qint64(data->size) >= stride * hh && fmt == "bgra") {
            const QImage view(static_cast<const uchar *>(data->data), int(w), int(hh), int(stride), QImage::Format_RGB32);
            const double scale = qMin(1.0, qMin(double(boxW) / double(w), double(boxH) / double(hh)));
            const QSize size(qMax(2, int(w * scale + 0.5)), qMax(2, int(hh * scale + 0.5)));
            // Eigene Kopie: mpv gibt seinen Puffer gleich danach frei
            img = size == view.size() ? view.copy() : view.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    }
    m.free_node_contents(&result);
    if (img.isNull()) *reason = QStringLiteral("no picture");
    return img;
}

// Rückfallweg: aktuelles Bild über den Software-Renderer der Render-API in die Box rendern
static QImage renderCurrent(MpvApi &m, mpv_handle *h, mpv_render_context *rc, int boxW, int boxH, QString *reason)
{
    int64_t dw = 0, dh = 0;
    if (m.get_property(h, "video-out-params/dw", MPV_FORMAT_INT64, &dw) < 0
        || m.get_property(h, "video-out-params/dh", MPV_FORMAT_INT64, &dh) < 0 || dw <= 0 || dh <= 0) {
        *reason = QStringLiteral("no video size");
        return {};
    }
    // Rückfallweg läuft mit video-rotate=no (der Software-Renderer von libmpv 0.37 bricht bei Drehung ab):
    // in der Ausrichtung des Containers rendern und das kleine Bild danach in Qt drehen
    int64_t rotate = 0;   // Drehung laut Container (video-params/rotate ist bei video-rotate=no 0)
    if (m.get_property(h, "current-tracks/video/demux-rotation", MPV_FORMAT_INT64, &rotate) < 0)
        m.get_property(h, "video-params/rotate", MPV_FORMAT_INT64, &rotate);
    rotate = ((rotate % 360) + 360) % 360;
    if (rotate % 180 == 90) std::swap(boxW, boxH);
    const double scale = qMin(1.0, qMin(double(boxW) / double(dw), double(boxH) / double(dh)));
    const int w = qMax(2, int(dw * scale + 0.5));
    const int hh = qMax(2, int(dh * scale + 0.5));
    QImage img(w, hh, QImage::Format_RGBX8888);
    if (img.isNull()) { *reason = QStringLiteral("out of memory"); return {}; }
    size_t stride = ((size_t(w) * 4 + 63) / 64) * 64;   // 64-Byte-Ausrichtung (Empfehlung der Render-API)
    std::unique_ptr<uint8_t[]> raw(new uint8_t[stride * size_t(hh) + 64]);
    auto *buf = reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(raw.get()) + 63) & ~uintptr_t(63));
    int size[2] = {w, hh};
    char fmt[] = "rgb0";
    int noBlock = 0;
    mpv_render_param rp[] = {
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &noBlock},
        {MPV_RENDER_PARAM_SW_SIZE, size},
        {MPV_RENDER_PARAM_SW_FORMAT, fmt},
        {MPV_RENDER_PARAM_SW_STRIDE, &stride},
        {MPV_RENDER_PARAM_SW_POINTER, buf},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    if (m.rc_render(rc, rp) < 0) { *reason = QStringLiteral("render failed"); return {}; }
    for (int y = 0; y < hh; ++y) {
        uint8_t *dst = img.scanLine(y);
        std::memcpy(dst, buf + size_t(y) * stride, size_t(w) * 4);
        for (int x = 0; x < w; ++x) dst[x * 4 + 3] = 0xff;
    }
    if (rotate) img = img.transformed(QTransform().rotate(double(rotate)));   // mpv: im Uhrzeigersinn
    return img;
}

bool PreviewWorker::openInstance(Instance &inst, const PreviewJob &job, QString *reason)
{
    MpvApi &m = mpvApi();
    if (!m.ok) { *reason = m.error; return false; }
    if (qEnvironmentVariable("UVEPREVIEW_SOURCE") == QLatin1String("render")) g_source = 1;   // Test des Rückfallwegs
    inst.media = job.media;
    inst.path = job.path;
    inst.failed = false;
    if (qEnvironmentVariableIsSet("UVEPREVIEW_DEBUG"))
        qDebug("preview open %s (%s)", qPrintable(QFileInfo(job.path).fileName()), g_source == 1 ? "render api" : "screenshot");
    inst.handle = m.create();
    if (!inst.handle) { *reason = QStringLiteral("mpv_create failed"); return false; }
    {
        std::lock_guard l(m_lock);
        m_fileOpen = true;
    }

    // Unabhängige, stille Instanz ohne Ausgabe (vo=null): keine Konfiguration, Skripte, Audio, Untertitel,
    // Netzwerk oder Fortsetzen. Software-Dekodierung mit zwei Threads, schneller Skalierer, kaum Vorauslesen.
    // Rückfallweg ohne Vorskalierung: der Software-Renderer skaliert selbst (und libmpv 0.37 bricht dort mit
    // vorskalierten Bildern in mp_image_crop ab)
    const QByteArray vf = g_source == 1 ? QByteArray() : scaleGraph(job);
    const char *opts[][2] = {
        {"config", "no"}, {"terminal", "no"}, {"msg-level", "all=no"}, {"load-scripts", "no"}, {"osc", "no"},
        {"ytdl", "no"}, {"input-default-bindings", "no"}, {"input-vo-keyboard", "no"}, {"load-stats-overlay", "no"},
        {"vo", g_source == 1 ? "libmpv" : "null"}, {"ao", "null"}, {"audio", "no"}, {"sid", "no"}, {"sub-auto", "no"}, {"audio-file-auto", "no"},
        {"cover-art-auto", "no"}, {"pause", "yes"}, {"keep-open", "always"}, {"idle", "yes"}, {"hwdec", "no"},
        {"vd-lavc-threads", "2"}, {"vd-lavc-skiploopfilter", "all"}, {"vd-lavc-fast", "yes"},
        {"sws-scaler", "fast-bilinear"}, {"hr-seek-framedrop", "yes"}, {"cache", "no"}, {"demuxer-readahead-secs", "0"},
        {"demuxer-max-bytes", "8MiB"}, {"demuxer-max-back-bytes", "0"}, {"resume-playback", "no"},
        {"save-position-on-quit", "no"}, {"image-display-duration", "inf"}, {"really-quiet", "yes"},
    };
    for (auto &o : opts) m.set_option_string(inst.handle, o[0], o[1]);
    if (!vf.isEmpty()) m.set_option_string(inst.handle, "vf", vf.constData());
    if (g_source == 1) m.set_option_string(inst.handle, "video-rotate", "no");
    if (m.initialize(inst.handle) < 0) { *reason = QStringLiteral("mpv_initialize failed"); return false; }

    inst.wake = std::make_unique<Wake>();
    m.set_wakeup_callback(inst.handle, onWakeup, inst.wake.get());
    if (g_source == 1) {
        mpv_render_param cparams[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW)},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        if (m.rc_create(&inst.render, inst.handle, cparams) < 0) {
            inst.render = nullptr;
            *reason = QStringLiteral("software renderer not available");
            return false;
        }
        m.rc_set_update_callback(inst.render, onWakeup, inst.wake.get());
    }

    // Datei selbst öffnen (QFile: Qt-Pfadlogik für Leerzeichen, Unicode, lange Pfade) und über uvepreview://
    // an mpv geben; der Dateiname im Pfad hilft nur bei der Formaterkennung
    inst.source = std::make_unique<StreamSource>();
    inst.source->path = QFileInfo(job.path).absoluteFilePath();
    if (m.stream_cb_add_ro(inst.handle, "uvepreview", inst.source.get(), streamOpen) < 0) {
        *reason = QStringLiteral("stream callback not available");
        return false;
    }
    const QByteArray path = "uvepreview://" + QUrl::toPercentEncoding(QFileInfo(job.path).fileName());
    const char *cmd[] = {"loadfile", path.constData(), nullptr};
    if (m.command(inst.handle, cmd) < 0) { *reason = QStringLiteral("loadfile failed"); return false; }

    QElapsedTimer t;
    t.start();
    bool loaded = false, ready = false;
    while (!ready && t.elapsed() < kOpenLimitMs) {
        if (interrupted()) { *reason = QStringLiteral("cancelled"); return false; }
        for (;;) {
            mpv_event *ev = m.wait_event(inst.handle, 0);
            if (ev->event_id == MPV_EVENT_NONE) break;
            if (ev->event_id == MPV_EVENT_SHUTDOWN) { *reason = QStringLiteral("mpv shut down"); return false; }
            if (ev->event_id == MPV_EVENT_END_FILE) {
                auto *ef = static_cast<mpv_event_end_file *>(ev->data);
                if (ef->reason != MPV_END_FILE_REASON_ERROR) continue;
                if (ef->error == MPV_ERROR_NOTHING_TO_PLAY || ef->error == MPV_ERROR_UNKNOWN_FORMAT) {
                    *reason = QStringLiteral("no playable video"); return false;
                }
                // Öffnen fehlgeschlagen: Netzlaufwerke (z. B. RaiDrive/WinFsp) verweigern einen zweiten Zugriff,
                // solange sie die gerade geladene Datei noch zwischenspeichern → kurz warten, erneut laden
                if (t.elapsed() > kOpenLimitMs - 1500) {
                    inst.transient = true;
                    *reason = QStringLiteral("file busy");
                    return false;
                }
                QElapsedTimer pause;
                pause.start();
                while (pause.elapsed() < 700) {
                    if (interrupted()) { *reason = QStringLiteral("cancelled"); return false; }
                    inst.wake->wait(50);
                }
                if (m.command(inst.handle, cmd) < 0) { *reason = QStringLiteral("loadfile failed"); return false; }
                loaded = false;
                if (qEnvironmentVariableIsSet("UVEPREVIEW_DEBUG")) qDebug("preview open retry after %lld ms", (long long)t.elapsed());
            }
            if (ev->event_id == MPV_EVENT_FILE_LOADED) {
                loaded = true;
                // Keine Videospur oder nur ein Cover-Bild (Audiodatei) → keine Bildvorschau
                int64_t vid = 0;
                if (m.get_property(inst.handle, "vid", MPV_FORMAT_INT64, &vid) < 0 || vid <= 0) {
                    *reason = QStringLiteral("no video track"); return false;
                }
                int flag = 0;
                if (m.get_property(inst.handle, "current-tracks/video/albumart", MPV_FORMAT_FLAG, &flag) >= 0 && flag) {
                    *reason = QStringLiteral("cover art only"); return false;
                }
                flag = 0;
                if (m.get_property(inst.handle, "current-tracks/video/image", MPV_FORMAT_FLAG, &flag) >= 0 && flag) {
                    *reason = QStringLiteral("still image"); return false;
                }
            }
            if (ev->event_id == MPV_EVENT_PLAYBACK_RESTART && loaded && !inst.render) ready = true;   // erstes Bild dekodiert
        }
        if (inst.render && loaded && (m.rc_update(inst.render) & MPV_RENDER_UPDATE_FRAME)) {
            renderCurrent(m, inst.handle, inst.render, job.boxWidth, job.boxHeight, reason);   // erstes Bild abholen, gibt die VO frei (Boxgröße: winzige Ziele lösen in libmpv 0.37 eine Assertion aus)
            reason->clear();
            ready = true;
        }
        if (!ready) inst.wake->wait(50);
    }
    if (!ready) { *reason = loaded ? QStringLiteral("no frame decoded") : QStringLiteral("timeout opening file"); return false; }

    int64_t vw = 0, vh = 0;
    m.get_property(inst.handle, "video-params/w", MPV_FORMAT_INT64, &vw);
    m.get_property(inst.handle, "video-params/h", MPV_FORMAT_INT64, &vh);
    inst.pixels = vw * vh;

    // HDR (PQ/HLG): mit einem kleinen Tonemapping-Filter in sinnvolles SDR bringen. Fehlt zscale in der libmpv,
    // bleibt es bei der reinen Farbraum-Umrechnung des Screenshots.
    const QString gamma = propString(m, inst.handle, "video-params/gamma");
    if (gamma == QLatin1String("pq") || gamma == QLatin1String("hlg")) {
        const QByteArray graph = "lavfi=[" + (vf.isEmpty() ? QByteArray() : vf + ",") + "zscale=t=linear:npl=100,format=gbrpf32le,zscale=p=bt709,"
                                 "tonemap=tonemap=hable:desat=0,zscale=t=bt709:m=bt709:r=tv,format=yuv420p]";
        const char *setVf[] = {"vf", "set", graph.constData(), nullptr};
        const int r = m.command(inst.handle, setVf);
        if (qEnvironmentVariableIsSet("UVEPREVIEW_DEBUG")) qDebug("preview HDR (%s) tonemap filter: %d", qPrintable(gamma), r);
    }
    return true;
}

QImage PreviewWorker::renderAt(Instance &inst, const PreviewJob &job, QString *reason)
{
    MpvApi &m = mpvApi();
    const QByteArray pos = QByteArray::number(qMax(0.0, job.time), 'f', 3);
    // Exakt bis 1920×1200; größere Videos (4K, lange GOPs: ~0,5 s pro exaktem Sprung) nur bei kurzer Länge exakt,
    // sonst Keyframe (~60 ms) – die Vorschau soll dem Zeiger folgen
    const bool exact = job.exact && (inst.pixels <= 1920 * 1200 || job.duration <= 60);
    const char *cmd[] = {"seek", pos.constData(), exact ? "absolute+exact" : "absolute+keyframes", nullptr};
    // Liegengebliebene Ereignisse (z. B. PLAYBACK_RESTART vom Öffnen) verwerfen – nur der Restart nach
    // diesem Sprung zählt
    while (m.wait_event(inst.handle, 0)->event_id != MPV_EVENT_NONE) {}
    if (inst.render) m.rc_update(inst.render);
    if (m.command(inst.handle, cmd) < 0) { *reason = QStringLiteral("seek failed"); return {}; }

    QElapsedTimer t;
    t.start();
    bool seeking = false, restarted = false;
    if (inst.render) {
        // Rückfallweg: neue Bilder laufend abholen (die VO wartet sonst bis zu 200 ms darauf), das endgültige Bild
        // aber erst nach PLAYBACK_RESTART rendern – ältere libmpv meldet direkt nach dem SEEK noch das alte Bild
        while (!restarted && t.elapsed() < kSeekLimitMs) {
            if (interrupted()) { *reason = QStringLiteral("cancelled"); return {}; }
            for (;;) {
                mpv_event *ev = m.wait_event(inst.handle, 0);
                if (ev->event_id == MPV_EVENT_NONE) break;
                if (ev->event_id == MPV_EVENT_SHUTDOWN) { *reason = QStringLiteral("mpv shut down"); return {}; }
                if (ev->event_id == MPV_EVENT_SEEK) seeking = true;
                if (ev->event_id == MPV_EVENT_PLAYBACK_RESTART && seeking) restarted = true;
            }
            if (!restarted && (m.rc_update(inst.render) & MPV_RENDER_UPDATE_FRAME))
                renderCurrent(m, inst.handle, inst.render, job.boxWidth, job.boxHeight, reason);
            if (!restarted) inst.wake->wait(10);
        }
        if (!restarted) { *reason = QStringLiteral("seek timeout"); return {}; }
        reason->clear();
        m.rc_update(inst.render);
        return renderCurrent(m, inst.handle, inst.render, job.boxWidth, job.boxHeight, reason);
    }
    while (!restarted && t.elapsed() < kSeekLimitMs) {
        if (interrupted()) { *reason = QStringLiteral("cancelled"); return {}; }
        for (;;) {
            mpv_event *ev = m.wait_event(inst.handle, 0);
            if (ev->event_id == MPV_EVENT_NONE) break;
            if (ev->event_id == MPV_EVENT_SHUTDOWN) { *reason = QStringLiteral("mpv shut down"); return {}; }
            if (ev->event_id == MPV_EVENT_SEEK) seeking = true;
            if (ev->event_id == MPV_EVENT_PLAYBACK_RESTART && seeking) restarted = true;
        }
        if (!restarted) inst.wake->wait(10);
    }
    if (!restarted) { *reason = QStringLiteral("seek timeout"); return {}; }
    const QImage img = grabCurrent(m, inst.handle, job.boxWidth, job.boxHeight, reason);
    if (qEnvironmentVariableIsSet("UVEPREVIEW_DEBUG")) {
        double tp = -1;
        m.get_property(inst.handle, "time-pos", MPV_FORMAT_DOUBLE, &tp);
        qDebug("preview seek %s -> time-pos %.3f after %lld ms (%dx%d)", pos.constData(), tp, (long long)t.elapsed(), img.width(), img.height());
    }
    return img;
}

void PreviewWorker::destroyInstance(Instance &inst)
{
    MpvApi &m = mpvApi();
    if (inst.render) m.rc_free(inst.render);   // vor mpv_terminate_destroy (Vorgabe der Render-API)
    if (inst.handle) {
        m.set_wakeup_callback(inst.handle, nullptr, nullptr);
        m.terminate_destroy(inst.handle);   // schließt auch die Datei (streamClose)
    }
    inst = Instance{};
    {
        std::lock_guard l(m_lock);
        m_fileOpen = false;
    }
    m_closedCv.notify_all();
}

} // namespace uve::preview
