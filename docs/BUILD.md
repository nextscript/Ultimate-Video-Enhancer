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
# Plugin windows, settings pages and removal: copy plugins\examples\modaldemo to <data folder>\plugins first
$env:UVE_SELFTEST_UI=1; $env:UVE_SELFTEST_ONLY="plugins"; .\build\bin\UVEPlayer.exe --selftest C:\temp\plugins.json tests\media\test-legacy.avi
# Language switching: all language pairs, every menu title checked by its pixels (exit code 1 on failure)
$env:UVE_SELFTEST_UI=1; $env:UVE_SELFTEST_ONLY="language"; .\build\bin\UVEPlayer.exe --selftest C:\temp\lang.json tests\media\test-legacy.avi
```

## Optical media (1.1.8)
| Platform | Disc support comes from | Notes |
|---|---|---|
| Windows | bundled shinchiro libmpv (contains libdvdnav/libdvdread and libbluray; no libcdio) + UVE's own CD reader | no extra DLLs, package size unchanged |
| Linux | the distribution's libmpv (CI: Ubuntu `libmpv-dev`) | DVD/Blu-ray/CDDA and menus depend on that build; *Disc information* shows what is present |
| macOS | Homebrew `mpv` | same; not verified |

- BD-J: `-DUVE_WITH_BDJ=ON` only makes the player look for `libbluray-j2se-*.jar` and Java next to the program; the release does not
  ship them (size, licence review). Nothing is downloaded at runtime.
- DVD test disc (dev dependency, not shipped): `tests/fixtures/make-dvd-fixture.sh <out>` needs ffmpeg, dvdauthor/spumux and genisoimage
  (e.g. in WSL Ubuntu without root: `apt-get download dvdauthor libdvdread8t64 genisoimage`, then `dpkg -x`). Then
  `$env:UVE_TEST_DVD_DIR="<out>"; ctest -R optical`. Without it the DVD cases of `tst_optical_playback` are skipped.
- Audio CD without hardware: `UVE_TEST_FAKE_CD=1` adds a synthetic test CD ("UVE Test-CD (synthetisch)", sine tones) to the drive list.

## Release
`powershell -ExecutionPolicy Bypass -File scripts\package.ps1` → `release\`: setup EXE, portable EXE, portable ZIP, `SHA256SUMS.txt`.
The script aborts if a test fails.

## Regenerating the UVE reference values
`node scripts/uve-reference.mjs <uve.user.js> tests/reference/uve_reference.json` – the UVE 1.17.5 userscript is no longer in this repository (the Electron version `legacy-electron/` was removed); take it from the Ultimate Video Enhancer release.
