# REA-Analyse

## Status
- **REA-MCP verbunden und tatsächlich verwendet.** Genutzte Tools: `open_binary`, `binary_overview`,
  `inspect_pe_resources`, `inspect_electron_fuses`, `analyze_javascript_application`
  (Namen und Schemas wie vom Server geliefert, über die Tool-Suche geladen).
- **Deep-Provider nicht verfügbar** (Ghidra/IDA nicht konfiguriert, Hopper nicht unter Windows, Kuna lässt PE nicht zu) →
  keine Disassemblierung/Dekompilierung. Ersatz: statische Import-/Exportanalyse mit `pefile` und öffentliche Quellen.
- Alle Analysen waren **read-only und statisch**; keine Laufzeitinjektion, keine Umgehung von Schutzmechanismen.

## Untersuchte Objekte

| Ziel | Version / Pfad | REA-Tool | Evidence | Ergebnis |
|---|---|---|---|---|
| VLC | 3.0.24, `C:\Program Files\VideoLAN\VLC\vlc.exe` | `inspect_pe_resources` | `ev_e335bb82…1063` | Nur Icons/Version/Manifest – Starter ohne UI-Ressourcen |
| VLC | dto. | `open_binary` + `binary_overview` | – | `provider_unavailable` (siehe REA_LIMITATIONS.md) |
| libmpv | Build 20261102, `deps\mpv\libmpv-2.dll` | `inspect_pe_resources` | `ev_a4f8b697…ef89` | PE32+, Version/Manifest; ergänzend 206 Exporte, Client-API 2.6 |
| Electron-Vorgänger | `legacy-electron\release\win-unpacked\UVE Player.exe` | `inspect_electron_fuses` | `ev_88cb1c9a…e8ae` | RunAsNode an, ASAR-Integrität aus |
| Electron-Vorgänger | `resources\app.asar` | `analyze_javascript_application` | `ev_b24cb423…0cb8` | Stream-/MSE-/GM-Shim-Pfade bestätigt (statisch, partial) |
| UVE-Userscript | `legacy-electron\plugins\uve\uve.user.js` (1.17.5) | statische Quellprüfung + `scripts/uve-reference.mjs` | tests/reference/uve_reference.json | Shader-Formeln, Parameter, Standardwerte – siehe UVE_PARITY_MATRIX.md |

Belege: [REA_EVIDENCE/](REA_EVIDENCE/README.md)

## Befunde je Funktion

| Funktion | Ziel / Artefakt | Beobachtung | Beleg | Interpretation | Eigene Umsetzung |
|---|---|---|---|---|---|
| Programmstart | vlc.exe | 7 libvlc-Importe (`new`, `add_intf`, `playlist_play`, `wait`, …), Export nur `WinMain` | pe_imports_exports.md | Starter delegiert alles an die Bibliothek | `app/main.cpp`: Laden der Engine, Einzelinstanz, Safe Mode, Übergabe der Argumente |
| Modularität | VLC `plugins\` | ~420 Module mit einheitlichem Einstieg `vlc_entry__3_0_0f`, Index `plugins.dat` | pe_imports_exports.md | Module werden ohne Ausführung inventarisiert | Plugin-Manifest wird vor dem Laden gelesen (`QPluginLoader::metaData`), versionierte API |
| Oberfläche als Modul | `libqt_plugin.dll` | GUI importiert Kern-API (playlist, var_*, config_*) | pe_imports_exports.md | GUI ist austauschbar | QML-Oberfläche greift nur über `PlayerApp`/`MpvController` zu |
| Videoausgabe | libmpv | Render-API exportiert; OpenGL-Header vorhanden, kein D3D11-Header | libmpv_pe_resources.md | OpenGL ist der verfügbare Einbettungspfad | `MpvVideoItem` (QQuickFramebufferObject) + OpenGL-RHI |
| Abhängigkeiten | libmpv | statischer Import `vulkan-1.dll` | libmpv_pe_resources.md | Ladefehler ohne Vulkan-Laufzeit möglich | Explizites Vorladen mit Klartext-Fehlermeldung |
| Effekte | uve.user.js | WebGL-Shader `getFragmentShader` (Z. 10024–10350), `updateParams` (Z. 10527–10611) | Quelltext, Referenz-JSON | Formeln portierbar | `plugins/uve/engine` (Shader-Generator für mpv-User-Shader) |
| Sicherheitsprofil | Electron-Vorgänger | RunAsNode-Fuse an | legacy_electron.md | zusätzliche Angriffsfläche | native EXE ohne JS-Laufzeit |

## Unsicherheiten
Siehe [REA_LIMITATIONS.md](REA_LIMITATIONS.md). Kein VLC-Code und keine undokumentierten internen Strukturen wurden übernommen;
VLC diente nur als Referenz für Bedienverhalten und Architektur auf Ebene öffentlich sichtbarer Schnittstellen.
