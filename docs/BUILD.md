# Bauen (Windows x64)

## Voraussetzungen
| Komponente | Getestete Version | Bezug |
|---|---|---|
| Visual Studio 2026 (MSVC 19.51) | 18.7.2 | Workload „Desktopentwicklung mit C++“ |
| CMake | 4.4.3 | cmake.org |
| Qt (MSVC 2022 64-bit) | 6.12.0 | `python -I scripts/fetch-qt.py 6.12.0 M:/LAPS/Qt` (offizielles Qt-Repository, SHA-1-geprüft) |
| libmpv (Entwicklerpaket) | shinchiro 20261102 (mpv v0.41.0-1386) | `deps/mpv/` (`include/`, `libmpv-2.dll`, `mpv.def`, `mpv.lib`, `version.json`) |
| yt-dlp | 2026.10.26 | `deps/tools/yt-dlp/` (SHA-256 gegen `SHA2-256SUMS` geprüft) |
| 7zr | 26.05 | `deps/tools/7zr.exe` (GitHub ip7z/7zip) – für den libmpv-Updater |
| NSIS | 3.0.4.1 | für Installer/portable EXE |

`mpv.lib` wird aus den echten DLL-Exporten erzeugt:
```
python -I -c "import pefile; …"   (Exportliste → deps/mpv/mpv.def)
lib /def:deps\mpv\mpv.def /machine:x64 /out:deps\mpv\mpv.lib
```

## Build und Tests
```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=M:/LAPS/Qt/6.12.0/msvc2022_64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```
Zum Starten aus `build\bin` einmal `windeployqt --release --qmldir app\qml --qmldir plugins\uve\qml build\bin\UVEPlayer.exe` ausführen.

## Selbsttests
```powershell
# Wiedergabe-Selbsttest (eigener Datenordner, JSON-Ergebnis + Screenshot)
$env:UVE_DATA_DIR="C:\temp\uve-test"; .\build\bin\UVEPlayer.exe --selftest C:\temp\ergebnis.json datei1.mkv https://…
# Oberflächen-Selbsttest mit echten Mausereignissen
$env:UVE_SELFTEST_UI=1; .\build\bin\UVEPlayer.exe --selftest C:\temp\ui.json tests\media\test-legacy.avi tests\media\test-2audio-sub.mkv
```

## Release
`powershell -ExecutionPolicy Bypass -File scripts\package.ps1` → `release\`: Setup-EXE, portable EXE, portable ZIP, `SHA256SUMS.txt`.
Das Skript bricht ab, wenn ein Test fehlschlägt.

## UVE-Referenzwerte neu erzeugen
`node scripts/uve-reference.mjs legacy-electron/plugins/uve/uve.user.js tests/reference/uve_reference.json`
