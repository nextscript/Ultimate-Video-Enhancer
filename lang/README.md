# Languages

UVE Player uses the Qt translation system. Each language consists of two files in this folder:

| File | Purpose |
|---|---|
| `uve_<code>.ts` | **Source** – XML in Qt Linguist format; this is the file you translate and edit |
| `uve_<code>.qm` | **Compiled** – loaded by the player (created with `lrelease`, automatically during the build) |

Every `uve_<code>.qm` in this folder automatically appears under **Settings → General → Language**. The default is
**English**; switching takes effect immediately, without a restart. Missing entries are shown in English, then in German.

| Code | Language |
|---|---|
| `en` | English (default) |
| `es` | Español |
| `it` | Italiano |
| `pl` | Polski |
| `el` | Ελληνικά |
| `tr` | Türkçe |
| – | Deutsch (built in: the original texts in the source code) |

## Creating your own language

### Option A – with Qt Linguist (recommended, no programming knowledge needed)

Qt Linguist is a free graphical translation tool (part of Qt, `linguist.exe`; also available as a separate
"Qt Linguist" download).

1. Copy `uve_en.ts` and name it after the language code, e.g. **`uve_fr.ts`** (French) or `uve_pt_BR.ts`
   (Portuguese, Brazil). The code after `uve_` determines the language.
2. Open `uve_fr.ts` in **Qt Linguist** → *Edit → Translation File Settings*: set the target language to *French*.
3. The contexts (windows/dialogs) are on the left, the **original German texts** in the middle, and the translation at
   the bottom. Because the file comes from `uve_en.ts`, the English version is already filled in – just overwrite it and
   mark each entry as done with **Ctrl+Enter**.
4. *File → Release* creates **`uve_fr.qm`** in the same folder.
5. Put `uve_fr.qm` into one of these folders:
   - `<program folder>\lang\` (e.g. `%LOCALAPPDATA%\Programs\UVE Player\lang\`), or
   - the user folder `%APPDATA%\nextscript\UVE Player\lang\` (`data\lang\` for portable use) – it takes precedence and
     is kept across updates.
6. Open Settings → **Language** → choose *Français*.

### Option B – with the script (developers)

```powershell
python -I scripts\update-lang.py --add fr       # creates lang\uve_fr.ts with all texts (empty)
linguist lang\uve_fr.ts                          # translate
python -I scripts\update-lang.py --check        # checks placeholders, spaces and open entries
cmake --build build --config Release            # also builds bin\lang\uve_fr.qm automatically
```

A text editor works without Qt Linguist too. The `.ts` file contains this for each text:

```xml
<message>
    <source>Datei öffnen…</source>
    <translation>Ouvrir un fichier…</translation>
</message>
```

Remove `type="unfinished"` from `<translation>` when the entry is done. Then create the `.qm` with
`lrelease uve_fr.ts` (from the Qt `bin\` folder).

### Rules

| Rule | Example |
|---|---|
| Keep the placeholders `%1`, `%2`, … (their order may change) | `Nicht abspielbar: %1 (%2)` → `Lecture impossible : %1 (%2)` |
| Keep leading and trailing spaces – many texts are building blocks that numbers are appended to | `" ms"`, `"Lautstärke "` |
| `&` marks the underlined letter in a menu (Alt+letter); use each letter only once in the menu bar | `&Datei` → `&Fichier` |
| Do not translate technical terms, file names or program names | `LUT`, `yt-dlp`, `My_Profile.json` |
| Diagnostics overlay (context *DiagnosticsOverlay*): pad labels with spaces to about 19 characters | `Auflösung:         ` |

### Adding new texts after an update

New versions can contain new texts; they appear in English until they are translated.
`python -I scripts\update-lang.py` synchronizes all `.ts` files with the source code (new texts are added as
"unfinished", existing translations are kept). In Qt Linguist, *Ctrl+J* jumps to the next open entry.

Finished translations are welcome in the project – they will then ship with the player.
