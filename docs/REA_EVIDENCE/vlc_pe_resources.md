# vlc.exe – PE-Ressourcen (REA `inspect_pe_resources`)

- **Evidence-ID:** `ev_e335bb8287716d2d84fbabee9fbe35f1ae8f65072d4cc91d9b8885e92a1f1063`
- **Artefakt:** `C:\Program Files\VideoLAN\VLC\vlc.exe`, 1 050 048 Bytes
- **SHA-256:** `63d0d02df6e466d642b824dc66be6b5a134bf98b26747ffac8ec0d879dc180d7`
- **Format:** PE32+ (machine 34404 = x86-64), Provider `rea-pe-resources` v1, Coverage `complete` (22 Einträge, 9 Ressourcen)

## Beobachtete Ressourcen
| Typ | Name | Inhalt |
|---|---|---|
| RT_ICON (3) | 1–6 | 6 Icon-Bilder: 32×32/8 bit, 16×16/8 bit, 256×256/32 bit (54 430 B, PNG-Größe), 48×48, 32×32, 16×16 |
| RT_GROUP_ICON (14) | `VLC_ICON` | Gruppe aus den 6 Bildern |
| RT_VERSION (16) | 1 | 940 Bytes |
| RT_MANIFEST (24) | 1 | 1 247 Bytes |

## Interpretation
`vlc.exe` enthält keine eigenen UI-Ressourcen (Dialoge, Menüs, Strings) – die Oberfläche kommt aus Modulen
(`plugins\gui\libqt_plugin.dll`). Bestätigt zusammen mit den Importen (siehe pe_imports_exports.md) das Bild eines
schlanken Starters. Übernommen als Gestaltungsprinzip für `app/main.cpp` (eigene Implementierung, kein VLC-Code).

## Grenzen (laut REA)
Nur statisches Ressourcen-Inventar; Inhalte außer RT_GROUP_ICON bleiben undurchsichtig; tatsächliches Laden durch
die Shell wird nicht beobachtet.
