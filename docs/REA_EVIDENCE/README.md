# REA-Belege

Diese Dateien enthalten Auszüge aus echten REA-Aufrufen dieser Sitzung (REA-MCP-Server, Tools mit Präfix `mcp__rea__`).
Vollständige Rohausgaben wurden nicht gespeichert; die hier festgehaltenen Werte (Evidence-ID, SHA-256, Befunde)
sind wörtlich aus den Tool-Antworten übernommen.

| Datei | Werkzeug | Ziel |
|---|---|---|
| [vlc_pe_resources.md](vlc_pe_resources.md) | `inspect_pe_resources` | `C:\Program Files\VideoLAN\VLC\vlc.exe` (VLC 3.0.24) |
| [vlc_binary_overview_failure.md](vlc_binary_overview_failure.md) | `open_binary`, `binary_overview` | `vlc.exe` – Deep-Provider nicht verfügbar |
| [libmpv_pe_resources.md](libmpv_pe_resources.md) | `inspect_pe_resources` | `deps\mpv\libmpv-2.dll` (Build 20261102) |
| [legacy_electron.md](legacy_electron.md) | `inspect_electron_fuses`, `analyze_javascript_application` | bisherige Electron-Version (`legacy-electron`) |
| [pe_imports_exports.md](pe_imports_exports.md) | ergänzend: `pefile` (Python) | Import-/Exporttabellen von vlc.exe, libvlc.dll, Qt-/D3D11-Plugin, libmpv-2.dll |
