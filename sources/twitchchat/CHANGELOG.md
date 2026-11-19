# Änderungsgeschichte – TwitchChat-Plugin (cc.ts3x.twitch-chat)

## 2.2.8 (2026-11-18) – Kanal auch bei Wiedergabe über einen Stream-Proxy
- Wird ein Twitch-Stream über einen Proxy abgespielt, der dem Player nur `tcp://host:port` oder eine
  `…/play.php?…`-Playlist übergibt, kam der Chat bisher nicht in den Kanal: in der Adresse steht kein Kanalname.
  Folge: kein Beitritt, daher auch keine angeheftete Nachricht, keine Umfragen/Vorhersagen und keine Zuschauerzahl.
- Jetzt liest der Chat in diesem Fall den Medientitel (der Proxy setzt ihn auf den Kanalnamen) und tritt dem Kanal
  automatisch bei. Lokale Dateien und andere Adressen sind davon nicht betroffen.

## 2.2.7 (2026-11-17) – Alles ansehen ohne Login, Login nur zum Schreiben
- Mod-Aktionen stehen jetzt genau im Chat, auch ohne Anmeldung: „Nachricht von X wurde gelöscht: „…““,
  „X wurde für 10 Min. stummgeschaltet (Timeout)“, „X wurde gebannt“, „Der Chat wurde von einem Moderator geleert“.
  Bisher stand dort immer nur „Ein Nutzer wurde temporär stummgeschaltet“ – auch bei Bann oder geleertem Chat.
- Betroffene Nachrichten verschwinden nicht: Sie stehen durchgestrichen in einer grauen Box mit „gelöscht“
  (auch Emote-Bilder bekommen die Linie).
- Nutzer-Menü: Das Kontoalter erscheint jetzt auch ohne Login (öffentlich über gql.twitch.tv).
- Unverändert ohne Login: Chat, Emotes, Abzeichen, Sub-/Raid-/Bits-/Bot-Meldungen, angeheftete Nachricht,
  Umfragen/Vorhersagen, Zuschauerzahl, Beliebt/Kategorien/Suche. Der Login bleibt nur für Schreiben und
  kontogebundene Dinge nötig (Senden, private Nachrichten, Abo-Emotes, „Gefolgt“, Blockieren, Moderieren).

## 2.2.6 (2026-11-17) – „Gefolgt“ ohne „Für dich empfohlen“
- Im Reiter **Gefolgt** gibt es die Sortierung „Für dich empfohlen“ nicht mehr: das Menü ⇅ und der Empfehlungs-Hinweis
  sind dort ausgeblendet, gefolgte Live-Kanäle stehen immer nach Zuschauern (viel → wenig).
- In allen anderen Reitern (Beliebt, Kategorien, Kanäle einer Kategorie, Suche) bleibt „Für dich empfohlen“ unverändert;
  die gespeicherte Sortierung wird durch „Gefolgt“ nicht verändert.

## 2.2.5 (2026-11-16) – „Für dich empfohlen“ wirkt, Filter in allen Reitern
- „Für dich empfohlen“ zeigte dieselbe Reihenfolge wie „Zuschauer“: Twitch personalisiert nur mit dem eigenen
  Website-Login (auch mit dem Empfehlungskontext der Webseite kam ohne ihn dieselbe Liste), und die offizielle Helix-API
  hat keine Empfehlungen. Jetzt empfiehlt das Plugin selbst: gefolgte Kanäle, im Fenster geöffnete Kanäle und deren
  Kategorien stehen oben, danach zählen die Zuschauer. Bei „Beliebt“ kommen Live-Kanäle aus den meistgeschauten
  Kategorien dazu. Gemerkt werden nur Kanal- und Kategorienamen mit Anzahl (lokal, max. 200 je Liste).
  Solange noch nichts gemerkt ist, erklärt ein Hinweis, warum vorerst die Zuschauerzahl entscheidet.
- „Zuschauer (viel → wenig)“ sortiert in jeder Liste streng nach Zuschauern.
- Sortierung jetzt auch im Reiter **Kategorien**: „Für dich empfohlen“ zeigt dort die meistgeklickten Kategorien zuerst
  (nach Anzahl der Klicks, auch wenn sie nicht zu den 30 größten gehören – sie werden dann gezielt nachgeladen), danach
  Kategorien, in denen du Kanäle geschaut hast (auch diese werden nachgeladen), dann nach Zuschauern. Ein Abzeichen
  auf dem Cover zeigt den Grund („★ 2× geöffnet“, „★ geschaut“, „♥ Gefolgte live“).
- **Gefolgt** zeigt alle gefolgten Kanäle, die gerade live sind (auch mehr als 100), ohne Sprachfilter; Offline-Kanäle
  erscheinen nicht. Das Sprachmenü ist dort ausgeblendet, die Sortierung bleibt.
- Sprache auch bei **Suche** (dort filtert das Plugin selbst, Twitch liefert die Sprache je Kanal mit).

## 2.2.4 (2026-11-16) – Sortierung und alle Sprachen als Auswahlmenü
- „Live auf Twitch“: Neues Auswahlmenü ⇅ für die Sortierung – **Für dich empfohlen** (Twitchs Empfehlung, Vorgabe) oder
  **Zuschauer (viel → wenig)**. Gilt für „Beliebt“ und die Kanäle einer Kategorie.
- Die Sprache ist jetzt ein Auswahlmenü mit allen Sprachen, die Twitch im Filter annimmt (35, z. B. Deutsch, English,
  日本語, 中文（香港）, Gebärdensprache ASL, Andere), statt nur der Player-Sprache. „Alle Sprachen“ steht oben.
- Sortierung und Sprache bleiben gespeichert und gelten beim nächsten Öffnen wieder.
- Hinweis: „Für dich empfohlen“ ist ohne Twitch-Weblogin nicht auf dein Konto zugeschnitten; der Chat-Login gehört zu einer
  anderen App-Kennung und wird dafür von Twitch nicht angenommen.

## 2.2.3 (2026-11-16) – Fenster in der Größe ziehen, Suche beim Tippen
- „Live auf Twitch“ lässt sich am Griff unten rechts größer und kleiner ziehen. Die Größe bleibt gespeichert und gilt
  beim nächsten Öffnen wieder (der Player begrenzt sie auf den Bildbereich).
- Suche ohne Enter: Ergebnisse erscheinen kurz nach dem Tippen. Ein Reiterwechsel währenddessen wird nicht mehr überschrieben.
- Behebung: Profilbilder (und die Ecken der Vorschaubilder) ragten beim Mouse-over mit einem Rahmen über die Karte hinaus.
  Die runden Ecken liegen jetzt vollständig innerhalb des Bildes.

## 2.2.2 (2026-11-16) – Live auf Twitch
- Neben der Zuschauerliste (👥) gibt es das Symbol 📺. Es öffnet ein eigenes Fenster über dem Bild: „Live auf Twitch“.
- Reiter **Gefolgt** (deine Kanäle, die gerade live sind – braucht die Anmeldung im Chat), **Beliebt** (die meisten
  Zuschauer, mit 🌐 nur in der eigenen Sprache), **Kategorien** (Cover und Zuschauer je Spiel/Kategorie; ein Klick zeigt
  deren Live-Kanäle) und **Suche** (Kanäle und passende Kategorien). Beliebt, Kategorien und Suche gehen ohne Login.
- Jede Kanal-Karte zeigt Vorschaubild, Profilbild, Name, Titel, Kategorie (als Chip, ein Klick öffnet die Kategorie)
  und Zuschauerzahl. Der laufende Kanal ist markiert („LÄUFT“).
- Ein Klick auf eine Karte spielt den Kanal über die vorhandene Wiedergabe des Players ab (Qualitätsmenü bleibt erhalten),
  der Chat wechselt in denselben Kanal und das Fenster schließt sich.
- Modernes Aussehen mit runden Karten und Pillen-Reitern; die Farben folgen dem Player (hell und dunkel). Mouse-over:
  die Karte hebt sich an, das Bild wird abgedunkelt und zeigt einen ▶-Knopf. Beim Laden erscheinen Platzhalter-Karten.
- Das Fenster braucht UVE Player 1.1.3 oder neuer; bei älteren Playern bleibt das Symbol ausgeblendet.

## 2.2.1 (2026-11-15)
- Die Plugin-Beschreibung in *Einstellungen → Plugins* gibt es jetzt in allen Player-Sprachen (de, en, es, it, pl, tr, el); der Player zeigt automatisch die eingestellte Sprache.

## 2.2.0 (2026-11-08) – Eingabefeld gibt die Tastatur frei, Emotes ohne Ränder
(Nachfolger von 2.1.9; die ältere 2.2.0 vom 06.11. steht weiter unten.)
- Ein Klick neben das Eingabefeld beendet die Eingabe, auch außerhalb des Chats. Danach
  reagieren die Tastenkürzel des Players wieder. „Senden“, Emote-Kacheln und Vorschläge
  lassen den Cursor im Feld, damit man weiterschreiben kann.
- Emotes im Eingabefeld sind genau so breit wie ihr Bild, ohne leere Ränder links und rechts.
  Der Text dahinter bleibt der Emote-Name: Senden, Rechtschreibung und Vorschläge sind unverändert.

## 2.1.9 (2026-11-08) – Abzeichen als Bild, du in der Zuschauerliste
- Im Chat stehen vor dem Namen jetzt die echten Twitch-Abzeichen als Bild, auch die eigenen
  Abo-Abzeichen des Kanals und alle, die vorher gar nicht angezeigt wurden (z. B. Bits,
  Geschenk-Subs). Der Katalog ist öffentlich, geht also auch ohne Login; die Bilder bleiben im
  Cache. Das Textschild (MOD, VIP …) erscheint nur noch, solange das Bild fehlt.
- Zuschauerliste: Wer angemeldet ist, sieht sich selbst ganz oben mit „(du)“, Rolle und Farbe.
  In dieser Zeile gibt es kein Ankreuzen und kein ✉, weil man sich nicht selbst schreiben kann.

## 2.1.8 (2026-11-08) – Knopf zum Twitch-Chat immer sichtbar
- Behebung: Bei gesperrter Vorhersage (und nach dem Ende) fehlte der Knopf. Er ist jetzt
  immer da: „Auf Twitch abstimmen ↗“, solange abgestimmt werden kann, sonst
  „Im Twitch-Chat öffnen ↗“.
- Ohne laufende Abstimmung wird alle 20 s statt alle 45 s nachgefragt, damit kurze
  Vorhersagen (ab 30 s) nicht schon gesperrt sind, bevor die Karte erscheint.

## 2.1.7 (2026-11-08) – Abstimmen über Twitch
- Umfrage- und Vorhersage-Karte haben den Knopf „Auf Twitch abstimmen ↗“, solange noch
  abgestimmt werden kann. Er öffnet den Pop-out-Chat des Kanals im Browser
  (twitch.tv/popout/<kanal>/chat); dort stimmt man eingeloggt wie gewohnt ab.
- Direkt im Plugin abstimmen geht nicht: Twitch bietet Zuschauern dafür keine Schnittstelle.

## 2.1.6 (2026-11-08) – Umfragen und Vorhersagen als Karte
- Laufende Umfragen 📊 und Vorhersagen 🔮 des Kanals erscheinen als Karte über dem Chat –
  wie die angeheftete Nachricht, darunter gestapelt; der Chat beginnt unter den Karten.
- Umfrage: Frage, Balken je Antwort mit Prozent und Stimmen, Restzeit; nach dem Ende
  noch 3 Minuten das Ergebnis (✔ beim Sieger).
- Vorhersage: Ausgänge in Blau/Pink mit Prozent, Kanalpunkten und Teilnehmern, Restzeit,
  „Gesperrt“ nach Ablauf, nach der Auflösung 3 Minuten der Gewinner (✔). Abgebrochene
  Vorhersagen werden nicht gezeigt.
- ▾/▸ klappt eine Karte auf eine Zeile mit der Frage zusammen (bleibt gespeichert),
  ✕ verbirgt sie, 📊 in der Statuszeile holt sie zurück.
- Ohne Login: Daten über gql.twitch.tv, alle 15 s solange abgestimmt wird, sonst alle 45 s (ab 2.1.8: 20 s).
  Abstimmen selbst geht weiter nur auf twitch.tv.

## 2.1.5 (2026-11-08) – Benachrichtigungs-Boxen einstellbar
- Neue Glocke 🔔 in der Statuszeile öffnet das Fenster „Benachrichtigungs-Boxen“.
- Farbe jeder Box-Art frei wählbar (Erwähnung, System, Follow, Bits, Event-Bots, tmi.twitch.tv,
  AutoMod): 18 Vorgaben oder eigener Hex-Wert, „↺“ bzw. „Standardfarbe“ setzt zurück.
- Event-Bots: eigene Bots per Twitch-Name eintragen (mit Etikett und eigener Farbe); die
  mitgelieferten Bots lassen sich ebenso bearbeiten, umfärben oder entfernen. „Bot-Liste
  zurücksetzen“ stellt die Standardliste wieder her.
- Änderungen gelten sofort, auch für Nachrichten, die schon im Chat stehen, und bleiben gespeichert.

## 2.1.4 (2026-11-08) – Follows, Bits und Event-Bots
- Neue Follower erscheinen als gelbe Box „Follow“ (Twitch-EventSub `channel.follow`, nur für
  Moderatoren und den Streamer).
- Cheers mit Bits stehen in einer gelben Box mit „N Bits“; Cheer-Wörter wie „Cheer250“ erscheinen als
  animiertes Cheer-Bild mit der Anzahl (globale Cheer-Emotes immer, kanaleigene der Partner nach der
  Anmeldung über Helix).
- Nachrichten bekannter Bots, die Follows/Spenden/Subs ankündigen (StreamElements, Streamlabs, Nightbot,
  Fossabot, Moobot, WizeBot, Sery_Bot, Ko-fi, SoundAlerts, StreamStickers, Streamloots, TipeeeStream,
  Lumia Stream, Blerp u. a.), stehen in einer gelben Box mit dem Bot-Namen.

## 2.1.3 (2026-11-08) – Twitch-Meldungen in der blauen Box
- Meldungen von tmi.twitch.tv (NOTICE) und die Systemzeilen „tmi“ stehen jetzt wie AutoMod in einer
  blauen Box. AutoMod-Hinweise an dich (z. B. „Deine Nachricht wird von den Mods geprüft“) tragen das
  Etikett „AutoMod“, alle anderen „tmi.twitch.tv“.
- Sub-, Raid-, Geschenk-Sub- und ähnliche Meldungen stehen als „System“ in einer gelben Box.
- Behebung: Bei diesen Meldungen fehlte Twitchs Text (falscher Feldname), und Raids ohne eigenen Text
  erschienen gar nicht.

## 2.1.2 (2026-11-08) – AutoMod im Chat
- Als Moderator oder Streamer erscheinen von AutoMod zurückgehaltene Nachrichten im Chat in einer
  blauen Box mit Grund und den Knöpfen „Freigeben“ / „Ablehnen“. Entscheidungen anderer Moderatoren
  und abgelaufene Fristen werden in der Box nachgetragen. Twitch-EventSub `automod.message.hold` /
  `automod.message.update` (v2), Freigabe über Helix `moderation/automod/message`.

## 2.1.1 (2026-11-08) – Emotes im Eingabefeld, Login aufgeräumt
- Deine eigenen Nachrichten, in denen du jemanden mit @Name ansprichst, stehen jetzt ebenfalls im
  Erwähnungs-Kasten. Eigene Nachrichten ohne @ bleiben wie bisher.
- Emotes erscheinen direkt im Eingabefeld als Bild, sobald der Name vollständig getippt ist (auch
  animiert). Die Vorschau-Zeile aus 2.1.0 entfällt.
- Behebung: Erwähnungen (@deinname) wurden nicht erkannt, wenn der unter „Login“ eingetragene Name
  nicht genau dem Twitch-Login entsprach. Jetzt zählen der bestätigte Login und der Anzeigename laut
  Twitch.
- Anmeldung nur noch über „Mit Twitch anmelden“: Felder für Name und Token, „Speichern“ und der
  ⚡-Knopf (twitchtokengenerator) sind entfernt. Ein früher von Hand eingefügter Token gilt weiter,
  bis er abläuft; der Login-Bereich bittet dann um eine neue Anmeldung.

## 2.1.0 (2026-11-08) – Anmelden ohne Token-Einfügen
Nachfolger von 2.0.9; nicht zu verwechseln mit dem älteren 2.1.0 vom 2026-11-06 weiter unten.
- „Mit Twitch anmelden“ unter „Login“: öffnet twitch.tv/activate mit fertigem Code; nach „Autorisieren“
  übernimmt das Plugin Name und Token selbst (Twitch-Device-Code-Anmeldung über eine eigene,
  öffentliche Twitch-App, ohne Passwort und ohne Geheimnis). Angefragt werden alle 81 Twitch-Rechte.
- Der Token erneuert sich 10 Minuten vor Ablauf selbst, auch nach einem Neustart; der Chat bleibt
  dabei verbunden. „Logout“ widerruft den Token bei Twitch.
- Der Weg über ⚡ mit Einfügen von Hand bleibt erhalten.
- Emote-Vorschau beim Schreiben: Enthält die Eingabe ein Emote, zeigt eine Zeile über dem Eingabefeld
  die Nachricht mit Emote-Bildern (auch animiert), so wie sie im Chat erscheinen wird.
- Behebung: Der Anmelde-Link von Twitch enthielt den Code doppelt.

## 2.0.9 (2026-11-08) – @-Namen vervollständigen
- „@“ im Eingabefeld zeigt bis zu fünf Namen aus dem Chat, wer zuletzt geschrieben hat zuerst; jeder
  weitere Buchstabe filtert nach Login und Anzeigename. Tab oder Klick setzt „@Name “ ein. Funktioniert
  auch bei ausgeschalteten Wortvorschlägen.

## 2.0.8 (2026-11-08) – Nutzer-Menü mit Moderation
- Klick auf einen Namen im Chat (oder in der Zuschauerliste) öffnet ein Menü mit Name, Rolle und
  Kontoalter. Für alle: Privat schreiben, @Erwähnen, Name kopieren, Kanal öffnen, Blockieren.
- Moderatoren: Nachricht löschen, Nachrichten entfernen, Timeouts (1 Min. bis 24 Std.), Verwarnen,
  Bannen, Bann aufheben, Shoutout. Streamer zusätzlich: Moderator/VIP vergeben und entziehen, Raid.
- Die eigene Rolle kommt aus `USERSTATE`; alle Aktionen laufen über Helix mit dem eigenen Token.
  Folgenreiche Aktionen fragen vorher nach, das Ergebnis erscheint als Systemzeile.
- Blockierte Konten werden im Chat ausgeblendet.
- „Privat schreiben“ steht jetzt im Nutzer-Menü am Namen statt im Rechtsklickmenü des Nachrichtentexts.
- Erwähnungen: Nachrichten mit @deinname (oder Antworten auf deine Nachrichten) stehen im Chat in
  einem farbig umrandeten Kasten.

## 2.0.7 (2026-11-07) – Eingabefeld wächst mit der Schrift
- Das Chat-Eingabefeld folgt „−“/„+“: Schrift und Feldhöhe wachsen mit, die Rechtschreib-Wellenlinien
  sitzen weiter unter den Wörtern. Wortvorschläge und Satzbau-Hinweise darüber wachsen ebenfalls.

## 2.0.6 (2026-11-07) – Lautstärke-Regler im Postfach
- Regler „Lautstärke“ oben im Postfach: Ton für private Nachrichten von 0 bis 100 % in 5er-Schritten,
  ab Werk 30 %. Beim Loslassen spielt eine Hörprobe; der Wert bleibt gespeichert.

## 2.0.5 (2026-11-07) – Zuschauerzahl und Zuschauerliste
- 👥 zeigt die echte Zuschauerzahl des Streams (öffentlich, alle 45 Sekunden), in der Statuszeile
  kompakt („1,2K“) und im Fenster genau; offline steht „Stream offline“.
- Die Namensliste richtet sich nicht mehr danach, wer geschrieben hat: Als Moderator oder Streamer
  kommt die vollständige Liste von Twitch, sonst die von Twitch beim Betreten gemeldeten Namen.
  Sortiert nach Streamer, Moderatoren, VIPs, dann alphabetisch.
- Der Ton für private Nachrichten spielt mit 30 % Lautstärke (nur dieser Ton; Windows- und
  Player-Lautstärke bleiben unberührt).
- 🔔/🔕 oben im Postfach schaltet den Ton an und aus; die Einstellung bleibt gespeichert.
- „−“/„+“ für die Chat-Schrift gilt jetzt überall: auch im Postfach (Liste, Gespräch, Eingabefeld,
  Gruppen) und in der Zuschauerliste.

## 2.0.4 (2026-11-07) – Ton bei privaten Nachrichten
- Jede eingehende private Nachricht spielt `pn.mp3` (liegt im Plugin-Ordner, lässt sich durch eine
  eigene MP3 gleichen Namens ersetzen). Abgespielt über die Windows-Mediensteuerung, ohne
  zusätzliche Abhängigkeiten. Mehrere Nachrichten innerhalb einer Sekunde klingeln nur einmal.

## 2.0.3 (2026-11-07) – Postfach, Gruppen, Leute im Chat
- **Postfach** (✉ in der Statuszeile): leuchtet bei ungelesenen privaten Nachrichten und zeigt die
  Anzahl. Gesprächsliste wie ein E-Mail-Postfach, Klick öffnet das Gespräch mit Eingabezeile.
  Empfang über Twitch-EventSub, Senden über Helix. Verlauf verschlüsselt (DPAPI) in `whispers.dat`.
- **Gruppen** als Verteiler: Eine Nachricht geht einzeln an jedes Mitglied; Antworten erscheinen in
  der Gruppe. Anlegen im Postfach oder aus der Liste „Leute im Chat“.
- **Leute im Chat** (👥): Liste mit Suche, privat schreiben, mehrere ankreuzen → Gruppe. Quelle:
  IRC-Beitritte und Schreibende, als Moderator die vollständige Helix-Liste.
- Rechtsklick auf eine Chatzeile: „Privat schreiben“.
- Kanalwechsel leert den Chat: Nachrichten des vorherigen Kanals verschwinden, auch beim automatischen
  Wechsel über „Auto-Kanal“.
- ⚡ erzeugt jetzt Tokens mit **allen** 81 aktuellen Twitch-Rechten, damit spätere Funktionen keinen
  neuen Token brauchen. Das Plugin nutzt davon nur Chat, Emotes, private Nachrichten und die
  Teilnehmerliste; der Token liegt weiter DPAPI-verschlüsselt in `tokens.dat`.

## 2.0.2 (2026-11-07) – Angeheftete Nachricht einklappen, Markieren mit der Maus
- Die angeheftete Nachricht verdeckt den Chat nicht mehr: Der Chat beginnt unter der Karte und wird
  kürzer; bei Auto-Scroll bleibt die neueste Nachricht sichtbar.
- ▾/▸ klappt die Karte auf eine Zeile zusammen und wieder auf; der Zustand bleibt gespeichert.
- Chat-Text lässt sich direkt mit der Maus markieren: über eine Nachricht ziehen. Gescrollt wird
  mit Mausrad, Scrollbalken oder in den Lücken zwischen den Nachrichten. Rechtsklick bietet dann
  „Markierung kopieren“.

## 2.0.1 (2026-11-07) – Einstellbare Chat-Schrift
- „−“ und „+“ rechts in der Statuszeile ändern die Chat-Schriftgröße (9–28 px). Namen, Uhrzeit,
  angeheftete Nachricht und Markier-Text wachsen mit, Emotes ab etwa 13 px ebenfalls. Die Größe
  bleibt gespeichert; ab Werk bleibt die bisherige Schrift.

## 2.0.0 (2026-11-07) – Neue Zählung; Chat-Text markieren und kopieren
Die Versionsnummer beginnt hier neu bei 2.0.0. Diese Version ist der Nachfolger von 2.10.0 und
enthält alles darunter. Nicht zu verwechseln mit dem älteren 2.0.0 vom 2026-11-06 weiter unten.
- Doppelklick auf eine Nachricht macht sie markierbar (Emotes als Name); Strg+C kopiert, Esc beendet.
  Solange markiert wird, scrollt der Chat nicht weiter.
- Rechtsklick auf eine Nachricht: Nachricht, Name oder Link kopieren und Text markieren – auch bei
  der angehefteten Nachricht.

## 2.10.0 (2026-11-07) – Angeheftete Nachrichten
- Die angeheftete Nachricht des Kanals erscheint als Karte oben über dem Chat, mit Emotes und
  klickbaren Links. ✕ verbirgt sie, 📌 in der Statuszeile zeigt sie wieder. Eine neue Anheftung
  erscheint von selbst; abgelaufene verschwinden. Abfrage alle 45 Sekunden über gql.twitch.tv,
  ohne Login.

## 2.9.1 (2026-11-07) – Anklickbare Links im Chat
- Links (`https://…`, `www.…`, Kurzformen wie `twitch.tv/name`) erscheinen blau unterstrichen und
  öffnen sich per Klick im Standardbrowser. Der Tooltip zeigt das echte Ziel. Klickbar ist nur http
  und https; Adressen mit Benutzerteil wie `https://twitch.tv@example.org` bleiben Text.
- Behebung: Chat-Text wird immer als reiner Text gezeichnet, „<b>“ von Fremden formatiert nichts mehr.

## 2.9.0 (2026-11-07) – Satzbauprüfung und Wortvorschläge
- **Satzbau:** Blaue Wellenlinie und eine Hinweiszeile über dem Eingabefeld mit der Korrektur zum
  Anklicken. Eigene Regeln laufen lokal. Zusätzlich prüft LanguageTool online; dafür geht der
  getippte Text an api.languagetool.org, ohne @Namen, Tags, Links und Emotes und nie bei Befehlen.
  Im Rechtsklickmenü abschaltbar.
- **Wortvorschläge:** Bis zu drei Vorschläge über dem Eingabefeld, Tab oder Klick übernimmt sie. Sie
  entstehen lokal aus Häufigkeitslisten, Emote-Namen und gelernten Wörtern.

## 2.8.5 (2026-11-07) – Britisches Englisch
- Die Rechtschreibprüfung kennt jetzt auch britisches Englisch: „colour“ und „color“ gelten beide
  als richtig. Der Schalter „Englisch prüfen“ gilt für beide Schreibweisen.

## 2.8.4 (2026-11-07) – Rechtschreibprüfung im Eingabefeld
- **Deutsch und Englisch:** Unbekannte Wörter bekommen beim Tippen eine rote Wellenlinie. Ein Wort
  gilt als richtig, wenn es in einer der beiden Sprachen stimmt. Die Prüfung läuft lokal mit
  Hunspell und mitgelieferten Wörterbüchern.
- **Rechtsklick:** Korrekturvorschläge übernehmen, Wörter ins eigene Wörterbuch aufnehmen und beide
  Sprachen einzeln ein- oder ausschalten.
- Emote-Namen, @Namen, Links, Befehle, GROSS geschriebene Wörter und gängige Chat-Wörter wie „gg“
  gelten nicht als Fehler.

## 2.8.3 (2026-11-07) – Token-Knopf mit Sofort-Freigabe
- Neuer Icon-Button (⚡) neben dem Login-Knopf: Er öffnet über einen Quick-Link von
  twitchtokengenerator.com („Authenticate immediately“) direkt die Twitch-Freigabe mit den Rechten
  `chat:read`, `chat:edit` und `user:read:emotes` und klappt die Login-Felder auf. Den erzeugten
  Access Token trägt man danach dort ein.

## 2.8.2 (2026-11-07) – Favorit per Rechtsklick
- Rechtsklick auf eine Kachel im Emote-Browser schaltet den Favoriten um, auch bei gesperrten
  Kanal-Emotes.

## 2.8.1 (2026-11-07) – „Abos“ nur noch mit echten Kanal-Abos
- Behebung: Unter „Abos“ standen auch die Affen- und Roboter-Smileys, weil Twitch sie als
  Abo-Emotes kennzeichnet. Sie erscheinen dort nicht mehr; in eigenen Nachrichten werden sie
  weiter als Bild dargestellt.

## 2.8.0 (2026-11-07) – Gesperrte Kanal-Emotes, freigeschaltete HypeTrain-Emotes
- **Kanal-Emotes ohne Abo** erscheinen abgeblendet und lassen sich nicht einsetzen. Favorisieren
  bleibt möglich.
- **TwitchHypeTrain:** Freigeschaltete Emotes kommen jetzt aus der vollständigen Emote-Liste des
  Kontos, weil Twitch sie einzeln und nicht über Sets vergibt. Dafür braucht der Token das Recht
  `user:read:emotes`; fehlt es, zeigt der Browser einen Hinweis.

## 2.7.1 (2026-11-07) – Emote-Browser bleibt an seiner Stelle
- Behebung: Beim Favorisieren sprang der Emote-Browser nach oben, ebenso wenn im Chat ein neues
  Emote auftauchte oder ein Katalog nachgeladen wurde. Die Liste bleibt jetzt stehen; laufende
  Animationen starten beim Favorisieren nicht mehr neu.
- **TwitchHypeTrain:** Der Abschnitt zeigt nur noch HypeTrain-Emotes des angemeldeten Kontos; der
  Abschnitt „nicht freigeschaltet“ aus 2.7.0 entfällt.

## 2.7.0 (2026-11-07) – Twitch-Global und TwitchHypeTrain
- **Twitch – Global:** Die rund 300 Global-Emotes und Smileys erscheinen im Browser, jetzt auch ohne
  Login. Die bisherige Abfrage nutzte Set-Kennungen, für die Twitch leere Listen liefert.
- **TwitchHypeTrain:** Eigene Abschnitte für freigeschaltete und für nicht freigeschaltete
  HypeTrain-Emotes. Nicht freigeschaltete zeigt Twitch beim Senden nur als Text.
- **Chat:** Selbst gesendete Global- und freigeschaltete HypeTrain-Emotes erscheinen als Bild.
- **Twitch – Kanal:** Die Abo-Emotes des aktuellen Kanals erscheinen jetzt auch ohne Login und
  unabhängig davon, woher der Token stammt.

## 2.6.0 (2026-11-07) – Emote-Kategorien, Twitch-Standard-Emotes, eigene Emotes im Chat
- **Kategorien im Emote-Browser:** Tabs „Alle“, „Abos“, „Twitch“, „7TV“, „BTTV“, „FFZ“ und
  „Favoriten“, dazu Überschriften je Anbieter und Kanal/Global. Gleichnamige Emotes
  verschiedener Anbieter stehen getrennt und lassen sich einzeln favorisieren.
- **Twitch-Standard-Emotes** (z. B. Kappa, LUL) erscheinen nach dem Login im Browser, auch
  wenn Twitch die bisherige Abfrage für den Token ablehnt.
- Behebung: In selbst gesendeten Nachrichten erschienen Twitch-Emotes (Abo, Standard, Follower,
  Bits) nur als Text, weil Twitch eigene Nachrichten nicht zurückschickt. Sie werden jetzt
  anhand der Emote-Liste des Kontos als Bild dargestellt.
- Der Emote-Knopf ist jetzt ein Smiley-Icon direkt vor „Senden“.

## 2.5.0 (2026-11-07) – Abo-Emotes im Browser, Chat passt in die Seitenleiste
- **Neuer Tab „Abos“ im Emote-Browser:** Zeigt die Emotes der Kanäle, die das angemeldete
  Konto abonniert hat, auch unter „Alle“ und mit ★-Favoriten. Zusätzliche Token-Rechte sind
  nicht nötig. Beim Abmelden oder Kontowechsel verschwinden sie wieder.
- Behebung: Chat, Kopfzeile und Eingabe liefen rechts aus der Seitenleiste; „Verbinden“,
  „Login“ und „Senden“ waren abgeschnitten, der Verbindungsstatus unsichtbar. Der Status hat
  nun eine eigene Zeile, und die Schalter brechen bei Platzmangel um.
- Behebung: Sehr lange Wörter und Links werden umgebrochen statt abgeschnitten. Lange
  Namenszeilen brechen ebenfalls um, und umbrochene Zeilen beginnen ohne Einzug.
- Emote-Browser: Das Suchfeld hat eine eigene Zeile.

## 2.4.0 (2026-11-07) – BTTV, FFZ und 7TV komplett, Zero-Width-Emotes
- **BetterTTV und FrankerFaceZ neu, 7TV überarbeitet:** Globale und Kanal-Emotes
  aller drei Anbieter erscheinen direkt im Chat und im Emote-Browser (mit Kennung
  „BTTV“, „FFZ“ oder „7TV“), ohne Login. Fällt ein Anbieter aus, laufen die anderen
  weiter; beim nächsten Verbinden wird er erneut gefragt.
- **Vorrang bei gleichem Namen:** Twitch-Emotes aus dem Nachrichten-Tag zuerst, dann
  Kanal vor global, innerhalb einer Ebene 7TV vor BTTV vor FFZ.
- **Erkennung wie in den Browser-Erweiterungen:** Ein Emote zählt nur als ganzes, durch
  Leerzeichen getrenntes Wort in exakter Schreibweise. Namen mit Satzzeichen wie `:tf:`
  und `D:` funktionieren; `SourPls,` oder `xSourPls` bleiben Text.
- **Animierte Emotes als WebP:** Alle drei Anbieter liefern animiertes WebP mit voller
  Transparenz und mehr Farben als GIF; GIF bleibt als Ausweichformat. Bereits geladene
  7TV-GIFs im Cache bleiben gültig. Ob ein Bild animiert ist, entscheidet der Dateiinhalt.
- **Zero-Width-Emotes** (z. B. 7TV `RainTime`, BTTV `cvMask`) liegen über dem Emote davor;
  ohne Emote davor stehen sie für sich.
- **Effekt-Befehle bleiben Text:** BTTV-Modifier wie `h!` und versteckte FFZ-Modifier wie
  `ffzSpin` sind keine Bilder. Ihre Effekte (Spiegeln, Drehen usw.) werden nicht dargestellt.
- **Darstellung:** Emotes werden nach Seitenverhältnis auf Zeilenhöhe skaliert und mit
  kleinem Abstand gesetzt. Zeilen ohne Änderung werden nicht neu aufgebaut, damit
  laufende Animationen nicht neu starten.
- Behebung: Emote-Positionen verrutschten, wenn vor einem Twitch-Emote ein Emoji stand.
- Behebung: Das Panel übernahm den bisherigen Verlauf nicht, wenn der Player den Controller
  erst nach dem Laden zuwies – nachgeladene Emote-Bilder konnten dann die falsche Zeile treffen.
- Behebung: Bei geöffnetem Emote-Browser war der Chat vollständig verdeckt.
- Neuer Test `emote_test`: Parser offline, echte Kataloge und Bilder im echten Panel,
  misst das Abspieltempo. `qml_panel_test` kompiliert wieder und findet den Loader.

## 2.3.0 (2026-11-06) – 7tv-Emotes, echte GIF-Animation, Auto-Scroll
- **7tv-Emotes werden erkannt und angezeigt** (ohne Login, ohne Erweiterung):
  Der Plugin fragt die öffentlichen 7tv-Listen ab – das Global-Set
  (`7tv.io/v3/emote-sets/global`) und das Set des Kanals
  (`7tv.io/v3/users/twitch/<Raum-ID>`). Kanäle kennt 7tv nur unter ihrer
  Ziffern-Raum-ID; die ID kommt aus der `room-id`-Markierung, die Twitch in
  jeder Nachricht mitschickt, und wird dauerhaft gemerkt (`roomIds` in den
  Einstellungen). Ein 7tv-Name in einer Nachricht (z. B. `Nous`) wird an
  Wortgrenzen wortgenau gefunden – aber nur außerhalb der Bereiche, die
  Twitch selbst als native Emotes markiert hat, und längste Treffer zuerst.
  Hinweis: Wer 7tv-Emotes *sendet*, schreibt ihren Namen ganz normal ins
  Chatfeld; sehen können ihn nur Zuschauer mit 7tv-Erweiterung – dieser
  Plugin stellt sie zusätzlich für alle dar, die im Chat eintreffen.
- **Animierte Emotes laufen wirklich**: Die Chat-Zeilen waren HTML-Richtext –
  Richtext-`<img>` kann GIF-Bewegung nicht. Jetzt wird jede Nachricht als
  Segment-Folge aufgebaut (Text-Wörter + Emote-Bilder), das Panel legt sie in
  einen Flow; animierte Emotes laufen in `AnimatedImage` (7tv animiert immer
  als GIF vom CDN, Twitch-Animierte werden am Dateiinhalt erkannt – deren
  „.png“ enthält GIF-Bytes). Kanalfremde 7tv-Emote-Dateien liegen getrennt
  unter `emotes/7tv/`.
- **Auto-Scroll-Schalter** (Standard: an): Der Chat folgt jeder neuen
  Nachricht zuverlässig nach unten, auch während Emoticon-Bilder nachgeladen
  werden. Wer den Schalter ausschaltet und selbst nach oben scrollt, wird
  nicht mehr nach unten gezogen; einschalten springt sofort ans Chat-Ende.
- **Emote-Browser wieder vollständig im Panel**: Knopf „Emotes“, Tabs (Alle /
  Kanal / Global / Favoriten), Suche, ★-Favoriten, „7TV“-Kennung an 7tv-Kacheln;
  Kachelklick setzt den Namen ins Eingabefeld. (Im ausgelieferten 2.2.1-Panel
  war dieser Browser versehentlich verschwunden.)
- Nachlauf: Trifft ein 7tv-Katalog verspätet ein, werden bereits angezeigte
  Nachrichten neu durchsucht und fehlende Bilder nachgeladen.
- Manifest/Einstellungen: neu `autoscroll` (Boolesch) und `roomIds`
  (Kanalname → Raum-ID, nur Ziffern, kein Token).
- Behebung eines Anzeigefehlers: Das Panel wies `AnimatedImage` die Eigenschaft
  `autoPlay` zu – die existiert dort nicht, also kippte der QML-Lader und das
  Chatfeld blieb leer. `AnimatedImage` spielt ohnehin von selbst, sobald das
  Bild geladen ist; die Zuweisung ist entfernt. Ein neuer Headless-Test
  (`qml_panel_test`) lädt das Panel jetzt in der QML-Engine hoch, damit solche
  Ladefehler nie wieder unbemerkt ins Paket gelangen.

## 2.2.1 (2026-11-06) – Emote-IDs mit Unterstrichen
- Kanal-Emote-IDs wie `ep_2222` wurden vom Cache-Ableger abgelehnt (nur
  Buchstaben/Ziffern erlaubt) → diese Emotes erschienen nie als Bild. Der
  Dateinamen-Schutz lässt jetzt Unterstriche zu. (Dieses Update verlor
  dabei versehentlich den Emote-Browser-Knopf im Panel – in 2.3.0 wieder drin.)

## 2.2.0 (2026-11-06) – Nur noch der native Chat, mit Emote-Browser
- **Eingebetteter Browser-Chat entfernt** (auf Nutzerwunsch): Das Panel zeigt
  jetzt ausschließlich den nativen Chat (eigene IRC-Verbindung über
  `wss://irc-ws.chat.twitch.tv`). `ChatWeb.qml` liegt nicht mehr im Paket, die
  DLL kennt keinen Embed-Modus mehr; ein altes `embedMode`-Einstellungsfeld
  wird beim nächsten Speichern bereinigt (ein übriggebliebenes `embedMode=true`
  im alten Setting blockiert den Chat nicht mehr).
- **Emote-Browser im Chat-Panel** (Knopf „Emotes“): Emotes werden als Bilder
  angezeigt, durchsuchbar, in vier Bereichen (Alle / Kanal / Global /
  Favoriten). Emote antippen → sein Name erscheint in der Eingabezeile;
  „Senden“ postet ihn (Twitch erkennt Emotes am Namen – so lassen sich Emotes
  sehen, auswählen und senden). ★ oben rechts favorisiert ein Emote dauerhaft.
- **Zwei Quellen für die Emote-Listen:** (1) Immer: im Chat beobachtete Emotes
  – der Plugin liest Name und ID aus den IRC-Emote-Markierungen jeder
  Nachricht (funktioniert auch anonym). (2) Mit Token-Login zusätzlich: die
  komplette Kanalliste und die globalen Emotes direkt von Twitch
  (`gql.twitch.tv`, öffentliche Web-Client-ID – kein Geheimnis, kein
  Client-Secret). Ohne Token oder bei Netzwerkfehlern zeigt der Browser
  automatisch die beobachteten Emotes.
- **Favoriten** werden nur als Name + Emote-ID in den Plugin-Einstellungen
  gespeichert (niemals Tokens).
- Die Eingabezeile ist jetzt immer sichtbar (vorher nur nach Login); ohne
  Token-Login steht ein Hinweis, dass zum Senden erst der „Login“-Bereich
  mit einem Token gefüllt werden muss.
- Fehler aus dem ersten 2.2.0-Test behoben: leeres Chat-Panel. Ursache: die
  neuen Emote-Tabs waren Buttons mit `color:` – QtQuick.Controls-Buttons haben
  keine `color`-Property, der Panel-Lader brach ab. Tabs sind jetzt Labels mit
  Klickfläche.
- Chat-Emoticons in empfangenen Nachrichten funktionieren wie bisher
  (Bild-Cache unter `<Plugin-Daten>/emotes/`).

## 2.1.2 (2026-11-06) – „Login”-Knopf im Chat-Panel
- Twitch zeigt den eigenen Login-Knopf im eingebetteten Chat in Fremd-
  Browsern oft gar nicht (oder lehnt ihn ab). Neu: Knopf **„Login“** in der
  Kopfzeile – er öffnet die normale Twitch-Login-Seite im selben Chat-Profil
  („twitchchat“). Einloggen (Name + Passwort, 2FA-Code falls aktiv – kein
  USB-Gerät nötig), dann **„Neu laden“**: der eingebettete Chat ist danach
  eingeloggt. Hinweisleiste über der Login-Seite erklärt den Ablauf.
- (Ein Login im normalen Edge/Chrome bringt dafür übrigens nichts: Twitch-
  Sitzungen sind pro Programm getrennt; der Chat hat sein eigenes Profil.)
- Kleiner Fehler behoben: Der Hinweis „Kein Kanal gewählt“ reagierte wegen
  `QUrl`-Zugriff (`url.length`) nie zuverlässig.

## 2.1.1 (2026-11-06) – Login im eingebetteten Chat möglich
- **„Dein Browser wird zurzeit nicht unterstützt“ behoben**: Twitch prüft beim
  Chat-Login den User-Agent und lehnt Fenster ab, in denen die Kennung
  „QtWebEngine/…“ steht. Qt 6.12 bringt Chromium 140 mit – der Chat meldet
  sich jetzt ohne die QtWebEngine-Kennung als genau dieses Chrome
  (`httpUserAgent` am WebEngine-Profil, live verifiziert mit probe\: Chat
  rendert mit bereinigtem UA, keine Browser-Absage mehr).
- Zum Login genügen Benutzername + Passwort (und ggf. der 2FA-Code aus der
  Authenticator-App aufs Handy). **Kein USB-Sicherheitsschlüssel und kein
  anderes Gerät nötig.** Zum reinen MITLESEN ist überhaupt kein Login nötig.

## 2.1.0 (2026-11-06) – Nur noch der Chat; Embed-Fehler behoben
- **Kanal-Browser entfernt** (auf Wunsch): kein „Twitch-Kanäle“-Bereich, keine
  Client-ID-/Token-Felder, kein OAuth-Login, keine Follow-Abfragen mehr. Das
  Plugin erzeugt den Browser-Controller nicht einmal noch – damit gibt es auch
  keinen „Login abgelaufen“-Ablauf mehr (die Log-Zeile „missing client secret“
  beim Token-Refresh hatte genau daher die Abmeldungen ausgelöst). Übrig bleibt
  ausschließlich der Chat (eingebettet als Standard, nativer IRC-Fallback).
- **„Huch! Diese Einbettung ist falsch konfiguriert“ behoben**: Twitch verlangt
  seit Mitte 2024 zwingend einen `parent`-Parameter am Einbettungs-Link; ohne
  ihn leitet Twitch auf `embed-error.html?errorCode=NoParent` um (live
  nachgeprüft). Der Chat-Link lautet jetzt
  `https://www.twitch.tv/embed/<Kanal>/chat?darkpopout&parent=twitch.tv` –
  derselbe Elternwert, den Twitch in seinem eigenen Player verwendet.
- Manifest gestrafft: nur noch `channel`, `autoChannel`, `embedMode` als
  Einstellungen; Browser-Fähigkeiten aus dem Manifest entfernt.
- `TwitchBrowser.qml` nicht mehr im Paket.

## 2.0.2 (2026-11-06) – Chat-Anzeige, Seitenschnitt und Login-Ausdauer
- **Eingebetteter Chat läuft wieder**: `ChatWeb.qml` verwendete
  `persistentCookiesPersistence` – diese Eigenschaft existiert in Qt 6.12
  nicht, das Web-Panel fiel beim Laden durch, und der Chat blieb
  (still auf den nativen Fallback umgebogen) schwarz. `storageName`
  allein hält das Profil „twitchchat“ dauerhaft; die Zeile ist entfernt.
- **Login lief nicht mehr „schnell ab“**: Jede einzelne 401-Antwort von
  Twitch führte bisher über `resume()` in einen Ast, der den verworfenen
  Access-Token für „noch gültig“ hielt, ohne je ein Refresh-POST zu
  schicken – danach kippte die Sitzung beim nächsten Request komplett
  („Login abgelaufen – bitte neu anmelden“, alle 30–120 s). Jetzt wird
  nach einer 401 zwingend ein echter Token-Refresh durchgeführt; das
  einmalige Wiederholungsrecht wird durch jede erfolgreiche API-Antwort
  wieder scharf geschaltet.
- **Client-ID-Fehlerquelle beseitigt**: Die Client-ID, mit der die Tokens
  geholt wurden, wird jetzt in der Vault-Sitzung gespeichert und für alle
  Helix-/Refresh-/Revoke-Anfragen verwendet – ändert man das Client-ID-
  Feld nach dem Login, widersprach der `Client-ID`-Header dem Token und
  Twitch antwortete auf alles mit 401.
- **Auth-Fehler sichtbar**: 401, Refresh-Versuche, Auth-Fehlschläge und
  API-Fehler werden jetzt als `warn` im Player-Log notiert (vorher
  unterm Log-Level `info` unsichtbar).
- **Nichts wird mehr rechts abgeschnitten**: `ElideRight`/`ElideMiddle`
  ohne `Text.`-Präfix und ungültige Enum-Schreibweisen
  (`Text.ElideEnd`, `ScrollBar.AlwaysAsNeeded`) waren Qt 6 unbekannt →
  Beschriftungen behielten ihre volle Breite und schoben Karten und
  Kopfzeilen über den Panelrand. Alle Beschriftungen nutzen jetzt
  gültige Enums plus `Layout.minimumWidth: 0` (Kartentitel Kürzung mit …).
- Tabs und Filter stehen in **zwei Reihen** (eine 380-px-Seitenleiste
  war dafür zu breit), Chat-Kopfzeilen-Knöpfe sind kurz
  („Auto”, „Nativ”), der ComboBox ist 140 px statt 150.
- ChatPanel: `webFailed` wird verspätet gesetzt (Bindungsschleife
  `useWeb` weg) und nur beim ausdrücklichen Umschalten nativ→embed
  zurückgenommen (kein Chat-Neustart-Churn mehr bei Einstellungen).
- Leerzustand des Browser-Teils ist kein Layout-Kind mit `anchors` mehr
  (warnte im Log).

## 2.0.1 (2026-11-06) – Panel-Ladefehler behoben
- `ChatPanel.qml` fehlte `import QtQuick.Layouts` → „ColumnLayout is not a
  type“, Panel blieb leer („Das Panel des Plugins konnte nicht geladen
  werden“). Import ergänzt.
- `Loader.onStatus` → `onStatusChanged` korrigiert (der Handler hieß im
  Player-Log bereits seit 1.2.0 „non-existent property“).

## 2.0.0 (2026-11-06) – Twitch-Kanal-Browser
- **Kanal-Browser im Sidebar-Panel über dem Chat** (einklappbar ▾, ziehbare
  Trennleiste, Verhältnis wird gespeichert): echte gefolgten Live-Kanäle
  über die Twitch-Helix-API – mit Vorschaubild (320×180), Zuschauerzahl,
  Titel, Kategorie und LIVE-Kennung. Keine Demodaten.
- **OAuth-Login ohne Client-Secret**: Device Code Grant
  (id.twitch.tv/oauth2/device + Polling), Scope standardmäßig
  `user:read:follows`; „Zu Twitch öffnen“ / „Code kopieren“ / Abbrechen;
  Refresh-Token-Auffrischung (einmalig verwendbare Refresh-Tokens werden
  ersetzt), Widerruf bei „Abmelden“, 401 → ein stiller Refresh-Versuch.
- **Sichere Token-Ablage**: DPAPI-verschlüsselte `tokens.dat`
  (Windows-Benutzerschlüssel, Plugin-Entropie); Tokens erscheinen nicht
  mehr in Plugin-Einstellungen oder Logs; alte Klartext-Logins werden beim
  ersten Start migriert und aus den Einstellungen entfernt. Fallback ohne
  DPAPI: reine Speichersitzung (niemals Klartext auf der Platte).
- **Wiedergabe im vorhandenen Player**: ▶ ruft nur `player.load` mit
  `https://www.twitch.tv/<login>` auf – der Player-Resolver (yt-dlp/HLS,
  Qualitätsmenü, Proxy) bleibt erhalten; der Chat folgt automatisch.
- **Tabs & Filter**: „Folge ich“ (live zuerst, optional offline),
  „Suchen“ (Kanalname/Titel), „Favoriten“ (☆, lokal persistiert);
  Sortierung zuletzt live / Zuschauer / A–Z / Favoriten; Cursor-
  Pagination („Mehr laden“, max. 3/live + 2/offline Seiten pro Auffrischung).
- **Thumbnail-Cache**: asynchrone Downloads, LRU im RAM, Dateicache
  `thumbs/` (max. 400 Dateien, >30 Tage werden beschnitten),
  429-/Rate-Limit-Backoff; Aktualisierung (Standard 90 s) nur bei
  sichtbarem Panel.
- Manuelle Token-Übernahme (mit API-Gegenprüfung), einstellbare Scopes
  und Aktualisierungszeit; neue Einstellungen in `configurationSchema`.
- Tests: neuer `browser_test` (API-Parser, OAuth-Zustände, Vault ohne
  Klartext auf der Platte, Fake-Host-Integration, Einstellungs-Persistenz).

## 1.2.0 – Eingebetteter Chat als Standard
- twitch.tv/embed-Chat (QtWebEngine) ist Standardansicht ab Player 1.0.7
  („webview.available“); Login/Emotes/Schreiben im eingebetteten Chat;
  dauerhafter Login über WebEngine-Profil „twitchchat“.
- Nativer IRC-Chat bleibt Fallback (Button „Nativ ohne WebEngine“);
  Button „Chat im Browser“ entfernt.

## 1.1.0 – Emotes und Komfort
- Native Twitch-Emotes (global + Kanal) als Bilder im nativen Chat,
  Emote-Cache, Neu-Rendering nach Bild-Download, Selbst-Echo-Anreicherung.
- Auto-Kanal auch für lokale .m3u8/.strm-Dateien; Reconnect-Backoff.

## 1.0.0 – Erstableichung
- Nativer Twitch-Chat über wss://irc-ws.chat.twitch.tv (IRCv3-Tags),
  Kanalerkennung aus Twitch-URLs, manuelle Kanaleinstellung,
  Login-Feld (Name + Token), Panel in der Seitenleiste, Hotkey Strg+Alt+T.
