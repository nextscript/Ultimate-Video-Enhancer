// UVE Player – Explorer-Thumbnail-Provider (IThumbnailProvider) für Videodateien.
//
// Wird nur genutzt, wenn UVE Player die Standard-App eines Videoformats ist: der Handler hängt am
// ProgID "UVEPlayer.Video" (Software\Classes\UVEPlayer.Video\ShellEx\{e357fccd-...}).
// Ein Vorschaubild wird mit der mitgelieferten libmpv (mpv\libmpv-2.dll neben dieser DLL) erzeugt:
// Software-Renderer der Render-API, Bild bei ~10 % der Laufzeit, keine Audio-/Untertitelspuren.
// Läuft in der isolierten Explorer-Thumbnail-Umgebung (dllhost); Dateizugriff über IInitializeWithStream.
// Bewusst ohne Qt – die DLL wird in fremde Prozesse geladen.

#include <windows.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <objidl.h>
#include <new>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <malloc.h>

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/stream_cb.h>

// {4D8C74FB-9580-421B-9221-9D77DFFD7450}
static const CLSID CLSID_UveThumbnailProvider = {0x4d8c74fb, 0x9580, 0x421b, {0x92, 0x21, 0x9d, 0x77, 0xdf, 0xfd, 0x74, 0x50}};

static HMODULE g_module = nullptr;
static std::atomic<long> g_objects{0};
static std::atomic<long> g_locks{0};

// ------------------------------------------------------------------ libmpv dynamisch laden
namespace {
struct MpvApi {
    decltype(&mpv_create) create = nullptr;
    decltype(&mpv_initialize) initialize = nullptr;
    decltype(&mpv_terminate_destroy) terminate_destroy = nullptr;
    decltype(&mpv_set_option_string) set_option_string = nullptr;
    decltype(&mpv_command) command = nullptr;
    decltype(&mpv_get_property) get_property = nullptr;
    decltype(&mpv_wait_event) wait_event = nullptr;
    decltype(&mpv_set_wakeup_callback) set_wakeup_callback = nullptr;
    decltype(&mpv_stream_cb_add_ro) stream_cb_add_ro = nullptr;
    decltype(&mpv_render_context_create) rc_create = nullptr;
    decltype(&mpv_render_context_set_update_callback) rc_set_update_callback = nullptr;
    decltype(&mpv_render_context_update) rc_update = nullptr;
    decltype(&mpv_render_context_render) rc_render = nullptr;
    decltype(&mpv_render_context_free) rc_free = nullptr;
    bool ok = false;
};

MpvApi &mpvApi()
{
    static MpvApi api;
    static std::once_flag once;
    std::call_once(once, [] {
        wchar_t path[MAX_PATH * 2] = {};
        if (!GetModuleFileNameW(g_module, path, MAX_PATH * 2)) return;
        PathRemoveFileSpecW(path);
        std::wstring dll = std::wstring(path) + L"\\mpv\\libmpv-2.dll";
        HMODULE h = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!h) return;
        auto get = [h](auto &fn, const char *name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(h, name));
            return fn != nullptr;
        };
        api.ok = get(api.create, "mpv_create") && get(api.initialize, "mpv_initialize")
              && get(api.terminate_destroy, "mpv_terminate_destroy") && get(api.set_option_string, "mpv_set_option_string")
              && get(api.command, "mpv_command") && get(api.get_property, "mpv_get_property")
              && get(api.wait_event, "mpv_wait_event") && get(api.set_wakeup_callback, "mpv_set_wakeup_callback")
              && get(api.stream_cb_add_ro, "mpv_stream_cb_add_ro") && get(api.rc_create, "mpv_render_context_create")
              && get(api.rc_set_update_callback, "mpv_render_context_set_update_callback")
              && get(api.rc_update, "mpv_render_context_update") && get(api.rc_render, "mpv_render_context_render")
              && get(api.rc_free, "mpv_render_context_free");
        // libmpv bleibt geladen (eigene Threads können den Aufruf überdauern)
    });
    return api;
}

// ------------------------------------------------------------------ IStream → mpv stream_cb
// mpv liest auf eigenen Threads; der IStream wird über die Global Interface Table in deren
// Apartment gereicht (direkter Zeiger bei agilen Streams, sonst Proxy).
struct StreamSource {
    IGlobalInterfaceTable *git = nullptr;
    DWORD cookie = 0;
    std::mutex lock;

    IStream *acquire()
    {
        static thread_local bool comInit = false;
        if (!comInit) { CoInitializeEx(nullptr, COINIT_MULTITHREADED); comInit = true; }
        IStream *s = nullptr;
        if (FAILED(git->GetInterfaceFromGlobal(cookie, IID_PPV_ARGS(&s)))) return nullptr;
        return s;
    }
};

int64_t cbRead(void *cookie, char *buf, uint64_t nbytes)
{
    auto *src = static_cast<StreamSource *>(cookie);
    std::lock_guard g(src->lock);
    IStream *s = src->acquire();
    if (!s) return -1;
    ULONG want = static_cast<ULONG>(nbytes > 0x7fffffffu ? 0x7fffffffu : nbytes), got = 0;
    HRESULT hr = s->Read(buf, want, &got);
    s->Release();
    return FAILED(hr) ? -1 : static_cast<int64_t>(got);
}
int64_t cbSeek(void *cookie, int64_t offset)
{
    auto *src = static_cast<StreamSource *>(cookie);
    std::lock_guard g(src->lock);
    IStream *s = src->acquire();
    if (!s) return MPV_ERROR_GENERIC;
    LARGE_INTEGER li; li.QuadPart = offset;
    ULARGE_INTEGER pos{};
    HRESULT hr = s->Seek(li, STREAM_SEEK_SET, &pos);
    s->Release();
    return FAILED(hr) ? MPV_ERROR_GENERIC : static_cast<int64_t>(pos.QuadPart);
}
int64_t cbSize(void *cookie)
{
    auto *src = static_cast<StreamSource *>(cookie);
    std::lock_guard g(src->lock);
    IStream *s = src->acquire();
    if (!s) return MPV_ERROR_UNSUPPORTED;
    STATSTG st{};
    HRESULT hr = s->Stat(&st, STATFLAG_NONAME);
    s->Release();
    return FAILED(hr) ? MPV_ERROR_UNSUPPORTED : static_cast<int64_t>(st.cbSize.QuadPart);
}
void cbClose(void *) {}
int cbOpen(void *userData, char *, mpv_stream_cb_info *info)
{
    auto *src = static_cast<StreamSource *>(userData);
    {   // am Dateianfang beginnen
        std::lock_guard g(src->lock);
        if (IStream *s = src->acquire()) { LARGE_INTEGER z{}; s->Seek(z, STREAM_SEEK_SET, nullptr); s->Release(); }
    }
    info->cookie = src;
    info->read_fn = cbRead;
    info->seek_fn = cbSeek;
    info->size_fn = cbSize;
    info->close_fn = cbClose;
    return 0;
}

std::string toUtf8(const std::wstring &w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// ------------------------------------------------------------------ Bild erzeugen
struct WakeCtx { HANDLE ev; std::atomic<bool> frame{false}; };

void onWakeup(void *p) { SetEvent(static_cast<WakeCtx *>(p)->ev); }
void onRenderUpdate(void *p) { SetEvent(static_cast<WakeCtx *>(p)->ev); }

// Wartet auf das Ereignis; in einem STA werden dabei Nachrichten verarbeitet (IStream-Proxy-Aufrufe)
void waitFor(HANDLE ev, DWORD ms)
{
    DWORD idx = 0;
    if (FAILED(CoWaitForMultipleHandles(0, ms, 1, &ev, &idx))) WaitForSingleObject(ev, ms);
}

HRESULT renderThumbnail(const std::string &url, StreamSource *stream, UINT cx, HBITMAP *outBmp)
{
    MpvApi &m = mpvApi();
    if (!m.ok) return E_FAIL;
    mpv_handle *h = m.create();
    if (!h) return E_OUTOFMEMORY;

    const char *opts[][2] = {
        {"config", "no"}, {"terminal", "no"}, {"msg-level", "all=no"}, {"load-scripts", "no"}, {"osc", "no"},
        {"ytdl", "no"}, {"input-default-bindings", "no"}, {"input-vo-keyboard", "no"},
        {"vo", "libmpv"}, {"ao", "null"}, {"aid", "no"}, {"sid", "no"}, {"sub-auto", "no"}, {"audio-file-auto", "no"},
        {"pause", "yes"}, {"start", "10%"}, {"hr-seek", "no"}, {"hwdec", "no"}, {"keep-open", "yes"},
        {"cache", "no"}, {"demuxer-readahead-secs", "1"}, {"demuxer-max-bytes", "32MiB"},
        {"sw-fast", "yes"}, {"image-display-duration", "inf"}, {"idle", "yes"}, {"resume-playback", "no"},
        {"save-position-on-quit", "no"}, {"network-timeout", "5"},
    };
    for (auto &o : opts) m.set_option_string(h, o[0], o[1]);
    if (m.initialize(h) < 0) { m.terminate_destroy(h); return E_FAIL; }

    WakeCtx wake;
    wake.ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m.set_wakeup_callback(h, onWakeup, &wake);

    mpv_render_param cparams[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW)},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context *rc = nullptr;
    HRESULT result = E_FAIL;
    if (m.rc_create(&rc, h, cparams) >= 0) {
        m.rc_set_update_callback(rc, onRenderUpdate, &wake);
        if (stream) m.stream_cb_add_ro(h, "uvethumb", stream, cbOpen);
        const char *cmd[] = {"loadfile", url.c_str(), nullptr};
        if (m.command(h, cmd) >= 0) {
            const ULONGLONG deadline = GetTickCount64() + 12000;
            bool failed = false, haveFrame = false;
            while (!failed && !haveFrame && GetTickCount64() < deadline) {
                for (;;) {
                    mpv_event *ev = m.wait_event(h, 0);
                    if (ev->event_id == MPV_EVENT_NONE) break;
                    if (ev->event_id == MPV_EVENT_SHUTDOWN) failed = true;
                    if (ev->event_id == MPV_EVENT_END_FILE) {
                        auto *ef = static_cast<mpv_event_end_file *>(ev->data);
                        if (ef->reason == MPV_END_FILE_REASON_ERROR) failed = true;
                    }
                    if (ev->event_id == MPV_EVENT_FILE_LOADED) {
                        int64_t vid = 0;   // keine Videospur (auch kein Cover) → sofort aufgeben
                        if (m.get_property(h, "vid", MPV_FORMAT_INT64, &vid) < 0) failed = true;
                    }
                }
                if (m.rc_update(rc) & MPV_RENDER_UPDATE_FRAME) haveFrame = true;
                if (!haveFrame && !failed) waitFor(wake.ev, 100);
            }
            int64_t dw = 0, dh = 0;
            if (haveFrame && m.get_property(h, "dwidth", MPV_FORMAT_INT64, &dw) >= 0
                && m.get_property(h, "dheight", MPV_FORMAT_INT64, &dh) >= 0 && dw > 0 && dh > 0) {
                // Seitenverhältnis erhalten, längste Seite = cx (nicht vergrößern über die Videogröße)
                double scale = static_cast<double>(cx) / static_cast<double>(dw > dh ? dw : dh);
                if (scale > 1.0) scale = 1.0;
                int w = static_cast<int>(dw * scale + 0.5), hgt = static_cast<int>(dh * scale + 0.5);
                if (w < 1) w = 1;
                if (hgt < 1) hgt = 1;
                size_t stride = ((static_cast<size_t>(w) * 4 + 63) / 64) * 64;
                auto *buf = static_cast<uint8_t *>(_aligned_malloc(stride * hgt + 64, 64));
                if (buf) {
                    int size[2] = {w, hgt};
                    char fmt[] = "bgr0";
                    mpv_render_param rp[] = {
                        {MPV_RENDER_PARAM_SW_SIZE, size},
                        {MPV_RENDER_PARAM_SW_FORMAT, fmt},
                        {MPV_RENDER_PARAM_SW_STRIDE, &stride},
                        {MPV_RENDER_PARAM_SW_POINTER, buf},
                        {MPV_RENDER_PARAM_INVALID, nullptr},
                    };
                    if (m.rc_render(rc, rp) >= 0) {
                        BITMAPINFO bi{};
                        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bi.bmiHeader.biWidth = w;
                        bi.bmiHeader.biHeight = -hgt;   // top-down
                        bi.bmiHeader.biPlanes = 1;
                        bi.bmiHeader.biBitCount = 32;
                        bi.bmiHeader.biCompression = BI_RGB;
                        void *bits = nullptr;
                        HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
                        if (bmp && bits) {
                            for (int y = 0; y < hgt; ++y) {
                                auto *dst = static_cast<uint8_t *>(bits) + static_cast<size_t>(y) * w * 4;
                                std::memcpy(dst, buf + y * stride, static_cast<size_t>(w) * 4);
                                for (int x = 0; x < w; ++x) dst[x * 4 + 3] = 0xff;
                            }
                            *outBmp = bmp;
                            result = S_OK;
                        } else if (bmp) {
                            DeleteObject(bmp);
                        }
                    }
                    _aligned_free(buf);
                }
            }
        }
        m.rc_free(rc);   // vor mpv_terminate_destroy freigeben (Render-API-Vorgabe)
    }
    m.set_wakeup_callback(h, nullptr, nullptr);
    m.terminate_destroy(h);
    CloseHandle(wake.ev);
    return result;
}
} // namespace

// ------------------------------------------------------------------ COM-Objekt
class UveThumbnailProvider final : public IThumbnailProvider, public IInitializeWithStream {
public:
    UveThumbnailProvider() { ++g_objects; }
    ~UveThumbnailProvider()
    {
        if (m_src.git) {
            if (m_src.cookie) m_src.git->RevokeInterfaceFromGlobal(m_src.cookie);
            m_src.git->Release();
        }
        --g_objects;
    }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        static const QITAB qit[] = {
            QITABENT(UveThumbnailProvider, IThumbnailProvider),
            QITABENT(UveThumbnailProvider, IInitializeWithStream),
            {nullptr, 0},
        };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG r = --m_ref;
        if (!r) delete this;
        return r;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream *stream, DWORD) override
    {
        if (!stream) return E_INVALIDARG;
        if (m_src.git) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        // Liefert der Stream einen vollständigen, vorhandenen Pfad, liest mpv die Datei direkt (schneller)
        STATSTG st{};
        if (SUCCEEDED(stream->Stat(&st, STATFLAG_DEFAULT)) && st.pwcsName) {
            std::wstring name = st.pwcsName;
            CoTaskMemFree(st.pwcsName);
            if (name.size() > 2 && (name[1] == L':' || name.rfind(L"\\\\", 0) == 0)
                && GetFileAttributesW(name.c_str()) != INVALID_FILE_ATTRIBUTES)
                m_path = name;
        }
        HRESULT hr = CoCreateInstance(CLSID_StdGlobalInterfaceTable, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_src.git));
        if (FAILED(hr)) return hr;
        return m_src.git->RegisterInterfaceInGlobal(stream, IID_IStream, &m_src.cookie);
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha) override
    {
        if (!phbmp || !pdwAlpha) return E_INVALIDARG;
        *phbmp = nullptr;
        *pdwAlpha = WTSAT_RGB;
        if (!m_src.git) return E_UNEXPECTED;
        if (cx < 16) cx = 16;
        return m_path.empty() ? renderThumbnail("uvethumb://stream", &m_src, cx, phbmp)
                              : renderThumbnail(toUtf8(m_path), nullptr, cx, phbmp);
    }

private:
    std::atomic<ULONG> m_ref{1};
    StreamSource m_src;
    std::wstring m_path;
};

class ClassFactory final : public IClassFactory {
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        static const QITAB qit[] = {QITABENT(ClassFactory, IClassFactory), {nullptr, 0}};
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return 2; }   // statisches Objekt
    IFACEMETHODIMP_(ULONG) Release() override { return 1; }
    IFACEMETHODIMP CreateInstance(IUnknown *outer, REFIID riid, void **ppv) override
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto *p = new (std::nothrow) UveThumbnailProvider();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override
    {
        if (lock) ++g_locks; else --g_locks;
        return S_OK;
    }
};

static ClassFactory g_factory;

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!IsEqualCLSID(clsid, CLSID_UveThumbnailProvider)) return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    // libmpv-Threads können noch laufen → DLL nur entladen, wenn wirklich nichts mehr aktiv ist
    return (g_objects == 0 && g_locks == 0) ? S_OK : S_FALSE;
}
