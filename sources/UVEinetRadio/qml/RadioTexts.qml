import QtQml

// Texte zu den Codes aus dem Plugin (Zustände und Fehler) – an einer Stelle übersetzt
QtObject {
    function stateText(state, buffering, paused) {
        switch (state) {
        case "checking": return qsTr("Prüfe Stream …")
        case "connecting": return qsTr("Verbinde …")
        case "playing": return paused ? qsTr("Pausiert") : buffering ? qsTr("Puffert …") : qsTr("Läuft")
        case "error": return qsTr("Fehler")
        case "stopped": return qsTr("Gestoppt")
        case "ended": return qsTr("Stream beendet")
        default: return qsTr("Bereit")
        }
    }
    function errorText(code, detail) {
        var t
        switch (code) {
        case "stream": t = qsTr("Der Player konnte den Stream nicht abspielen."); break
        case "timeout": t = qsTr("Der Sender antwortet nicht (Zeitüberschreitung)."); break
        case "unreachable": t = qsTr("Der Stream ist nicht erreichbar."); break
        case "certificate": t = qsTr("Das HTTPS-Zertifikat des Senders ist ungültig – Verbindung abgelehnt."); break
        case "redirect": t = qsTr("Unsichere oder ungültige Weiterleitung abgelehnt (nur http/https, kein Wechsel von HTTPS zu HTTP)."); break
        case "webpage": t = qsTr("Diese Adresse ist eine Webseite, kein Audio-Stream. Gib die direkte Stream-Adresse ein (z. B. …/stream.mp3, .m3u8, .pls)."); break
        case "scheme": t = qsTr("Nur http:// und https:// Adressen werden unterstützt."); break
        case "host": t = qsTr("Die Adresse enthält keinen Server."); break
        case "empty": t = qsTr("Bitte eine Stream-Adresse eingeben."); break
        case "notInCatalog": t = qsTr("Der Sender ist nicht mehr im Katalog."); break
        case "api": t = qsTr("Der Senderkatalog ist gerade nicht erreichbar."); break
        default: t = qsTr("Unbekannter Fehler.")
        }
        return detail && detail !== "" && code !== "webpage" ? t + "\n" + detail : t
    }
    function bytes(n) {
        if (n < 1024) return n + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(0) + " KB"
        return (n / 1024 / 1024).toFixed(1) + " MB"
    }
}
