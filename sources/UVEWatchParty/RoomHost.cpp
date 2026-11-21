#include "RoomHost.h"

#include "Crypto.h"

#include <QUrl>

#include <cmath>

namespace uve::watchparty {

static QJsonObject msg(const char *type) { return QJsonObject{{"type", QString::fromLatin1(type)}}; }

RoomHost::RoomHost(const QString &roomId, const QString &name, bool isPrivate, const QString &hostPk, const QString &hostNick, Clock now)
    : m_now(std::move(now)), m_roomId(roomId), m_name(name), m_private(isPrivate)
{
    const qint64 t = m_now();
    Member m;
    m.id = m_nextId++;
    m.pk = hostPk;
    m.nick = hostNick;
    m.joinedAt = m.lastSeen = t;
    m_members.insert(m.id, m);
    m_hostId = m.id;
    m_ref = t;
    m_reason = QStringLiteral("create");
}

RoomHost RoomHost::fromSnapshot(const QJsonObject &s, const QString &myPk, Clock now, bool failover)
{
    RoomHost h;
    h.m_now = std::move(now);
    const qint64 t = h.m_now();
    const QJsonObject room = s.value("room").toObject();
    h.m_roomId = room.value("id").toString();
    h.m_name = room.value("name").toString();
    h.m_private = room.value("private").toBool();
    h.m_epoch = qint64(s.value("epoch").toDouble()) + (failover ? 1 : 0);
    for (const auto &v : s.value("members").toArray()) {
        const QJsonObject o = v.toObject();
        Member m;
        m.id = o.value("id").toInt();
        m.pk = o.value("pk").toString();
        m.nick = o.value("nick").toString();
        m.mode = o.value("mode").toString();
        m.ready = o.value("ready").toBool();
        m.buffering = o.value("buffering").toBool();
        m.joinedAt = qint64(o.value("joinedAt").toDouble());
        m.lastSeen = o.value("connected").toBool() ? t : t - kOfflineMs;
        h.m_members.insert(m.id, m);
        h.m_nextId = qMax(h.m_nextId, m.id + 1);
        if (m.pk == myPk) h.m_hostId = m.id;
    }
    if (failover) {
        // The old host is gone: it shows as disconnected and is removed after the resume window
        const int old = s.value("hostId").toInt();
        if (h.m_members.contains(old) && old != h.m_hostId) h.m_members[old].lastSeen = t - kOfflineMs;
    }
    const QJsonObject media = s.value("media").toObject();
    h.m_gen = qint64(media.value("gen").toDouble());
    h.m_title = media.value("title").toString();
    h.m_duration = media.value("duration").toDouble();
    h.m_url = media.value("url").toString();
    h.m_live = media.value("live").toBool();
    h.m_chat = s.value("chat").toArray();
    const QJsonObject st = s.value("state").toObject();
    h.m_seq = qint64(st.value("seq").toDouble());
    h.m_paused = st.value("paused").toBool(true);
    h.m_position = st.value("position").toDouble();
    h.m_ref = qint64(st.value("ref").toDouble());
    h.m_share = s.value("share").toObject();
    h.m_nextShare = s.value("nextShare").toInt(1);
    // Clock bases of hosts differ: re-base the running state on our own clock
    h.m_position = h.positionNow();
    h.m_ref = t;
    if (failover) h.m_paused = true;                // host left unexpectedly → room pauses
    h.m_seq++;
    h.m_reason = failover ? QStringLiteral("host_away") : QStringLiteral("host");
    return h;
}

QString RoomHost::hostPk() const
{
    return m_members.value(m_hostId).pk;
}

RoomHost::Member *RoomHost::byPk(const QString &pk)
{
    for (auto &m : m_members) if (m.pk == pk) return &m;
    return nullptr;
}

double RoomHost::positionNow() const
{
    double p = m_position;
    if (!m_paused) p += double(qMax<qint64>(0, m_now() - m_ref)) / 1000.0;
    if (m_duration > 0) p = qMin(p, m_duration);
    return p;
}

QJsonObject RoomHost::stateJson() const
{
    return QJsonObject{{"gen", double(m_gen)}, {"seq", double(m_seq)}, {"paused", m_paused}, {"position", m_position},
                       {"ref", double(m_ref)}, {"reason", m_reason}, {"by", m_by}};
}

QJsonObject RoomHost::shareJson() const
{
    if (m_share.isEmpty()) return {};
    QJsonObject s = m_share;
    const auto src = m_members.find(s.value("sourceId").toInt());
    s["sourceNick"] = src != m_members.end() ? src->nick : QString();
    s["sourceOnline"] = src != m_members.end() && connected(*src);
    return s;
}

QJsonObject RoomHost::snapshot() const
{
    QJsonArray members;
    for (const auto &m : m_members) {
        members.append(QJsonObject{{"id", m.id}, {"pk", m.pk}, {"nick", m.nick}, {"host", m.id == m_hostId}, {"connected", connected(m)},
                                   {"ready", m.ready}, {"buffering", m.buffering}, {"mode", m.mode}, {"joinedAt", double(m.joinedAt)}});
    }
    QJsonObject s{{"type", "snapshot"}, {"room", QJsonObject{{"id", m_roomId}, {"name", m_name}, {"private", m_private}}},
                  {"epoch", double(m_epoch)}, {"hostId", m_hostId}, {"hostPk", hostPk()}, {"members", members},
                  {"media", QJsonObject{{"gen", double(m_gen)}, {"title", m_title}, {"duration", m_duration}, {"url", m_url}, {"live", m_live}}},
                  {"state", stateJson()}, {"now", double(m_now())}, {"nextShare", m_nextShare}};
    s["share"] = m_share.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(shareJson());
    if (!m_shareEnded.isEmpty()) s["shareEnded"] = m_shareEnded;
    return s;
}

void RoomHost::setState(bool paused, double position, const QString &reason, int by, QList<Out> &out, bool broadcast)
{
    m_paused = paused;
    m_position = m_duration > 0 ? qMin(position, m_duration) : position;
    m_ref = m_now();
    m_seq++;
    m_reason = reason;
    m_by = by;
    if (broadcast) {
        QJsonObject m = msg("state");
        m["state"] = stateJson();
        m["now"] = double(m_now());
        out << all(m);
    }
}

void RoomHost::endShare(const QString &reason, QList<Out> &out)
{
    if (m_share.isEmpty()) return;
    const QString id = m_share.value("id").toString();
    m_share = {};
    m_shareEnded = QJsonObject{{"reason", reason}, {"shareId", id}, {"at", double(m_now())}};
    out << all(QJsonObject{{"type", "share-ended"}, {"shareId", id}, {"reason", reason}});
}

// Only real web links are passed on: no local paths, no loopback addresses (those only work on one device)
static QString shareableUrl(const QString &u)
{
    const QUrl url(u);
    static const QStringList schemes{"http", "https", "rtmp", "rtmps", "rtsp", "srt", "udp", "tcp", "ytdl"};
    if (!url.isValid() || u.size() > 2000 || !schemes.contains(url.scheme().toLower()) || url.host().isEmpty()) return {};
    const QString h = url.host().toLower();
    if (h == QLatin1String("localhost") || h.startsWith(QLatin1String("127.")) || h == QLatin1String("::1")) return {};
    return u;
}

void RoomHost::chat(const QString &text, int from, bool system, QList<Out> &out)
{
    const QString t = text.trimmed().left(500);
    if (t.isEmpty()) return;
    const QJsonObject m{{"from", from}, {"nick", m_members.value(from).nick}, {"text", t}, {"system", system}, {"at", double(m_now())}};
    m_chat.append(m);
    while (m_chat.size() > kChatHistory) m_chat.removeFirst();
    QJsonObject msg = m;
    msg["type"] = "chat";
    out << all(msg);
}

void RoomHost::setMedia(const QString &title, double duration, int by, QList<Out> &out, const QString &url)
{
    endShare(QStringLiteral("media_changed"), out);
    m_gen++;
    m_url = shareableUrl(url);
    m_title = title.left(200);
    m_duration = duration;
    for (auto &m : m_members) { m.ready = false; m.buffering = false; m.mode.clear(); }
    setState(true, 0, QStringLiteral("media"), by, out, false);
}

const RoomHost::Member *RoomHost::pickSuccessor() const
{
    // Longest connected ready member; if nobody is ready the longest connected one (the room stays paused)
    const Member *best = nullptr;
    for (const bool needReady : {true, false}) {
        for (const auto &m : m_members) {
            if (m.id == m_hostId || !connected(m) || (needReady && !m.ready)) continue;
            if (!best || m.joinedAt < best->joinedAt) best = &m;
        }
        if (best) return best;
    }
    return nullptr;
}

void RoomHost::handOver(int toId, QList<Out> &out)
{
    // The snapshot with the new host and a higher epoch makes the target the authority
    m_hostId = toId;
    m_epoch++;
    m_handedOver = true;
    QJsonObject snap = snapshot();
    snap["chat"] = m_chat;
    out << all(snap);
}

void RoomHost::removeMember(int id, QList<Out> &out)
{
    chat(QStringLiteral("left"), id, true, out);
    const Member m = m_members.take(id);
    if (!m_share.isEmpty() && m_share.value("sourceId").toInt() == id) m_share["sourceOnline"] = false;   // swarm keeps its blocks
    out << all(QJsonObject{{"type", "peer-left"}, {"memberId", id}});
}

QList<RoomHost::Out> RoomHost::handle(const QString &fromPk, const QJsonObject &in)
{
    QList<Out> out;
    if (m_closed || m_handedOver) return out;
    const QString type = in.value("type").toString();
    const qint64 now = m_now();
    Member *m = byPk(fromPk);
    if (m) m->lastSeen = now;

    if (type == QLatin1String("hello")) {
        if (!m) {
            if (m_members.size() >= kMaxMembers) { out << Out{fromPk, QJsonObject{{"type", "error"}, {"code", "room_full"}}}; return out; }
            Member nm;
            nm.id = m_nextId++;
            nm.pk = fromPk;
            nm.nick = in.value("nick").toString().left(32);
            if (nm.nick.trimmed().isEmpty()) nm.nick = QStringLiteral("?");
            nm.joinedAt = nm.lastSeen = now;
            m_members.insert(nm.id, nm);
            m = &m_members[nm.id];
        }
        // Full snapshot on join and reconnect
        const bool isNew = m->joinedAt == now;
        QJsonObject snap = snapshot();
        snap["chat"] = m_chat;
        out << Out{fromPk, QJsonObject{{"type", "welcome"}, {"memberId", m->id}, {"snapshot", snap}}};
        out << all(snapshot());
        if (isNew) chat(QStringLiteral("joined"), m->id, true, out);
        return out;
    }
    if (!m) { if (type != QLatin1String("leave")) out << Out{fromPk, QJsonObject{{"type", "rejoin"}}}; return out; }
    const bool isHost = m->id == m_hostId;

    if (type == QLatin1String("ping")) {
        out << Out{fromPk, QJsonObject{{"type", "pong"}, {"t", in.value("t")}, {"server", double(now)}}};
    } else if (type == QLatin1String("leave")) {
        if (isHost) {
            if (const Member *next = pickSuccessor()) { const int id = next->id; m_members.remove(m->id); handOver(id, out); }
            else m_closed = true;
            return out;
        }
        removeMember(m->id, out);
        out << all(snapshot());
    } else if (type == QLatin1String("close")) {
        if (!isHost) return out;
        m_closed = true;
        out << all(msg("closed"));
    } else if (type == QLatin1String("ready")) {
        if (qint64(in.value("gen").toDouble()) != m_gen) return out;      // event of an earlier medium
        m->ready = in.value("ready").toBool();
        m->buffering = in.value("buffering").toBool();
        m->mode = in.value("mode").toString() == QLatin1String("stream") ? QStringLiteral("stream")
                : in.value("mode").toString() == QLatin1String("own") ? QStringLiteral("own")
                : in.value("mode").toString() == QLatin1String("nolink") ? QStringLiteral("nolink")
                : in.value("mode").toString() == QLatin1String("link") ? QStringLiteral("link") : QString();
        out << all(snapshot());
    } else if (type == QLatin1String("media")) {
        if (!isHost) return out;
        setMedia(in.value("title").toString(), qMax(0.0, in.value("duration").toDouble()), m->id, out, in.value("url").toString());
        m_live = in.value("live").toBool() && !m_url.isEmpty();
        out << all(snapshot());
    } else if (type == QLatin1String("chat")) {
        chat(in.value("text").toString(), m->id, false, out);
    } else if (type == QLatin1String("action")) {
        // Rights are checked here: only the host changes the shared state
        if (!isHost || qint64(in.value("gen").toDouble()) != m_gen) return out;
        const QString op = in.value("op").toString();
        const double pos = qBound(0.0, in.value("position").toDouble(), 1e6);
        const bool paused = op == QLatin1String("play") ? false : op == QLatin1String("pause") ? true : in.value("paused").toBool(m_paused);
        if (op != QLatin1String("play") && op != QLatin1String("pause") && op != QLatin1String("seek")) return out;
        setState(paused, pos, op, m->id, out);
        if (!paused) m_lastProgress = now;
    } else if (type == QLatin1String("position")) {
        if (!isHost || qint64(in.value("gen").toDouble()) != m_gen || m_paused) return out;
        m_lastProgress = now;
        const double pos = in.value("position").toDouble();
        if (std::abs(positionNow() - pos) > 1.5) setState(false, pos, QStringLiteral("rebase"), m->id, out);
    } else if (type == QLatin1String("share")) {
        const QString op = in.value("op").toString();
        if (op == QLatin1String("offer")) {
            if (!m_share.isEmpty() && m_share.value("sourceId").toInt() != m->id) {
                out << Out{fromPk, QJsonObject{{"type", "error"}, {"code", "share_busy"}}};
                return out;
            }
            const bool tunnel = in.value("kind").toString() == QLatin1String("tunnel");
            const QString manifest = in.value("manifest").toString();
            const double size = in.value("size").toDouble(), bs = in.value("blockSize").toDouble();
            const QString entry = in.value("entry").toString(), audio = in.value("audio").toString();
            // File: private block swarm with manifest. Tunnel: the source relays what its player plays (links)
            if (tunnel ? (!entry.startsWith(QLatin1String("/o/")) || entry.size() > 8000 || (!audio.isEmpty() && !audio.startsWith(QLatin1String("/o/"))))
                       : (manifest.size() != 64 || size < 1 || bs < 16384)) return out;
            if (!m_share.isEmpty()) endShare(QStringLiteral("replaced"), out);
            QJsonObject ns{{"id", QStringLiteral("%1-%2").arg(m_nextShare++).arg(QString::fromLatin1(crypto::randomBytes(6).toHex()))},
                           {"sourceId", m->id}, {"status", isHost ? "confirmed" : "pending"}, {"kind", tunnel ? "tunnel" : "file"},
                           {"title", in.value("title").toString().left(200)}, {"duration", in.value("duration").toDouble()}};
            if (tunnel) { ns["entry"] = entry; ns["audio"] = audio.left(8000); ns["force"] = in.value("force").toBool(); ns["live"] = in.value("live").toBool(); }
            else { ns["size"] = size; ns["blockSize"] = bs; ns["manifest"] = manifest; }
            // Room without a medium: the host's share defines it
            if (isHost && m_gen == 0) setMedia(ns.value("title").toString(), ns.value("duration").toDouble(), m->id, out);
            m_share = ns;
            m_share["gen"] = double(m_gen);
            m_shareEnded = {};
            out << all(snapshot());
        } else {
            if (m_share.isEmpty() || m_share.value("id").toString() != in.value("shareId").toString()) return out;
            if (op == QLatin1String("confirm")) {
                if (!isHost || m_share.value("status").toString() != QLatin1String("pending")) return out;
                const QJsonObject keep = m_share;
                if (m_gen == 0) { m_share = {}; setMedia(keep.value("title").toString(), keep.value("duration").toDouble(), m->id, out); m_share = keep; }
                m_share["status"] = "confirmed";
                m_share["gen"] = double(m_gen);
                out << all(snapshot());
            } else if (op == QLatin1String("stop")) {
                if (!isHost && m_share.value("sourceId").toInt() != m->id) return out;
                endShare(QStringLiteral("stopped"), out);
                out << all(snapshot());
            }
        }
    } else if (type == QLatin1String("host")) {
        const int to = in.value("memberId").toInt();
        if (!isHost || !m_members.contains(to) || to == m->id || !connected(m_members[to])) return out;
        handOver(to, out);
    }
    return out;
}

QList<RoomHost::Out> RoomHost::tick()
{
    QList<Out> out;
    if (m_closed || m_handedOver) return out;
    const qint64 now = m_now();
    m_members[m_hostId].lastSeen = now;
    bool changed = false;
    for (const int id : m_members.keys()) {
        if (id != m_hostId && now - m_members[id].lastSeen > kRemoveMs) { removeMember(id, out); changed = true; }
    }
    // A sign of life of the room (also how joiners find it); status changes (connected → disconnected) go out too
    if (changed || now - m_lastBeacon >= kBeaconMs) {
        m_lastBeacon = now;
        out << all(snapshot());
    }
    return out;
}

} // namespace uve::watchparty
