# libmpv-2.dll – PE-Ressourcen (REA `inspect_pe_resources`)

- **Evidence-ID:** `ev_a4f8b6974be3b2798d994bd11240a205c400546bb59d4b58a185a2550922ef89`
- **Artefakt:** `M:\LAPS\UVE Player\deps\mpv\libmpv-2.dll`, 121 485 824 Bytes
- **SHA-256:** `4752efe9982ed763f0a85ade83b55cf34442cad0c19341ad9b90769146cd2f3c`
- **Herkunft:** shinchiro/mpv-winbuild-cmake Release `20261102`, Asset `mpv-dev-x86_64-20261102-git-2137d21b2e.7z`
- **Format:** PE32+ x86-64, Coverage `complete` (22 Einträge, 9 Ressourcen)

## Ressourcen
RT_ICON 1–6 (16, 24, 32, 48, 64, 256 px), RT_GROUP_ICON `IDI_ICON1`, RT_VERSION (912 B), RT_MANIFEST (2 121 B).

## Ergänzende Befunde (pefile, siehe pe_imports_exports.md)
- 206 Exporte, darunter die komplette mpv-Client-API (`mpv_create`, `mpv_command_async`, `mpv_observe_property`,
  `mpv_set_wakeup_callback`, …) und die Render-API (`mpv_render_context_create/render/set_update_callback/free`).
- Header `client.h`: `MPV_CLIENT_API_VERSION = MPV_MAKE_VERSION(2, 6)`; `render.h` nennt OpenGL, D3D11 und Software als
  API-Typen, der Build liefert aber nur `render_gl.h` (kein `render_d3d11.h`) → Integration über OpenGL.
- **Statischer Import von `vulkan-1.dll`** → ohne Vulkan-Laufzeit (Bestandteil der GPU-Treiber) lädt die DLL nicht.
  Der Player fängt das ab (main.cpp: explizites `LoadLibraryExW` mit verständlicher Meldung statt Absturz).
- Laufzeitprüfung (Selbsttest): `mpv-version` = `mpv v0.41.0-1386-g2137d21b2`.
