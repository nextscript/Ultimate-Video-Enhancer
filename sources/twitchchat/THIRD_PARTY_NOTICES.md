# Drittanbieter-Hinweise

## Twitch (Twitch Interactive, Inc.)
Dieses Plugin nutzt die öffentliche **Twitch API (Helix)** und die
**Twitch OAuth-Endpunkte** (id.twitch.tv/oauth2/*, api.twitch.tv/helix/*)
sowie für den eingebetteten Chat **twitch.tv/embed**. Es verwendet die von
dir selbst registrierte Client-ID. Es ist kein offizielles Twitch-Produkt.
Es gelten die Twitch-Nutzungsbedingungen und die Developer-Agreement:
  - https://www.twitch.tv/p/legal/terms-of-service/
  - https://dev.twitch.tv/docs/legal/
Der eingebettete Chat unterliegt den Cookie-/Datenschutzbestimmungen von
twitch.tv; die Login-Daten des eingebetteten Chats liegen im lokalen
QtWebEngine-Profil „twitchchat“.

## 7tv (7tv.app)
Für die Emote-Listen und -Bilder des Drittanbieters 7tv werden ausschließlich
die **öffentlichen 7tv-API-Endpunkte** (`https://7tv.io/v3/…`) und die
**öffentliche 7tv-CDN** (`https://cdn.7tv.app/emote/…`) anonym, ohne
Zugangsdaten, abgerufen. Emotes und Namen sind Eigentum ihrer Ersteller;
7tv selbst ist nicht mit diesem Plugin oder mit Twitch verbunden. Es gelten
die Nutzungsbedingungen von 7tv.app:
  - https://7tv.app/legal/terms
  - https://7tv.app/legal/privacy

## Qt 6 (Qt Company)
Build-Bibliotheken: Qt Core, Qt Network, Qt Gui (und zur Laufzeit im
Player: Qt Qml/Quick/Controls, QtWebEngine). Qt unterliegt der
**GNU LGPL v3** (kommerzielle Option: Qt Commercial License):
  - https://www.qt.io/licensing/
QtWebEngine enthält Chromium (BSD-artige Lizenz, siehe Qt-Dokumentation).

## Hunspell 1.7.6 (Rechtschreibprüfung)
Unverändert statisch in `twitchchat.dll` gelinkt. Dreifach lizenziert (MPL 1.1 / GPL 2 / LGPL 2.1);
genutzt wird die **MPL 1.1** (`dict/COPYING_Hunspell_MPL.txt`). Quelltext:
  - https://github.com/hunspell/hunspell/releases/tag/v1.7.6

## Wörterbücher im Ordner `dict/`
Eigenständige Datendateien, unverändert aus https://github.com/LibreOffice/dictionaries:
  - **Deutsch** (`de_DE_frami`, auf Basis von igerman98): **GNU GPL Version 2 oder 3**, © Björn Jacke
    und Franz Michael Baumann, siehe `dict/README_de_DE_frami.txt`, `dict/COPYING_GPLv2` und
    `dict/COPYING_GPLv3`. Die `.aff`/`.dic`-Dateien sind selbst die Quellform. Die MIT-Lizenz des
    Plugins gilt für sie nicht.
  - **Englisch, US** (`en_US`, aus SCOWL): freizügige Lizenz, siehe `dict/README_en_US.txt`.
  - **Englisch, britisch** (`en_GB`): **GNU LGPL**, siehe `dict/README_en_GB.txt`.

## Häufigkeitslisten `dict/freq_de.txt`, `dict/freq_en.txt`
Bearbeitet nach FrequencyWords von Hermit Dave (https://github.com/hermitdave/FrequencyWords), Daten
aus OpenSubtitles 2018. **CC BY-SA 4.0**, siehe `dict/README_freq.txt`.

## Private Nachrichten
Das Postfach nutzt die offiziellen Twitch-Schnittstellen EventSub (`wss://eventsub.wss.twitch.tv/ws`,
Ereignis `user.whisper.message`) und Helix (`/helix/whispers`, `/helix/users`, `/helix/chat/chatters`)
mit dem eigenen Token. Es gelten die Twitch-Bedingungen, insbesondere gegen Spam.

## LanguageTool
Die optionale Online-Prüfung ruft die öffentliche Schnittstelle `https://api.languagetool.org/v2/check`
auf. Es gelten deren Bedingungen und Datenschutzhinweise: https://languagetool.org/legal/

## Eigene Bestandteile
Plugin-Quellcode (MiniWebSocket, EmoteCache, TwitchChatController,
TwitchBrowserController, TwitchHelix, TwitchAuthService, TokenVault,
ThumbnailCache, QML-Panels): MIT-Lizenz, © ts3x.cc.

## Emote-Anbieter und Referenz
BetterTTV, FrankerFaceZ und 7TV sind eigenständige Dienste; ihre Emotes gehören den
jeweiligen Erstellern. Das Plugin ruft nur deren öffentliche Schnittstellen auf.
Das GPLv3-Projekt jChat (github.com/giambaJ/jChat) diente als Referenz für die
Arbeitsweise; aus ihm ist kein Code übernommen.

## Windows-API
DPAPI (CryptProtectData/CryptUnprotectData, crypt32.dll) ist Teil von
Windows und unterliegt der Microsoft-Systemlizenz.

## Twitch-Marken
„Twitch“, das Twitch-Logo und der Twitch-Chat sind Marken von Twitch
Interactive, Inc. Dieses Plugin ist damit nicht verbunden und wird nicht
von Twitch unterstützt oder gesponsert.
