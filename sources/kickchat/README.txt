KickChat-Plugin für den UVE Player  (cc.ts3x.kick-chat, v1.3.1)
================================================================

Der Kick-Chat direkt im Player – wie der Twitch-Chat-Plugin, nur für kick.com.
Der Player selbst wird dafür nicht verändert.

Installation
------------
A) Über den Plugin-Manager des Players: Einstellungen → Plugins →
   „Plugin installieren …“ → kickchat-1.3.1.zip wählen → Player neu starten.
B) Von Hand: Inhalt des ZIP nach
   %APPDATA%\nextscript\UVE Player\plugins\kickchat\
   legen (kickchat.dll, manifest.json, ChatPanel.qml, KickChat.qml, pn.mp3, README.txt,
   CHANGELOG.md, THIRD_PARTY_NOTICES.md und Ordner dict\) und den Player neu starten.
Chat-Panel: Seitenleiste „Kick Chat“, Menü Plugins → „Kick Chat öffnen“ oder Strg+Alt+K.

Kanal automatisch
-----------------
Wird ein Kick-Stream geöffnet (z. B. https://kick.com/jomotic), nimmt der Chat den
Kanalnamen automatisch und verbindet sich. Abschalten: ☰ → „Auto-Kanal“.
Von Hand: Kanalnamen oder kick.com-Link oben eintippen → Enter / „Verbinden“.

Was angezeigt wird
------------------
• Live-Chat (ohne Anmeldung) inklusive der letzten Nachrichten beim Verbinden.
• Kick-Emotes (Kanal, Global, Emojis) – auch animiert – und 7TV-Emotes
  (global und, falls vorhanden, die 7TV-Emotes des Kick-Kanals).
• Abzeichen: Level-Abzeichen und Abo-Abzeichen des Kanals als Bild; Streamer, Mod,
  VIP, OG, Founder, Gifter, Verified, Bot … als farbiges Etikett (Tooltip = Details).
• Antworten („↪ @Name: …“), gelöschte Nachrichten und Timeouts/Banns ausgegraut.
• Gelbe Box für Abos, Geschenk-Abos, Hosts, Kicks, eingelöste Belohnungen,
  Stream live/offline, Chat geleert.
• Erwähnungen deines Namens grün umrandet (wenn angemeldet).
• Angeheftete Nachricht als Karte oben; ✕ blendet sie aus, 📌 holt sie zurück.
• Zuschauer: 👥 in der Statuszeile zeigt die Zuschauerzahl (alle 60 s), ein Klick
  öffnet „Leute im Chat“ mit Suche: Streamer, Mods und VIPs zuerst, dann wer zuletzt
  geschrieben hat (mit Anzahl Nachrichten). Klick auf einen Namen: @Erwähnen, Name
  kopieren, Kanal öffnen. Eine Namensliste ALLER Zuschauer gibt Kick nicht heraus –
  die Liste enthält alle, die seit dem Verbinden (inkl. Verlauf) geschrieben haben.
• Statuszeile: LIVE, ⚙ zeigt Chat-Modi
  (Langsam-, Follower-, Abo-, Emote-Modus), Tooltip auf dem Kanal = Stream-Titel.
• „−“/„+“: Schriftgröße (9–28 px), Emotes wachsen mit. ☰ → Uhrzeit anzeigen,
  Auto-Scroll, Neu verbinden, Kanal im Browser öffnen.
• Links sind klickbar (Tooltip zeigt das Ziel). Rechtsklick auf eine Nachricht:
  Antworten, Nachricht/Name kopieren, @Erwähnen, Kanal der Person öffnen.
  Klick auf einen Namen setzt „@Name“ ins Eingabefeld.

Nutzer-Menü (Klick auf einen Namen)
-----------------------------------
Klick auf einen Namen im Chat oder in „Leute im Chat“ öffnet eine Karte: Profilbild,
Rolle, Abzeichen, Konto erstellt, folgt seit, Abo-Monate, ggf. Bann/Timeout-Status.
• Alle: @Erwähnen, Antworten, Name kopieren, Kanal auf kick.com, Blockieren
  (Nachrichten blockierter Nutzer werden ausgeblendet; Liste in ☰ → Benachrichtigungen).
• Moderatoren und Streamer (nicht gegen Streamer/Mods): Nachricht löschen, Timeout
  1 Min / 10 Min / 1 Std / 24 Std / 7 Tage, Bannen (mit Grund, fragt vorher nach),
  Bann/Timeout aufheben. Rechtsklick auf eine Nachricht: „Nachricht löschen (Mod)“.
  Das Ergebnis steht über dem Eingabefeld („✓ …“ oder „✗ …“ mit Grund).
  Hinweis: Wer sich vor dieser Version angemeldet hat, meldet sich einmal ab und
  wieder an, damit Kick die Moderationsrechte freigibt.

Benachrichtigungen & Boxen (🔔 in der Statuszeile oder ☰ → „Benachrichtigungen & Boxen …“)
-------------------------------------------------------------------------------------------
• Ton (pn.mp3, 30 %) bei Erwähnung (@deinname) oder Antwort auf deine Nachricht;
  🔔/🔕 oben im Fenster schaltet ihn um (die Glocke in der Statuszeile zeigt 🔕, wenn er
  aus ist), Lautstärke-Regler (Loslassen spielt eine Hörprobe) und ▶.
  pn.mp3 neben der DLL lässt sich durch eine eigene MP3 gleichen Namens ersetzen.
• Farbige Boxen mit Etikett: Erwähnung (@), System (Abos, Geschenke, Hosts, Live),
  Follow, Kicks, Event-Bots (Etikett = Name des Bots), Moderation. Farben je Art
  wählbar, „Farben zurücksetzen“.
• Farben: Palette oder „Eigene:“ mit #RRGGBB → „Übernehmen“ (für Boxen und Bots).
• Event-Bots: ✎ bearbeitet Name, Etikett und Farbe der Bot-Box (eigene Farbe oder „–“ =
  Standardfarbe der Bot-Boxen), ✕ entfernt; neue Bots mit Name, Etikett und Farbe.
  Bekannte Bots: BotRix, KickBot, Kicklet, StreamElements, Streamlabs, Fossabot, Nightbot,
  Sery_Bot, WizeBot, Moobot, TipeeeStream, SoundAlerts, Ko-fi – eigene hinzufügen
  oder entfernen, „Standard-Bots wiederherstellen“.
• Neue Follower im Chat ein-/ausblenden.

Umfragen
--------
Startet der Kanal eine Umfrage, steht sie als Karte oben (Antworten mit Balken,
Prozent, Stimmen, Restzeit). ✕ blendet sie aus, 📊 holt sie zurück. Abstimmen geht
nur auf kick.com (Kick bietet dafür keine Schnittstelle).

Eingabefeld
-----------
Ein getippter Emote-Name erscheint direkt im Feld als Bild (auch animiert), genau so
breit wie das Bild – ohne Lücke. Klick außerhalb des Feldes (Chat, Video, Player)
oder Esc gibt die Tastatur wieder frei, damit die Player-Tasten (Leertaste …) gehen.

Rechtschreibung und Wortvorschläge
----------------------------------
Beim Tippen bekommen unbekannte Wörter eine rote Wellenlinie, sobald das Wort fertig ist.
Geprüft wird Deutsch und Englisch (US und britisch): Ein Wort gilt als richtig, wenn es in
einer der Sprachen stimmt. Emote-Namen, @Namen, Links, Befehle und GROSS geschriebene
Wörter bleiben außen vor. Rechtsklick auf ein markiertes Wort: Vorschlag übernehmen oder
„Zum Wörterbuch hinzufügen“. Dort und im ☰-Menü lassen sich Deutsch, Englisch und die
Wortvorschläge einzeln an- und abschalten. Alles läuft lokal (Hunspell), kein Text geht hinaus.
Wortvorschläge: Über dem Eingabefeld stehen bis zu drei Vorschläge; Tab oder Klick
übernimmt. Sie entstehen lokal aus Häufigkeitslisten, Emote-Namen und dem, was du und der
Chat schreiben. Eigene Wörter liegen unverschlüsselt in predict.json im Datenordner des
Plugins (Datei löschen = vergessen), Chat-Wörter nur im Arbeitsspeicher.

Satzbau
-------
Fundstellen bekommen eine blaue Wellenlinie. Über dem Eingabefeld stehen der Hinweis und
die Korrektur zum Anklicken; Rechtsklick auf die Stelle zeigt sie auch.
• Lokal (Standard an, ohne Netz): doppelte Wörter, Satzanfang, Komma vor „dass“, a/an.
• ACHTUNG, DATENVERSAND: „Online mit LanguageTool“ ist ab Werk AN. Dann geht dein
  getippter Text nach einer kurzen Tipp-Pause an api.languagetool.org – auch wenn du ihn
  gar nicht absendest. @Namen, #Tags, Links und Emotes werden ausgeblendet; Nachrichten
  mit / oder ! am Anfang gehen nie hinaus. Abschalten: ☰ → „Satzbau online mit
  LanguageTool“ oder Rechtsklick ins Eingabefeld. Ohne Login; die Gratis-Stufe ist
  begrenzt und findet längst nicht jeden Fehler.

Chat-Text markieren und kopieren
--------------------------------
Mit gedrückter linker Maustaste über eine Nachricht ziehen markiert den Text direkt in der
Zeile, Doppelklick markiert die ganze Nachricht. Strg+C kopiert (Emotes als Name). Esc oder
ein Klick daneben hebt die Markierung auf; solange markiert ist, scrollt der Chat nicht
weiter. Gescrollt wird mit Mausrad oder Scrollbalken.
Rechtsklick auf eine Nachricht: „Markierung kopieren“, „Alles markieren“, „Nachricht
kopieren“, „Name kopieren“ …

Was es bei Kick nicht gibt
--------------------------
• Private Nachrichten (Postfach): Kick bietet dafür keine Schnittstelle – weder in der
  offiziellen API noch öffentlich. Deshalb gibt es im Kick-Chat kein Postfach.
• Liste ALLER Zuschauer-Namen, Mod/VIP vergeben, Raids/Hosts starten, Verwarnen,
  Shoutouts: nicht in Kicks Schnittstelle.

Emote-Browser (☺ links neben dem Eingabefeld)
---------------------------------------------
Tabs Alle / Kanal / Global / Emojis / 7TV / ★, Suche, Klick fügt das Emote ein,
Rechtsklick = Favorit (★ bleibt gespeichert). Abo-Emotes sind leicht abgeblendet.
Beim Senden werden Kick-Emote-Namen automatisch als Kick-Emote verschickt;
7TV-Emotes sehen andere nur mit der 7TV-Erweiterung.

Schreiben (Anmeldung)
---------------------
☰ → „Bei Kick anmelden …“ → „Anmelden“ (oder „Anmelden“ neben dem Eingabefeld).
Der Browser öffnet id.kick.com – dort mit dem eigenen Kick-Konto bestätigen, fertig.
Eine eigene Kick-App ist nicht nötig. Die Tokens liegen nur Windows-DPAPI-verschlüsselt
in tokens.dat im Datenordner des Plugins (nie in den Einstellungen oder im Log) und
werden automatisch erneuert. „Abmelden“ widerruft den Token bei Kick.
Die Anmeldung nutzt kurz den lokalen Port 53135 (http://localhost:53135/callback).
Mit Anmeldung: Antworten (Rechtsklick → Antworten), @-Vorschläge beim Tippen
(Tab übernimmt den ersten), höchstens eine Nachricht pro Sekunde, max. 500 Zeichen.

Technik
-------
• Kanal-Infos/Zuschauer: kick.com/api/v2/channels/<kanal>; Verlauf:
  kick.com/api/v2/channels/<id>/messages; Emotes: kick.com/emotes/<kanal>;
  7TV: 7tv.io/v3. Alles öffentlich, ohne Anmeldung.
• Live-Chat: Kicks Pusher-WebSocket (wss://ws-us2.pusher.com), Kanäle
  chatrooms.<id>.v2 und channel.<id>; Neuverbinden mit Backoff.
  Kick hat den Pusher-Schlüssel im Oktober 2026 gewechselt. Ändert er sich
  wieder, kann man ihn ohne neue DLL in den Plugin-Einstellungen als
  „pusherKey“ (und „pusherCluster“) eintragen.
• Bild-Cache: <Plugin-Daten>\emotes\kick|7tv|img\.
• Build: Qt 6.12 MSVC 2022 x64, C++20; Quellen unter src\, Tests: chat_test
  (Parser + echter Kanal), qml_panel_test (Panel offscreen mit echtem Chat),
  plugin_probe (DLL als Plugin erkennbar).
