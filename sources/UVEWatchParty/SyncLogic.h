#pragma once
// Pure synchronisation helpers (no Qt objects, no I/O) – unit tested in tst_uvewatchparty.cpp.
#include <QByteArray>
#include <QString>
#include <QtGlobal>
#include <cmath>

namespace uve::watchparty::sync {

// Room ID: 6 random letters and digits without separator, e.g. K7M3QX (no 0/O, 1/I/L, U/V – easy to read and type)
inline constexpr const char *kRoomIdAlphabet = "23456789ABCDEFGHJKMNPQRSTWXYZ";

inline QString normalizeRoomId(const QString &input)
{
    QString raw;
    for (const QChar c : input) {
        if (c.isSpace() || c == u'-' || c == u'_') continue;
        raw += c.toUpper();
    }
    if (raw.size() != 6) return {};
    const QByteArray alphabet(kRoomIdAlphabet);
    for (const QChar c : raw) if (c.unicode() == 0 || c.unicode() >= 128 || !alphabet.contains(char(c.unicode()))) return {};
    return raw;
}

// Authoritative room state as sent by the server
struct RoomState {
    qint64 gen = 0;
    qint64 seq = 0;
    bool paused = true;
    double position = 0;
    qint64 ref = 0;          // server time (ms) at which `position` was valid
};

// Reference position plus elapsed server time while playing, clamped to the duration
inline double targetPosition(const RoomState &s, qint64 serverNowMs, double duration)
{
    double p = s.position;
    if (!s.paused) p += double(qMax<qint64>(0, serverNowMs - s.ref)) / 1000.0;
    if (duration > 0) p = qMin(p, duration);
    return qMax(0.0, p);
}

// Durations differ noticeably (other cut, wrong file): more than 3 s and more than 1 %
inline bool durationMismatch(double a, double b)
{
    if (a <= 0 || b <= 0) return false;
    const double d = std::abs(a - b);
    return d > 3.0 && d > 0.01 * qMax(a, b);
}

// Drift correction for guests: within the tolerance nothing happens; moderate drift is removed with a slightly
// changed playback speed (no visible jumps); only large drift seeks. A short debounce ignores single outliers.
class DriftController
{
public:
    enum Kind { Keep, SetSpeed, Seek };
    struct Decision { Kind kind = Keep; double speed = 1.0; };

    static constexpr double kTolerance = 0.25;   // start value from the spec, confirmed in tests
    static constexpr double kSettle = 0.08;      // back to 1× below this
    static constexpr double kSeekAbove = 3.0;
    static constexpr int kDebounceTicks = 2;     // 2 × 250 ms

    void reset() { m_over = 0; m_speed = 1.0; }
    double speed() const { return m_speed; }

    // drift = local position − target position (positive: local is ahead)
    Decision update(double drift, bool playing)
    {
        if (!playing) {
            if (m_speed != 1.0) { m_speed = 1.0; return {SetSpeed, 1.0}; }
            return {};
        }
        const double a = std::abs(drift);
        if (a > kSeekAbove) {
            if (++m_over < kDebounceTicks) return {};
            m_over = 0;
            m_speed = 1.0;
            return {Seek, 1.0};
        }
        if (m_speed != 1.0) {
            if (a < kSettle || (drift > 0) != (m_speed < 1.0)) { m_speed = 1.0; m_over = 0; return {SetSpeed, 1.0}; }
            return {};
        }
        if (a <= kTolerance) { m_over = 0; return {}; }
        if (++m_over < kDebounceTicks) return {};
        m_over = 0;
        // 0.25 s → 2.5 %, 1 s → 10 % (cap): converges in ~10 s without audible pitch issues (mpv scaletempo)
        const double adj = qBound(0.025, a * 0.1, 0.10);
        m_speed = drift > 0 ? 1.0 - adj : 1.0 + adj;
        return {SetSpeed, m_speed};
    }

private:
    int m_over = 0;
    double m_speed = 1.0;
};

// Observes the local player from the poll timer (the host has no event for manual seeks, see README):
// separates normal progress, deliberate jumps and changes caused by the synchronisation itself.
class PlayerObserver
{
public:
    struct Change { bool paused = false; bool played = false; bool seeked = false; double position = 0; };
    static constexpr double kJump = 1.2;          // s beyond the expected progress
    static constexpr qint64 kExpectWindow = 2000; // ms in which our own change is absorbed

    void reset(double pos, bool paused, qint64 nowMs)
    {
        m_pos = pos; m_paused = paused; m_last = nowMs; m_valid = true; m_expectUntil = 0;
    }

    // The plugin itself is about to pause/seek: do not report it back to the server
    void expect(double pos, bool paused, qint64 nowMs, bool seek)
    {
        m_expPos = pos; m_expPaused = paused; m_expSeek = seek; m_expAt = nowMs; m_expectUntil = nowMs + kExpectWindow;
    }

    Change update(double pos, bool paused, double speed, qint64 nowMs)
    {
        Change c;
        c.position = pos;
        if (!m_valid) { reset(pos, paused, nowMs); return c; }
        const double dt = double(nowMs - m_last) / 1000.0;
        const double expected = m_pos + (m_paused ? 0.0 : dt * (speed > 0 ? speed : 1.0));
        const bool jumped = std::abs(pos - expected) > kJump;
        const bool pauseChanged = paused != m_paused;
        const bool inWindow = nowMs < m_expectUntil;
        bool ownPause = false, ownSeek = false;
        if (inWindow) {
            ownPause = paused == m_expPaused;
            const double expNow = m_expPos + (m_expPaused ? 0.0 : double(nowMs - m_expAt) / 1000.0);
            ownSeek = !m_expSeek || std::abs(pos - expNow) < 2.0;
        }
        if (pauseChanged && !ownPause) { if (paused) c.paused = true; else c.played = true; }
        if (jumped && !ownSeek) c.seeked = true;
        m_pos = pos; m_paused = paused; m_last = nowMs;
        return c;
    }

private:
    double m_pos = 0;
    bool m_paused = true;
    qint64 m_last = 0;
    bool m_valid = false;
    double m_expPos = 0;
    bool m_expPaused = true;
    bool m_expSeek = false;
    qint64 m_expAt = 0;
    qint64 m_expectUntil = 0;
};

// Single HTTP byte range (RFC 9110). Invalid syntax and multiple ranges → full response (allowed by HTTP).
struct ByteRange {
    enum Type { Full, Range, Unsatisfiable } type = Full;
    qint64 start = 0;
    qint64 end = -1;
};

inline ByteRange parseRange(const QByteArray &header, qint64 size)
{
    ByteRange r;
    const QByteArray h = header.trimmed();
    if (!h.toLower().startsWith("bytes=")) return r;
    const QByteArray spec = h.mid(6).trimmed();
    if (spec.contains(',')) return r;
    const int dash = spec.indexOf('-');
    if (dash < 0) return r;
    const QByteArray a = spec.left(dash).trimmed(), b = spec.mid(dash + 1).trimmed();
    bool okA = true, okB = true;
    const qint64 va = a.isEmpty() ? -1 : a.toLongLong(&okA);
    const qint64 vb = b.isEmpty() ? -1 : b.toLongLong(&okB);
    if (!okA || !okB || (a.isEmpty() && b.isEmpty()) || va < -1 || vb < -1) return r;
    if (a.isEmpty()) {
        if (vb == 0) { r.type = ByteRange::Unsatisfiable; return r; }
        r.type = ByteRange::Range; r.start = qMax<qint64>(0, size - vb); r.end = size - 1;
        return r;
    }
    if (!b.isEmpty() && vb < va) return r;
    if (va >= size) { r.type = ByteRange::Unsatisfiable; return r; }
    r.type = ByteRange::Range; r.start = va; r.end = b.isEmpty() ? size - 1 : qMin(vb, size - 1);
    return r;
}

} // namespace uve::watchparty::sync
