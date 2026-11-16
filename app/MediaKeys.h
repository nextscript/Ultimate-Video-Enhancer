#pragma once
#include <QObject>
#include <QString>
#include <memory>

// Medientasten (Wiedergabe/Pause, Stopp, Weiter, Zurück) über die Windows-Mediensteuerung (SystemMediaTransportControls).
// Wirkt auch, wenn der Player nicht im Vordergrund ist, und zeigt Titel und Status im Windows-Medien-Overlay.
// Auf anderen Systemen ohne Funktion (available() == false).
class MediaKeys : public QObject
{
    Q_OBJECT
public:
    explicit MediaKeys(QObject *parent = nullptr);
    ~MediaKeys() override;

    bool attach(quintptr nativeWindow);   // HWND des Hauptfensters; false, wenn die Mediensteuerung nicht verfügbar ist
    bool available() const;
    QString lastError() const { return m_error; }

    enum class Status { Stopped, Playing, Paused, Closed };
    void setStatus(Status status);
    void setTitle(const QString &title, const QString &artist, bool video);
    void setNavigation(bool canNext, bool canPrevious);

signals:
    // Aktions-IDs wie in den Tastenkürzeln: play.toggle | play.play | play.pause | play.stop | play.next | play.prev
    void pressed(const QString &action);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    QString m_error;
};
