# REA – Grenzen und nicht verifizierte Aussagen

| Thema | Status | Grund / Auswirkung |
|---|---|---|
| Disassemblierung/Dekompilierung von vlc.exe, libvlc, libmpv | **nicht durchgeführt** | REA meldete `provider_unavailable`: Ghidra (`GHIDRA_INSTALL_DIR`, Java fehlen), IDA (`REA_IDA_MCP_CONFIG` fehlt), Hopper (nur macOS), Kuna (PE nicht zugelassen). |
| Laufzeitverhalten von VLC (Fenster, Hotkeys, Fullscreen) | **nicht mit REA beobachtet** | Kein dynamischer Provider genutzt; Bedienverhalten aus öffentlicher VLC-Dokumentation und eigener Kenntnis übernommen. |
| mpv als installierte Anwendung | **nicht vorhanden** | Nur libmpv (Entwicklerpaket) lokal; Verhalten über öffentliche API-Doku (client.h/render.h) und eigene Tests belegt. |
| UVE Manager (Electron-Branch `uve-manager`) | **nicht analysiert** | Gehört nicht zum lokalen Projekt; Spezifikation verbietet Veränderungen. Für den Port reichte das Userscript. |
| Electron-Vorgänger: JS-Analyse | **partial** | REA analysierte 4 ausgewählte Dateien statisch; 22 Dateien außerhalb der Auswahl. |
| UVE-Parität visuell | **teilweise** | Formeln und Umrechnungen sind gegen das Original-JS getestet; ein pixelgenauer Vergleich Browser ↔ nativ wurde nicht durchgeführt (unterschiedliche Farbpipelines, siehe UVE_PARITY_MATRIX.md). |
| Linux/macOS | **nicht gebaut/getestet** | Nur Windows 10 x64 (LTSC 2021) mit NVIDIA-GPU geprüft. |
| HDR10-Ausgabe, Multi-Monitor, AMD/Intel-GPUs | **nicht getestet** | Keine Testgeräte; keine Zusage. |
