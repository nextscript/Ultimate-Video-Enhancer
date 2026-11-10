#pragma once
#include "CdAudioStream.h"
#include "DiscCapabilities.h"
#include "DiscSourceResolver.h"
#include "OpticalDriveEnumerator.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace uve {

class MpvController;
class PlaylistModel;

// Optische Medien: Laufwerke, Images/Ordner, Ladeplan, Disc-Navigation. Dekodiert nichts selbst –
// geladen wird über MpvController::load() (gleiche asynchrone Befehlswarteschlange wie bei Dateien,
// gleicher Renderpfad vo=libmpv → MpvVideoItem, ShaderChain unverändert). In QML als app.disc.
class OpticalMediaService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList drives READ drives NOTIFY drivesChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY drivesChanged)
    Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)                 // aktuelle Wiedergabe ist eine Disc
    Q_PROPERTY(QString activeKind READ activeKind NOTIFY stateChanged)      // dvd | bluray | cdda | ""
    Q_PROPERTY(bool navigationAvailable READ navigationAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool menuActive READ menuActive NOTIFY stateChanged)          // Disc zeigt gerade ein Menü
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)  // [{index, title, time}]
    Q_PROPERTY(int chapter READ chapter NOTIFY chaptersChanged)
    Q_PROPERTY(QVariantList titles READ titles NOTIFY chaptersChanged)      // edition-list: [{id, title, current}]

public:
    OpticalMediaService(MpvController *player, PlaylistModel *playlist, const QString &appDir, QObject *parent = nullptr);

    // nach MpvController::initialize(): Fähigkeiten abfragen, CD-Leser anmelden, Eigenschaften beobachten
    void attach();

    QVariantList drives() const { return m_drives; }
    bool scanning() const { return m_scanning; }
    QVariantMap capabilities() const { return m_caps.toMap(); }
    const optical::DiscCapabilities &caps() const { return m_caps; }
    void setCapabilities(const optical::DiscCapabilities &caps) { m_caps = caps; emit capabilitiesChanged(); }   // Tests
    bool active() const { return m_activeKind != optical::DiscKind::Unknown; }
    QString activeKind() const { return optical::kindId(m_activeKind); }
    bool navigationAvailable() const;
    bool menuActive() const { return m_menuActive && navigationAvailable(); }
    QVariantList chapters() const { return m_chapters; }
    int chapter() const { return m_chapter; }
    QVariantList titles() const { return m_titles; }

    static bool isDiscEntry(const QString &path) { return optical::resolver::isEntry(path); }
    // Playlist-Eintrag laden (von PlayerApp::loadPath); false + *error bei nicht ladbarer Quelle
    bool load(const QString &entry, double start, bool paused, QString *error);

    Q_INVOKABLE void refreshDrives();
    // Ordner oder Image prüfen (im Hintergrund) → inspected(path, info)
    Q_INVOKABLE void inspect(const QString &pathOrUrl);
    // Laufwerk bzw. Image/Ordner öffnen (im Hintergrund prüfen, dann in die Wiedergabeliste) → openFailed bei Fehler
    Q_INVOKABLE void openDrive(const QString &device, bool startInMenu);
    Q_INVOKABLE void openSource(const QString &pathOrUrl, bool startInMenu);
    Q_INVOKABLE bool navigate(const QString &action);
    Q_INVOKABLE QString menuKeyAction(const QString &key) const;
    Q_INVOKABLE QStringList menuKeys() const;
    Q_INVOKABLE void selectChapter(int index);
    Q_INVOKABLE void selectTitle(int editionId);
    Q_INVOKABLE QVariantList info() const;   // Disc-Informationen: [{label, value}]
    Q_INVOKABLE QString errorText(int code) const;
    Q_INVOKABLE QString kindLabel(const QString &kind) const;

    // Fehler beim Abspielen einer Disc einordnen (z. B. Medium entfernt); Text für die Anzeige
    QString failureText() const;

signals:
    void drivesChanged();
    void capabilitiesChanged();
    void stateChanged();
    void chaptersChanged();
    void inspected(const QString &path, const QVariantMap &info);
    void openFailed(const QString &message);
    void playRequested(int index);
    void osd(const QString &text, int ms);

private:
    struct Prepared { optical::DiscSource source; optical::DiscError error = optical::DiscError::None; QList<optical::CdTrack> tracks; };
    static Prepared prepare(const QString &device, bool isDrive, bool menu, optical::DiscKind knownKind);
    void finishOpen(const Prepared &p);
    void onObserved(const QString &name, const QVariant &value);
    void onFileLoaded();
    void setActive(optical::DiscKind kind);

    MpvController *m_player;
    PlaylistModel *m_playlist;
    QString m_appDir;
    optical::DiscCapabilities m_caps;
    optical::CdDeviceFactory m_cdFactory;
    QVariantList m_drives;
    bool m_scanning = false;
    optical::DiscSource m_current;          // zuletzt geladene Disc-Quelle
    optical::DiscKind m_activeKind = optical::DiscKind::Unknown;
    bool m_menuActive = false;
    QVariantList m_chapters, m_titles;
    int m_chapter = -1;
};

} // namespace uve
