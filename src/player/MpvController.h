#pragma once
#include <QHash>
#include <QMutex>
#include <QRect>
#include <atomic>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QVariant>

struct mpv_handle;
struct mpv_event;

namespace uve {

// Adapter um libmpv (Client-API). Lebt im GUI-Thread; mpv-Ereignisse werden über den
// Wakeup-Callback per Qt::QueuedConnection eingesammelt (kein blockierendes Polling).
class MpvController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(double volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaTitleChanged)
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList videoTracks READ videoTracks NOTIFY tracksChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY seekableChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY tracksChanged)
    Q_PROPERTY(double bufferedUntil READ bufferedUntil NOTIFY bufferedChanged)
    Q_PROPERTY(double subDelay READ subDelay NOTIFY delaysChanged)
    Q_PROPERTY(double audioDelay READ audioDelay NOTIFY delaysChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool abLoopActive READ abLoopActive NOTIFY abLoopChanged)
    Q_PROPERTY(QString abLoopText READ abLoopText NOTIFY abLoopChanged)
    // Sichtbare Videofläche im Ausgabebild (mpv "osd-dimensions": w, h, ml, mt, mr, mb in Pixeln)
    Q_PROPERTY(QVariantMap osdDimensions READ osdDimensions NOTIFY osdDimensionsChanged)
    // Video-Equalizer (brightness/contrast/saturation/hue, je −100 … 100) – z. B. von Auto-Scene-Match gesetzt
    Q_PROPERTY(QVariantMap videoEq READ videoEq NOTIFY videoEqChanged)

public:
    enum State { Idle, Opening, Playing, Paused, Buffering, Ended, Error };
    Q_ENUM(State)

    explicit MpvController(QObject *parent = nullptr);
    ~MpvController() override;

    // Muss vor dem ersten Render-Kontext aufgerufen werden; Optionen siehe .cpp
    bool initialize(const QVariantMap &options, QString *error);
    mpv_handle *handle() const { return m_mpv; }
    QString mpvVersion() const;
    bool loadfileTakesIndex() const;

    State state() const { return m_state; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    bool paused() const { return m_paused; }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    double speed() const { return m_speed; }
    QString mediaTitle() const { return m_title; }
    QString path() const { return m_path; }
    QVariantList audioTracks() const { return m_audio; }
    QVariantList subtitleTracks() const { return m_subs; }
    QVariantList videoTracks() const { return m_video; }
    bool seekable() const { return m_seekable; }
    bool hasVideo() const { return !m_video.isEmpty(); }
    double bufferedUntil() const { return m_bufferedUntil; }
    double subDelay() const { return m_subDelay; }
    double audioDelay() const { return m_audioDelay; }
    QString errorString() const { return m_error; }
    QVariantMap diagnostics() const { return m_diag; }
    bool abLoopActive() const;
    QString abLoopText() const;
    QVariantMap osdDimensions() const { return m_osdDims; }
    QVariantMap videoEq() const { return m_eq; }

    // Render-Kontext (Videoausgabe) bereit? Vorher geladene Dateien würden ohne Video starten
    // (vo=libmpv hat ohne Render-Kontext keine Ausgabe) – load() wartet daher darauf.
    bool renderReady() const { return m_renderReady; }
    Q_INVOKABLE void markRenderReady();

    // Befehle (alle aus QML aufrufbar)
    Q_INVOKABLE void load(const QString &url, double startSeconds = 0, bool startPaused = false);
    // wie load(), mit zusätzlichen Datei-Optionen (z. B. "ytdl-format=137+ba/137"); Werte ohne Komma
    void load(const QString &url, double startSeconds, bool startPaused, const QStringList &fileOptions);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setPaused(bool p);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double seconds, bool exact = false);
    Q_INVOKABLE void seekRelative(double seconds);
    Q_INVOKABLE void seekPercent(double percent);
    Q_INVOKABLE void frameStep();
    Q_INVOKABLE void frameBackStep();
    Q_INVOKABLE void setVolume(double v);
    Q_INVOKABLE void setMuted(bool m);
    Q_INVOKABLE void setSpeed(double s);
    // Spur, die statt mpvs Auswahl laufen sollte (nur Einträge aus mpvs "track-list"); 0 = Auswahl beibehalten.
    // languages in Prioritätsreihenfolge, Schreibweise egal ("de", "ger", "deu" und "de-DE" sind dieselbe Sprache).
    static int preferredTrackId(const QVariantList &tracks, const QString &type, const QStringList &languages);

    Q_INVOKABLE void setAudioTrack(int id);
    Q_INVOKABLE void setSubtitleTrack(int id); // 0 = aus
    Q_INVOKABLE void cycleAudio();
    Q_INVOKABLE void cycleSubtitle();
    Q_INVOKABLE void addSubtitle(const QString &file);
    Q_INVOKABLE void setSubDelay(double seconds);
    Q_INVOKABLE void setAudioDelay(double seconds);
    Q_INVOKABLE void cycleAbLoop();
    Q_INVOKABLE void setAspect(const QString &mode); // fit|stretch|16:9|4:3|21:9
    // Vorher/Nachher-Ansicht: der UVE-Shader liefert ein doppelt breites Bild (links Original, rechts mit Effekten).
    // mpv zeigt es mit doppeltem Seitenverhältnis an; die Oberfläche setzt beide Hälften am Schieber zusammen.
    // Dadurch ändert das Ziehen des Schiebers nichts an mpv (jede Shader-Änderung baut mpvs Render-Kette neu auf).
    Q_INVOKABLE void setSplitView(bool on);
    Q_INVOKABLE QString screenshot(const QString &directory);
    // Aktueller dekodierter Frame (ohne OSD/Untertitel/Benutzer-Shader), auf maxWidth verkleinert; leer bei Fehler
    QImage grabFrame(int maxWidth) const;
    Q_INVOKABLE void showText(const QString &text, int ms = 1500);
    Q_INVOKABLE QVariant getProperty(const QString &name) const;
    Q_INVOKABLE bool setProperty(const QString &name, const QVariant &value);
    Q_INVOKABLE bool command(const QStringList &args);
    // Wie setProperty, blockiert den aufrufenden Thread aber nicht (mpv_set_property_async)
    void setPropertyAsync(const QString &name, const QVariant &value);
    // Zusätzliche Eigenschaft beobachten (z. B. disc-menu-active, chapter-list für optische Medien);
    // Änderungen kommen über observedPropertyChanged. Mehrfaches Anmelden ist wirkungslos.
    void observe(const QString &name);

    // Kleine Bildprobe der Videoausgabe (GPU-Rücklesen im Render-Thread, ohne den mpv-Kern zu blockieren).
    // requestDisplaySample() fordert eine neue Probe an; displaySample() liefert die zuletzt fertige (96×54, RGB32).
    void requestDisplaySample() { m_sampleRequested.store(true); }
    QImage displaySample() const { QMutexLocker l(&m_sampleLock); return m_sample; }
    // Render-Thread (MpvVideoItem)
    bool takeDisplaySampleRequest() { return m_sampleRequested.exchange(false); }
    void storeDisplaySample(const QImage &img) { QMutexLocker l(&m_sampleLock); m_sample = img; }
    // Sichtbare Videofläche (osd-dimensions) als Anteil des Ausgabebilds: x, y, w, h in 0 … 1
    QRectF videoAreaFraction() const
    {
        QMutexLocker l(&m_sampleLock);
        // Vergleichsansicht: nur die rechte Hälfte (mit Effekten) analysieren
        if (m_sampleSplit) return QRectF(m_videoArea.x() + m_videoArea.width() / 2, m_videoArea.y(), m_videoArea.width() / 2, m_videoArea.height());
        return m_videoArea;
    }

signals:
    void stateChanged();
    void positionChanged();
    void durationChanged();
    void pausedChanged();
    void volumeChanged();
    void mutedChanged();
    void speedChanged();
    void mediaTitleChanged();
    void pathChanged();
    void tracksChanged();
    void seekableChanged();
    void bufferedChanged();
    void delaysChanged();
    void diagnosticsChanged();
    void abLoopChanged();
    void osdDimensionsChanged();
    void videoEqChanged();

    void fileLoaded();
    void endOfFile(const QString &reason);   // "eof", "error", "stop", "quit", "redirect"
    void logMessage(const QString &prefix, const QString &level, const QString &text);
    void shaderError(const QString &text);
    void observedPropertyChanged(const QString &name, const QVariant &value);

private slots:
    void processEvents();
    void pollDiagnostics();

private:
    static void wakeup(void *ctx);
    void handleEvent(mpv_event *e);
    void handlePropertyChange(const QString &name, const QVariant &value);
    void setState(State s, const QString &error = {});
    void updateTracks(const QVariant &trackList);
    void updatePlayState();
    void applyLanguagePreference();

    mpv_handle *m_mpv = nullptr;
    State m_state = Idle;
    double m_position = 0, m_duration = 0, m_volume = 100, m_speed = 1;
    double m_bufferedUntil = 0, m_subDelay = 0, m_audioDelay = 0;
    double m_abA = -1, m_abB = -1;
    bool m_paused = false, m_muted = false, m_seekable = false, m_buffering = false, m_loaded = false;
    QString m_title, m_path, m_error;
    QVariantList m_audio, m_subs, m_video;
    QVariantMap m_diag;
    QVariantMap m_osdDims;
    QVariantMap m_eq{{"brightness", 0.0}, {"contrast", 0.0}, {"saturation", 0.0}, {"hue", 0.0}};
    std::atomic<bool> m_sampleRequested{false};
    mutable QMutex m_sampleLock;
    QImage m_sample;
    QRectF m_videoArea{0, 0, 1, 1};
    void applyAspect();
    QString m_aspectMode = QStringLiteral("fit");
    bool m_splitView = false;
    bool m_sampleSplit = false;   // Kopie für den Render-Thread (unter m_sampleLock)
    QVariant m_subVisibilityBeforeSplit;
    bool m_eventsQueued = false;
    bool m_renderReady = false;
    QStringList m_pendingLoad; // zurückgestellter loadfile-Befehl bis zur Videoausgabe
    QStringList m_extraObserved;
};

} // namespace uve
