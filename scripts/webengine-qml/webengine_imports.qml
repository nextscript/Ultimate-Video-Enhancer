import QtQuick
import QtWebEngine

// Referenzdatei fuer macdeployqt (laufzeitlich ungenutzt, wird nie geladen):
// Player-Plugins importieren QtWebEngine in eigenen Panel-Seiten aus dem Plugin-Ordner,
// den macdeployqt nicht scannt. Dieser Import sorgt dafuer, dass das QtWebEngine-QML-Modul
// samt QtWebEngineProcess-Helfer in den App-Bundle gelangt (siehe scripts/package-macos.sh).
// Nur relevant, wenn der Build mit UVE_WITH_WEBENGINE (QtWebEngine-Komponente) gebaut wurde.
