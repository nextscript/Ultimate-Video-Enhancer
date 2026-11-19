"""Trägt die englischen Texte in lang/glsl_manager_en.ts ein (nach lupdate aufrufen). Meldet fehlende Texte."""
import sys
import xml.etree.ElementTree as ET

EN = {
    # GlslEditor
    "GLSL-Fehler: ": "GLSL error: ",
    "Name, z. B. Sobel Sketch": "Name, e.g. Sobel Sketch",
    "Mischmodus": "Blend mode",
    "mpv-Hook-Shader bestimmen ihr Bild selbst und nutzen keinen Mischmodus.": "mpv hook shaders output their own image and do not use a blend mode.",
    "Tags, mit Komma getrennt": "Tags, comma-separated",
    "Kategorie": "Category",
    "Dieser Typ läuft nur im Browser. Er bleibt gespeichert und wird mit exportiert, aber hier nicht abgespielt.": "This type only runs in the browser. It stays saved and is exported, but it does not play here.",
    "mpv-Hook": "mpv hook",
    "GLSL-Filter": "GLSL filter",
    "mpv-Hook-Shader: läuft unverändert mit allen Pässen.": "mpv hook shader: runs unchanged with all passes.",
    "GLSL-Filter: wird für mpv umgewandelt.": "GLSL filter: converted for mpv.",
    "Einfügen": "Paste",
    "Kopieren": "Copy",
    "Im Tab": "In tab",
    "Größer": "Larger",
    "GLSL-Fragment-Shader oder mpv-Hook-Shader.\n\nVerfügbar: u_video, u_res, u_time, v_uv, fragColor\n\nvoid main() {\n    fragColor = texture(u_video, v_uv);\n}\n\nRegler:\n// @uniform float STARK 1.0 0.0 2.0 \"Stärke\"":
        "GLSL fragment shader or mpv hook shader.\n\nAvailable: u_video, u_res, u_time, v_uv, fragColor\n\nvoid main() {\n    fragColor = texture(u_video, v_uv);\n}\n\nSliders:\n// @uniform float AMOUNT 1.0 0.0 2.0 \"Amount\"",
    "Shader-Parameter": "Shader parameters",
    "Shaderfehler: ": "Shader error: ",
    "Vorschau wird geprüft …": "Checking preview …",
    "Vorschau läuft ohne Fehler.": "Preview is running without errors.",
    "Kein Video geladen: geprüft wird beim nächsten Abspielen.": "No video loaded: it will be checked on the next playback.",
    "Abbrechen": "Cancel",
    "Testen": "Test",
    "Trotzdem": "Save anyway",
    "Speichern": "Save",
    "Hinzufügen": "Add",
    # GlslPanel
    "Import: %1 neu, %2 ersetzt, %3 übersprungen": "Import: %1 new, %2 replaced, %3 skipped",
    "Die Zwischenablage enthält keinen Text.": "The clipboard contains no text.",
    "Beispiele konnten nicht geladen werden (HTTP %1).": "Could not load the examples (HTTP %1).",
    "Filter importieren": "Import filters",
    "Filter und Shader (*.json *.glsl *.hook *.frag *.txt)": "Filters and shaders (*.json *.glsl *.hook *.frag *.txt)",
    "Alle Dateien (*)": "All files (*)",
    "Filter exportieren": "Export filters",
    "%1 Filter exportiert.": "%1 filter(s) exported.",
    "Alle Filter gemeinsam ein- oder ausschalten": "Turn all filters on or off",
    "%1 von %2 aktiv": "%1 of %2 active",
    "Neu": "New",
    "Neuen Filter anlegen": "Create a new filter",
    "Shader-Code oder JSON aus der Zwischenablage übernehmen": "Use shader code or JSON from the clipboard",
    "Import": "Import",
    "JSON-Export des Userscripts oder Shader-Datei laden": "Load a JSON export of the userscript or a shader file",
    "Export": "Export",
    "Alle Filter als JSON speichern (mit dem Userscript kompatibel)": "Save all filters as JSON (compatible with the userscript)",
    "Lädt …": "Loading …",
    "Beispiele": "Examples",
    "Beispielfilter des Userscripts von GitHub laden": "Load the userscript's example filters from GitHub",
    "Bibliothek": "Library",
    "Filter-Bibliothek des Userscripts im Browser öffnen": "Open the userscript's filter library in the browser",
    "Suche in Name, Tags, Kategorie …": "Search name, tags, category …",
    "Alle": "All",
    "Noch keine Filter. Lege einen an, füge Code ein oder importiere eine Datei.": "No filters yet. Create one, paste code or import a file.",
    "Kein Filter passt zur Suche.": "No filter matches the search.",
    "Taste: %1 (ändern oder entfernen)": "Key: %1 (change or remove)",
    "Taste festlegen": "Set key",
    "Bearbeiten": "Edit",
    "Duplizieren": "Duplicate",
    "Löschen": "Delete",
    "Nur im Browser möglich, wird hier nicht abgespielt.": "Browser only, does not play here.",
    "Shaderfehler, Filter pausiert: ": "Shader error, filter paused: ",
    "Erneut": "Retry",
    "Reihenfolge: am Griff ⠿ ziehen. Oben läuft zuerst.": "Order: drag the ⠿ handle. The top filter runs first.",
    "Doppelte Filter": "Duplicate filters",
    "%1 Filter mit gleichem Namen gibt es schon:": "%1 filter(s) with the same name already exist:",
    "Überspringen": "Skip",
    "Ersetzen": "Replace",
    "„%1“ wirklich löschen?": "Really delete \"%1\"?",
    "Drücke eine Taste. Sie schaltet den Filter um und hat Vorrang vor einem gleichen Player-Kürzel.": "Press a key. It toggles the filter and takes priority over the same player shortcut.",
    "Entfernen (%1)": "Remove (%1)",
    # GlslController
    "GLSL-Filter: an": "GLSL filters: on",
    "GLSL-Filter: aus": "GLSL filters: off",
    "Der GPU-Treiber hat den Shader abgelehnt (Details im Player-Protokoll).": "The GPU driver rejected the shader (details in the player log).",
    "Shaderfehler in „%1“": "Shader error in \"%1\"",
    "Shaderdatei nicht schreibbar: ": "Cannot write shader file: ",
    "Unbenannt": "Untitled",
    " (Kopie)": " (copy)",
    "Der Code darf nicht leer sein.": "The code must not be empty.",
    "Geschweifte Klammern { } passen nicht zusammen.": "Curly braces { } do not match.",
    "Runde Klammern ( ) passen nicht zusammen.": "Parentheses ( ) do not match.",
    "Ungültige JSON-Datei: ": "Invalid JSON file: ",
    "Keine gültigen Filter in dieser Datei gefunden.": "No valid filters found in this file.",
    "Datei ist zu groß.": "The file is too large.",
    "Datei nicht lesbar: ": "Cannot read file: ",
    "Keine Filter zum Exportieren vorhanden.": "There are no filters to export.",
    "Datei nicht schreibbar: ": "Cannot write file: ",
    ": an": ": on",
    ": aus": ": off",
}

path = sys.argv[1]
tree = ET.parse(path)
missing = []
total = 0
for message in tree.getroot().iter("message"):
    total += 1
    source = message.find("source").text
    translation = message.find("translation")
    if source in EN:
        translation.text = EN[source]
        translation.attrib.pop("type", None)
    else:
        missing.append(source)
with open(path, "wb") as f:
    f.write(b'<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n')
    tree.write(f, encoding="utf-8", xml_declaration=False)
print(f"{total - len(missing)} von {total} übersetzt")
for m in missing:
    print("FEHLT:", m.encode("ascii", "backslashreplace").decode())
