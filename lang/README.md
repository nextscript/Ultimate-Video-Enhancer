# Sprachen / Languages

UVE Player nutzt das Übersetzungssystem von Qt. Jede Sprache besteht aus zwei Dateien in diesem Ordner:

| Datei | Zweck |
|---|---|
| `uve_<code>.ts` | **Quelle** – XML im Qt-Linguist-Format, wird übersetzt/bearbeitet |
| `uve_<code>.qm` | **Kompiliert** – wird vom Player geladen (erzeugt mit `lrelease`, beim Build automatisch) |

Jede `uve_<code>.qm` hier erscheint automatisch unter **Einstellungen → Allgemein → Sprache**. Voreinstellung ist
**Englisch**; die Umschaltung wirkt sofort, ohne Neustart. Fehlende Einträge werden auf Englisch, danach auf Deutsch angezeigt.

| Code | Sprache |
|---|---|
| `en` | English (Voreinstellung / default) |
| `es` | Español |
| `it` | Italiano |
| `pl` | Polski |
| `el` | Ελληνικά |
| `tr` | Türkçe |
| – | Deutsch (eingebaut: Originaltexte des Quellcodes) |

## Eigene Sprache erstellen

### Weg A – mit Qt Linguist (empfohlen, ohne Programmierkenntnisse)

Qt Linguist ist ein kostenloses grafisches Übersetzungsprogramm (Teil von Qt, `linguist.exe`; auch einzeln als
„Qt Linguist“-Download erhältlich).

1. `uve_en.ts` kopieren und nach dem Sprachkürzel benennen, z. B. **`uve_fr.ts`** (Französisch) oder `uve_pt_BR.ts`
   (Portugiesisch, Brasilien). Das Kürzel nach `uve_` bestimmt die Sprache.
2. `uve_fr.ts` in **Qt Linguist** öffnen → *Bearbeiten → Einstellungen der Übersetzungsdatei*: Zielsprache auf
   *Französisch* stellen.
3. Links stehen die Bereiche (Fenster/Dialoge), in der Mitte die **deutschen Originaltexte**, unten die Übersetzung.
   Weil die Datei aus `uve_en.ts` stammt, steht dort schon die englische Fassung – einfach überschreiben und jeden
   Eintrag mit **Strg+Enter** als erledigt markieren.
4. *Datei → Freigeben* (Release) erzeugt **`uve_fr.qm`** im selben Ordner.
5. `uve_fr.qm` in einen dieser Ordner legen:
   - `<Programmordner>\lang\` (z. B. `%LOCALAPPDATA%\Programs\UVE Player\lang\`), oder
   - den Benutzerordner `%APPDATA%\nextscript\UVE Player\lang\` (bei portabler Nutzung `data\lang\`) – hat Vorrang und
     bleibt bei Updates erhalten.
6. Einstellungen öffnen → **Sprache** → *Français* wählen.

### Weg B – mit dem Skript (Entwickler)

```powershell
python -I scripts\update-lang.py --add fr       # legt lang\uve_fr.ts mit allen Texten an (leer)
linguist lang\uve_fr.ts                          # übersetzen
python -I scripts\update-lang.py --check        # Platzhalter, Leerzeichen, offene Einträge prüfen
cmake --build build --config Release            # baut bin\lang\uve_fr.qm automatisch mit
```

Ohne Qt Linguist geht auch ein Texteditor: In der `.ts`-Datei steht pro Text

```xml
<message>
    <source>Datei öffnen…</source>
    <translation>Ouvrir un fichier…</translation>
</message>
```

`type="unfinished"` an `<translation>` entfernen, wenn der Eintrag fertig ist. Danach mit
`lrelease uve_fr.ts` (aus dem Qt-Ordner `bin\`) die `.qm` erzeugen.

### Regeln

| Regel | Beispiel |
|---|---|
| Platzhalter `%1`, `%2`, … übernehmen (Reihenfolge darf sich ändern) | `Nicht abspielbar: %1 (%2)` → `Lecture impossible : %1 (%2)` |
| Leerzeichen am Anfang/Ende beibehalten – viele Texte sind Bausteine, an die Zahlen angehängt werden | `" ms"`, `"Lautstärke "` |
| `&` markiert den unterstrichenen Buchstaben im Menü (Alt+Buchstabe); in der Menüleiste jeden Buchstaben nur einmal verwenden | `&Datei` → `&Fichier` |
| Fachbegriffe, Datei- und Programmnamen nicht übersetzen | `LUT`, `yt-dlp`, `My_Profile.json` |
| Diagnose-Overlay (Bereich *DiagnosticsOverlay*): Beschriftungen mit Leerzeichen auf ca. 19 Zeichen auffüllen | `Auflösung:         ` |

### Nach einem Update neue Texte ergänzen

Neue Versionen können neue Texte enthalten; sie erscheinen bis zur Übersetzung auf Englisch.
`python -I scripts\update-lang.py` gleicht alle `.ts`-Dateien mit dem Quellcode ab (neue Texte werden als „unfertig“
ergänzt, vorhandene Übersetzungen bleiben). In Qt Linguist springt man mit *Strg+J* zum nächsten offenen Eintrag.

Fertige Übersetzungen gern im Projekt einreichen – sie werden dann mit dem Player ausgeliefert.

---

## Creating your own language (English summary)

1. Copy `uve_en.ts` to `uve_<code>.ts` (e.g. `uve_fr.ts`).
2. Open it in **Qt Linguist**, set the target language (*Edit → Translation File Settings*), translate each entry
   (sources are the German texts; English is pre-filled), mark entries done with *Ctrl+Enter*.
3. *File → Release* creates `uve_fr.qm`.
4. Put the `.qm` into `<program folder>\lang\` or `%APPDATA%\nextscript\UVE Player\lang\` (user folder wins, survives updates).
5. Choose it under **Settings → General → Language**. Keep `%1` placeholders, leading/trailing spaces and one `&` per menu title.
