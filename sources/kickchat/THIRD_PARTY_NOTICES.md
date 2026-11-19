# Drittanbieter-Hinweise – Kick Chat

## Kick (Kick Streaming Pty Ltd)
Das Plugin nutzt öffentliche Kick-Endpunkte (kick.com/api/v2, kick.com/emotes, Pusher-WebSocket des
Kick-Webchats) und die offizielle Kick-API (id.kick.com/oauth, api.kick.com/public/v1). Es ist kein
offizielles Kick-Produkt. Es gelten die Kick-Nutzungsbedingungen: https://kick.com/terms-of-service

## 7TV
Emote-Kataloge und Bilder von 7tv.io / cdn.7tv.app (öffentlich, ohne Anmeldung). 7TV ist nicht mit
diesem Plugin oder mit Kick verbunden.

## LanguageTool (ab Werk an, abschaltbar)
Ist „Satzbau online“ eingeschaltet (Standard), geht der getippte Text an die öffentliche API von
api.languagetool.org (LanguageTooler GmbH). Es gelten deren Bedingungen:
https://languagetool.org/legal/

## Qt 6 (Qt Company)
Build-Bibliotheken: Qt Core, Qt Network, Qt Gui (zur Laufzeit im Player: Qt Qml/Quick/Controls).
Qt unterliegt der **GNU LGPL v3**: https://www.qt.io/licensing/

## Hunspell 1.7.6 (Rechtschreibprüfung)
Unverändert statisch in `kickchat.dll` gelinkt. Dreifach lizenziert (MPL 1.1 / GPL 2 / LGPL 2.1);
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

## Benachrichtigungston `pn.mp3`
Aus dem TwitchChat-Plugin desselben Herausgebers übernommen.
