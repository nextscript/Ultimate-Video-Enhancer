# GLSL Manager

Plugin für den UVE Player. Spielt die GLSL-Filter (Custom Filter Codes) des Userscripts
[Ultimate Video Enhancer](https://greasyfork.org/de/scripts/561189-ultimate-video-enhancer-sharpen-hdr-color-boost) als mpv-Shader ab.

## Installieren

*Einstellungen → Plugins → Plugin installieren …* → `GLSLManager-<Version>.zip` wählen und den Player neu starten. Das ZIP enthält das Modul für Windows x64, Linux x86-64 und macOS ARM64, der Player nimmt das passende.
Danach gibt es in der Seitenleiste den Tab **GLSL**. Voraussetzung: UVE Player ab 1.2.9 (GLSL Manager bis 1.0.1: nur Windows, ab Player 1.1.5).

## Funktionen

| Funktion | So geht es |
|---|---|
| Neu, Bearbeiten | **Neu** oder ✎. Der Editor öffnet sich im Tab, **Größer** schiebt ihn über das Video |
| Einfügen | **Einfügen**: Code öffnet den Editor, JSON geht direkt in den Import |
| Testen | **Testen** wendet den Code an, ohne ihn zu speichern. Fehler erscheinen im Editor |
| Regler | `// @uniform`, `// @select`, `//!PARAM` und numerische `#define` werden zu Reglern mit Live-Vorschau |
| Ein/Aus | Häkchen je Filter, Schalter oben für alle, Kürzel `Ctrl+Alt+Shift+G` (änderbar in *Einstellungen → Tastenkürzel*) |
| Reihenfolge | am Griff ⠿ ziehen, oben läuft zuerst |
| Taste je Filter | ⌨: eine Taste ohne Umschalter schaltet den Filter um (`Esc` bricht ab) |
| Duplizieren, Löschen | ⧉ und ✕ |
| Suchen | Name, Tags, Kategorie, Beschreibung, dazu Filter nach Typ |
| Import, Export | JSON im Format des Userscripts, außerdem `.glsl`, `.hook`, `.frag`, `.txt`. Doppelte Namen: abbrechen, überspringen oder ersetzen |
| Beispiele, Bibliothek | lädt die Beispielfilter von GitHub; öffnet svg.ts3x.cc im Browser |
| Mischmodus | die 16 Modi des Userscripts, für GLSL-Filter |

## Unterstützte Shader

- **GLSL-Filter** mit `void main()`, Shadertoy-`mainImage`, Hilfsfunktion oder nacktem Rumpf
- **mpv-Hook-Shader** (`//!HOOK`), zum Beispiel Anime4K: laufen unverändert mit allen Pässen

## Unterschiede zum Userscript

- SVG, Canvas 2D und Audio laufen nur im Browser. Diese Einträge bleiben erhalten und werden mit exportiert, aber nicht abgespielt.
- `u_mouse` ist fest die Bildmitte, `u_zoom`, `u_strength` und `u_layers` sind 1.0.
- `u_time` wird aus dem Bildzähler und der Bildrate des Videos berechnet und steht bei Pause still.
- Regler und Mischmodus schreiben den Shader neu. Beim Ziehen kann das Bild daher kurz nachziehen.
- Entfallen, weil es im Player keinen Sinn hat: Domain-Blacklist und GLSL-Modus (30/60/120 FPS), die Filter laufen mit jedem Videobild.
- Hook-Shader nutzen keinen Mischmodus.

## Bauen

Benötigt MSVC 2022 und Qt 6.12 wie der Player, außerdem dessen Quellcode für `PluginApi.h`.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt>/msvc2022_64
cmake --build build
```

Der fertige Plugin-Ordner liegt danach unter `build/package/GLSLManager`.
