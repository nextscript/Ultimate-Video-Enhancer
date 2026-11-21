#pragma once
// UVEPreview: Vorschau-Decoder. Eine eigene libmpv-Instanz (dieselbe libmpv, die der Player schon geladen hat)
// in einem Hintergrund-Thread lädt das Medium (lokale Datei oder Stream) ein zweites Mal, springt an die gewünschte
// Stelle und holt ein kleines Bild. Die Hauptwiedergabe bleibt unberührt.
// Pro Medium höchstens eine Instanz; es wartet immer nur der neueste Auftrag (ältere werden ersetzt).
#include <QImage>
#include <QString>
#include <QStringList>
#include <QThread>

#include <condition_variable>
#include <mutex>

#include <algorithm>

namespace uve::preview {

// Zeitraster der Vorschau: ~400 Plätze über die Länge; kurze Videos fein (bis 1/25 s), lange höchstens 10 s
inline double previewStep(double duration)
{
    return duration <= 0 ? 1.0 : std::clamp(duration / 400.0, 0.04, 10.0);
}

// Abruf eines Streams wie im Player: dieselben Kopfzeilen, Cookies und derselbe Proxy
struct StreamAccess {
    QString userAgent;
    QString referrer;
    QStringList headerFields;   // "Name: Wert", einzeln angehängt (Werte dürfen Kommas enthalten)
    QString proxy;
    bool cookies = false;
    QString cookiesFile;
};

struct PreviewJob {
    quint64 id = 0;        // Anfragekennung
    quint64 media = 0;     // Kennung der Wiedergabesitzung
    QString path;          // lokaler Dateipfad oder (network) die Adresse, die der Player geöffnet hat
    bool network = false;  // Stream (http/https/ftp/edl) statt lokaler Datei
    StreamAccess access;   // nur bei network
    double time = 0;       // Zielzeit in Sekunden
    int index = -1;        // Rasterplatz (Cache-Schlüssel)
    int boxWidth = 240;    // Bild passt in boxWidth × boxHeight, Seitenverhältnis bleibt
    int boxHeight = 180;
    double duration = 0;   // Länge des Mediums (Sekunden)
    bool exact = true;     // exakter Sprung gewünscht (sonst Keyframe – schneller); große Videos s. renderAt()
};

class PreviewWorker : public QThread
{
    Q_OBJECT
public:
    explicit PreviewWorker(QObject *parent = nullptr);
    ~PreviewWorker() override;

    static bool mpvAvailable(QString *error = nullptr);

    void submit(const PreviewJob &job);   // ersetzt einen noch wartenden Auftrag
    void cancelPending();                 // wartenden Auftrag verwerfen (laufender endet normal)
    // Instanz schließen (Medienwechsel, Plugin aus). waitMs > 0: warten, bis die Datei wirklich geschlossen ist –
    // manche Netzlaufwerke (RaiDrive) lassen den Player die Datei sonst nicht erneut öffnen
    void closeMedia(int waitMs = 0);
    void shutdown();                      // Thread beenden und warten (räumt mpv auf)

signals:
    // Aus dem Worker-Thread; Verbindung zum Hauptthread läuft über die Ereignisschleife
    // image: das Bild; url: dasselbe als PNG-data:-URL für QML (im Worker-Thread kodiert, nicht im UI-Thread)
    void frameReady(quint64 id, quint64 media, int index, const QImage &image, const QString &url);
    void frameFailed(quint64 id, quint64 media, int index, const QString &reason);
    void mediaUnsupported(quint64 media, const QString &reason);

protected:
    void run() override;

private:
    struct Instance;
    bool openInstance(Instance &inst, const PreviewJob &job, QString *reason);
    void destroyInstance(Instance &inst);
    QImage renderAt(Instance &inst, const PreviewJob &job, QString *reason);
    bool interrupted();   // Thread soll enden oder Medium schließen

    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_hasJob = false;
    PreviewJob m_job;
    bool m_stop = false;
    bool m_close = false;
    bool m_fileOpen = false;              // eine Instanz hält die Datei offen
    std::condition_variable m_closedCv;
};

} // namespace uve::preview
