#pragma once
// Benachrichtigungston (MP3) über die Windows-Mediensteuerung (MCI, winmm.dll). Der Player liefert
// kein Qt Multimedia mit; MCI gehört zu jedem Windows und spielt MP3 ohne weitere Abhängigkeiten.

#include <QElapsedTimer>
#include <QString>

class NotifySound
{
public:
    NotifySound();
    ~NotifySound();

    void setFile(const QString &path);       // leer oder fehlend = stumm
    void setVolumePercent(int percent);      // nur dieser Ton, 0–100; die Windows-Lautstärke bleibt
    int volumePercent() const { return m_volume; }
    QString file() const { return m_file; }
    // Spielt von vorn; mehrere Nachrichten kurz hintereinander klingeln nur einmal.
    bool play();
    QString mode() const;                    // „playing“, „stopped“ … (für Tests)
    int deviceVolume() const;                // was Windows gerade eingestellt hat, 0–1000 (für Tests)
    int playCount() const { return m_plays; }

private:
    void closeDevice();
    QString m_file;
    QString m_alias;
    bool m_open = false;
    int m_plays = 0;
    int m_volume = 100;
    QElapsedTimer m_last;
};
