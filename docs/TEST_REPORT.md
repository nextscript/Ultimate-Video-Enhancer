# Testbericht – UVE Player 1.0.0

Testsystem: Windows 10 IoT Enterprise LTSC 2021 (19044), x64, NVIDIA-GPU, MSVC 19.51, Qt 6.12.0, libmpv 20261102 (mpv v0.41.0-1386).
Alle Angaben stammen aus tatsächlich ausgeführten Läufen am 2026-11-02. Nicht Getestetes ist ausdrücklich als solches markiert.

## Automatische Tests (CTest, `ctest --test-dir build -C Release`)

| Suite | Inhalt | Ergebnis |
|---|---|---|
| tst_manifest | Manifest-Prüfung (id, Version, apiVersion, minHostVersion, Plattform, Backends) | bestanden |
| tst_playlist | Hinzufügen, Verschieben, Sortieren, Wiederholen/Zufall, M3U/PLS | bestanden |
| tst_hotkeys | Standardbelegung, Konflikterkennung, Zurücksetzen | bestanden |
| tst_proxy | URL-Kodierung, Prüfung, DPAPI-Speicherung; Brücke gegen lokalen SOCKS5-Server (407 ohne Zugang, CONNECT-Tunnel, Auflösung am Proxy, tcp-Weiterleitung); optional Live-Proxy | bestanden (Live: öffentliche IP direkt 84.118.8.160, über Proxy 88.151.59.152) |
| tst_i18n | alle `lang/uve_*.ts`: vollständig, Platzhalter, Randleerzeichen, eindeutige Menü-Kürzel; Umschalten en/el/tr/de mit gebauten `.qm`, Rückfall, defekte Datei | bestanden (6 Sprachen × 678 Texte) |
| tst_uve | 15 UVE-Umrechnungen exakt gegen Original-JS (Node-vm), Profile-Roundtrip, `.cube`-Parser, ZIP/Inflate/CRC, echtes `LUTsProfiles_v3.0.zip` (83 Profile), Prüfsummen GitHub/jsDelivr, BL/WL/SR-Werte, Shaderaufbau | bestanden |
| tst_mpv_integration | libmpv laden, Datei öffnen, Spurwechsel, Suchen, EOF | bestanden |

Ergebnis: **7/7 bestanden**.

## Oberflächen-Selbsttest (`UVE_SELFTEST_UI=1 UVEPlayer.exe --selftest …`, echte Mausereignisse)

| Prüfung | Ergebnis |
|---|---|
| Doppelklick in der Wiedergabeliste spielt Zeile 2 | ok |
| Klick aufs Bild pausiert / setzt fort; Doppelklick → Vollbild ohne Pause | ok |
| Rechtsklick-Kontextmenü | ok |
| Effekte wirken im Bild (Sättigung −10: Farbsättigung auf 41 % gemessen) | ok |
| Beispielprofil My_Profile.json von GitHub: BL 0.7, WL 0.2, SR 1.1, SL 1.2, Dark & Moody, LUT-Profil Fujifilm DCI-P3 | ok |
| LUT-Paket v3.0 automatisch nachgeladen (83 Profile), LUT-Profil im Bild messbar (Schwarzweiß-LUT: Sättigung → 0) | ok |
| LUT-Galerie mit 83 Vorschaubildern aus dem aktuellen Frame (53–64 ms) | ok |
| Einstellungen → Netzwerk | ok |
| Sprache Englisch (Voreinstellung) und Griechisch, 0 Warnungen im Protokoll | ok |
| Sauberes Beenden (Exitcode 0) | ok |

## Wiedergabe echter Quellen

| Quelle | Ergebnis |
|---|---|
| MKV HEVC 10-Bit + AC3, 2 Audio-/2 Untertitelspuren | NVDEC, Start 3,0 s, Suchen 212 ms |
| AVI MPEG-4/MP3, MKV H.264 + AAC/AC3 + SRT (Testmedien) | ok |
| YouTube 1080p (yt-dlp) | Start 6,5 s, Suchen 200 ms |
| Twitch mihanaplays (live) | 1080p |
| `tcp://hls.ts3x.cc:8113` (live) | 1080p, Start 0,7 s |
| **Über SOCKS5-Proxy** albcloud.ts3x.cc:2888: Twitch + tcp:// | 1080p/NVDEC; alle Verbindungen über die Brücke (gql.twitch.tv, usher, Playlist, 4× Segmente, hls.ts3x.cc), 7,5 MB |

## Nicht getestet
- Linux, macOS (nicht gebaut)
- AMD-/Intel-GPUs, HDR10-Ausgabe, Mehrschirmbetrieb
- Installation des Setups auf einem frischen System ohne Visual-C++-Laufzeit (Laufzeit-DLLs werden von windeployqt beigelegt)
- Pixelgenauer Vergleich Browser-UVE ↔ nativ (siehe UVE_PARITY_MATRIX.md)
