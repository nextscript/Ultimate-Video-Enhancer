#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace uve {

class MpvController;

// Ein Videofilter (mpv "vf") mit Frame-Historie, z. B. Temporal Denoising. Die Stufe trägt ein eigenes Label; andere
// Einträge der vf-Liste bleiben unberührt.
//
// Software-Filter brauchen Bilder im Arbeitsspeicher. Direkte Hardware-Decodierung (z. B. nvdec) liefert GPU-Oberflächen,
// daran scheitert der Filter. Solange die Stufe aktiv ist, läuft die Decodierung daher im Copy-Modus; danach wird
// die vorherige Einstellung wiederhergestellt. mpv speichert das nicht, settings.json bleibt unverändert.
class VideoFilterStage : public QObject
{
    Q_OBJECT
public:
    explicit VideoFilterStage(MpvController *mpv, QObject *parent = nullptr);

    // Leerer Graph entfernt die Stufe. false + *error bei ungültigem Graph oder fremdem Besitzer.
    bool set(const QString &owner, const QString &graph, QString *error = nullptr);
    void clear(const QString &owner);

    // active, graph, error (letzter Fehler des Filters; wird beim nächsten set gelöscht), hwdec, dropped, fps
    QVariantMap status();

    static bool isValidGraph(const QString &graph);

private:
    void remove();
    void onLog(const QString &prefix, const QString &level, const QString &text);
    QString hwdecSetting() const;

    MpvController *m_mpv;
    QString m_owner, m_graph, m_error, m_savedHwdec, m_lastFilterLog;
    QElapsedTimer m_sinceSet;
};

} // namespace uve
