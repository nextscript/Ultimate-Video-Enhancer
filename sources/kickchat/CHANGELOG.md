# Kick Chat – Änderungen

## 1.3.1 (2026-11-09)
- Satzbau online mit LanguageTool ist jetzt ab Werk an (wie im Twitch-Chat). Ein in 1.3.0 automatisch gespeichertes „aus“ wird dabei überschrieben; selbst gewählte Einstellungen bleiben.

## 1.3.0 (2026-11-09)
- Satzbauprüfung: lokale Regeln (doppelte Wörter, Satzanfang, Komma vor „dass“, a/an) mit blauer Wellenlinie, Hinweis und Korrektur zum Anklicken; optional online mit LanguageTool (ab Werk aus, schickt Text an api.languagetool.org).
- Chat-Text markieren: Ziehen markiert in der Zeile, Doppelklick die ganze Nachricht, Strg+C kopiert, Esc/Klick daneben hebt auf; „Markierung kopieren“ und „Alles markieren“ im Rechtsklick-Menü.
- Aneinanderhängende Emotes bleiben beim Kopieren durch Leerzeichen getrennt.

## 1.2.0 (2026-11-09)
- Rechtschreibprüfung (Deutsch, US- und britisches Englisch, Hunspell, lokal) mit roter Wellenlinie, Vorschlägen per Rechtsklick und eigenem Wörterbuch.
- Wortvorschläge beim Tippen (Tab oder Klick), lernt aus eigenen und Chat-Nachrichten.
- 🔔 in der Statuszeile wie im Twitch-Chat: öffnet „Benachrichtigungen & Boxen“; dort 🔔/🔕 für den Ton bei Erwähnungen.
- Event-Bots bearbeitbar (✎): Name, Etikett und eigene Box-Farbe; eigene Farben (#RRGGBB) auch für alle Box-Arten.

## 1.1.0 (2026-11-09)
- Nutzer-Menü per Klick auf Namen: Profilbild, Rolle, Konto-/Follow-Alter, Abo-Monate; Erwähnen, Antworten, Kopieren, Blockieren.
- Moderation für Mods/Streamer: Nachricht löschen, Timeout (1 Min–7 Tage), Bann mit Grund, Entbannen (offizielle Kick-API).
- Benachrichtigungen: Ton bei Erwähnung/Antwort (🔔/🔕, Lautstärke), farbige Boxen je Art (einstellbar), Event-Bots, Follows.
- Umfragen als Karte mit Balken und Restzeit.
- Emotes direkt im Eingabefeld als Bild, ohne Lücke.
- Klick außerhalb des Eingabefelds gibt die Tastatur wieder frei.
- Kick-Logo in der Kopfzeile entfernt.

## 1.0.0 (2026-11-09)
- Erste Version: Kick-Chat als UVE-Player-Plugin (Seitenleiste, Menü, Strg+Alt+K).
- Auto-Kanal aus laufenden Kick-Medien (kick.com/<kanal>).
- Live-Chat über Kicks Pusher-WebSocket (neuer Schlüssel seit Oktober 2026, in den Einstellungen überschreibbar), Verlauf beim Verbinden.
- Kick-Emotes (auch animiert), 7TV-Emotes, Emote-Browser mit Suche und Favoriten.
- Abzeichen, Antworten, gelöschte Nachrichten/Timeouts, Abo-/Geschenk-/Host-/Kicks-Meldungen, angeheftete Nachricht.
- Live-Status, Zuschauerzahl mit Liste „Leute im Chat“ (Suche, Rollen, Aktivität), Chat-Modi, Schriftgröße, Auto-Scroll, Uhrzeit, Kopieren, klickbare Links.
- Schreiben über die offizielle Kick-API (OAuth 2.1 + PKCE, Anmeldung mit einem Klick über die eingebaute Kick-App, DPAPI-verschlüsselte Token-Ablage).
