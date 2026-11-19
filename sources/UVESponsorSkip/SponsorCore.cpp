#include "SponsorCore.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace uve::sponsor {

const QList<Category> &categories()
{
    static const QList<Category> list = {
        {"sponsor", "Sponsor", "Sponsor", Mode::Auto, "#00d400"},
        {"selfpromo", "Eigenwerbung", "Self promotion", Mode::Manual, "#ffff00"},
        {"interaction", "Abo-/Like-Erinnerung", "Interaction reminder", Mode::Manual, "#cc00ff"},
        {"intro", "Intro", "Intro", Mode::Manual, "#00ffff"},
        {"outro", "Outro / Abspann", "Outro / endcards", Mode::Manual, "#0202ed"},
        {"preview", "Vorschau / Rückblick", "Preview / recap", Mode::Off, "#008fd6"},
        {"music_offtopic", "Musik: kein Musikteil", "Music: non-music section", Mode::Off, "#ff9900"},
        {"filler", "Füller / Abschweifung", "Filler tangent", Mode::Off, "#7300ff"},
    };
    return list;
}

const Category *category(const QString &id)
{
    for (const Category &c : categories())
        if (id == QLatin1String(c.id)) return &c;
    return nullptr;
}

QString videoIdFromUrl(const QString &url)
{
    static const QRegularExpression idRe(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    const QUrl u(url.trimmed());
    const QString host = u.host().toLower();
    QString id;
    if (host == QLatin1String("youtu.be") || host.endsWith(QLatin1String(".youtu.be"))) {
        id = u.path().section(QLatin1Char('/'), 1, 1);
    } else if (host == QLatin1String("youtube.com") || host.endsWith(QLatin1String(".youtube.com"))
               || host == QLatin1String("youtube-nocookie.com") || host.endsWith(QLatin1String(".youtube-nocookie.com"))) {
        const QStringList parts = u.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (!parts.isEmpty() && parts.first() == QLatin1String("watch"))
            id = QUrlQuery(u).queryItemValue(QStringLiteral("v"));
        else if (parts.size() >= 2 && QStringList{"shorts", "live", "embed", "v", "e"}.contains(parts.first()))
            id = parts.at(1);
    }
    return idRe.match(id).hasMatch() ? id : QString();
}

QString hashPrefix(const QString &videoId, int length)
{
    return QString::fromLatin1(QCryptographicHash::hash(videoId.toUtf8(), QCryptographicHash::Sha256).toHex().left(length));
}

QString skipSegmentsUrl(const QString &server, const QString &prefix)
{
    QStringList cats;
    for (const Category &c : categories()) cats << QStringLiteral("\"%1\"").arg(QLatin1String(c.id));
    QUrl u(server.trimmed().chopped(server.trimmed().endsWith(QLatin1Char('/')) ? 1 : 0) + QStringLiteral("/api/skipSegments/") + prefix);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("categories"), QLatin1Char('[') + cats.join(QLatin1Char(',')) + QLatin1Char(']'));
    q.addQueryItem(QStringLiteral("actionTypes"), QStringLiteral("[\"skip\",\"mute\"]"));
    u.setQuery(q);
    return u.toString(QUrl::FullyEncoded);
}

QString viewedUrl(const QString &server, const QString &uuid)
{
    QUrl u(server.trimmed().chopped(server.trimmed().endsWith(QLatin1Char('/')) ? 1 : 0) + QStringLiteral("/api/viewedVideoSponsorTime"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("UUID"), uuid);
    u.setQuery(q);
    return u.toString(QUrl::FullyEncoded);
}

QList<Segment> segmentsFromJson(const QByteArray &body, const QString &videoId)
{
    QList<Segment> out;
    const QJsonArray videos = QJsonDocument::fromJson(body).array();
    for (const QJsonValue &v : videos) {
        const QJsonObject o = v.toObject();
        if (o.value("videoID").toString() != videoId) continue;
        for (const QJsonValue &sv : o.value("segments").toArray()) {
            const QJsonObject so = sv.toObject();
            const QJsonArray range = so.value("segment").toArray();
            Segment s;
            s.start = range.at(0).toDouble();
            s.end = range.at(1).toDouble();
            s.category = so.value("category").toString();
            s.actionType = so.value("actionType").toString(QStringLiteral("skip"));
            s.uuid = so.value("UUID").toString();
            if (range.size() < 2 || s.end - s.start < 0.1 || !category(s.category)) continue;
            if (s.actionType != QLatin1String("skip") && s.actionType != QLatin1String("mute")) continue;
            out << s;
        }
    }
    std::sort(out.begin(), out.end(), [](const Segment &a, const Segment &b) { return a.start < b.start; });
    return out;
}

QString formatTime(double seconds)
{
    const int t = qMax(0, int(seconds + 0.5));
    const int h = t / 3600, m = (t / 60) % 60, s = t % 60;
    return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
                 : QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

// ---------------------------------------------------------------------------------------------------------------

void SkipEngine::setSegments(const QList<Segment> &segments)
{
    m_segments = segments;
    m_suppressed.clear();
    m_prompted.clear();
}

void SkipEngine::clear() { setSegments({}); }

Mode SkipEngine::mode(const QString &cat) const
{
    if (m_modes.contains(cat)) return m_modes.value(cat);
    const Category *c = category(cat);
    return c ? c->defaultMode : Mode::Off;
}

bool SkipEngine::active(const Segment &s, double position) const
{
    // Segmente ab Sekunde 0 auch dann, wenn die Wiedergabe minimal danach beginnt
    return position >= s.start - 0.05 && position < s.end - kMinRemaining && mode(s.category) != Mode::Off;
}

template <typename Pred>
SkipEngine::Jump SkipEngine::chain(double position, Pred pred) const
{
    Jump j;
    for (const Segment &s : m_segments) {
        if (!active(s, position) || !pred(s)) continue;
        if (!j.valid) j.category = s.category;
        j.valid = true;
        j.to = qMax(j.to, s.end);
        j.uuids << s.uuid;
    }
    if (!j.valid) return j;
    // Überlappende oder direkt anschließende Segmente in einem Sprung mitnehmen
    for (bool grew = true; grew;) {
        grew = false;
        for (const Segment &s : m_segments) {
            if (j.uuids.contains(s.uuid) || !pred(s) || mode(s.category) == Mode::Off) continue;
            if (s.start <= j.to + 0.2 && s.end > j.to) { j.to = s.end; j.uuids << s.uuid; grew = true; }
        }
    }
    return j;
}

SkipEngine::Tick SkipEngine::tick(double position)
{
    Tick t;
    t.jump = chain(position, [this](const Segment &s) {
        return s.actionType == QLatin1String("skip") && mode(s.category) == Mode::Auto && !m_suppressed.contains(s.uuid);
    });
    for (const Segment &s : m_segments) {
        const bool in = active(s, position);
        if (!in) { m_prompted.remove(s.uuid); continue; }
        const Mode m = mode(s.category);
        if (s.actionType == QLatin1String("mute") && m == Mode::Auto && !m_suppressed.contains(s.uuid)) t.mute = true;
        const bool wantsKey = m == Mode::Manual || m_suppressed.contains(s.uuid);
        if (wantsKey && !t.jump.valid && !m_prompted.contains(s.uuid)) {
            m_prompted.insert(s.uuid);
            if (t.prompt.isEmpty()) t.prompt = s.category;
        }
    }
    return t;
}

SkipEngine::Jump SkipEngine::manualJump(double position) const
{
    return chain(position, [](const Segment &) { return true; });
}

void SkipEngine::suppress(const QStringList &uuids)
{
    for (const QString &u : uuids) { m_suppressed.insert(u); m_prompted.insert(u); }
}

} // namespace uve::sponsor
