// Prüft UVEThumbnailer.dll wie der Explorer: DllGetClassObject → IInitializeWithStream → GetThumbnail.
//   tst_thumbnailer <UVEThumbnailer.dll> <video> [out.bmp]
// Zwei Durchläufe: Dateistream (Pfad über IStream::Stat) und Speicherstream (mpv liest über stream_cb).
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
    ms->Release();

    cf->Release();
    CoUninitialize();
    return failures ? 1 : 0;
}
