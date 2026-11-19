#pragma once
// Benachrichtigungston (MP3). Windows: Mediensteuerung (MCI, winmm.dll). Linux/macOS: eine eigene, unsichtbare
// Instanz der libmpv, die der Player ohnehin geladen hat (das laufende Video bleibt unberührt); ohne libmpv das
// Abspielprogramm des Systems (afplay bzw. pw-play, paplay, ffplay, mpg123). Der Player liefert kein Qt Multimedia mit.

#include <QElapsedTimer>
#include <QString>

class NotifySound
{
public:
    NotifySound();
    ~NotifySound();

    void setFile(const QString &path);       // leer oder fehlend = stumm
    void setVolumePercent(int percent);      // nur dieser Ton, 0–100; die System-Lautstärke bleibt
    int volumePercent() const { return m_volume; }
    QString file() const { return m_file; }
    // Spielt von vorn; mehrere Nachrichten kurz hintereinander klingeln nur einmal.
    bool play();
    QString mode() const;                    // „playing“, „stopped“ … (für Tests, nur Windows)
    int deviceVolume() const;                // was Windows gerade eingestellt hat, 0–1000 (für Tests, nur Windows)
    int playCount() const { return m_plays; }

private:
    void closeDevice();
    QString m_file;
    QString m_alias;
    bool m_open = false;
    void *m_mpv = nullptr;                   // Linux/macOS: eigene libmpv-Instanz für den Ton
    int m_plays = 0;
    int m_volume = 100;
    QElapsedTimer m_last;
};
