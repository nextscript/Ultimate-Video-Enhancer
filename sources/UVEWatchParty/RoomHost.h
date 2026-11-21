#pragma once
// Room authority without a server: runs in the plugin of the current room host. It keeps members, the
// authoritative playback state (media generation, position, pause, reference time, increasing state number) and
// the share, checks the host rights itself and produces the messages for the room channel.
// Guests accept state only from the current host's key; a host change raises the epoch.
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <functional>

namespace uve::watchparty {

class RoomHost
{
public:
    struct Out { QString toPk; QJsonObject msg; };   // toPk empty = whole room
    using Clock = std::function<qint64()>;

    static constexpr qint64 kOfflineMs = 35000;      // no sign of life → shown as disconnected
    static constexpr qint64 kRemoveMs = 95000;       // disconnected this long → removed (resume window 60 s)
    static constexpr qint64 kBeaconMs = 15000;       // snapshot as sign of life of the host
    static constexpr int kMaxMembers = 16;
    static constexpr int kChatHistory = 40;          // last messages handed to members who join

    // New room
    RoomHost(const QString &roomId, const QString &name, bool isPrivate, const QString &hostPk, const QString &hostNick, Clock now);
    // Take over an existing room (host hand-over or failover) from the last snapshot
    static RoomHost fromSnapshot(const QJsonObject &snapshot, const QString &myPk, Clock now, bool failover);

    QList<Out> handle(const QString &fromPk, const QJsonObject &msg);
    QList<Out> tick();
    QJsonObject snapshot() const;
    QString hostPk() const;
    int hostId() const { return m_hostId; }
    qint64 epoch() const { return m_epoch; }
    bool closed() const { return m_closed; }
    bool handedOver() const { return m_handedOver; }
    double positionNow() const;

private:
    struct Member {
        int id = 0;
        QString pk, nick, mode;
        qint64 joinedAt = 0, lastSeen = 0;
        bool ready = false, buffering = false;
    };
    RoomHost() = default;
    Member *byPk(const QString &pk);
    bool connected(const Member &m) const { return m_now() - m.lastSeen < kOfflineMs; }
    void setState(bool paused, double position, const QString &reason, int by, QList<Out> &out, bool broadcast = true);
    void setMedia(const QString &title, double duration, int by, QList<Out> &out, const QString &url = {});
    void chat(const QString &text, int from, bool system, QList<Out> &out);
    void endShare(const QString &reason, QList<Out> &out);
    void removeMember(int id, QList<Out> &out);
    void handOver(int toId, QList<Out> &out);
    const Member *pickSuccessor() const;
    QJsonObject stateJson() const;
    QJsonObject shareJson() const;
    static Out all(const QJsonObject &m) { return {QString(), m}; }

    Clock m_now;
    QString m_roomId, m_name;
    bool m_private = false;
    qint64 m_epoch = 1;
    int m_hostId = 0;
    int m_nextId = 1;
    QMap<int, Member> m_members;
    qint64 m_gen = 0;
    QString m_title;
    bool m_live = false;          // live stream: only start/pause are synchronised
    QString m_url;                // web link of the medium (streams, YouTube …); never a local path
    QJsonArray m_chat;
    double m_duration = 0;
    qint64 m_seq = 1;
    bool m_paused = true;
    double m_position = 0;
    qint64 m_ref = 0;
    QString m_reason;
    int m_by = 0;
    qint64 m_lastProgress = 0;
    QJsonObject m_share;          // empty = no share
    QJsonObject m_shareEnded;
    int m_nextShare = 1;
    qint64 m_lastBeacon = 0;
    bool m_closed = false;
    bool m_handedOver = false;
};

} // namespace uve::watchparty
