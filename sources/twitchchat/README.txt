TwitchChat-Plugin für den UVE Player  (cc.ts3x.twitch-chat, v2.2.8)
====================================================================

Der eine native Chat: Der Plugin verbindet sich selbst per IRC über
wss://irc-ws.chat.twitch.tv (kein eingebetteter Twitch-Browser). Dazu:
Emote-Browser, BTTV-, FFZ- und 7TV-Emotes (auch animiert) und Auto-Scroll.

Installation
------------
A) Über den Plugin-Manager des Players: „Plugin installieren“ →
  twitchchat-2.2.8.zip wählen → Plugin beim nächsten Start geladen.
B) Von Hand: Inhalt des ZIP nach
   %APPDATA%\nextscript\UVE Player\plugins\twitchchat\
   legen (twitchchat.dll, manifest.json, ChatPanel.qml, ChatNative.qml,
   Inbox.qml, ChattersPopup.qml, LiveChannels.qml, pn.mp3, README.txt, CHANGELOG.md,
   THIRD_PARTY_NOTICES.md und Ordner dict\)
   und den Player neu starten.
Der Player braucht dafür keine bestimmte Version mehr – WebEngine wird nicht
verwendet. Chat-Panel: Menü bzw. Strg+Alt+T.

Live-Kanäle
-----------
Das Symbol 📺 neben der Zuschauerliste (👥) öffnet das Fenster „Live auf Twitch“
über dem Bild (UVE Player 1.1.3 oder neuer). Reiter:
  Gefolgt     – alle deine Kanäle, die gerade live sind, in jeder Sprache, nach Zuschauern (Anmeldung im Chat nötig)
  Beliebt     – Twitchs Empfehlungen oder nach Zuschauern sortiert, wahlweise in einer Sprache
  Kategorien  – Spiele/Kategorien mit Cover und Zuschauerzahl; Klick zeigt deren Kanäle
  Suche       – Kanäle und passende Kategorien
Jede Karte zeigt Vorschaubild, Name, Titel, Kategorie und Zuschauerzahl. Ein Klick
spielt den Kanal über die normale Wiedergabe des Players (Qualitätsmenü bleibt),
der Chat wechselt in denselben Kanal und das Fenster schließt sich. Ein Klick auf
die Kategorie einer Karte öffnet diese Kategorie. Farben folgen dem Player (hell/dunkel).
Die Suche zeigt Ergebnisse schon beim Tippen. Das Fenster lässt sich am Griff unten rechts
größer und kleiner ziehen; die Größe bleibt für das nächste Öffnen gespeichert.
Oben rechts gibt es zwei Auswahlmenüs:
  ⇅ Sortierung – „Für dich empfohlen“ oder „Zuschauer (viel → wenig)“ (auch für die Kategorienliste; nicht bei „Gefolgt“)
  🌐 Sprache    – alle 35 Twitch-Sprachen oder alle (nicht bei „Gefolgt“ und der Kategorienliste selbst)
Beides ist unabhängig von der Player-Sprache und bleibt gespeichert.
Bei der Kategorienliste zeigt „Für dich empfohlen“ deine meistgeklickten Kategorien zuerst,
dann Kategorien, in denen du geschaut hast; ein Abzeichen auf dem Cover nennt den Grund.
„Für dich empfohlen“: Twitch liefert ohne Website-Login nur die Zuschauer-Reihenfolge, und die
offizielle Twitch-API kennt keine Empfehlungen. Darum empfiehlt das Plugin selbst: gefolgte
Kanäle (angemeldet), Kanäle, die du über dieses Fenster geöffnet hast, und deren Kategorien
kommen nach oben; bei „Beliebt“ kommen Live-Kanäle aus deinen meistgeschauten Kategorien dazu.
Gemerkt werden nur Kanal- und Kategorienamen mit Anzahl, lokal in den Plugin-Einstellungen.

Rechtschreibprüfung
-------------------
Emotes im Eingabefeld: Ein vollständig getippter Emote-Name erscheint direkt im
Feld als Bild (auch animiert). Das Bild bekommt genau den Platz des Namens; bei
sehr langen Namen steht es mittig in einer etwas breiteren Lücke.

Beim Tippen bekommen unbekannte Wörter eine rote Wellenlinie, sobald das Wort
fertig ist. Geprüft wird Deutsch und Englisch (US und britisch, also „color“ und
„colour“): Ein Wort gilt als richtig, wenn es in einer der Sprachen stimmt. Emote-Namen, @Namen, Links, Befehle und
GROSS geschriebene Wörter bleiben außen vor.
Rechtsklick auf ein markiertes Wort: Vorschlag übernehmen oder „Zum Wörterbuch
hinzufügen“. Rechtsklick ins Eingabefeld: Deutsch und Englisch einzeln an- oder
abschalten. Die Rechtschreibprüfung läuft lokal, es wird kein Text verschickt.

Erwähnungen
-----------
Beim Schreiben: „@“ tippen zeigt über dem Eingabefeld Namen aus dem Chat (wer
zuletzt geschrieben hat, zuerst); jeder weitere Buchstabe filtert. Tab oder
Klick setzt „@Name “ ein. Das gilt auch, wenn die Wortvorschläge aus sind.
Nachrichten, die dich mit @deinname ansprechen oder auf deine Nachricht
antworten, stehen im Chat in einem farbig umrandeten Kasten mit „@“. Deine
eigenen Nachrichten, in denen du jemanden mit @Name ansprichst, ebenfalls.

AutoMod
-------
Als Moderator oder Streamer: Nachrichten, die AutoMod zurückhält, stehen im Chat
in einer blauen Box mit „AutoMod“ und dem Grund. „Freigeben“ lässt sie in den
Chat, „Ablehnen“ verwirft sie. Hat jemand anders entschieden oder ist die Frist
abgelaufen, steht das in der Box. Zuschauer sehen davon nichts – Twitch meldet
zurückgehaltene Nachrichten nur Moderatoren.
Meldungen von Twitch selbst (tmi.twitch.tv, z. B. „Deine Nachricht wird von den
Mods geprüft“) und die Ergebniszeilen des Plugins stehen ebenfalls in einer
blauen Box – AutoMod-Hinweise mit „AutoMod“, alle anderen mit „tmi.twitch.tv“.
Sub-, Raid- und Geschenk-Meldungen stehen als „System“ in einer gelben Box, mit
Twitchs Text (z. B. „… hat für 12 Monate abonniert“) und dem Text des Zuschauers.
Ebenfalls gelb: Bits („100 Bits“; „Cheer100“ erscheint als animiertes Cheer-Bild
mit der Zahl dahinter), neue Follower („Follow“ – meldet Twitch nur
Moderatoren und dem Streamer) und Nachrichten bekannter Event-Bots wie
StreamElements, Streamlabs, Nightbot, Fossabot, Moobot, WizeBot, Sery_Bot, Ko-fi,
SoundAlerts, Streamloots, TipeeeStream u. a., die Follows und Spenden ankündigen
(Etikett = Name des Bots).

Nutzer-Menü (Klick auf einen Namen)
-----------------------------------
Ein Klick auf einen Namen im Chat oder in der Zuschauerliste öffnet ein Menü.
Oben stehen Name, Rolle und Kontoalter. Was darunter steht, hängt von deiner
Rolle im Kanal ab:
• Alle: Privat schreiben, @Erwähnen, Name kopieren, Kanal auf Twitch öffnen,
  Blockieren / Blockierung aufheben (Blockierte blendet der Chat aus).
• Moderator (gegenüber normalen Nutzern): Diese Nachricht löschen, Nachrichten
  entfernen (1 s Timeout), Timeout 1 Min. / 10 Min. / 1 Std. / 24 Std.,
  Verwarnen (mit Grund), Bannen (Grund freiwillig), Bann/Timeout aufheben,
  Shoutout (nur während der Stream läuft).
• Streamer zusätzlich: Zum Moderator machen / entfernen, VIP geben / entfernen,
  Diesen Kanal raiden.
Bannen, Verwarnen, Blockieren, Moderator-/VIP-Änderungen und Raids fragen vorher
nach. Das Ergebnis steht als Zeile im Chat („✓ …“ oder „✗ …“ mit dem Grund von
Twitch). Moderatoren und den Streamer kann niemand stummschalten oder bannen.

Private Nachrichten (Postfach)
------------------------------
✉ in der Statuszeile öffnet das Postfach. Bei ungelesenen Nachrichten leuchtet
es und zeigt die Anzahl; jede neue private Nachricht spielt den Ton pn.mp3 mit
30 % Lautstärke (liegt neben der DLL und lässt sich durch eine eigene MP3
gleichen Namens ersetzen). 🔔/🔕 oben im Postfach schaltet den Ton an und aus,
der Regler „Lautstärke“ darunter stellt ihn von 0 bis 100 % ein (Loslassen
spielt eine Hörprobe). Ein Klick auf ein Gespräch zeigt die Nachrichten, unten
schreibst du die Antwort. ✎ schreibt jemand Neuem; „Privat schreiben“ steht
auch im Nutzer-Menü (Klick auf einen Namen).
Voraussetzungen von Twitch: Anmeldung über „Login“ → „Mit Twitch anmelden“
(die Rechte für private Nachrichten sind darin enthalten). Zum Senden verlangt Twitch eine verifizierte Telefonnummer und
erlaubt höchstens 40 verschiedene Empfänger pro Tag. Einen Verlauf von früher
liefert Twitch nicht: Das Postfach beginnt, wenn das Plugin läuft, und speichert
die Gespräche mit Windows-DPAPI verschlüsselt in whispers.dat im Datenordner.

Gruppen: Twitch kennt keine Gruppen-Nachrichten. Eine Gruppe im Plugin ist ein
Verteiler – deine Nachricht geht einzeln an jedes Mitglied, die Mitglieder sehen
einander und ihre Antworten nicht. Antwortet ein Mitglied, erscheint das in der
Gruppe, an die du ihm zuletzt geschrieben hast. Anlegen: 👥 im Postfach, oder in
der Liste „Leute im Chat“ Personen ankreuzen → „Gruppe erstellen“.

Zuschauer
---------
👥 in der Statuszeile zeigt, wie viele den Stream gerade schauen (z. B. „👥 1,2K“,
alle 45 Sekunden aktualisiert), ein Klick öffnet die Namensliste mit Suche. ✉ in
einer Zeile schreibt der Person privat.
Die Namen aller Zuschauer gibt Twitch nur an Moderatoren und den Streamer heraus
(Recht moderator:read:chatters, in der Anmeldung enthalten): Dort kommt die vollständige
Liste aller im Chat Angemeldeten, alle 60 Sekunden aktualisiert. Zuschauer ohne
Twitch-Anmeldung nennt Twitch nie. In fremden Kanälen ohne Moderatorrechte
stehen nur die Namen, die Twitch beim Betreten des Chats meldet – in großen
Kanälen meldet Twitch das gar nicht.

Schriftgröße
------------
„−“ und „+“ rechts in der Statuszeile machen die Chat-Schrift kleiner oder größer
(9 bis 28 Pixel). Emotes, Namen, die angeheftete Nachricht, das Eingabefeld mit
Wortvorschlägen und Satzbau-Hinweisen, das Postfach und die Zuschauerliste
wachsen mit. Die
Größe bleibt gespeichert.

Chat-Text markieren und kopieren
--------------------------------
Mit gedrückter linker Maustaste über eine Nachricht ziehen markiert den Text
direkt in der Zeile, Doppelklick markiert die ganze Nachricht. Strg+C kopiert
(Emotes als Name). Esc oder ein Klick daneben hebt die Markierung auf. Solange
markiert ist, scrollt der Chat nicht weiter. Gescrollt wird mit Mausrad,
Scrollbalken oder in den Lücken zwischen den Nachrichten.
Rechtsklick auf eine Nachricht: „Markierung kopieren“, „Nachricht kopieren“,
„Name kopieren“, „Alles markieren“ und über einem Link „Link kopieren“. Das
Kopieren geht auch bei der angehefteten Nachricht.

Angeheftete Nachricht
---------------------
Hat der Kanal eine Nachricht angeheftet, steht sie als Karte oben, der Chat
beginnt darunter und wird dafür kürzer. Sie zeigt Emotes und klickbare Links.
▾ klappt sie auf eine Zeile zusammen, ▸ wieder auf. ✕ verbirgt sie; 📌 in der
Statuszeile holt sie zurück. Eine neue Anheftung erscheint wieder von selbst. Twitch meldet Anheftungen
nicht über den Chat, das Plugin fragt deshalb alle 45 Sekunden nach (ohne Login).

Links im Chat
-------------
Links wie https://…, www.… oder twitch.tv/name sind blau unterstrichen; ein Klick
öffnet sie im Standardbrowser, der Tooltip zeigt das echte Ziel. Links kommen von
fremden Leuten: Im Zweifel erst den Tooltip ansehen.

Satzbau
-------
Fundstellen bekommen eine blaue Wellenlinie. Über dem Eingabefeld stehen der
Hinweis und die Korrektur zum Anklicken; Rechtsklick auf die Stelle zeigt sie auch.
Eigene Regeln prüfen lokal (doppelte Wörter, Satzanfang, Komma vor „dass“, a/an).
ACHTUNG, DATENVERSAND: „Online mit LanguageTool“ ist ab Werk an. Dann geht dein
getippter Text nach einer kurzen Tipp-Pause an api.languagetool.org, auch wenn du
ihn gar nicht absendest. Dabei werden @Namen, #Tags, Links und bekannte Emotes
ausgeblendet; Nachrichten mit / oder ! am Anfang gehen nie hinaus. Abschalten:
Rechtsklick ins Eingabefeld. Ohne Login, die Gratis-Stufe ist begrenzt und findet
längst nicht jeden Fehler.

Wortvorschläge
--------------
Über dem Eingabefeld stehen bis zu drei Vorschläge; Tab oder Klick übernimmt sie.
Sie entstehen lokal aus Häufigkeitslisten, Emote-Namen und dem, was du und der Chat
schreiben. Folgewörter nach einem Leerzeichen erscheinen erst, wenn sie gelernt
wurden. Eigene Wörter liegen unverschlüsselt in predict.json im Datenordner des
Plugins (Datei löschen = vergessen), Chat-Wörter nur im Arbeitsspeicher.

Chat benutzen
-------------
• Kanalnamen eintippen oder „Auto-Kanal“ anlassen: läuft ein Twitch-Medium
  (Live-URL, VOD-Link, .m3u8/.strm mit Kanalnamen), folgt der Chat automatisch.
• Ohne Login: alles ansehen – anonym (justinfan). Dazu gehören Chat mit Emote-Bildern
  und Abzeichen, Sub-/Raid-/Geschenk-/Bits-/Ankündigungs-Meldungen, Event-Bots,
  Mod-Aktionen (gelöschte Nachrichten, Timeouts mit Dauer, Banns, Chat geleert –
  die betroffenen Nachrichten bleiben lesbar: durchgestrichen in einer grauen
  Box „gelöscht“),
  Meldungen von Twitch, angeheftete Nachricht, Umfragen/Vorhersagen,
  Zuschauerzahl, Kontoalter im Nutzer-Menü sowie Beliebt/Kategorien/Suche.
  Der Login wird nur zum Schreiben gebraucht und für Dinge, die Twitch allein
  an dein Konto bindet: Senden, private Nachrichten, Abo-Emotes, „Gefolgt“,
  Blockieren und Moderieren. AutoMod-Boxen und neue Follower meldet Twitch nur
  Moderatoren und dem Streamer.
• Zum Selber-Schreiben, am einfachsten: „Login“ → „Mit Twitch anmelden“. Der
  Browser öffnet twitch.tv/activate mit fertigem Code; nach „Autorisieren“
  trägt das Plugin Name und Token selbst ein und erneuert den Token danach
  von allein (auch nach einem Neustart). „Logout“ widerruft ihn bei Twitch.
  Angefragt werden alle 81 Twitch-Rechte (Wunsch: keine neue Anmeldung für
  spätere Funktionen). Twitch-App: Client-ID iaecrrj7jo49svbbbpg1o33gzcajt0
  (öffentlicher Client, kein Geheimnis).
• Auto-Scroll (Standard an): der Chat hält die Ansicht am Ende und folgt
  jeder neuen Nachricht nach unten – auch während Emote-Bilder nachgeladen
  werden. Ausgeschaltet entscheidet wie früher: nur wer unten steht, folgt.

Emotes im Chat (was angezeigt wird)
-----------------------------------
• Native Twitch-Emotes: aus den Markierungen der Nachricht – Bild aus
  static-cdn.jtvnw.net, animierte laufen als GIF (auch wenn Twitch sie als
  „.png“ ausliefert).
• BTTV-, FFZ- und 7TV-Emotes: Namen aus den öffentlichen Global- und Kanal-Listen
  (Kanalerkennung über die Raum-ID, die Twitch in jeder Nachricht nennt; ohne Login).
  Ein Emote zählt als ganzes, durch Leerzeichen getrenntes Wort in exakter
  Schreibweise. Bilder kommen vom CDN des Anbieters, animierte laufen im Chat
  (Cache unter emotes\7tv\, emotes\bttv\ und emotes\ffz\).
  Vorrang bei gleichem Namen: Kanal vor global, dann 7TV vor BTTV vor FFZ.
• Zero-Width-Emotes liegen über dem Emote davor. Effekt-Befehle wie BTTV „h!“
  oder FFZ „ffzSpin“ bleiben Text; ihre Effekte werden nicht dargestellt.
  Achtung beim SENDEN dieser Emotes: du schreibst nur den Namen; andere sehen das
  Bild nur mit der passenden Erweiterung, Twitch allein kennt ihn nicht – dieser
  Plugin stellt ihn zusätzlich für dich dar.

Emote-Browser (Smiley-Icon vor „Senden“)
----------------------------------------
• Ansehen: Kacheln mit Emote-Bild und Name; „BTTV“, „FFZ“, „7TV“ nennen den Anbieter,
  animierte Kacheln bewegen sich.
• Kategorien: „Alle“, „Abos“, „Twitch“, „7TV“, „BTTV“, „FFZ“, „Favoriten“. Überschriften
  trennen Anbieter und Kanal/Global. Suche-Feld filtert live.
• „Abos“ zeigt die Emotes abonnierter Kanäle (Login nötig). „Twitch“ zeigt Kanal,
  Global und unter „TwitchHypeTrain“ deine freigeschalteten HypeTrain-Emotes (Token mit
  Recht user:read:emotes nötig). Kanal-Emotes ohne Abo sind abgeblendet.
• Emote antippen → sein Name erscheint in der Eingabezeile; „Senden“ schickt
  die Nachricht (Twitch erkennt native Emotes am Namen).
• ★/☆ oben rechts an der Kachel oder Rechtsklick: Emote favorisieren (bleibt gespeichert).
• Woher die Listen kommen:
  – Ohne Token: alle Emotes, die im Chat schon gesehen wurden (Namen+IDs holt
    sich der Plugin aus den IRC-Emote-Markierungen jeder Nachricht), die
    BTTV-, FFZ- und 7TV-Listen (anonym) sowie die Twitch-Listen für Kanal
    und Global (gql.twitch.tv, öffentliche Web-Client-ID –
    kein Client-Secret nötig). Klappt das nicht (offline/Netzfehler), zeigt
    der Browser automatisch die gesehenen Emotes; ein Hinweis steht im Log
    („Emote-Vollliste … nicht abrufbar“).
  – Mit Token-Login: zusätzlich deine Abo-Emotes; mit dem Recht user:read:emotes auch
    einzeln freigeschaltete wie HypeTrain (Twitch-API).

Sicherheit
----------
• Kein Client-Secret im Plugin, nirgends.
• Token nur DPAPI-verschlüsselt in tokens.dat; nie in Einstellungen/Log.
• Favoriten sind nur Name + Emote-ID (keine Tokens).
• BTTV-, FFZ- und 7TV-Abfragen sind rein öffentlich (API + CDN), ohne jegliche Zugangsdaten.
• Login passiert ausschließlich auf der Twitch-eigenen Seite twitch.tv/activate
  (Device-Code); dem Plugin nie ein Passwort geben.

Technik
-------
• Native IRCv3-Tags über WebSocket; Emote-Bilder von static-cdn.jtvnw.net
  (Cache: <Plugin-Daten>/emotes/<id>.png) sowie cdn.betterttv.net,
  cdn.frankerfacez.com und cdn.7tv.app
  (Cache: <Plugin-Daten>/emotes/<anbieter>/<id>.webp|.gif|.png); Kanalerkennung per
  URL-/Pfadmuster; Reconnect mit Backoff; Limit 1,2 s zwischen zwei
  gesendeten Nachrichten.
• Nachrichten werden als Segment-Folge (Text-Wörter + Emote-Bild) aufgebaut
  und im Panel als Flow dargestellt – nur so können WebP und GIF wirklich laufen
  (RichText-<img> kann keine Bewegung zeigen).
• Kataloge via GET an api.betterttv.net/3/cached, api.frankerfacez.com/v1 und
  7tv.io/v3 (global; Kanal über die Ziffern-Raum-ID aus der room-id-Markierung –
  Login-Namen gehen dort nicht).
• Emote-Volllisten via POST an gql.twitch.tv (Client-ID kimne… – die öffentliche
  Web-Client-ID von Twitch; Autorisierung mit dem OWNEN User-Token als Bearer).
• Einstellungen: channel, autoChannel, autoscroll, favoriteEmotes, roomIds
  (gemerkte Twitch-Raum-IDs pro Kanal, nur Ziffern). Ein altes embedMode-Feld
  wird beim Speichern entfernt.
• Build: Qt 6.12 MSVC 2022 x64, C++20; Quellen unter src\, Tests (chat_test,
  browser_test, plugin_probe) laufen über CMake.
