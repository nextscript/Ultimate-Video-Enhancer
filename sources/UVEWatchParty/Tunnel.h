#pragma once
// Media tunnel over WebRTC: the source member (usually the host) fetches what its own player plays – the address
// a plugin such as UVEBeeg resolved, with the same User-Agent, Referer and extra headers – and passes the bytes to
// the viewers. Viewers need no site plugin: their libmpv reads a loopback address and every request (byte ranges for
// seeking, HLS/DASH playlists and segments) travels over the direct connection to the source.
//
// Paths: "/o/<base64url(origin)>/<path>?<query>" – relative references in manifests keep resolving inside the tunnel;
// absolute and root-relative ones are rewritten by the source. The source only fetches origins of the shared medium
// (entry + origins found in its manifests), never arbitrary addresses a viewer asks for.
//
// Wire messages on the channel "uvewp-tunnel" (binary, big endian):
//   1 REQ    u32 id, JSON {p: path, m: GET|HEAD, r: Range}      (viewer → source)
//   2 HEAD   u32 id, JSON {s: status, h: {name: value}, x: rewrite flag}
//   3 DATA   u32 id, bytes            4 END u32 id            5 FAIL u32 id, JSON {e}
//   6 CANCEL u32 id                   7 CREDIT u32 id, u32 bytes (viewer allows more data – backpressure)
#include "Swarm.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTcpSocket;

namespace rtc { class PeerConnection; class DataChannel; }

namespace uve::watchparty {

class TunnelRelay;

class Tunnel : public QObject
{
    Q_OBJECT
public:
    struct Fetch { QByteArray userAgent, referrer; QList<QPair<QByteArray, QByteArray>> headers; };

    // Source: entryUrl = the address the own player plays
    static Tunnel *createSource(int myId, const QString &shareId, const QUrl &entryUrl, const Fetch &fetch, const SwarmConfig &cfg, QObject *parent);
    // Viewer: connects to the source member and serves libmpv on 127.0.0.1
    static Tunnel *createViewer(int myId, const QString &shareId, int sourceId, const QString &entryPath, const SwarmConfig &cfg, QObject *parent);
    ~Tunnel() override;

    static QString entryPathFor(const QUrl &url);
    // Rewrites an HLS playlist so that every reference points into the tunnel; adds the origins it meets
    static QByteArray rewritePlaylist(const QByteArray &body, const QUrl &base, QSet<QString> *origins);
    static QByteArray rewriteDash(const QByteArray &body, QSet<QString> *origins);

    bool isSource() const { return m_source; }
    QString shareId() const { return m_shareId; }
    QString url() const { return m_url; }          // viewer: address for libmpv
    QString urlFor(const QString &path) const { return m_base + path; }
    void allowUrl(const QUrl &url);                 // source: a further address of the medium (e.g. separate audio)
    void setParticipants(const QList<int> &memberIds);
    void onSignal(int from, const QString &kind, const QString &data);
    void removePeer(int memberId);
    QVariantList peerInfo() const;
    QString stall() const { return m_stall; }
    qint64 uploaded() const { return m_up; }
    qint64 downloaded() const { return m_down; }
    double uploadRate() const { return m_upRate; }
    double downloadRate() const { return m_downRate; }

signals:
    void signalOut(int to, const QString &kind, const QString &data);
    void statusChanged();

private:
    friend class TunnelRelay;
    Tunnel(int myId, const QString &shareId, bool source, const SwarmConfig &cfg, QObject *parent);

    struct Peer {
        int id = 0;
        quint64 epoch = 0;
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> dc;
        bool initiator = false, open = false, remoteSet = false, relay = false;
        QString state = QStringLiteral("connecting");
        qint64 createdAt = 0;
    };
    // source side: one origin request per viewer request
    struct Upstream { int peer = 0; quint32 id = 0; QPointer<QNetworkReply> reply; qint64 credit = 0; bool rewrite = false; QByteArray body; QUrl url; };
    // viewer side: one local libmpv connection per request
    struct Local { QTcpSocket *sock = nullptr; QByteArray head; bool sent = false; quint32 id = 0; bool rewrite = false; QByteArray body;
                   qint64 granted = 0; qint64 written = 0; qint64 since = 0; bool headSeen = false; };

    Peer *peer(int id, quint64 epoch);
    Peer *createPeer(int id, bool initiator);
    void attach(Peer &p, std::shared_ptr<rtc::DataChannel> dc);
    void handleState(int id, quint64 epoch, int state);
    void handleDescription(int id, quint64 epoch, const QString &type, const QString &sdp);
    void handleChannel(int id, quint64 epoch, std::shared_ptr<rtc::DataChannel> dc);
    void handleOpen(int id, quint64 epoch);
    void handleClosed(int id, quint64 epoch);
    void handleMessage(int id, quint64 epoch, const QByteArray &m);
    void handleLow(int id, quint64 epoch);
    void send(int peerId, const QByteArray &m);
    size_t buffered(int peerId) const;

    // source
    void onRequest(int peerId, quint32 id, const QJsonObject &req);
    void pumpUpstream(quint64 key);
    void finishUpstream(quint64 key, bool ok, const QString &error = {});
    QUrl resolvePath(const QString &path) const;
    // viewer
    bool startServer(const QString &entryPath);
    void onLocalConnection();
    void onLocalData(QTcpSocket *s);
    void localDone(QTcpSocket *s);
    void grantCredit(Local &l);
    void tick();

    std::shared_ptr<TunnelRelay> m_relay;
    int m_myId;
    QString m_shareId;
    bool m_source;
    SwarmConfig m_cfg;
    QMap<int, Peer> m_peers;
    quint64 m_nextEpoch = 1;
    QElapsedTimer m_clock;
    QTimer m_tick;
    qint64 m_up = 0, m_down = 0, m_upMark = 0, m_downMark = 0, m_rateAt = 0;
    double m_upRate = 0, m_downRate = 0;
    QString m_stall;

    // source
    QNetworkAccessManager *m_nam = nullptr;
    Fetch m_fetch;
    QSet<QString> m_origins;               // allowed origins (scheme://host[:port])
    QHash<quint64, Upstream> m_upstreams;  // (peer << 32 | id)
    // viewer
    int m_sourceId = 0;
    QTcpServer *m_server = nullptr;
    QString m_key;
    QString m_url;
    QString m_base;
    QHash<QTcpSocket *, Local> m_local;
    QHash<quint32, QTcpSocket *> m_byId;
    quint32 m_nextId = 1;
};

} // namespace uve::watchparty
