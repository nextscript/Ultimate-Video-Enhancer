#pragma once
// Auto-Scene-Match – 1:1-Port aus Ultimate Video Enhancer 1.17.5 (uve.user.js, "Auto Scene Match",
// Zeilen 11072–11760): Bildstatistik eines 96×54-Rohbilds, Schnitterkennung, Bewegungsschwelle,
// adaptive Analyse-Rate 2–10 fps, Glättung und daraus die Auto-Matrix (Farbton → Sättigung → Helligkeit/Kontrast).
//
// Unterschied zum Userscript: die Matrix wird nicht als Shader-Uniform, sondern über den mpv-Video-Equalizer
// angewendet (brightness/contrast/saturation/hue). Der wirkt wie uAutoMatrix früh im Rohbild (bei der
// YUV→RGB-Wandlung) und ändert sich ohne Shader-Neukompilierung. Die Umrechnung ist exakt für YUV-Videos
// (BT.709-Luma wie UVE); bei RGB-Quellen wirken nur Helligkeit und Kontrast.

#include <QImage>
#include <QVector>
#include <cstdint>

namespace uve::fx {

struct AutoStats
{
    double mR = 0, mG = 0, mB = 0, mY = 0, sdY = 0, mCh = 0, mSat = 0.25, skin = 0;
    double cutScore = 0;
};

struct AutoGrade
{
    double br = 1.0, ct = 1.0, sat = 1.0, hue = 0.0;   // wie AUTO.cur im Userscript
};

// mpv-Equalizer-Werte (Optionen −100 … 100)
struct MpvEq
{
    double brightness = 0, contrast = 0, saturation = 0, hue = 0;
    bool operator==(const MpvEq &) const = default;
};

class AutoSceneMatch
{
public:
    // Einstellungen (UVE: autoStrength 0 … 1, Standard 0.65; autoLockWB Standard an)
    double strength = 0.65;
    bool lockWB = true;

    // Zustand zurücksetzen (Datei geladen / Wiedergabe startet: kurzer Boost wie resetAuto())
    void reset(qint64 nowMs, bool boost);
    // Ein Analyseschritt mit einem Bild (wird auf 96×54 skaliert). Rückgabe: true, wenn die Auto-Matrix
    // aktualisiert wurde. nextIntervalMs liefert den Abstand bis zur nächsten Analyse.
    bool step(const QImage &frame, qint64 nowMs);
    // Kein Bild verfügbar / pausiert: Zustand wie im Userscript-Zweig "kein Video" bzw. "paused"
    void idle(bool noVideo);
    int nextIntervalMs() const;

    const AutoGrade &current() const { return m_cur; }
    const AutoGrade &target() const { return m_tgt; }
    double adaptiveFps() const { return m_fps; }

    // Hilfsfunktionen (öffentlich für Tests)
    static AutoStats computeFrameStats(const QImage &img96x54);
    static MpvEq toMpvEq(const AutoGrade &g);
    // Rechnet einen mpv-Equalizer aus einem angezeigten Bild heraus (Umkehrung von csputils: Kontrast/Helligkeit
    // auf RGB, Sättigung/Farbton in BT.709-YCbCr). So analysiert Auto-Scene-Match ohne Rückkopplung auf sich selbst.
    static QImage removeEq(const QImage &img, const MpvEq &eq);
    static AutoGrade neutral() { return {}; }

    static constexpr int kCanvasW = 96;
    static constexpr int kCanvasH = 54;

private:
    double computeMotion(const QImage &img);
    bool detectCut(AutoStats &sig) const;
    AutoStats updateStatsAveraging(const AutoStats &sig);
    void updateTargets(const AutoStats &sig);
    void updateSmoothing(bool isCut);
    double calculateAdaptiveFps(double changeScore);
    double pickAutoFps(qint64 nowMs, double cutScore);

    AutoGrade m_cur, m_tgt;
    bool m_haveLastSig = false;
    AutoStats m_lastSig;
    bool m_haveEma = false;
    AutoStats m_statsEma;
    QVector<uint8_t> m_lastLuma;
    double m_motionEma = 0;
    int m_motionFrames = 0;
    double m_scoreEma = 0;
    qint64 m_boostStart = 0, m_boostUntil = 0;
    double m_fps = 2.0;               // ADAPTIVE_FPS.current
    QVector<double> m_fpsHistory;     // ADAPTIVE_FPS.history (5)
    double m_nextFps = 2.0;
};

} // namespace uve::fx
