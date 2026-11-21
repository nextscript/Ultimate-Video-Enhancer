#pragma once
// UVE Watch Party: rooms, synchronised playback and the private WebRTC stream. Object for the QML pages.
//   • Signaling + room synchronisation: end-to-end encrypted messages over several public Nostr relays (no server,
//     no account). The plugin of the room host is the authority (RoomHost); another member takes over if it is gone.
//   • Video bytes: peer-to-peer over WebRTC DataChannels (Swarm), libmpv reads them from a loopback HTTP address.
#include "Manifest.h"
#include "RoomHost.h"
#include "Swarm.h"
#include "SyncLogic.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <optional>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace uve { class IPlayerHost; }

namespace uve::watchparty {

class NostrPool;
class Swarm;
class StreamServer;

class WatchPartyController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY changed)
    Q_PROPERTY(QString connState READ connState NOTIFY changed)          // offline, connecting, online, reconnecting
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool statusError READ statusError NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int relays READ relays NOTIFY changed)                  // connected Nostr relays
    Q_PROPERTY(bool needPassword READ needPassword NOTIFY changed)
    Q_PROPERTY(bool inRoom READ inRoom NOTIFY changed)
    Q_PROPERTY(QString roomId READ roomId NOTIFY changed)
    Q_PROPERTY(QString roomName READ roomName NOTIFY changed)
    Q_PROPERTY(bool roomPrivate READ roomPrivate NOTIFY changed)
    Q_PROPERTY(bool isHost READ isHost NOTIFY changed)
    Q_PROPERTY(int myId READ myId NOTIFY changed)
    Q_PROPERTY(QString hostNick READ hostNick NOTIFY changed)
    Q_PROPERTY(QVariantList members READ members NOTIFY changed)
    Q_PROPERTY(int mediaGen READ mediaGen NOTIFY changed)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY changed)
    Q_PROPERTY(double mediaDuration READ mediaDuration NOTIFY changed)
    Q_PROPERTY(QString localMode READ localMode NOTIFY changed)          // "", own, stream
    Q_PROPERTY(bool localReady READ localReady NOTIFY changed)
    Q_PROPERTY(bool localFileLoaded READ localFileLoaded NOTIFY changed)
    Q_PROPERTY(bool outOfSync READ outOfSync NOTIFY changed)
    Q_PROPERTY(QString mediaWarning READ mediaWarning NOTIFY changed)
    Q_PROPERTY(QVariantMap share READ share NOTIFY changed)
    Q_PROPERTY(bool preparing READ preparing NOTIFY changed)
    Q_PROPERTY(int prepareProgress READ prepareProgress NOTIFY changed)
    Q_PROPERTY(QVariantMap stream READ stream NOTIFY streamChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList chat READ chat NOTIFY chatChanged)
    Q_PROPERTY(QString mediaUrl READ mediaUrl NOTIFY changed)          // web link of the room medium (or empty)
    Q_PROPERTY(bool mediaLive READ live NOTIFY changed)
    Q_PROPERTY(bool streamAll READ streamAll NOTIFY settingsChanged)   // host: relay every medium itself (YouTube & co. too)
public:
    static constexpr const char *kPluginId = "org.uveplayer.uvewatchparty";

    WatchPartyController(uve::IPlayerHost *host, QObject *parent = nullptr);
    ~WatchPartyController() override;

    QString language() const { return m_language; }
    void setLanguage(const QString &code);
    QString connState() const { return m_connState; }
    QString status() const { return m_status; }
    bool statusError() const { return m_statusError; }
    bool busy() const { return m_busy; }
    int relays() const;
    bool needPassword() const { return m_needPassword; }
    bool inRoom() const { return m_inRoom; }
    QString roomId() const { return m_roomId; }
    QString roomName() const { return m_roomName; }
    bool roomPrivate() const { return m_roomPrivate; }
    bool isHost() const { return m_inRoom && m_hostId == m_myId; }
    int myId() const { return m_myId; }
    QString hostNick() const;
    QVariantList members() const { return m_members; }
    int mediaGen() const { return int(m_mediaGen); }
    QString mediaTitle() const { return m_mediaTitle; }
    double mediaDuration() const { return m_mediaDuration; }
    QString localMode() const { return m_mode; }
    bool localReady() const { return m_localReady; }
    bool localFileLoaded() const;
    bool outOfSync() const { return m_outOfSync; }
    QString mediaWarning() const { return m_mediaWarning; }
    QVariantMap share() const;
    bool preparing() const;
    int prepareProgress() const { return m_prepareProgress; }
    QVariantMap stream() const;
    QVariantMap settings() const { return m_settings; }
    QVariantList chat() const { return m_chat; }
    QString mediaUrl() const { return m_mediaUrl; }

    Q_INVOKABLE void createRoom(const QString &name, const QString &nick, bool isPrivate, const QString &password);
    Q_INVOKABLE void joinRoom(const QString &roomId, const QString &nick, const QString &password);
    Q_INVOKABLE void leaveRoom();
    Q_INVOKABLE void closeRoom();
    Q_INVOKABLE void copyRoomId();
    Q_INVOKABLE void copyRoomName();
    Q_INVOKABLE void markReady();
    Q_INVOKABLE void syncNow();
    Q_INVOKABLE void shareCurrentFile();
    Q_INVOKABLE void cancelPrepare();
    Q_INVOKABLE void stopShare();
    Q_INVOKABLE void confirmShare();
    Q_INVOKABLE void watchStream();
    Q_INVOKABLE void transferHost(int memberId);
    Q_INVOKABLE void saveSettings(const QVariantMap &values);
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void sendChat(const QString &text);
    Q_INVOKABLE void setStreamAll(bool on);
    bool streamAll() const { return m_settings.value("streamAll").toBool(); }
    Q_INVOKABLE QString text(const QString &de, const QString &en) const;
    Q_INVOKABLE QString formatTime(double seconds) const;

    // From the plugin
    void onEvent(const QString &name, const QVariantMap &data);
    void setActive(bool active);
    void shutdown();

signals:
    void changed();
    void streamChanged();
    void settingsChanged();
    void chatChanged();
    void toast(const QString &text);

private:
    // room channel
    void startChannel(const QString &roomId, const QList<QByteArray> &keys);
    void onChannel(const QString &pk, const QByteArray &content);
    void publish(QJsonObject msg, const QByteArray &key = {});
    void send(QJsonObject msg);                     // to the authority (local or remote)
    void dispatch(const QList<RoomHost::Out> &outs);
    void onMessage(const QString &fromPk, const QJsonObject &m);
    void becomeHost(bool failover);
    void hostWatch();
    void joinTimedOut();
    qint64 hostClock() const;
    void sendReady();
    void endSession(const QString &message, bool error);
    void setStatus(const QString &text, bool error = false);
    QString errorText(const QString &code) const;
    QString pkOf(int memberId) const;

    // room state
    void applySnapshot(const QJsonObject &s);
    void applyState(const QJsonObject &st, bool force);
    qint64 serverNow() const;

    // player
    QVariantMap playerState() const;
    void tick();
    void hostTick(const QVariantMap &st, qint64 now);
    void guestTick(const QVariantMap &st, qint64 now);
    void syncToRoom(bool force);
    void playerPause(bool paused, double pos);
    void playerSeek(double pos, bool paused);
    void setSpeed(double speed);
    bool matched(const QString &path) const;
    void updateMediaWarning();
    void addChat(const QJsonObject &m);
    void startHostShare(bool force);
    void offerTunnel(const QUrl &video, const QUrl &audio, const QByteArray &ua, const QByteArray &referrer,
                     const QList<QPair<QByteArray, QByteArray>> &headers, bool live, bool force);
    void startTunnel(bool source);
    void setAudioFiles(const QStringList &files);
    void linkFailed();
    bool live() const;
    void adoptCurrentMedia();

    // share / swarm
    void onManifestReady(const QString &path, const Manifest &m);
    void updateSwarm();
    void startSwarm(bool seed);
    void stopSwarm();
    SwarmConfig swarmConfig() const;
    QList<int> swarmParticipants() const;

    void loadSettings();

    uve::IPlayerHost *m_host;
    NostrPool *m_pool = nullptr;
    std::optional<RoomHost> m_authority;
    QTimer m_poll, m_ping, m_hostTick, m_hello, m_joinTimeout;
    QElapsedTimer m_mono;
    QString m_language = QStringLiteral("de");
    QVariantMap m_settings;
    bool m_active = false;

    // connection / session
    QString m_connState = QStringLiteral("offline");
    QString m_status;
    bool m_statusError = false;
    bool m_busy = false;
    bool m_needPassword = false;
    QString m_myPk;
    QByteArray m_key;                       // room key (AES-256-GCM)
    QList<QByteArray> m_candidateKeys;      // while joining: with and without the entered password
    bool m_joining = false;
    bool m_undecryptable = false;           // saw room traffic we cannot read (wrong / missing password)
    QString m_joinNick, m_joinPassword;
    QString m_hostPk;
    qint64 m_epoch = 0;
    qint64 m_lastHostSeen = 0;
    QJsonObject m_lastSnapshot;
    QSet<QString> m_failoverSkip;
    // clock: server time ≈ local monotonic + offset, best (lowest RTT) of recent samples
    qint64 m_offset = 0;
    qint64 m_bestRtt = -1;
    int m_pings = 0;

    // room
    bool m_inRoom = false;
    QString m_roomId, m_roomName;
    bool m_roomPrivate = false;
    int m_myId = 0;
    int m_hostId = 0;
    QVariantList m_members;
    QJsonArray m_rawMembers;
    qint64 m_mediaGen = 0;
    QString m_mediaTitle;
    double m_mediaDuration = 0;
    QString m_mediaUrl;
    bool m_mediaLive = false;
    QVariantList m_chat;
    sync::RoomState m_state;
    QJsonObject m_share;

    // local playback
    QString m_mode;                 // own | stream | ""
    QString m_ownPath;
    QString m_ownUrl;               // host: web link of the opened medium (YouTube, stream …)
    QString m_linkPath;             // guest: what the player made of the room link
    qint64 m_announceAt = 0;
    bool m_localReady = false;
    bool m_outOfSync = false;
    bool m_buffering = false;
    QString m_mediaWarning;
    bool m_announcePending = false; // host loaded a file, waiting for its duration
    qint64 m_pushedGen = -1;        // generation whose host state was already pushed
    qint64 m_awaitGen = -1;         // host announced a medium, waiting for the new generation
    qint64 m_lastHeartbeat = 0;
    bool m_resyncPending = false;
    qint64 m_resyncAt = 0;
    // Common start: a new medium of the host waits at the beginning until all guests are ready
    bool m_hold = false;
    qint64 m_holdDeadline = 0;
    int othersConnected() const;
    int othersReady() const;
    sync::PlayerObserver m_obs;
    sync::DriftController m_drift;
    bool m_speedChanged = false;

    // share / stream
    ManifestBuilder *m_builder;
    int m_prepareProgress = 0;
    QString m_preparePath;
    QString m_seedPath;
    std::optional<Manifest> m_seedManifest;
    QPointer<Swarm> m_swarm;
    QPointer<class Tunnel> m_tunnel;
    struct PendingTunnel { QUrl video, audio; QByteArray ua, referrer; QList<QPair<QByteArray, QByteArray>> headers; } m_pendingTunnel;
    bool m_audioFilesSet = false;
    bool m_sharing = false;                // host share being prepared (yt-dlp / hashing)
    QTimer m_linkWatch;
    QPointer<StreamServer> m_server;
    QString m_streamUrl;
    bool m_wantStream = false;
    QString m_shareEndedText;
};

} // namespace uve::watchparty
