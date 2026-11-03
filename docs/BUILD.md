# Building (Windows x64)

## Requirements
| Component | Tested version | Source |
|---|---|---|
| Visual Studio 2026 (MSVC 19.51) | 18.7.2 | Workload "Desktop development with C++" |
| CMake | 4.4.3 | cmake.org |
| Qt (MSVC 2022 64-bit) | 6.12.0 | `python -I scripts/fetch-qt.py 6.12.0 M:/LAPS/Qt` (official Qt repository, SHA-1 verified) |
| libmpv (developer package) | shinchiro 20261102 (mpv v0.41.0-1386) | `deps/mpv/` (`include/`, `libmpv-2.dll`, `mpv.def`, `mpv.lib`, `version.json`) |
| yt-dlp | 2026.10.26 | `deps/tools/yt-dlp/` (SHA-256 verified against `SHA2-256SUMS`) |
| 7zr | 26.05 | `deps/tools/7zr.exe` (GitHub ip7z/7zip) – used by the libmpv updater |
| NSIS | 3.0.4.1 | for the installer / portable EXE |

`mpv.lib` is generated from the actual DLL exports:
```
python -I -c "import pefile; …"   (export list → deps/mpv/mpv.def)
lib /def:deps\mpv\mpv.def /machine:x64 /out:deps\mpv\mpv.lib
```

## Build and tests
```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=M:/LAPS/Qt/6.12.0/msvc2022_64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```
To run from `build\bin`, execute `windeployqt --release --qmldir app\qml --qmldir plugins\uve\qml build\bin\UVEPlayer.exe` once.

## Self-tests
```powershell
# Playback self-test (separate data folder, JSON result + screenshot)
$env:UVE_DATA_DIR="C:\temp\uve-test"; .\build\bin\UVEPlayer.exe --selftest C:\temp\result.json file1.mkv https://…
# UI self-test with real mouse events
$env:UVE_SELFTEST_UI=1; .\build\bin\UVEPlayer.exe --selftest C:\temp\ui.json tests\media\test-legacy.avi tests\media\test-2audio-sub.mkv
```

## Release
`powershell -ExecutionPolicy Bypass -File scripts\package.ps1` → `release\`: setup EXE, portable EXE, portable ZIP, `SHA256SUMS.txt`.
The script aborts if a test fails.

## Regenerating the UVE reference values
`node scripts/uve-reference.mjs legacy-electron/plugins/uve/uve.user.js tests/reference/uve_reference.json`
