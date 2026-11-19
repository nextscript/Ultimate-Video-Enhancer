#pragma once
// Eingebaute Kick-App des Plugin-Herausgebers: Damit kann sich jeder Nutzer mit einem Klick
// anmelden (wie beim Twitch-Chat), ohne eine eigene App anzulegen. Eine eigene App im
// Anmeldefenster hat Vorrang.
// HINWEIS: Kick verlangt beim Token-Tausch immer das Client-Secret. Ein Secret in einer DLL
// ist für jeden auslesbar, der die DLL hat – der Herausgeber hat das bewusst so entschieden.
#define KICK_DEFAULT_CLIENT_ID "01M710E3E099PRXG1WKHM9Q378"
// Der öffentliche Build (sources/kickchat im Plugin-Katalog) setzt den Wert aus einem GitHub-Secret.
#ifndef KICK_DEFAULT_CLIENT_SECRET
#error "KICK_DEFAULT_CLIENT_SECRET fehlt: beim Build aus dem GitHub-Secret PLUGIN_BUILD_SECRETS (siehe uve-build.json) oder lokal per -D setzen"
#endif