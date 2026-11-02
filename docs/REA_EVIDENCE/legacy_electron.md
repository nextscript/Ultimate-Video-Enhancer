# Bisherige Electron-Version (REA)

## `inspect_electron_fuses`
- **Evidence-ID:** `ev_88cb1c9a53350110f293c4402e078ec8fbcb7b7ab1b2ec9e4f67af520bfe95ae`
- **Artefakt:** `legacy-electron\release\win-unpacked\UVE Player.exe`, 249 798 656 Bytes,
  SHA-256 `7e6c9ac96a408c74d9ca61edf3aae00b59dc22918aecc52788c0b3197fa89c83`
- Fuses (Sentinel @ 202514712, Wire-Version 1): RunAsNode **enabled**, EnableCookieEncryption disabled,
  EnableNodeOptionsEnvironmentVariable enabled, EnableNodeCliInspectArguments enabled,
  EnableEmbeddedAsarIntegrityValidation **disabled**, OnlyLoadAppFromAsar disabled, GrantFileProtocolExtraPrivileges enabled.
- Bedeutung: `ELECTRON_RUN_AS_NODE` wird beachtet (die Electron-Version nutzte das bewusst als JS-Laufzeit für yt-dlp);
  ASAR-Integrität wird nicht erzwungen. Für den nativen Player entfällt diese Angriffsfläche.

## `analyze_javascript_application` (detail: summary)
- **Evidence-ID:** `ev_b24cb4236b282c29118e2f64af8717770f08124095dee57237f2d7c217880cb8`
  (Eltern-Evidence `ev_aec6318cae68e461257a843739437d54948558af80cc5217127f8e3c10d8d522`)
- **Artefakt:** `resources\app.asar`, SHA-256 `4c09b9b9f6e7d5fbc9cb554fa06894b352e60e5b97faa35b7aaff3f48cb9cb96`
- Ausgewählte Quellen: `src/main/stream.js`, `src/renderer/js/core/stream-session.js`, `src/renderer/js/plugins/gm-shim.js`,
  `src/main/protocol.js` (4 Dateien geparst, 8 386 AST-Knoten, 45 Befunde, 0 Parse-Fehler); 22 Dateien außerhalb der Auswahl.
- Coverage laut REA `partial` (statische Syntaxbeobachtungen; keine Laufzeitausführung).
- Verwendung: Bestätigung der Renderpfade der Electron-Version (FFmpeg → fMP4 → MSE, GM-Shim) als Abgrenzung –
  der native Player ersetzt diese Kette durch libmpv.
