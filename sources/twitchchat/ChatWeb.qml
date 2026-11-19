import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine

// Eingebetteter Twitch-Chat – Standardansicht ab UVE Player 1.0.7:
// zeigt https://www.twitch.tv/embed/<Kanal>/chat direkt im Player (wie play.php es tut).
// Login, Emotes und Schreiben übernimmt der eingebettete Chat selbst; der Login bleibt
// über das WebEngine-Profil "twitchchat" dauerhaft erhalten (kein Token nötig).
ColumnLayout {
    id: root
    property var controller: null
    property var plugin: null
    property var theme: null

    function tr2(de, en) { return plugin && plugin.language === "de" ? de : en }
    function muted()       { return theme && theme.muted ? theme.muted : "#9a9a9a" }

    // parent= ist Pflicht: Twitch leitet ohne parent-Parameter auf
    // embed-error.html?errorCode=NoParent um – das ist die Meldung
    // "Huch! Diese Einbettung ist falsch konfiguriert." Der direkt geladene
    // Chat gehört zu twitch.tv selbst, also ist twitch.tv der richtige Elternwert
    // (genau so ruft Twitch auch seinen eigenen Player-Chat auf).
    function embedUrl() {
        var ch = controller ? controller.channel : ""
        return ch.length ? "https://www.twitch.tv/embed/" + ch + "/chat?darkpopout&parent=twitch.tv" : ""
    }

    // storageName genügt: Qt legt das Profil dauerhaft unter diesem Namen ab.
    // persistentCookiesPersistence existiert in Qt 6.12 NICHT und ließ den
    // ganzen Chat-Loader fehlschlagen (Chat fiel stumm auf ChatNative zurück).

    // Twitch lehnt den Chat-Login ab („Dein Browser wird zurzeit nicht
    // unterstützt“), sobald die Kennung „QtWebEngine/…“ im User-Agent steht.
    // Qt 6.12 bringt Chromium 140 mit – ohne die QtWebEngine-Kennung meldet
    // sich der Chat schlicht als genau dieses Chrome und Twitch akzeptiert
    // ihn als vollwertigen Browser. Live-Probe 2026-11-06 (probe\): Chat
    // rendert mit bereinigtem UA einwandfrei, keine Browser-Absage.
    WebEngineProfile { id: engineDefault }   // Aux: Standard-UA der Engine
    WebEngineProfile {
        id: chatProfile
        storageName: "twitchchat"
        httpUserAgent: engineDefault.httpUserAgent.replace(/QtWebEngine\/[0-9.]+ /, "")
    }

    // ——— Kopfzeile: Kanal + Umschalter ———
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Label {
            text: tr2("Kanal:", "Channel:")
            color: muted()
            font.pixelSize: 12
        }
        TextField {
            id: channelField
            Layout.fillWidth: true
            Layout.minimumWidth: 40
            placeholderText: "xnowski"
            text: controller ? controller.channel : ""
            onEditingFinished: { if (controller) controller.channel = text }
        }
        CheckBox {
            text: tr2("Auto", "Auto")   // kurz halten: die Kopfzeile muss in ~380 px passen
            font.pixelSize: 12
            checked: controller ? controller.autoChannel : true
            enabled: controller !== undefined && controller !== null
            onToggled: { if (controller) controller.autoChannel = checked }
        }
        Button {
            flat: true
            font.pixelSize: 12
            text: tr2("Login", "Login")
            // Öffnet die normale Twitch-Login-Seite im selben Profil wie der
            // Chat. Die Login-Oberfläche des eingebetteten Chats selbst wird
            // von Twitch in Fremd-Browsern gern versteckt bzw. abgelehnt –
            // dieser Weg umgeht das. Nach dem Einloggen: "Neu laden".
            onClicked: chatView.load("https://www.twitch.tv/login")
        }
        Button {
            flat: true
            font.pixelSize: 12
            text: tr2("Neu laden", "Reload")
            onClicked: chatView.reload()
        }
        Button {
            flat: true
            font.pixelSize: 12
            visible: controller ? controller.webAvailable : false
            text: tr2("Nativ", "Native")   // kurz: sonst wird der Knopf rechts abgeschnitten
            onClicked: { if (controller) controller.embedMode = false }
        }
    }

    // ——— Eingebetteter Chat ———
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        WebEngineView {
            id: chatView
            anchors.fill: parent
            profile: chatProfile
            url: embedUrl()
        }
        Label {
            anchors.centerIn: parent
            visible: chatView.url.toString().length === 0
            color: muted()
            text: tr2("Kein Kanal gewählt.", "No channel selected.")
        }
        Label {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            visible: chatView.url.toString().indexOf("twitch.tv/login") >= 0
            color: "#0a0a0a"
            font.pixelSize: 11
            padding: 6
            background: Rectangle { color: "#ffd76e"; radius: 4 }
            text: tr2("Jetzt hier einloggen (Benutzername + Passwort; 2FA-Code falls aktiv). Danach oben auf „Neu laden“ klicken – der Chat ist dann eingeloggt.",
                      "Log in here now (user name + password; 2FA code if enabled). Then press 'Reload' above – the chat becomes logged in.")
        }
    }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: muted()
        font.pixelSize: 11
        text: tr2("Login, Emotes und Schreiben übernimmt der eingebettete Twitch-Chat; der Login bleibt lokal im Profil „twitchchat“ erhalten. Keinen Login-Knopf im Chat gefunden? Oben auf „Login“ klicken und danach „Neu laden“.",
                  "The embedded Twitch chat handles login, emotes and posting; your login stays local in the „twitchchat“ profile. No login button in the chat? Click 'Login' above, then 'Reload'.")
    }
}
