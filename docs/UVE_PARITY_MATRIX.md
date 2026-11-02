# UVE-Parity-Matrix (Ultimate Video Enhancer 1.17.5 → UVE Player 1.0.0)

Quelle: `legacy-electron/plugins/uve/uve.user.js` (UVE 1.17.5). Zeilenangaben beziehen sich auf diese Datei.
Referenzwerte: `tests/reference/uve_reference.json`, erzeugt durch Ausführen der **Original-Funktionen** in einer
Node-vm-Sandbox (`scripts/uve-reference.mjs`). Test: `tests/unit/tst_uve.cpp` (alle 15 Umrechnungsfunktionen exakt, Toleranz 1e-9).

Farbraum-Konvention: UVE rechnet im Browser auf sRGB-kodierten Canvas-Werten. Der native Port arbeitet im mpv-Hook `MAIN`
(RGB nach YUV→RGB, quellkodiert, z. B. BT.1886/sRGB). Für SDR-Material entspricht das der UVE-Konvention; bei HDR-Quellen
(PQ/HLG) wirken die Regler auf die kodierten Werte, bevor mpv tonemappt – **nicht gleichwertig**, siehe unten.

| Funktion | Source-Stelle | WebGL/JS-Implementierung | Native Umsetzung | Status | Test |
|---|---|---|---|---|---|
| Kontrast (S-Kurve, Pivot 0.5) | Z. 4980, 9338–9342, Shader 10173 | `cc - a·sin(2πcc)/2π` | identische Formel, Pass „Farbkorrektur“ | **portiert** | Umrechnung exakt; Shaderaufbau |
| Schwarz-/Weißwert (Levels, Soft-Toe 0.05) | Z. 4993–5016, 9457–9466, Shader 10165–10172 | Levels mit weicher Schulter | identisch | **portiert** | exakt (`userBlackOffset/WhiteOffset`) |
| Lichter / Schatten | Z. 5017–5042, 9396–9510, Shader 10178–10188 | Kurven t^p(1−t) bzw. t(1−t)^p mit Kappung | identisch inkl. Kappungsberechnung | **portiert** | exakt |
| Sättigung / Sättigungskurve | Z. 5043, 9364, Shader 10140–10153 | linear + Kurve, Desaturierung gleichmäßig | identisch | **portiert** | exakt |
| Dynamik (Vibrance, Hautschutz 0.6) | Z. 5058, 9374, Shader 10131–10138 | Kurve mit pow 3 | identisch | **portiert** | exakt |
| Gamma (Schatten-Steilheit 2.0) | Z. 5073, 9330, Shader 10190–10194 | out = in^(1/g) | identisch | **portiert** | exakt |
| Farbton (±30°, Hautschutz 0.7) | Z. 5124, 9433, Shader 10123–10127 | Rotation um Luma-Achse | identisch | **portiert** | exakt |
| RGB-Gain | Z. 9528, Shader 10116–10119 | Faktor v/128 | identisch | **portiert** | Shaderwerte |
| Grundton / Profile Film·Anime·Gaming·EyeCare | Z. 4898, 10527–10556 | Multiplikatoren | identisch | **portiert** | `shaderValuesFollowUpdateParams` |
| Dark & Moody / Kräftige Farben / Teal-Orange | Z. 4921–4949, 10558–10575 | GPU-Multiplikatoren, Hue −5° | identisch (nur GPU-Pfad-Werte) | **portiert (GPU-Pfad)** | Shaderwerte |
| Schärfe SL + Detail-Schärfe | Z. 5088, 10561, Shader 10268–10283 | Luma-Detailkurve mit Rauschschwelle, Halo-Balance | identisch; **Abweichung:** Nachbarn aus gegradetem Bild statt Original | **portiert mit Abweichung** | exakt (Umrechnung) |
| Kanten (Sobel) | Shader 10285–10306 | Abdunkeln echter Kanten | identisch (gegradetes Bild) | **portiert** | Shaderaufbau |
| HDR-like (Belichtung + ACES) | Z. 4950, 10580–10590, Shader 10199–10214 | max. 0.65 (Einzelvideo) | identisch; Warm-up-Faktor entfällt | **portiert** | `hdr` gekappt auf 0.65 |
| Rauschfilter + CAS | Shader 10216–10237 (und Duplikat 10308–10337) | Bilateral 3×3 + CAS, immer aktiv | als eigener Pass **vor** dem Grading, abschaltbar | **portiert mit Abweichung** | Shaderaufbau |
| Korn (DN negativ) / Filmkorn-Kurve | Z. 5108, 10562, Shader 10216–10232 | Hash-Rauschen, Luma-gewichtet | identisch (statisches Muster wie Original) | **portiert** | exakt |
| UVE-LUT-Profile (`gvf-lut-profile`, 4×5-Matrix) | Import Z. 7789 ff., Shader Z. 10108/10341, Upload Z. 10809 | Matrix zuletzt im sRGB-Raum, begrenzt | identische Formel (+ Stärke), Profil-Schlüssel „Gruppe‖Name“ wie UVE | **portiert** | Matrix im Shader; echtes Paket v3.0 (83 Profile) |
| LUT-Paket-Bibliothek von GitHub | `LUT_REPO_*`, `listRepoLutZipFiles` Z. 7629–7700 | höchste Version `LUTsProfiles_v*.zip` | gleiche Auswahl (aktuell v3.0); Prüfsumme (Git-Blob-SHA-1/SHA-256); Ersatz bei API-Limit: ETag-Cache, jsDelivr-Liste+CDN, letzte Liste; nur auf Knopfdruck | **portiert** | Auswahl, Listen, Prüfsummen; Selbsttest mit echtem Download |
| Beispielprofil „Load Example“ | Z. 17032–17060 (`My_Profile.json`) | Download + Import | gleiches Profil von GitHub (Ersatz jsDelivr), wird angewendet und als Preset gespeichert; fehlendes LUT-Paket wird mitgeladen | **portiert** | Selbsttest: BL/WL/SR/SL + LUT-Profil |
| LUT-Galerie | – (neu) | – | aktueller Frame (mpv `screenshot-raw`, ohne Effekte) mit jedem LUT-Profil, gleiche Formel wie Shader | **neu** | Selbsttest: 83 Vorschaubilder |
| LUT (.cube) | Z. 18078 ff., Shader 10339–10342 | GPU-Pfad: **4×4-Matrix-Näherung** (`uProfileMatrix`) | **echte 3D-Textur** (rgba16f, trilinear) mit Stärke | **abweichend (genauer)** | Parser, Halbfloat, Texturgröße |
| Vorher/Nachher | Compare-Slider (Strg+Alt+U) | Overlay im DOM | Split im Shader (Original via `//!SAVE UVEORIG`) | **neu umgesetzt** | UI-Selbsttest |
| Profile (gvf-settings 1.10) | `exportSettings` Z. 17533 ff. | JSON | Import/Export inkl. Durchreichen unbekannter Felder | **portiert** | Roundtrip, Quelldatei unverändert |
| BL – Schwarzpegel | Z. 9322, 9533, `mkOffsetCT` 18803, `buildFilter` 19397 | SVG: Versatz −BL·0.04 (BL auf 1 Stelle gerundet) | identisch, am Anfang des Grading-Passes | **portiert** | exakt (−0.028 bei 0.7) |
| WL – Weißpegel | Z. 9323, 9534, `mkHighlightsTableCT` 18813, 19402 | SVG: 17-Punkt-Tabelle, Knie 0.78, WL·0.06 | identische Tabelle (toFixed(4)), lineare Interpolation | **portiert** | Tabellenwerte exakt |
| SR – Radius | Z. 9321, 9532, `buildFilter` 19368 ff. | SVG: Gauß-Radius der Unschärfemaske (|SR|, min. 0.1) | Abtastradius der Detailschärfe (|SR|, min. 1 px) | **Näherung** | Shaderwert |
| Auto-Scene-Match / Auto-Weißabgleich | `uAutoMatrix`, AUTO_* | Bildstatistik per Canvas | – | **nicht portiert** (eigene GPU-Statistik nötig) | – |
| Scopes-HUD | `SCOPES` Z. 17354 | Canvas-Readback | – | **nicht portiert** | – |
| Eigene SVG/GLSL/Canvas-Filtercodes | Z. 3886 ff. | Benutzer-Code im Browser | – | **nicht portiert** (keine Ausführung fremder Codes) | – |
| Farbsehschwäche-Filter | `CB_FILTER` | SVG-Matrizen | – | **nicht portiert** | – |
| KI-Upscaling | UVE Manager | externe Backends | – | **future** | – |

## Bekannte Abweichungen (bewusst)
1. **Rauschfilter/CAS-Reihenfolge:** Im UVE-Shader sampelt der Block die *ungegradete* Quelle (`uVideoTex`), verwendet aber
   die *gegradete* Farbe als Zentrum – dadurch wird das Grading teilweise herausgemischt. Nativ läuft der Block als eigener
   Pass vor dem Grading.
2. **LUT:** echte 3D-Interpolation statt 4×4-Matrix-Näherung → näher an der Absicht der `.cube`-Datei, sichtbar anders als UVE-GPU-Modus.
3. **HDR-Quellen:** Effekte wirken auf PQ/HLG-kodierte Werte vor mpvs Tonemapping; Ergebnis unterscheidet sich vom Browser.
4. **HDR-Warm-up und Mehrfach-Video-Drosselung** entfallen (nur ein Video).
5. **SVG- und GPU-Kette kombiniert:** In UVE schließen sich SVG-Modus (SL/SR/BL/WL) und GPU-Modus (Grading-Shader) gegenseitig aus.
   Der native Port wendet beide in einer Kette an: Rauschfilter/CAS → BL/WL → Grading → Detailschärfe (SL, Radius SR) → LUT-Profil.
   Damit wirken Profile wie `My_Profile.json` (`renderMode: "svg"`, BL 0.7, WL 0.2) mit ihren Hauptreglern, auch wenn Grading-Werte gesetzt sind.
