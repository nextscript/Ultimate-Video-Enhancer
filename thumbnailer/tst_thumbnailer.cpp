// Prüft UVEThumbnailer.dll wie der Explorer: DllGetClassObject → IInitializeWithStream → GetThumbnail.
//   tst_thumbnailer <UVEThumbnailer.dll> <video> [out.bmp]
// Drei Durchläufe: Dateistream (Pfad über IStream::Stat), Speicherstream (mpv liest über stream_cb) und
// ein an das STA gebundener Stream wie im Explorer-Host: jeder Zugriff von einem fremden Thread zählt als
// Fehler (früher Deadlock in mpv_terminate_destroy, der den Explorer bei „Öffnen mit“ hängen ließ).
#include <windows.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

static const CLSID CLSID_UveThumbnailProvider = {0x4d8c74fb, 0x9580, 0x421b, {0x92, 0x21, 0x9d, 0x77, 0xdf, 0xfd, 0x74, 0x50}};

static bool saveBmp(HBITMAP bmp, const wchar_t *path)
{
    DIBSECTION ds{};
    if (!GetObjectW(bmp, sizeof ds, &ds) || !ds.dsBm.bmBits) return false;
    BITMAPINFOHEADER bih = ds.dsBmih;
    bih.biHeight = -std::abs(bih.biHeight);
    DWORD imgSize = ds.dsBm.bmWidthBytes * ds.dsBm.bmHeight;
    BITMAPFILEHEADER bfh{0x4d42, DWORD(sizeof bfh + sizeof bih + imgSize), 0, 0, DWORD(sizeof bfh + sizeof bih)};
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    WriteFile(f, &bfh, sizeof bfh, &w, nullptr);
    WriteFile(f, &bih, sizeof bih, &w, nullptr);
    WriteFile(f, ds.dsBm.bmBits, imgSize, &w, nullptr);
    CloseHandle(f);
    return true;
}

// IStream-Hülle, die nur auf ihrem Erzeuger-Thread benutzt werden darf (wie ein Proxy im STA)
class StaBoundStream final : public IStream {
public:
    explicit StaBoundStream(IStream *inner) : m_inner(inner), m_owner(GetCurrentThreadId()) { m_inner->AddRef(); }
    ~StaBoundStream() { m_inner->Release(); }
    long foreignCalls = 0;

    HRESULT check() { if (GetCurrentThreadId() != m_owner) { ++foreignCalls; return RPC_E_WRONG_THREAD; } return S_OK; }
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IStream || riid == IID_ISequentialStream) { *ppv = static_cast<IStream *>(this); AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() override { ULONG r = InterlockedDecrement(&m_ref); if (!r) delete this; return r; }
    IFACEMETHODIMP Read(void *pv, ULONG cb, ULONG *pcb) override { HRESULT h = check(); return FAILED(h) ? h : m_inner->Read(pv, cb, pcb); }
    IFACEMETHODIMP Write(const void *, ULONG, ULONG *) override { return STG_E_ACCESSDENIED; }
    IFACEMETHODIMP Seek(LARGE_INTEGER d, DWORD o, ULARGE_INTEGER *n) override { HRESULT h = check(); return FAILED(h) ? h : m_inner->Seek(d, o, n); }
    IFACEMETHODIMP SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
    IFACEMETHODIMP CopyTo(IStream *, ULARGE_INTEGER, ULARGE_INTEGER *, ULARGE_INTEGER *) override { return E_NOTIMPL; }
    IFACEMETHODIMP Commit(DWORD) override { return E_NOTIMPL; }
    IFACEMETHODIMP Revert() override { return E_NOTIMPL; }
    IFACEMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    IFACEMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    IFACEMETHODIMP Stat(STATSTG *st, DWORD f) override
    {
        HRESULT h = check();
        if (FAILED(h)) return h;
        h = m_inner->Stat(st, f | STATFLAG_NONAME);   // kein Pfad → Zugriff nur über den Stream
        if (SUCCEEDED(h)) st->pwcsName = nullptr;
        return h;
    }
    IFACEMETHODIMP Clone(IStream **) override { return E_NOTIMPL; }

private:
    IStream *m_inner;
    DWORD m_owner;
    LONG m_ref = 1;
};

static int run(IClassFactory *cf, IStream *stream, const char *label, const wchar_t *out)
{
    IInitializeWithStream *init = nullptr;
    if (FAILED(cf->CreateInstance(nullptr, IID_PPV_ARGS(&init)))) { std::printf("FAIL %s: CreateInstance\n", label); return 1; }
    IThumbnailProvider *tp = nullptr;
    init->QueryInterface(IID_PPV_ARGS(&tp));
    HRESULT hr = init->Initialize(stream, STGM_READ);
    HBITMAP bmp = nullptr;
    WTS_ALPHATYPE at{};
    ULONGLONG t0 = GetTickCount64();
    if (SUCCEEDED(hr)) hr = tp->GetThumbnail(256, &bmp, &at);
    ULONGLONG ms = GetTickCount64() - t0;
    int rc = 1;
    if (SUCCEEDED(hr) && bmp) {
        BITMAP bm{};
        GetObjectW(bmp, sizeof bm, &bm);
        // Bild darf nicht vollständig schwarz sein
        const auto *px = static_cast<const unsigned char *>(bm.bmBits);
        long sum = 0;
        for (long i = 0; px && i < long(bm.bmWidthBytes) * bm.bmHeight; i += 4) sum += px[i] + px[i + 1] + px[i + 2];
        bool ok = bm.bmWidth > 0 && bm.bmHeight > 0 && (bm.bmWidth == 256 || bm.bmHeight == 256) && sum > 0;
        std::printf("%s %s: %ldx%ld in %llu ms\n", ok ? "PASS" : "FAIL", label, bm.bmWidth, bm.bmHeight, ms);
        if (out) saveBmp(bmp, out);
        DeleteObject(bmp);
        rc = ok ? 0 : 1;
    } else {
        std::printf("FAIL %s: hr=0x%08lx after %llu ms\n", label, static_cast<unsigned long>(hr), ms);
    }
    tp->Release();
    init->Release();
    return rc;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 3) { std::printf("usage: tst_thumbnailer <dll> <video> [out.bmp]\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // Explorer ruft Thumbnail-Handler im STA auf
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) { std::printf("FAIL: DLL not loadable (%lu)\n", GetLastError()); return 1; }
    auto getCO = reinterpret_cast<HRESULT(STDAPICALLTYPE *)(REFCLSID, REFIID, void **)>(GetProcAddress(dll, "DllGetClassObject"));
    IClassFactory *cf = nullptr;
    if (!getCO || FAILED(getCO(CLSID_UveThumbnailProvider, IID_PPV_ARGS(&cf)))) { std::printf("FAIL: DllGetClassObject\n"); return 1; }

    int failures = 0;
    IStream *fs = nullptr;
    if (FAILED(SHCreateStreamOnFileEx(argv[2], STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &fs))) {
        std::printf("FAIL: cannot open video\n");
        return 1;
    }
    failures += run(cf, fs, "file stream", argc > 3 ? argv[3] : nullptr);

    // Speicherstream ohne Namen → Zugriff ausschließlich über IStream
    STATSTG st{};
    fs->Stat(&st, STATFLAG_NONAME);
    std::vector<BYTE> data(static_cast<size_t>(st.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    fs->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG got = 0;
    fs->Read(data.data(), static_cast<ULONG>(data.size()), &got);
    fs->Release();
    IStream *ms = SHCreateMemStream(data.data(), got);
    failures += run(cf, ms, "memory stream", nullptr);

    // An das STA gebundener Stream (Explorer-Host): Zugriffe nur auf diesem Thread erlaubt
    auto *sta = new StaBoundStream(ms);
    ULONGLONG t0 = GetTickCount64();
    failures += run(cf, sta, "STA-bound stream", nullptr);
    if (sta->foreignCalls) { std::printf("FAIL STA-bound stream: %ld calls from other threads\n", sta->foreignCalls); ++failures; }
    if (GetTickCount64() - t0 > 20000) { std::printf("FAIL STA-bound stream: took too long\n"); ++failures; }
    sta->Release();
    ms->Release();

    cf->Release();
    CoUninitialize();
    return failures ? 1 : 0;
}
