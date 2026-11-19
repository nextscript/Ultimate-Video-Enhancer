<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en" sourcelanguage="de">
<context>
    <name>GlslEditor</name>
    <message>
        <location filename="../qml/GlslEditor.qml" line="96" />
        <source>GLSL-Fehler: </source>
        <translation>GLSL error: </translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="169" />
        <source>Name, z. B. Sobel Sketch</source>
        <translation>Name, e.g. Sobel Sketch</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="186" />
        <source>Mischmodus</source>
        <translation>Blend mode</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="196" />
        <source>mpv-Hook-Shader bestimmen ihr Bild selbst und nutzen keinen Mischmodus.</source>
        <translation>mpv hook shaders output their own image and do not use a blend mode.</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="204" />
        <source>Tags, mit Komma getrennt</source>
        <translation>Tags, comma-separated</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="205" />
        <source>Kategorie</source>
        <translation>Category</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="214" />
        <source>Dieser Typ läuft nur im Browser. Er bleibt gespeichert und wird mit exportiert, aber hier nicht abgespielt.</source>
        <translation>This type only runs in the browser. It stays saved and is exported, but it does not play here.</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="225" />
        <source>mpv-Hook</source>
        <translation>mpv hook</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="225" />
        <source>GLSL-Filter</source>
        <translation>GLSL filter</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="226" />
        <source>mpv-Hook-Shader: läuft unverändert mit allen Pässen.</source>
        <translation>mpv hook shader: runs unchanged with all passes.</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="226" />
        <source>GLSL-Filter: wird für mpv umgewandelt.</source>
        <translation>GLSL filter: converted for mpv.</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="232" />
        <source>Einfügen</source>
        <translation>Paste</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="243" />
        <source>Kopieren</source>
        <translation>Copy</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="251" />
        <source>Im Tab</source>
        <translation>In tab</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="251" />
        <source>Größer</source>
        <translation>Larger</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="279" />
        <source>GLSL-Fragment-Shader oder mpv-Hook-Shader.

Verfügbar: u_video, u_res, u_time, v_uv, fragColor

void main() {
    fragColor = texture(u_video, v_uv);
}

Regler:
// @uniform float STARK 1.0 0.0 2.0 "Stärke"</source>
        <translation>GLSL fragment shader or mpv hook shader.

Available: u_video, u_res, u_time, v_uv, fragColor

void main() {
    fragColor = texture(u_video, v_uv);
}

Sliders:
// @uniform float AMOUNT 1.0 0.0 2.0 "Amount"</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="298" />
        <source>Shader-Parameter</source>
        <translation>Shader parameters</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="385" />
        <source>Shaderfehler: </source>
        <translation>Shader error: </translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="386" />
        <source>Vorschau wird geprüft …</source>
        <translation>Checking preview …</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="387" />
        <source>Vorschau läuft ohne Fehler.</source>
        <translation>Preview is running without errors.</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="388" />
        <source>Kein Video geladen: geprüft wird beim nächsten Abspielen.</source>
        <translation>No video loaded: it will be checked on the next playback.</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="394" />
        <source>Abbrechen</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="397" />
        <source>Testen</source>
        <translation>Test</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="404" />
        <source>Trotzdem</source>
        <translation>Save anyway</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="404" />
        <source>Speichern</source>
        <translation>Save</translation>
    </message>
    <message>
        <location filename="../qml/GlslEditor.qml" line="404" />
        <source>Hinzufügen</source>
        <translation>Add</translation>
    </message>
</context>
<context>
    <name>GlslPanel</name>
    <message>
        <location filename="../qml/GlslPanel.qml" line="70" />
        <source>Import: %1 neu, %2 ersetzt, %3 übersprungen</source>
        <translation>Import: %1 new, %2 replaced, %3 skipped</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="75" />
        <source>Die Zwischenablage enthält keinen Text.</source>
        <translation>The clipboard contains no text.</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="88" />
        <source>Beispiele konnten nicht geladen werden (HTTP %1).</source>
        <translation>Could not load the examples (HTTP %1).</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="139" />
        <source>Filter importieren</source>
        <translation>Import filters</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="140" />
        <source>Filter und Shader (*.json *.glsl *.hook *.frag *.txt)</source>
        <translation>Filters and shaders (*.json *.glsl *.hook *.frag *.txt)</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="140" />
        <source>Alle Dateien (*)</source>
        <translation>All files (*)</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="150" />
        <source>Filter exportieren</source>
        <translation>Export filters</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="157" />
        <source>%1 Filter exportiert.</source>
        <translation>%1 filter(s) exported.</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="175" />
        <source>Alle Filter gemeinsam ein- oder ausschalten</source>
        <translation>Turn all filters on or off</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="184" />
        <source>%1 von %2 aktiv</source>
        <translation>%1 of %2 active</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="193" />
        <source>Neu</source>
        <translation>New</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="195" />
        <source>Neuen Filter anlegen</source>
        <translation>Create a new filter</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="200" />
        <source>Einfügen</source>
        <translation>Paste</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="201" />
        <source>Shader-Code oder JSON aus der Zwischenablage übernehmen</source>
        <translation>Use shader code or JSON from the clipboard</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="205" />
        <source>Import</source>
        <translation>Import</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="206" />
        <source>JSON-Export des Userscripts oder Shader-Datei laden</source>
        <translation>Load a JSON export of the userscript or a shader file</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="210" />
        <source>Export</source>
        <translation>Export</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="211" />
        <source>Alle Filter als JSON speichern (mit dem Userscript kompatibel)</source>
        <translation>Save all filters as JSON (compatible with the userscript)</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="218" />
        <source>Lädt …</source>
        <translation>Loading …</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="218" />
        <source>Beispiele</source>
        <translation>Examples</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="220" />
        <source>Beispielfilter des Userscripts von GitHub laden</source>
        <translation>Load the userscript's example filters from GitHub</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="224" />
        <source>Bibliothek</source>
        <translation>Library</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="225" />
        <source>Filter-Bibliothek des Userscripts im Browser öffnen</source>
        <translation>Open the userscript's filter library in the browser</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="234" />
        <source>Suche in Name, Tags, Kategorie …</source>
        <translation>Search name, tags, category …</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="242" />
        <source>Alle</source>
        <translation>All</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="273" />
        <source>Noch keine Filter. Lege einen an, füge Code ein oder importiere eine Datei.</source>
        <translation>No filters yet. Create one, paste code or import a file.</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="274" />
        <source>Kein Filter passt zur Suche.</source>
        <translation>No filter matches the search.</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="360" />
        <source>Taste: %1 (ändern oder entfernen)</source>
        <translation>Key: %1 (change or remove)</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="360" />
        <location filename="../qml/GlslPanel.qml" line="512" />
        <source>Taste festlegen</source>
        <translation>Set key</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="369" />
        <source>Bearbeiten</source>
        <translation>Edit</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="370" />
        <source>Duplizieren</source>
        <translation>Duplicate</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="375" />
        <location filename="../qml/GlslPanel.qml" line="494" />
        <source>Löschen</source>
        <translation>Delete</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="405" />
        <source>Nur im Browser möglich, wird hier nicht abgespielt.</source>
        <translation>Browser only, does not play here.</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="418" />
        <source>Shaderfehler, Filter pausiert: </source>
        <translation>Shader error, filter paused: </translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="420" />
        <source>Erneut</source>
        <translation>Retry</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="438" />
        <source>Reihenfolge: am Griff ⠿ ziehen. Oben läuft zuerst.</source>
        <translation>Order: drag the ⠿ handle. The top filter runs first.</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="466" />
        <source>Doppelte Filter</source>
        <translation>Duplicate filters</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="471" />
        <source>%1 Filter mit gleichem Namen gibt es schon:</source>
        <translation>%1 filter(s) with the same name already exist:</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="476" />
        <location filename="../qml/GlslPanel.qml" line="493" />
        <location filename="../qml/GlslPanel.qml" line="522" />
        <source>Abbrechen</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="477" />
        <source>Überspringen</source>
        <translation>Skip</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="478" />
        <source>Ersetzen</source>
        <translation>Replace</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="490" />
        <source>„%1“ wirklich löschen?</source>
        <translation>Really delete "%1"?</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="518" />
        <source>Drücke eine Taste. Sie schaltet den Filter um und hat Vorrang vor einem gleichen Player-Kürzel.</source>
        <translation>Press a key. It toggles the filter and takes priority over the same player shortcut.</translation>
    </message>
    <message>
        <location filename="../qml/GlslPanel.qml" line="525" />
        <source>Entfernen (%1)</source>
        <translation>Remove (%1)</translation>
    </message>
</context>
<context>
    <name>glslmgr::GlslController</name>
    <message>
        <location filename="../src/GlslController.cpp" line="156" />
        <source>GLSL-Filter: an</source>
        <translation>GLSL filters: on</translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="156" />
        <source>GLSL-Filter: aus</source>
        <translation>GLSL filters: off</translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="178" />
        <source>Der GPU-Treiber hat den Shader abgelehnt (Details im Player-Protokoll).</source>
        <translation>The GPU driver rejected the shader (details in the player log).</translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="195" />
        <source>Shaderfehler in „%1“</source>
        <translation>Shader error in "%1"</translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="238" />
        <source>Shaderdatei nicht schreibbar: </source>
        <translation>Cannot write shader file: </translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="370" />
        <source>Unbenannt</source>
        <translation>Untitled</translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="415" />
        <source> (Kopie)</source>
        <translation> (copy)</translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="450" />
        <source>Der Code darf nicht leer sein.</source>
        <translation>The code must not be empty.</translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="451" />
        <source>Geschweifte Klammern { } passen nicht zusammen.</source>
        <translation>Curly braces { } do not match.</translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="452" />
        <source>Runde Klammern ( ) passen nicht zusammen.</source>
        <translation>Parentheses ( ) do not match.</translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="512" />
        <source>Ungültige JSON-Datei: </source>
        <translation>Invalid JSON file: </translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="524" />
        <source>Keine gültigen Filter in dieser Datei gefunden.</source>
        <translation>No valid filters found in this file.</translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="576" />
        <source>Datei ist zu groß.</source>
        <translation>The file is too large.</translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="577" />
        <source>Datei nicht lesbar: </source>
        <translation>Cannot read file: </translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="592" />
        <source>Keine Filter zum Exportieren vorhanden.</source>
        <translation>There are no filters to export.</translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="599" />
        <source>Datei nicht schreibbar: </source>
        <translation>Cannot write file: </translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="672" />
        <source>: an</source>
        <translation>: on</translation>
    </message>
    <message>
        <location filename="../src/GlslController.cpp" line="672" />
        <source>: aus</source>
        <translation>: off</translation>
    </message>
</context>
</TS>