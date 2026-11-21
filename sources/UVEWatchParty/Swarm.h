#pragma once
// Private block swarm on WebRTC DataChannels (libdatachannel), BitTorrent-like but closed to the room:
// peers are only the room members the signaling service addresses for the confirmed share – no trackers, no DHT,
// no magnet links, no peer discovery outside the group.
//
// Wire messages on the reliable, ordered channel "uvewp" (binary, big endian, ≤ 16 KiB payload each):
//   1 HELLO   u8 version, 32-byte manifest identity, u8 flags (1 = seed, 2 = has manifest)
//   2 MREQ    –                          request the manifest
//   3 MPART   u32 offset, u32 total, data
//   4 BITS    u32 first block, u32 count, bitmap      (which verified blocks a peer has)
//   5 HAVE    u32 block                  6 WANT u32 block       9 CANCEL u32 block     10 DROP u32 block
//   7 PIECE   u32 block, u32 offset, data (block fragment)
//   8 REJECT  u32 block, u8 reason (1 upload off, 2 not available, 3 busy)
#include "BlockCache.h"
#include "Manifest.h"

#include <QBitArray>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <memory>
#include <optional>

namespace rtc { class PeerConnection; class DataChannel; }

namespace uve::watchparty {

struct IceServerSpec {
    QString url;          // stun:host:port, turn:host:port?transport=udp, turns:host:port
    QString username;
    QString credential;
};

struct SwarmConfig {
    QList<IceServerSpec> iceServers;
    bool uploadEnabled = true;
    qint64 uploadLimit = 0;          // bytes per second, 0 = unlimited
    qint64 cacheBytes = 2048LL << 20;
    qint64 cacheMaxAgeMs = 2 * 3600 * 1000;
    QString cacheDir;
};

class SwarmRelay;   // thread bridge for libdatachannel callbacks

class Swarm : public QObject
{
    Q_OBJECT
public:
    // digest: confirmed manifest identity (32 bytes); size/blockSize from the share
    Swarm(int myId, const QString &shareId, const QByteArray &digest, qint64 size, quint32 blockSize,
          const SwarmConfig &cfg, QObject *parent = nullptr);
    ~Swarm() override;

    // Seed: the original file (manifest already built locally, its digest must match)
    bool setSeed(const QString &path, const Manifest &manifest);
    bool isSeed() const { return m_seed; }

    // Room members taking part (source + stream viewers, without myself); creates/removes peer connections
    void setParticipants(const QList<int> &memberIds);
    void onSignal(int from, const QString &kind, const QString &data, const QString &mid);
    void removePeer(int memberId);

    // Reading for the local HTTP interface
    int openReader(qint64 offset);
    void moveReader(int handle, qint64 offset);
    void closeReader(int handle);
    QByteArray read(qint64 offset, qint64 maxLen);   // empty while the block is missing
    void hintPosition(double seconds, double duration);

    void setUploadEnabled(bool on) { m_cfg.uploadEnabled = on; }
    void setUploadLimit(qint64 bytesPerSecond) { m_cfg.uploadLimit = bytesPerSecond; }

    QString shareId() const { return m_shareId; }
    qint64 size() const { return m_size; }
    bool hasManifest() const { return m_manifest.has_value(); }
    int blockCount() const { return m_count; }
    int haveCount() const { return m_haveCount; }
    qint64 uploaded() const { return m_up; }
    qint64 downloaded() const { return m_down; }
    double uploadRate() const { return m_upRate; }
    double downloadRate() const { return m_downRate; }
    bool bufferingAhead(qint64 offset, int blocks) const;
    QVariantList peerInfo() const;
    // Why the stream cannot continue: "" (fine), "unreachable" (no usable connection), "missing" (nobody has the block)
    QString stall() const { return m_stall; }

signals:
    void signalOut(int to, const QString &kind, const QString &data, const QString &mid);
    void blockAvailable(int index);
    void manifestAvailable();
    void statusChanged();

private:
    friend class SwarmRelay;
    struct OutBlock { int index = 0; QByteArray data; int offset = 0; };
    struct Peer {
        int id = 0;
        quint64 epoch = 0;
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> dc;
        bool initiator = false;
        bool open = false;
        bool hello = false;
        bool seed = false;
        bool hasManifest = false;
        bool relay = false;
        QString state = QStringLiteral("connecting");
        QBitArray have;
        int inflight = 0;
        QList<OutBlock> queue;
        int strikes = 0;
        qint64 up = 0, down = 0;
        qint64 createdAt = 0;
        QList<QPair<QString, QString>> pendingCandidates;
        bool remoteSet = false;
    };
    struct Pending { int peer = 0; qint64 deadline = 0; QByteArray buf; qint64 got = 0; bool urgent = false; };

    // called on the main thread (via SwarmRelay)
    void handleState(int id, quint64 epoch, int state);
    void handleLocalDescription(int id, quint64 epoch, const QString &type, const QString &sdp);
    void handleLocalCandidate(int id, quint64 epoch, const QString &cand, const QString &mid);
    void handleChannel(int id, quint64 epoch, std::shared_ptr<rtc::DataChannel> dc);
    void handleOpen(int id, quint64 epoch);
    void handleClosed(int id, quint64 epoch);
    void handleMessage(int id, quint64 epoch, const QByteArray &msg);
    void handleBufferedLow(int id, quint64 epoch);

    Peer *peer(int id, quint64 epoch);
    Peer *createPeer(int id, bool initiator);
    void attachChannel(Peer &p, std::shared_ptr<rtc::DataChannel> dc);
    void send(Peer &p, const QByteArray &msg);
    void sendHello(Peer &p);
    void sendBits(Peer &p);
    void broadcast(const QByteArray &msg);
    void onWant(Peer &p, int index);
    void onPiece(Peer &p, int index, int offset, const QByteArray &data);
    void onManifestPart(Peer &p, quint32 offset, quint32 total, const QByteArray &data);
    void pump(Peer &p);
    void schedule();
    void tick();
    QByteArray blockData(int index);
    void completeBlock(int index, const QByteArray &data);
    void dropBlocks(const QList<int> &indices);
    void updateStall();
    bool haveBlock(int index) const;
    void applyManifest(const Manifest &m);

    std::shared_ptr<SwarmRelay> m_relay;
    int m_myId;
    QString m_shareId;
    QByteArray m_digest;
    qint64 m_size;
    quint32 m_blockSize;
    int m_count;
    SwarmConfig m_cfg;
    bool m_seed = false;
    QFile m_seedFile;
    std::optional<Manifest> m_manifest;
    QByteArray m_manifestBuf;
    int m_manifestFrom = 0;
    qint64 m_manifestAskedAt = 0;
    BlockCache m_cache;
    QBitArray m_have;
    int m_haveCount = 0;
    QMap<int, Peer> m_peers;
    quint64 m_nextEpoch = 1;
    QHash<int, Pending> m_pending;
    QHash<quint64, qint64> m_banned;     // (block << 20 | peer) → until
    QHash<int, qint64> m_readers;
    int m_nextReader = 1;
    qint64 m_hintBlock = -1;
    QTimer m_tick;
    QElapsedTimer m_clock;
    double m_tokens = 0;
    qint64 m_lastRefill = 0;
    qint64 m_up = 0, m_down = 0, m_upMark = 0, m_downMark = 0, m_rateAt = 0;
    double m_upRate = 0, m_downRate = 0;
    QString m_stall;
    qint64 m_stallSince = 0;
};

QList<IceServerSpec> parseIceServers(const QVariantList &servers);

} // namespace uve::watchparty
