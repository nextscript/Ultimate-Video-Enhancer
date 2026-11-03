#include "AutoSceneMatch.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace uve::fx {

namespace {

constexpr double LR = 0.2126, LG = 0.7152, LB = 0.0722;   // UVE LUMA

// Konstanten aus dem Userscript (const AUTO / ADAPTIVE_FPS)
constexpr double kFpsMin = 2.0, kFpsMax = 10.0;
constexpr int kFpsHistory = 5;
constexpr qint64 kBoostMs = 800, kMinBoostEarlyMs = 700;
constexpr double kScoreAlpha = 0.16, kMotionAlpha = 0.20, kMotionThresh = 0.015, kStatsAlpha = 0.06;
constexpr int kMotionMinFrames = 8;

double clampd(double v, double lo, double hi) { return std::min(std::max(v, lo), hi); }

double approach(double cur, double tgt, double a, double dead)
{
    const double d = tgt - cur;
    if (std::fabs(d) < dead) return tgt;
    return cur + d * a;
}

double wrapHueDeg(double d)
{
    while (d > 180) d -= 360;
    while (d < -180) d += 360;
    return d;
}

QImage toCanvas(const QImage &frame)
{
    // ctx.drawImage(v, 0, 0, 96, 54): auf die Canvas-Größe gestreckt
    QImage img = frame.size() == QSize(AutoSceneMatch::kCanvasW, AutoSceneMatch::kCanvasH)
        ? frame
        : frame.scaled(AutoSceneMatch::kCanvasW, AutoSceneMatch::kCanvasH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32) img = img.convertToFormat(QImage::Format_RGB32);
    return img;
}

} // namespace

AutoStats AutoSceneMatch::computeFrameStats(const QImage &img)
{
    double sumR = 0, sumG = 0, sumB = 0, sumY = 0, sumY2 = 0, sumCh = 0, sumSatRel = 0;
    int satRelCount = 0, skinCount = 0, count = 0;
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; y += 2) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; x += 2) {
            const QRgb px = line[x];
            const double r = qRed(px) / 255.0, g = qGreen(px) / 255.0, b = qBlue(px) / 255.0;
            const double Y = LR * r + LG * g + LB * b;
            sumR += r; sumG += g; sumB += b;
            sumY += Y; sumY2 += Y * Y;
            const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
            sumCh += mx - mn;
            if (mx > 0.06) { sumSatRel += (mx - mn) / mx; ++satRelCount; }
            if (r > g && g >= b && r > 0.12) {
                const double span = r - b;
                if (span > 0.04) {
                    const double hRatio = (g - b) / span, sRel = span / r;
                    if (hRatio > 0.15 && hRatio < 0.80 && sRel > 0.12 && sRel < 0.70) ++skinCount;
                }
            }
            ++count;
        }
    }
    const double inv = 1.0 / std::max(1, count);
    AutoStats s;
    s.mR = sumR * inv; s.mG = sumG * inv; s.mB = sumB * inv; s.mY = sumY * inv;
    s.sdY = std::sqrt(std::max(0.0, sumY2 * inv - s.mY * s.mY));
    s.mCh = sumCh * inv;
    s.mSat = satRelCount > 0 ? sumSatRel / satRelCount : 0.25;
    s.skin = skinCount * inv;
    return s;
}

double AutoSceneMatch::computeMotion(const QImage &img)
{
    QVector<uint8_t> cur;
    cur.reserve(((img.width() + 1) / 2) * ((img.height() + 1) / 2));
    for (int y = 0; y < img.height(); y += 2) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += 2) {
            const QRgb px = line[x];
            cur.push_back(uint8_t((qRed(px) * 54 + qGreen(px) * 183 + qBlue(px) * 19) >> 8));
        }
    }
    const QVector<uint8_t> prev = m_lastLuma;
    m_lastLuma = cur;
    if (prev.isEmpty() || prev.size() != cur.size()) return 1.0;
    double diff = 0;
    for (qsizetype i = 0; i < cur.size(); ++i) diff += std::abs(int(cur[i]) - int(prev[i]));
    return diff / double(cur.size());
}

bool AutoSceneMatch::detectCut(AutoStats &sig) const
{
    if (!m_haveLastSig) return false;
    const AutoStats &l = m_lastSig;
    const double dY = std::fabs(sig.mY - l.mY);
    const double dCh = std::fabs(sig.mCh - l.mCh);
    const double dRB = std::fabs((sig.mR - sig.mB) - (l.mR - l.mB));
    const double dGB = std::fabs((sig.mG - sig.mB) - (l.mG - l.mB));
    sig.cutScore = dY * 1.1 + dCh * 0.9 + dRB * 0.7 + dGB * 0.7;
    return sig.cutScore > 0.14;
}

AutoStats AutoSceneMatch::updateStatsAveraging(const AutoStats &sig)
{
    const double a = clampd(kStatsAlpha, 0.05, 0.95);
    if (!m_haveEma) { m_statsEma = sig; m_haveEma = true; return m_statsEma; }
    AutoStats &e = m_statsEma;
    e.mR = e.mR * (1 - a) + sig.mR * a;
    e.mG = e.mG * (1 - a) + sig.mG * a;
    e.mB = e.mB * (1 - a) + sig.mB * a;
    e.mY = e.mY * (1 - a) + sig.mY * a;
    e.sdY = e.sdY * (1 - a) + sig.sdY * a;
    e.mCh = e.mCh * (1 - a) + sig.mCh * a;
    e.mSat = e.mSat * (1 - a) + sig.mSat * a;
    e.skin = e.skin * (1 - a) + sig.skin * a;
    e.cutScore = sig.cutScore;
    return e;
}

void AutoSceneMatch::updateTargets(const AutoStats &sig)
{
    const double s = clampd(strength, 0, 1);
    const double errY = clampd(0.50 - sig.mY, -0.22, 0.22);
    const double br = clampd(1.0 + errY * 0.85, 0.78, 1.22);
    // Kontrast nie unter 1.0 (ct < 1 hebt den Schwarzpunkt → milchiger Schleier)
    const double errSd = clampd(0.23 - sig.sdY, -0.18, 0.18);
    const double ct = clampd(1.0 + errSd * 0.85, 1.0, 1.30);
    // Sättigung nur anheben, relative Sättigung, Hautschutz
    const double errSat = clampd(0.30 - sig.mSat, 0, 0.20);
    const double skinDamp = 1.0 - clampd(sig.skin / 0.08, 0, 1) * 0.70;
    const double sat = clampd(1.0 + errSat * 0.60 * skinDamp, 1.0, 1.15);
    double hue = 0.0;
    if (lockWB) {
        const double rb = clampd(sig.mR - sig.mB, -0.18, 0.18);
        hue = clampd(-rb * 28.0, -10.0, 10.0);
    }
    m_tgt.br = clampd(1.0 + (br - 1.0) * s, 0.78, 1.22);
    m_tgt.ct = clampd(1.0 + (ct - 1.0) * s, 1.0, 1.30);
    m_tgt.sat = clampd(1.0 + (sat - 1.0) * s, 1.0, 1.15);
    m_tgt.hue = clampd(hue * s, -12.0, 12.0);
}

void AutoSceneMatch::updateSmoothing(bool isCut)
{
    const double a = isCut ? 0.10 : 0.025;
    m_cur.br = approach(m_cur.br, m_tgt.br, a, 0.003);
    m_cur.ct = approach(m_cur.ct, m_tgt.ct, a, 0.003);
    m_cur.sat = approach(m_cur.sat, m_tgt.sat, a, 0.004);
    m_cur.hue = wrapHueDeg(approach(m_cur.hue, m_tgt.hue, a, 0.06));
}

double AutoSceneMatch::calculateAdaptiveFps(double changeScore)
{
    m_fpsHistory.push_back(changeScore);
    if (m_fpsHistory.size() > kFpsHistory) m_fpsHistory.removeFirst();
    double avg = 0;
    for (double v : m_fpsHistory) avg += v;
    avg /= m_fpsHistory.size();
    const double t1 = std::min(avg, 0.1) / 0.1;
    const double t2 = std::max(0.0, std::min(avg - 0.1, 0.2)) / 0.2;
    const double t3 = std::max(0.0, std::min(avg - 0.3, 0.7)) / 0.7;
    const double target = (avg < 0.1 ? 2 + t1 * 2 : 0) + (avg >= 0.1 && avg < 0.3 ? 4 + t2 * 3 : 0) + (avg >= 0.3 ? 7 + t3 * 3 : 0);
    const double rounded = std::round(clampd(target, kFpsMin, kFpsMax) * 2) / 2;
    m_fps += clampd(rounded - m_fps, -1, 1);
    return m_fps;
}

double AutoSceneMatch::pickAutoFps(qint64 nowMs, double cutScore)
{
    m_scoreEma = m_scoreEma * (1 - kScoreAlpha) + cutScore * kScoreAlpha;
    const double adaptive = calculateAdaptiveFps(cutScore);
    if (nowMs < m_boostUntil) {
        const qint64 age = nowMs - (m_boostStart ? m_boostStart : nowMs);
        const bool early = age >= 0 && age < kMinBoostEarlyMs;
        return std::max(adaptive, early ? 10.0 : 8.0);
    }
    return adaptive;
}

void AutoSceneMatch::reset(qint64 nowMs, bool boost)
{
    m_haveLastSig = false;
    m_lastLuma.clear();
    m_motionEma = 0;
    m_motionFrames = 0;
    m_scoreEma = 0;
    m_haveEma = false;
    if (boost) { m_boostStart = nowMs; m_boostUntil = nowMs + kBoostMs; }
    m_fps = kFpsMin;
    m_fpsHistory.clear();
    m_nextFps = kFpsMin;
}

void AutoSceneMatch::idle(bool noVideo)
{
    if (noVideo) {
        m_haveLastSig = false;
        m_lastLuma.clear();
        m_motionEma = 0;
        m_haveEma = false;
    }
    m_motionFrames = 0;
    m_nextFps = kFpsMin;
}

bool AutoSceneMatch::step(const QImage &frame, qint64 nowMs)
{
    if (frame.isNull()) { idle(true); return false; }
    const QImage img = toCanvas(frame);

    const double motion = computeMotion(img);
    m_motionEma = m_motionEma * (1 - kMotionAlpha) + motion * kMotionAlpha;
    m_motionFrames = (m_motionEma >= kMotionThresh) ? m_motionFrames + 1 : 0;

    AutoStats raw = computeFrameStats(img);
    const bool isCut = detectCut(raw);
    m_lastSig = raw;
    m_haveLastSig = true;
    const AutoStats sig = updateStatsAveraging(raw);
    if (isCut) { m_boostStart = nowMs; m_boostUntil = nowMs + kBoostMs; }

    const double rawScore = clampd(raw.cutScore, 0, 1);
    const bool allowUpdate = isCut || m_motionFrames >= kMotionMinFrames;
    m_nextFps = m_fps;
    if (allowUpdate) {
        m_nextFps = pickAutoFps(nowMs, rawScore);
        updateTargets(sig);
        updateSmoothing(isCut);
    }
    return allowUpdate;
}

int AutoSceneMatch::nextIntervalMs() const
{
    return std::max(80, int(std::lround(1000.0 / std::max(1.0, m_nextFps))));
}

QImage AutoSceneMatch::removeEq(const QImage &src, const MpvEq &eq)
{
    if (eq == MpvEq()) return src;
    QImage img = src.convertToFormat(QImage::Format_RGB32);
    const double c = 1.0 + eq.contrast / 100.0, b = eq.brightness / 100.0;
    const double s = std::max(0.01, 1.0 + eq.saturation / 100.0);
    const double th = eq.hue / 100.0 * std::numbers::pi;   // mpv dreht [U,V] effektiv um −th → zurück um +th
    const double cs = std::cos(th) / s, sn = std::sin(th) / s;
    const double invC = c > 0.01 ? 1.0 / c : 1.0;
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            double r = (qRed(line[x]) / 255.0 - b) * invC, g = (qGreen(line[x]) / 255.0 - b) * invC, bl = (qBlue(line[x]) / 255.0 - b) * invC;
            const double Y = LR * r + LG * g + LB * bl;
            const double u = (bl - Y) / 1.8556, v = (r - Y) / 1.5748;
            // Vorwärts: U' = cU + sV, V' = −sU + cV (je · Sättigung) → rückwärts: U = (cU' − sV')/s, V = (sU' + cV')/s
            const double u0 = cs * u - sn * v, v0 = sn * u + cs * v;
            r = Y + 1.5748 * v0;
            bl = Y + 1.8556 * u0;
            g = (Y - LR * r - LB * bl) / LG;
            auto q = [](double t) { return int(std::lround(clampd(t, 0.0, 1.0) * 255.0)); };
            line[x] = qRgb(q(r), q(g), q(bl));
        }
    }
    return img;
}

MpvEq AutoSceneMatch::toMpvEq(const AutoGrade &g)
{
    // UVE: M = BrightnessContrast(br, ct) · Saturation(sat) · HueRotate(hue)
    //   BC: out = br·ct·x + br·0.5·(1 − ct)
    // mpv (video/csputils.c): Farbton dreht [U,V], Sättigung skaliert [U,V], danach
    //   out = contrast·rgb + brightness  mit contrast = (opt + 100)/100, brightness = opt/100, hue = opt/100·π.
    // Die mpv-Drehung läuft in der UV-Ebene entgegengesetzt zu UVE-hueRotate → Vorzeichen umkehren.
    const double br = clampd(g.br, 0.78, 1.22), ct = clampd(g.ct, 1.0, 1.30);
    const double sat = clampd(g.sat, 1.0, 1.15), hue = clampd(g.hue, -12, 12);
    MpvEq e;
    e.contrast = (br * ct - 1.0) * 100.0;
    e.brightness = br * 0.5 * (1.0 - ct) * 100.0;
    e.saturation = (sat - 1.0) * 100.0;
    e.hue = -hue / 180.0 * 100.0;
    auto r = [](double v) { return std::round(v * 100.0) / 100.0; };
    e.contrast = r(e.contrast); e.brightness = r(e.brightness); e.saturation = r(e.saturation); e.hue = r(e.hue);
    return e;
}

} // namespace uve::fx
