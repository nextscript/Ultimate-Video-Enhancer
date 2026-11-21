#include "WatchPartyController.h"

#include "PluginApi.h"
#include "StreamServer.h"
#include "Tunnel.h"
#include "Swarm.h"
#include "Crypto.h"
#include "Nostr.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QProcess>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>

namespace uve::watchparty {

static constexpr int kPollMs = 250;

static bool isLocalFile(const QString &path)
{
    if (path.isEmpty()) return false;
    if (path.startsWith(QLatin1String("file:"))) return QFileInfo(QUrl(path).toLocalFile()).isFile();
    if (path.contains(QLatin1String("://"))) return false;
    return QFileInfo(path).isFile();
}

static QString localPath(const QString &path)
{
    return path.startsWith(QLatin1String("file:")) ? QUrl(path).toLocalFile() : path;
}

WatchPartyController::WatchPartyController(uve::IPlayerHost *host, QObject *parent)
    : QObject(parent), m_host(host), m_builder(new ManifestBuilder(this))
{
    m_mono.start();
    loadSettings();
    m_poll.setInterval(kPollMs);
    connect(&m_poll, &QTimer::timeout, this, &WatchPartyController::tick);
    connect(&m_ping, &QTimer::timeout, this, [this] {
        QJsonObject p{{"type", "ping"}, {"t", double(m_mono.elapsed())}};
        send(p);
        // A few quick samples for the clock offset, then every 10 s (also the guest's sign of life)
        if (++m_pings == 5) m_ping.setInterval(10000);
        if (m_pings % 12 == 0 && m_bestRtt > 0) m_bestRtt = qint64(m_bestRtt * 1.5);
    });
    // Host: heartbeat of the room logic; guests: watch the host (failover)
    m_hostTick.setInterval(1000);
    connect(&m_hostTick, &QTimer::timeout, this, [this] {
        if (!m_authority) { m_hostTick.stop(); return; }
        dispatch(m_authority->tick());
    });
    connect(&m_ping, &QTimer::timeout, this, &WatchPartyController::hostWatch);
    // Joining: repeat the hello until the host answers (relays may connect at different times)
    m_hello.setInterval(3000);
    connect(&m_hello, &QTimer::timeout, this, [this] {
        for (const auto &k : std::as_const(m_candidateKeys)) publish(QJsonObject{{"type", "hello"}, {"nick", m_joinNick}}, k);
    });
    m_linkWatch.setSingleShot(true);
    connect(&m_linkWatch, &QTimer::timeout, this, [this] { if (m_linkPath.isEmpty()) linkFailed(); });
    m_joinTimeout.setSingleShot(true);
    m_joinTimeout.setInterval(25000);
    connect(&m_joinTimeout, &QTimer::timeout, this, &WatchPartyController::joinTimedOut);
    connect(m_builder, &ManifestBuilder::progress, this, [this](int p) { m_prepareProgress = p; emit changed(); });
    connect(m_builder, &ManifestBuilder::finished, this, &WatchPartyController::onManifestReady);
    connect(m_builder, &ManifestBuilder::failed, this, [this](const QString &, const QString &why) {
        m_preparePath.clear();
        setStatus(text(QStringLiteral("Datei konnte nicht gelesen werden: %1"), QStringLiteral("File could not be read: %1")).arg(why), true);
    });
}

WatchPartyController::~WatchPartyController()
{
    stopSwarm();
}

// ------------------------------------------------------------------ texts and settings

QString WatchPartyController::text(const QString &de, const QString &en) const
{
    return m_language.startsWith(QLatin1String("de")) ? de : en;
}

QString WatchPartyController::formatTime(double seconds) const
{
    const qint64 s = qint64(qMax(0.0, seconds));
    const QString mmss = QStringLiteral("%1:%2").arg((s % 3600) / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return s >= 3600 ? QStringLiteral("%1:%2").arg(s / 3600).arg(mmss) : mmss;
}

void WatchPartyController::setLanguage(const QString &code)
{
    if (code.isEmpty() || code == m_language) return;
    m_language = code;
    emit changed();
    emit streamChanged();
}

void WatchPartyController::loadSettings()
{
    const QJsonObject o = m_host ? m_host->loadPluginSettings(QString::fromLatin1(kPluginId)) : QJsonObject();
    m_settings = QVariantMap{
        {"relays", o.value("relays").toString()},
        {"nick", o.value("nick").toString()},
        {"stunServers", o.value("stunServers").toString()},
        {"turnUrl", o.value("turnUrl").toString()},
        {"turnUser", o.value("turnUser").toString()},
        {"turnPassword", o.value("turnPassword").toString()},
        {"uploadEnabled", o.value("uploadEnabled").toBool(true)},
        {"uploadLimitKBps", o.value("uploadLimitKBps").toInt(0)},
        {"cacheMB", qBound(256, o.value("cacheMB").toInt(2048), 65536)},
        {"allowInsecureLoopback", o.value("allowInsecureLoopback").toBool(false)},
        {"streamAll", o.value("streamAll").toBool(false)},
    };
}

void WatchPartyController::saveSettings(const QVariantMap &values)
{
    for (auto it = values.cbegin(); it != values.cend(); ++it) if (m_settings.contains(it.key())) m_settings.insert(it.key(), it.value());
    m_settings["cacheMB"] = qBound(256, m_settings.value("cacheMB").toInt(), 65536);
    m_settings["uploadLimitKBps"] = qMax(0, m_settings.value("uploadLimitKBps").toInt());
    if (m_host) m_host->savePluginSettings(QString::fromLatin1(kPluginId), QJsonObject::fromVariantMap(m_settings));
    if (m_swarm) {
        m_swarm->setUploadEnabled(m_settings.value("uploadEnabled").toBool());
        m_swarm->setUploadLimit(qint64(m_settings.value("uploadLimitKBps").toInt()) * 1024);
    }
    emit settingsChanged();
    emit streamChanged();
}

bool WatchPartyController::live() const
{
    // Live stream: position sync makes no sense, only start/pause follow the host
    return !m_mediaUrl.isEmpty() && (m_mediaLive || m_mediaDuration <= 0);
}

// Channel pages of live platforms (not their recordings / clips)
static bool isLiveChannelUrl(const QString &u)
{
    const QUrl url(u);
    const QString h = url.host().toLower(), p = url.path().toLower();
    if (h.endsWith(QLatin1String("twitch.tv")))
        return !p.contains(QLatin1String("/videos/")) && !p.startsWith(QLatin1String("/videos")) && !p.contains(QLatin1String("/clip"));
    if (h.endsWith(QLatin1String("kick.com"))) return !p.contains(QLatin1String("/videos/")) && !p.contains(QLatin1String("/clips"));
    return false;
}

// A room created while something already plays takes that medium over (otherwise only media opened later counts)
void WatchPartyController::adoptCurrentMedia()
{
    const QVariantMap st = playerState();
    const QString path = st.value("path").toString();
    if (path.isEmpty() || (!m_streamUrl.isEmpty() && path == m_streamUrl)) return;
    onEvent(QStringLiteral("media.loaded"), {{"path", path}, {"source", st.value("source")}, {"title", st.value("title")}, {"duration", st.value("duration")}});
    m_announceAt = m_mono.elapsed() - 2500;     // already running: announce right away
}

void WatchPartyController::addChat(const QJsonObject &m)
{
    const bool system = m.value("system").toBool();
    const int from = m.value("from").toInt();
    QString t = m.value("text").toString();
    if (system) t = t == QLatin1String("joined") ? text(QStringLiteral("ist beigetreten"), QStringLiteral("joined"))
                  : t == QLatin1String("left") ? text(QStringLiteral("hat den Raum verlassen"), QStringLiteral("left the room")) : t;
    m_chat << QVariantMap{{"nick", m.value("nick").toString()}, {"text", t}, {"system", system}, {"me", from == m_myId},
                          {"time", QDateTime::fromMSecsSinceEpoch(qint64(m.value("at").toDouble())).toString(QStringLiteral("HH:mm"))}};
    while (m_chat.size() > 200) m_chat.removeFirst();
    // Short note on the video for messages of others (the panel may be hidden in full screen)
    if (!system && from != m_myId && m_host)
        m_host->showOsd(m.value("nick").toString() + QStringLiteral(": ") + t.left(120), 3500);
    emit chatChanged();
}

void WatchPartyController::sendChat(const QString &message)
{
    const QString t = message.trimmed();
    if (!m_inRoom || t.isEmpty()) return;
    send(QJsonObject{{"type", "chat"}, {"text", t.left(500)}});
}

void WatchPartyController::openSettings()
{
    if (m_host) m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("panel.open"), {{"panel", QStringLiteral("settings")}});
}

void WatchPartyController::setStatus(const QString &t, bool error)
{
    m_status = t;
    m_statusError = error;
    emit changed();
}

QString WatchPartyController::errorText(const QString &code) const
{
    if (code == QLatin1String("room_not_found")) return text(QStringLiteral("Raum nicht gefunden – stimmt die Raum-ID und ist jemand im Raum online?"), QStringLiteral("Room not found – is the room ID right and is someone in the room online?"));
    if (code == QLatin1String("wrong_password")) return text(QStringLiteral("Falsches Passwort."), QStringLiteral("Wrong password."));
    if (code == QLatin1String("password_required")) return text(QStringLiteral("Privater Raum – bitte das Passwort eingeben."), QStringLiteral("Private room – please enter the password."));
    if (code == QLatin1String("too_many_attempts")) return text(QStringLiteral("Zu viele Fehlversuche – bitte später erneut versuchen."), QStringLiteral("Too many failed attempts – please try again later."));
    if (code == QLatin1String("room_full")) return text(QStringLiteral("Der Raum ist voll."), QStringLiteral("The room is full."));
    if (code == QLatin1String("server_full")) return text(QStringLiteral("Der Watch-Party-Dienst ist ausgelastet."), QStringLiteral("The watch party service is busy."));
    if (code == QLatin1String("session_expired")) return text(QStringLiteral("Sitzung abgelaufen."), QStringLiteral("Session expired."));
    if (code == QLatin1String("not_host")) return text(QStringLiteral("Das darf nur der Host."), QStringLiteral("Only the host can do that."));
    if (code == QLatin1String("share_busy")) return text(QStringLiteral("Ein anderer Teilnehmer gibt bereits eine Datei frei."), QStringLiteral("Another member is already sharing a file."));
    if (code == QLatin1String("unsupported_version")) return text(QStringLiteral("Der Dienst spricht eine andere Protokollversion – Plugin aktualisieren."), QStringLiteral("The service uses another protocol version – update the plugin."));
    return text(QStringLiteral("Fehler: %1"), QStringLiteral("Error: %1")).arg(code);
}

// ------------------------------------------------------------------ room channel (public Nostr relays, encrypted)

int WatchPartyController::othersConnected() const
{
    int n = 0;
    for (const auto &v : m_rawMembers) { const QJsonObject o = v.toObject(); if (o.value("id").toInt() != m_myId && o.value("connected").toBool()) n++; }
    return n;
}

int WatchPartyController::othersReady() const
{
    int n = 0;
    for (const auto &v : m_rawMembers) {
        const QJsonObject o = v.toObject();
        if (o.value("id").toInt() != m_myId && o.value("connected").toBool() && o.value("ready").toBool() && !o.value("buffering").toBool()) n++;
    }
    return n;
}

int WatchPartyController::relays() const
{
    return m_pool ? m_pool->connectedCount() : 0;
}

QString WatchPartyController::pkOf(int memberId) const
{
    for (const auto &v : m_rawMembers) if (v.toObject().value("id").toInt() == memberId) return v.toObject().value("pk").toString();
    return {};
}

void WatchPartyController::startChannel(const QString &roomId, const QList<QByteArray> &keys)
{
    if (m_pool) { m_pool->disconnect(this); m_pool->stop(); m_pool->deleteLater(); }
    m_pool = new NostrPool(this);
    m_pool->setAllowInsecureLoopback(m_settings.value("allowInsecureLoopback").toBool());
    m_myPk = m_pool->publicKeyHex();
    m_candidateKeys = keys;
    m_key = keys.size() == 1 ? keys.first() : QByteArray();
    m_roomId = roomId;
    connect(m_pool, &NostrPool::received, this, &WatchPartyController::onChannel);
    connect(m_pool, &NostrPool::diagnostic, this, [this](const QString &t) {
        if (m_host) m_host->log(QString::fromLatin1(kPluginId), QStringLiteral("info"), QStringLiteral("watch party: ") + t);
    });
    connect(m_pool, &NostrPool::connectionChanged, this, [this] {
        const int n = m_pool ? m_pool->connectedCount() : 0;
        if (m_inRoom) m_connState = n > 0 ? QStringLiteral("online") : QStringLiteral("reconnecting");
        if (m_joining) setStatus(text(QStringLiteral("Raum wird gesucht … (%1 von %2 Relays verbunden)"), QStringLiteral("Looking for the room … (%1 of %2 relays connected)"))
                                     .arg(n).arg(m_pool ? m_pool->relayCount() : 0));
        emit changed();
    });
    QStringList relays = m_settings.value("relays").toString().split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
    if (relays.isEmpty()) relays = NostrPool::defaultRelays();
    m_pool->start(relays, crypto::roomTopic(roomId));
    m_pings = 0;
    m_bestRtt = -1;
}

void WatchPartyController::publish(QJsonObject msg, const QByteArray &key)
{
    if (!m_pool) return;
    msg["v"] = 1;
    msg["rid"] = m_roomId;
    const QByteArray k = key.isEmpty() ? m_key : key;
    if (k.isEmpty()) return;
    m_pool->publish(crypto::encrypt(k, QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void WatchPartyController::onChannel(const QString &pk, const QByteArray &content)
{
    QByteArray plain;
    QByteArray usedKey;
    const QList<QByteArray> keys = m_key.isEmpty() ? m_candidateKeys : QList<QByteArray>{m_key};
    for (const auto &k : keys) if (crypto::decrypt(k, content, &plain)) { usedKey = k; break; }
    if (usedKey.isEmpty()) { m_undecryptable = true; return; }      // other password (or another room)
    const QJsonObject m = QJsonDocument::fromJson(plain).object();
    if (m.value("v").toInt() != 1 || m.value("rid").toString() != m_roomId) return;
    const QString to = m.value("to").toString();
    if (!to.isEmpty() && to != m_myPk) return;
    if (m_joining && m.value("type").toString() == QLatin1String("welcome")) m_key = usedKey;
    if (m_key.isEmpty()) return;
    if (m_authority) {
        // We are the authority: guest requests go into the room logic
        static const QSet<QString> fromGuests{"hello", "ping", "leave", "ready", "share", "close", "media", "action", "position", "host", "chat"};
        const QString type = m.value("type").toString();
        if (fromGuests.contains(type)) { dispatch(m_authority->handle(pk, m)); return; }
        if (type == QLatin1String("snapshot")) {
            // Two hosts after a network split: higher epoch wins, equal epoch → lower member id
            const qint64 e = qint64(m.value("epoch").toDouble());
            if (e > m_authority->epoch() || (e == m_authority->epoch() && m.value("hostId").toInt() < m_authority->hostId())) {
                m_authority.reset();
                m_hostTick.stop();
                m_hostPk.clear();
            } else {
                return;
            }
        }
    }
    onMessage(pk, m);
}

void WatchPartyController::send(QJsonObject msg)
{
    if (!m_inRoom && !m_joining) return;
    if (m_authority) {
        msg["v"] = 1;
        msg["rid"] = m_roomId;
        dispatch(m_authority->handle(m_myPk, msg));
        if (m_authority && (m_authority->handedOver() || m_authority->closed())) { m_authority.reset(); m_hostTick.stop(); }
        return;
    }
    publish(msg);
}

void WatchPartyController::dispatch(const QList<RoomHost::Out> &outs)
{
    for (const auto &o : outs) {
        QJsonObject m = o.msg;
        if (!o.toPk.isEmpty()) m["to"] = o.toPk;
        if (o.toPk != m_myPk) publish(m);
        if (o.toPk.isEmpty() || o.toPk == m_myPk) { m["v"] = 1; m["rid"] = m_roomId; onMessage(m_myPk, m); }
    }
}

// Authority (host) for this room: explicit hand-over or failover from the last snapshot
void WatchPartyController::becomeHost(bool failover)
{
    m_authority = RoomHost::fromSnapshot(m_lastSnapshot, m_myPk, [] { return QDateTime::currentMSecsSinceEpoch(); }, failover);
    m_hostTick.start();
    m_bestRtt = 0;
    m_offset = QDateTime::currentMSecsSinceEpoch() - m_mono.elapsed();
    m_hostPk = m_myPk;
    dispatch({RoomHost::Out{QString(), m_authority->snapshot()}});
    if (failover && m_host) m_host->showOsd(text(QStringLiteral("Watch Party: Host weg – du bist jetzt Host"), QStringLiteral("Watch Party: host gone – you are the host now")), 3000);
}

// Guests: is the host still there? If not, the longest connected ready member takes over
void WatchPartyController::hostWatch()
{
    if (!m_inRoom || m_authority || m_lastSnapshot.isEmpty()) return;
    const qint64 now = m_mono.elapsed();
    if (now - m_lastHostSeen < 40000) return;
    const QJsonArray members = m_lastSnapshot.value("members").toArray();
    const int oldHost = m_lastSnapshot.value("hostId").toInt();
    QJsonObject best;
    for (const bool needReady : {true, false}) {
        for (const auto &v : members) {
            const QJsonObject o = v.toObject();
            if (o.value("id").toInt() == oldHost || !o.value("connected").toBool() || m_failoverSkip.contains(o.value("pk").toString())) continue;
            if (needReady && !o.value("ready").toBool()) continue;
            if (best.isEmpty() || o.value("joinedAt").toDouble() < best.value("joinedAt").toDouble()) best = o;
        }
        if (!best.isEmpty()) break;
    }
    if (best.isEmpty() || best.value("pk").toString() == m_myPk) {
        becomeHost(true);
        return;
    }
    // Give the chosen member 20 s; if nothing comes from it either, the next one takes over
    if (now - m_lastHostSeen > 60000) { m_failoverSkip.insert(best.value("pk").toString()); m_lastHostSeen = now - 40000; }
}

void WatchPartyController::joinTimedOut()
{
    if (!m_joining) return;
    const QString code = !m_undecryptable ? QStringLiteral("room_not_found")
                       : m_joinPassword.isEmpty() ? QStringLiteral("password_required") : QStringLiteral("wrong_password");
    const int relays = m_pool ? m_pool->connectedCount() : 0;
    const QString msg0 = relays == 0 && code == QLatin1String("room_not_found")
                   ? text(QStringLiteral("Keine Verbindung zu den Nostr-Relays (%1). Blockiert eine Firewall, ein Virenscanner oder das Netzwerk Verbindungen nach außen (Port 443)?"),
                          QStringLiteral("No connection to the Nostr relays (%1). Is a firewall, an antivirus or the network blocking outgoing connections (port 443)?"))
                         .arg(m_pool && !m_pool->lastError().isEmpty() ? m_pool->lastError() : text(QStringLiteral("kein Grund gemeldet"), QStringLiteral("no reason reported")))
                   : errorText(code);
    endSession(msg0, true);
    m_needPassword = code != QLatin1String("room_not_found");
    emit changed();
}

void WatchPartyController::onMessage(const QString &fromPk, const QJsonObject &m)
{
    const QString type = m.value("type").toString();
    const bool fromHost = fromPk == m_myPk ? bool(m_authority) : (!m_hostPk.isEmpty() && fromPk == m_hostPk);
    if (type == QLatin1String("welcome")) {
        if (m_inRoom) { if (fromHost) applySnapshot(m.value("snapshot").toObject()); return; }
        m_joining = false;
        m_hello.stop();
        m_joinTimeout.stop();
        m_inRoom = true;
        m_busy = false;
        m_needPassword = false;
        m_connState = QStringLiteral("online");
        m_myId = m.value("memberId").toInt();
        m_lastHostSeen = m_mono.elapsed();
        if (!m_poll.isActive()) m_poll.start();
        m_ping.setInterval(2000);
        m_ping.start();
        m_chat.clear();
        for (const auto &v : m.value("snapshot").toObject().value("chat").toArray()) addChat(v.toObject());
        applySnapshot(m.value("snapshot").toObject());
        send(QJsonObject{{"type", "ping"}, {"t", double(m_mono.elapsed())}});
        setStatus(text(QStringLiteral("Verbunden."), QStringLiteral("Connected.")));
        return;
    }
    if (!m_inRoom) return;
    if (type == QLatin1String("snapshot")) {
        const qint64 epoch = qint64(m.value("epoch").toDouble());
        // Accepted only from the host named in it, and never from an older epoch
        if (m.value("hostPk").toString() != fromPk || epoch < m_epoch) return;
        if (fromPk != m_hostPk) { m_bestRtt = fromPk == m_myPk ? 0 : -1; m_failoverSkip.clear(); }   // new host: clock again
        m_lastHostSeen = m_mono.elapsed();
        applySnapshot(m);
        return;
    }
    if (!fromHost) {
        // Peer-to-peer WebRTC signaling: only from the member the sender claims to be
        if (type == QLatin1String("signal") && m_swarm && m.value("shareId").toString() == m_swarm->shareId()) {
            const int fromId = m.value("fromId").toInt();
            if (pkOf(fromId) == fromPk) m_swarm->onSignal(fromId, m.value("kind").toString(), m.value("data").toString(), QString());
        }
        if (type == QLatin1String("signal") && m_tunnel && m.value("shareId").toString() == m_tunnel->shareId()) {
            const int fromId = m.value("fromId").toInt();
            if (pkOf(fromId) == fromPk) m_tunnel->onSignal(fromId, m.value("kind").toString(), m.value("data").toString());
        }
        return;
    }
    m_lastHostSeen = m_mono.elapsed();
    if (type == QLatin1String("state")) {
        applyState(m.value("state").toObject(), false);
    } else if (type == QLatin1String("signal")) {
        if (m_swarm && m.value("shareId").toString() == m_swarm->shareId())
            m_swarm->onSignal(m.value("fromId").toInt(), m.value("kind").toString(), m.value("data").toString(), QString());
        if (m_tunnel && m.value("shareId").toString() == m_tunnel->shareId())
            m_tunnel->onSignal(m.value("fromId").toInt(), m.value("kind").toString(), m.value("data").toString());
    } else if (type == QLatin1String("peer-left")) {
        if (m_swarm) m_swarm->removePeer(m.value("memberId").toInt());
        if (m_tunnel) m_tunnel->removePeer(m.value("memberId").toInt());
    } else if (type == QLatin1String("share-ended")) {
        const QString reason = m.value("reason").toString();
        if ((m_swarm && m_swarm->shareId() == m.value("shareId").toString()) || (m_tunnel && m_tunnel->shareId() == m.value("shareId").toString())) {
            const bool viewing = m_mode == QLatin1String("stream");
            stopSwarm();
            if (viewing) {
                m_mode.clear();
                m_localReady = false;
                setStatus(reason == QLatin1String("media_changed")
                              ? text(QStringLiteral("Neues Medium im Raum – die Freigabe wurde beendet."), QStringLiteral("New medium in the room – the share has ended."))
                              : text(QStringLiteral("Freigabe beendet. Warte auf eine erneute Freigabe oder öffne eine eigene Datei."),
                                     QStringLiteral("Share ended. Wait for a new share or open your own file.")), reason != QLatin1String("media_changed"));
            }
        }
        if (m_seedManifest && reason != QLatin1String("replaced")) { m_seedManifest.reset(); m_seedPath.clear(); }
    } else if (type == QLatin1String("pong")) {
        const qint64 now = m_mono.elapsed();
        const qint64 rtt = now - qint64(m.value("t").toDouble());
        if (rtt >= 0 && (m_bestRtt < 0 || rtt <= m_bestRtt)) {
            m_bestRtt = rtt;
            m_offset = qint64(m.value("server").toDouble()) + rtt / 2 - now;
        }
    } else if (type == QLatin1String("chat")) {
        addChat(m);
    } else if (type == QLatin1String("rejoin")) {
        // The host no longer knows us (away too long): join again
        send(QJsonObject{{"type", "hello"}, {"nick", m_settings.value("nick").toString()}});
    } else if (type == QLatin1String("error")) {
        const QString code = m.value("code").toString();
        if (code == QLatin1String("room_full")) endSession(errorText(code), true);
        else setStatus(errorText(code), true);
    } else if (type == QLatin1String("closed")) {
        if (!m_authority) endSession(text(QStringLiteral("Der Host hat den Raum geschlossen."), QStringLiteral("The host closed the room.")), false);
    }
}

void WatchPartyController::endSession(const QString &message, bool error)
{
    const QVariantMap st = playerState();
    if (m_inRoom && matched(st.value("path").toString()) && !st.value("paused").toBool()) m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("player.pause"), {{"paused", true}});
    const bool wasStream = !m_streamUrl.isEmpty() && st.value("path").toString() == m_streamUrl;
    stopSwarm();
    if (wasStream) m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("player.stop"));
    setSpeed(1.0);
    m_builder->cancel();
    m_poll.stop();
    m_ping.stop();
    m_hostTick.stop();
    m_hello.stop();
    m_joinTimeout.stop();
    if (m_pool) {
        // Give the last messages (leave / hand-over) a moment to reach the relays
        NostrPool *p = m_pool;
        m_pool = nullptr;
        p->disconnect(this);
        QTimer::singleShot(1500, p, [p] { p->stop(); p->deleteLater(); });
    }
    m_authority.reset();
    m_inRoom = false;
    m_joining = false;
    m_busy = false;
    m_connState = QStringLiteral("offline");
    m_key.clear(); m_candidateKeys.clear();
    m_hostPk.clear(); m_epoch = 0; m_lastSnapshot = {}; m_failoverSkip.clear();
    m_roomId.clear(); m_roomName.clear();
    m_myId = 0; m_hostId = 0;
    m_members.clear(); m_rawMembers = {};
    m_mediaGen = 0; m_mediaTitle.clear(); m_mediaDuration = 0; m_mediaUrl.clear(); m_mediaLive = false;
    m_hold = false;
    m_ownUrl.clear(); m_linkPath.clear();
    m_linkWatch.stop();
    m_pendingTunnel = {};
    m_sharing = false;
    m_chat.clear();
    emit chatChanged();
    m_state = {};
    m_share = {};
    m_mode.clear(); m_ownPath.clear();
    m_localReady = false; m_outOfSync = false; m_buffering = false;
    m_mediaWarning.clear();
    m_announcePending = false; m_awaitGen = -1;
    m_seedManifest.reset(); m_seedPath.clear(); m_preparePath.clear();
    m_shareEndedText.clear();
    m_needPassword = false;
    setStatus(message, error);
    emit streamChanged();
}

// ------------------------------------------------------------------ room actions

static QString newRoomId()
{
    const QByteArray alphabet(sync::kRoomIdAlphabet);
    QString raw;
    const QByteArray r = crypto::randomBytes(48);
    for (const char b : r) {
        const int v = uchar(b);
        if (v >= 232) continue;                          // 232 = 8 × 29: no modulo bias
        raw += QLatin1Char(alphabet.at(v % alphabet.size()));
        if (raw.size() == 6) break;
    }
    return sync::normalizeRoomId(raw.size() == 6 ? raw : newRoomId());
}

void WatchPartyController::createRoom(const QString &name, const QString &nick, bool isPrivate, const QString &password)
{
    if (m_inRoom || m_joining) return;
    if (name.trimmed().isEmpty() || nick.trimmed().isEmpty()) { setStatus(text(QStringLiteral("Bitte Raumname und Anzeigenick eingeben."), QStringLiteral("Please enter a room name and a nickname.")), true); return; }
    if (isPrivate && password.size() < 4) { setStatus(text(QStringLiteral("Das Passwort braucht mindestens 4 Zeichen."), QStringLiteral("The password needs at least 4 characters.")), true); return; }
    saveSettings({{"nick", nick.trimmed()}});
    // The room ID is made here; the key from ID + password is known only to the members
    const QString id = newRoomId();
    startChannel(id, {crypto::deriveRoomKey(id, isPrivate ? password : QString())});
    RoomHost host(id, name.trimmed().left(60), isPrivate, m_myPk, nick.trimmed().left(32), [] { return QDateTime::currentMSecsSinceEpoch(); });
    m_lastSnapshot = host.snapshot();
    m_inRoom = true;
    m_myId = host.hostId();
    m_connState = QStringLiteral("online");
    m_poll.start();
    m_authority = host;
    m_hostTick.start();
    m_bestRtt = 0;
    m_offset = QDateTime::currentMSecsSinceEpoch() - m_mono.elapsed();
    m_hostPk = m_myPk;
    dispatch({RoomHost::Out{QString(), m_authority->snapshot()}});
    setStatus(text(QStringLiteral("Raum erstellt – kopiere die Raum-ID und schicke sie deinen Freunden."), QStringLiteral("Room created – copy the room ID and send it to your friends.")));
    adoptCurrentMedia();
}

void WatchPartyController::joinRoom(const QString &roomId, const QString &nick, const QString &password)
{
    if (m_inRoom || m_joining) return;
    const QString id = sync::normalizeRoomId(roomId);
    if (id.isEmpty()) { setStatus(text(QStringLiteral("Ungültige Raum-ID – sie hat 6 Zeichen, z. B. K7M3QX."), QStringLiteral("Invalid room ID – it has 6 characters, e.g. K7M3QX.")), true); return; }
    if (nick.trimmed().isEmpty()) { setStatus(text(QStringLiteral("Bitte einen Anzeigenick eingeben."), QStringLiteral("Please enter a nickname.")), true); return; }
    saveSettings({{"nick", nick.trimmed()}});
    m_joinNick = nick.trimmed().left(32);
    m_joinPassword = password;
    // With a password: the private key; without (or as fallback): the key of a public room
    QList<QByteArray> keys;
    if (!password.isEmpty()) keys << crypto::deriveRoomKey(id, password);
    keys << crypto::deriveRoomKey(id, QString());
    startChannel(id, keys);
    m_joining = true;
    m_busy = true;
    m_undecryptable = false;
    m_connState = QStringLiteral("connecting");
    m_hello.start();
    QTimer::singleShot(800, this, [this] {
        if (m_joining) for (const auto &k : std::as_const(m_candidateKeys)) publish(QJsonObject{{"type", "hello"}, {"nick", m_joinNick}}, k);
    });
    m_joinTimeout.start();
    setStatus(text(QStringLiteral("Raum wird gesucht …"), QStringLiteral("Looking for the room …")));
}

void WatchPartyController::leaveRoom()
{
    if (!m_inRoom) return;
    send(QJsonObject{{"type", "leave"}});            // as host: hands the room over to the next member
    endSession(text(QStringLiteral("Raum verlassen."), QStringLiteral("Left the room.")), false);
}

void WatchPartyController::closeRoom()
{
    if (!isHost()) return;
    send(QJsonObject{{"type", "close"}});
    endSession(text(QStringLiteral("Raum geschlossen."), QStringLiteral("Room closed.")), false);
}

void WatchPartyController::copyRoomId()
{
    if (m_roomId.isEmpty()) return;
    QGuiApplication::clipboard()->setText(m_roomId);   // only the id – never a password
    if (m_host) m_host->showOsd(text(QStringLiteral("Raum-ID kopiert"), QStringLiteral("Room ID copied")), 1500);
    emit toast(text(QStringLiteral("Raum-ID kopiert"), QStringLiteral("Room ID copied")));
}

void WatchPartyController::copyRoomName()
{
    if (m_roomName.isEmpty()) return;
    QGuiApplication::clipboard()->setText(m_roomName);
    if (m_host) m_host->showOsd(text(QStringLiteral("Raumname kopiert"), QStringLiteral("Room name copied")), 1500);
    emit toast(text(QStringLiteral("Raumname kopiert"), QStringLiteral("Room name copied")));
}

void WatchPartyController::transferHost(int memberId)
{
    if (isHost() && memberId != m_myId) send(QJsonObject{{"type", "host"}, {"memberId", memberId}});
}

void WatchPartyController::sendReady()
{
    if (!m_inRoom) return;
    send(QJsonObject{{"type", "ready"}, {"gen", double(m_mediaGen)}, {"ready", m_localReady}, {"buffering", m_buffering}, {"mode", m_mode}});
}

void WatchPartyController::markReady()
{
    const QVariantMap st = playerState();
    const QString path = st.value("path").toString();
    if (!m_inRoom || m_mediaGen == 0 || path.isEmpty() || path == m_streamUrl) return;
    m_ownPath = path;
    m_mode = QStringLiteral("own");
    m_localReady = true;
    m_outOfSync = false;
    m_obs.reset(st.value("position").toDouble(), st.value("paused").toBool(), m_mono.elapsed());
    sendReady();
    updateMediaWarning();
    syncToRoom(true);
    emit changed();
}

void WatchPartyController::syncNow()
{
    m_outOfSync = false;
    m_resyncPending = true;
    m_drift.reset();
    emit changed();
}

QString WatchPartyController::hostNick() const
{
    for (const auto &v : m_members) if (v.toMap().value("id").toInt() == m_hostId) return v.toMap().value("nick").toString();
    return {};
}

bool WatchPartyController::localFileLoaded() const
{
    return isLocalFile(playerState().value("path").toString());
}

// ------------------------------------------------------------------ snapshot / state

qint64 WatchPartyController::serverNow() const
{
    return m_mono.elapsed() + m_offset;
}

void WatchPartyController::applySnapshot(const QJsonObject &s)
{
    const QJsonObject room = s.value("room").toObject();
    m_roomId = room.value("id").toString();
    m_roomName = room.value("name").toString();
    m_roomPrivate = room.value("private").toBool();
    const int oldHost = m_hostId;
    m_hostId = s.value("hostId").toInt();
    m_hostPk = s.value("hostPk").toString();
    m_epoch = qint64(s.value("epoch").toDouble());
    m_lastSnapshot = s;
    bool known = false;
    for (const auto &v : s.value("members").toArray())
        if (v.toObject().value("pk").toString() == m_myPk) { m_myId = v.toObject().value("id").toInt(); known = true; }
    if (!known && m_inRoom && !m_authority) {
        // Not (or no longer) in the member list, e.g. after being away too long: join again
        publish(QJsonObject{{"type", "hello"}, {"nick", m_settings.value("nick").toString()}});
    }
    // Explicit hand-over: the snapshot names us as host → we are the authority now
    if (!m_authority && m_hostPk == m_myPk && known) {
        becomeHost(false);
        return;
    }
    m_rawMembers = s.value("members").toArray();
    m_members.clear();
    for (const auto &v : std::as_const(m_rawMembers)) {
        QVariantMap mm = v.toObject().toVariantMap();
        mm["me"] = mm.value("id").toInt() == m_myId;
        m_members << mm;
    }
    if (oldHost && oldHost != m_hostId && m_hostId == m_myId && m_host)
        m_host->showOsd(text(QStringLiteral("Watch Party: Du bist jetzt Host"), QStringLiteral("Watch Party: you are the host now")), 2500);

    const QJsonObject media = s.value("media").toObject();
    const qint64 gen = qint64(media.value("gen").toDouble());
    const QJsonObject st = s.value("state").toObject();
    const bool newGen = gen != m_mediaGen;
    if (newGen) {
        m_mediaGen = gen;
        m_localReady = false;
        m_outOfSync = false;
        m_drift.reset();
        setSpeed(1.0);
        if (isHost() && m_awaitGen >= 0 && gen > m_awaitGen) {
            // Our announced medium is now the room medium: the host is ready with its own file
            m_awaitGen = -1;
            m_mode = QStringLiteral("own");
            m_localReady = true;
            // New medium of the host: provided to the room right away – no button. A local file is streamed to everybody
            // without an own copy; a link is opened by the guests themselves (or relayed when "stream everything" is on)
            QTimer::singleShot(300, this, [this] {
                const bool file = isLocalFile(playerState().value("path").toString());
                if (file || streamAll()) startHostShare(true);
            });
        } else {
            // New medium: the previous stream, link or file no longer counts (auto stream applies again; an own copy is
            // confirmed with "Ready")
            m_mode.clear();
            m_linkPath.clear();
        }
    }
    m_mediaTitle = media.value("title").toString();
    m_mediaDuration = media.value("duration").toDouble();
    const QString url = media.value("url").toString();
    m_mediaLive = media.value("live").toBool();
    if (url != m_mediaUrl || newGen) {
        m_mediaUrl = url;
        // Room medium is a web link (YouTube, Twitch, stream …): every guest opens the same link by itself
        if (!url.isEmpty() && !isHost() && m_awaitGen < 0) {
            m_mode = QStringLiteral("link");
            m_linkPath.clear();
            m_localReady = false;
            m_outOfSync = false;
            m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("player.load"), {{"url", url}});
            m_linkWatch.start(25000);
            setStatus(text(QStringLiteral("Der Host spielt einen Link – er wird bei dir geöffnet …"), QStringLiteral("The host plays a link – opening it here …")));
        }
    }
    if (qint64(st.value("seq").toDouble()) >= m_state.seq || qint64(st.value("gen").toDouble()) != m_state.gen) {
        m_state.gen = qint64(st.value("gen").toDouble());
        m_state.seq = qint64(st.value("seq").toDouble());
        m_state.paused = st.value("paused").toBool(true);
        m_state.position = st.value("position").toDouble();
        m_state.ref = qint64(st.value("ref").toDouble());
    }
    m_share = s.value("share").toObject();
    if (isHost() && m_localReady && m_mode == QLatin1String("own") && m_awaitGen < 0 && st.value("reason").toString() == QLatin1String("media")
        && qint64(st.value("by").toDouble()) == m_myId && m_pushedGen != m_mediaGen) {
        // Push the host's real playback state into the fresh generation (once – the room logic answers synchronously)
        m_pushedGen = m_mediaGen;
        sendReady();
        const QVariantMap ps = playerState();
        if (othersConnected() > 0 && !live()) {
            // Nobody misses the beginning: the host waits at the start until the others have loaded the medium
            m_hold = true;
            m_holdDeadline = m_mono.elapsed() + 45000;
            playerPause(true, 0);
            if (ps.value("position").toDouble() > 0.3) playerSeek(0, true);
            send(QJsonObject{{"type", "action"}, {"gen", double(m_mediaGen)}, {"op", "seek"}, {"position", 0.0}, {"paused", true}});
            m_obs.expect(0, true, m_mono.elapsed(), true);
            setStatus(text(QStringLiteral("Warte, bis alle bereit sind …"), QStringLiteral("Waiting until everybody is ready …")));
        } else {
            send(QJsonObject{{"type", "action"}, {"gen", double(m_mediaGen)}, {"op", "seek"}, {"position", ps.value("position").toDouble()},
                             {"paused", ps.value("paused").toBool()}});
            m_obs.reset(ps.value("position").toDouble(), ps.value("paused").toBool(), m_mono.elapsed());
        }
    }
    updateSwarm();
    updateMediaWarning();
    if (m_authority && m_share.isEmpty() && !m_mediaUrl.isEmpty() && !m_sharing) {
        for (const auto &v : std::as_const(m_rawMembers))
            if (v.toObject().value("mode").toString() == QLatin1String("nolink")) { QTimer::singleShot(0, this, [this] { startHostShare(false); }); break; }
    }
    emit changed();
    emit streamChanged();
}

void WatchPartyController::applyState(const QJsonObject &st, bool force)
{
    const qint64 gen = qint64(st.value("gen").toDouble());
    const qint64 seq = qint64(st.value("seq").toDouble());
    if (gen != m_mediaGen || seq <= m_state.seq) return;      // previous medium or already applied
    m_state.gen = gen;
    m_state.seq = seq;
    m_state.paused = st.value("paused").toBool(true);
    m_state.position = st.value("position").toDouble();
    m_state.ref = qint64(st.value("ref").toDouble());
    const QString reason = st.value("reason").toString();
    if (m_swarm && !m_swarm->isSeed()) m_swarm->hintPosition(sync::targetPosition(m_state, serverNow(), m_mediaDuration), m_mediaDuration);
    if (isHost() && qint64(st.value("by").toDouble()) == m_myId) return;   // our own action
    if (isHost()) return;
    const bool jump = reason == QLatin1String("seek") || reason == QLatin1String("rebase") || reason == QLatin1String("media");
    syncToRoom(force || jump);
    emit changed();
}

// ------------------------------------------------------------------ player

QVariantMap WatchPartyController::playerState() const
{
    return m_host ? m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("player.state")).toMap() : QVariantMap();
}

bool WatchPartyController::matched(const QString &path) const
{
    if (path.isEmpty()) return false;
    if (m_mode == QLatin1String("stream")) return !m_streamUrl.isEmpty() && path == m_streamUrl;
    if (m_mode == QLatin1String("own")) return path == m_ownPath;
    if (m_mode == QLatin1String("link")) return !m_linkPath.isEmpty() && path == m_linkPath;
    return false;
}

void WatchPartyController::playerPause(bool paused, double pos)
{
    m_obs.expect(pos, paused, m_mono.elapsed(), false);
    m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("player.pause"), {{"paused", paused}});
}

void WatchPartyController::playerSeek(double pos, bool paused)
{
    m_obs.expect(pos, paused, m_mono.elapsed(), true);
    m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("player.seek"), {{"seconds", pos}});
}

void WatchPartyController::setSpeed(double speed)
{
    if (!m_host || (!m_speedChanged && speed == 1.0)) return;
    m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("mpv.set"), {{"name", QStringLiteral("speed")}, {"value", speed}});
    m_speedChanged = speed != 1.0;
}

void WatchPartyController::syncToRoom(bool force)
{
    if (isHost() || !m_localReady || m_outOfSync) return;
    if (m_bestRtt < 0) { m_resyncPending = true; return; }     // server clock not known yet
    const QVariantMap st = playerState();
    if (!matched(st.value("path").toString())) return;
    const double duration = st.value("duration").toDouble() > 0 ? st.value("duration").toDouble() : m_mediaDuration;
    const double target = sync::targetPosition(m_state, serverNow(), duration);
    const double pos = st.value("position").toDouble();
    const bool paused = st.value("paused").toBool();
    if (paused != m_state.paused) playerPause(m_state.paused, pos);
    if (!live() && ((force && std::abs(pos - target) > 0.5) || std::abs(pos - target) > 1.5)) {
        playerSeek(target, m_state.paused);
        m_drift.reset();
        setSpeed(1.0);
    }
}

void WatchPartyController::tick()
{
    if (!m_inRoom || !m_host) return;
    const QVariantMap st = playerState();
    const qint64 now = m_mono.elapsed();
    const QString path = st.value("path").toString();
    if (matched(path)) {
        const bool pfc = m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("mpv.get"), {{"name", QStringLiteral("paused-for-cache")}}).toBool();
        if (pfc != m_buffering) {
            m_buffering = pfc;
            if (!pfc) m_resyncPending = true;     // back from buffering: re-sync to the room position
            sendReady();
            emit changed();
        }
    }
    if (isHost()) hostTick(st, now); else guestTick(st, now);
}

void WatchPartyController::hostTick(const QVariantMap &st, qint64 now)
{
    const QString path = st.value("path").toString();
    const double pos = st.value("position").toDouble();
    const bool paused = st.value("paused").toBool();
    const double duration = st.value("duration").toDouble();
    if (m_announcePending && path == m_ownPath && (duration > 0 || (!m_ownUrl.isEmpty() && now - m_announceAt > 3000))) {
        m_announcePending = false;
        m_awaitGen = m_mediaGen;
        QString title = st.value("title").toString();
        if (title.isEmpty()) title = m_ownUrl.isEmpty() ? QFileInfo(localPath(path)).fileName() : m_ownUrl;
        QJsonObject media{{"type", "media"}, {"title", title.left(200)}, {"duration", duration}};
        if (!m_ownUrl.isEmpty()) {
            media["url"] = m_ownUrl;
            // Live: no fixed duration, not seekable, or a channel page of a live platform (Twitch, Kick)
            const bool seekable = m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("mpv.get"), {{"name", QStringLiteral("seekable")}}).toBool();
            media["live"] = duration <= 0 || !seekable || isLiveChannelUrl(m_ownUrl);
        }
        send(media);
        return;
    }
    if (!matched(path) || m_mediaGen == 0 || m_awaitGen >= 0) { m_obs.reset(pos, paused, now); return; }
    if (!m_localReady) {
        // Host watching the room stream: ready once playback runs
        if (m_mode == QLatin1String("stream") && duration > 0 && !m_buffering) { m_localReady = true; sendReady(); emit changed(); }
        m_obs.reset(pos, paused, now);
        return;
    }
    if (m_hold) {
        // Longer while the file is still being prepared for the room
        if (m_builder->running() || m_sharing) m_holdDeadline = qMax(m_holdDeadline, now + 20000);
        const int others = othersConnected(), ready = othersReady();
        if ((others > 0 && ready >= others) || others == 0 || now > m_holdDeadline) {
            m_hold = false;
            playerPause(false, pos);
            send(QJsonObject{{"type", "action"}, {"gen", double(m_mediaGen)}, {"op", "play"}, {"position", pos}, {"paused", false}});
            m_lastHeartbeat = now;
            setStatus(text(QStringLiteral("Alle bereit – los geht's."), QStringLiteral("Everybody ready – here we go.")));
            if (m_host) m_host->showOsd(text(QStringLiteral("Watch Party: Start für alle"), QStringLiteral("Watch Party: starting for everybody")), 1500);
            return;
        }
        const QString t = text(QStringLiteral("Warte, bis alle bereit sind (%1/%2) …"), QStringLiteral("Waiting until everybody is ready (%1/%2) …")).arg(ready).arg(others);
        if (m_status != t) setStatus(t);
    }
    const double speed = m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("mpv.get"), {{"name", QStringLiteral("speed")}}).toDouble();
    const auto ch = m_obs.update(pos, paused, speed, now);
    if (m_hold && ch.played) m_hold = false;          // the host pressed play himself: start right away
    const auto action = [&](const char *op) {
        send(QJsonObject{{"type", "action"}, {"gen", double(m_mediaGen)}, {"op", QString::fromLatin1(op)}, {"position", pos}, {"paused", paused}});
        m_lastHeartbeat = now;
    };
    if (ch.seeked && !live()) action("seek");
    else if (ch.played) action("play");
    else if (ch.paused) action("pause");
    // Confirmed playback progress (keeps a really running room alive, re-bases host buffering on the server)
    if (!paused && now - m_lastHeartbeat > 5000) {
        send(QJsonObject{{"type", "position"}, {"gen", double(m_mediaGen)}, {"position", pos}, {"paused", false}});
        m_lastHeartbeat = now;
    }
}

void WatchPartyController::guestTick(const QVariantMap &st, qint64 now)
{
    const QString path = st.value("path").toString();
    const double pos = st.value("position").toDouble();
    const bool paused = st.value("paused").toBool();
    const double duration = st.value("duration").toDouble();
    if (!matched(path)) { m_obs.reset(pos, paused, now); return; }
    if ((m_mode == QLatin1String("stream") || m_mode == QLatin1String("link")) && !m_localReady) {
        // Ready only after a successful media start
        if ((duration > 0 || (m_mode == QLatin1String("link") && !paused)) && !m_buffering) {
            m_localReady = true;
            m_outOfSync = false;
            sendReady();
            m_obs.reset(pos, paused, now);
            syncToRoom(true);
            emit changed();
        }
        return;
    }
    if (!m_localReady) { m_obs.reset(pos, paused, now); return; }
    const double speed = m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("mpv.get"), {{"name", QStringLiteral("speed")}}).toDouble();
    const auto ch = m_obs.update(pos, paused, speed, now);
    if (ch.seeked || ch.played || ch.paused) {
        // Local change by the guest: always brought back to the room automatically (no button)
        m_drift.reset();
        setSpeed(1.0);
        if (!m_resyncPending && m_host) m_host->showOsd(text(QStringLiteral("Watch Party: Der Host steuert – zurück zum Raum"), QStringLiteral("Watch Party: the host is in control – back to the room")), 1500);
        m_resyncPending = true;
        m_resyncAt = now + 700;
        return;
    }
    if (m_bestRtt < 0) return;
    if (m_resyncPending && now < m_resyncAt) return;
    if (m_resyncPending) { m_resyncPending = false; syncToRoom(true); return; }
    if (paused != m_state.paused) { playerPause(m_state.paused, pos); return; }
    if (paused || m_buffering || live()) { const auto d = m_drift.update(0, false); if (d.kind == sync::DriftController::SetSpeed) setSpeed(1.0); return; }
    const double target = sync::targetPosition(m_state, serverNow(), duration > 0 ? duration : m_mediaDuration);
    const auto d = m_drift.update(pos - target, true);
    if (d.kind == sync::DriftController::SetSpeed) setSpeed(d.speed);
    else if (d.kind == sync::DriftController::Seek) { setSpeed(1.0); playerSeek(target + 0.1, false); }
}

void WatchPartyController::updateMediaWarning()
{
    QString w;
    if (m_inRoom && m_mediaGen > 0 && m_mode == QLatin1String("own")) {
        const QVariantMap st = playerState();
        const double d = st.value("duration").toDouble();
        if (st.value("path").toString() == m_ownPath && sync::durationMismatch(d, m_mediaDuration))
            w = text(QStringLiteral("Deine Datei ist %1 lang, im Raum %2 – evtl. eine andere Fassung."),
                     QStringLiteral("Your file is %1 long, the room's %2 – possibly another cut.")).arg(formatTime(d), formatTime(m_mediaDuration));
    }
    m_mediaWarning = w;
}

void WatchPartyController::onEvent(const QString &name, const QVariantMap &data)
{
    if (name == QLatin1String("language.changed")) { setLanguage(data.value("code").toString()); return; }
    if (name == QLatin1String("app.quit")) { shutdown(); return; }
    if (!m_inRoom) return;
    if (name == QLatin1String("media.loaded")) {
        const QString path = data.value("path").toString();
        const QString source = data.value("source").toString();
        if (!m_streamUrl.isEmpty() && path == m_streamUrl) {
            m_mode = QStringLiteral("stream");
            setAudioFiles({});     // the external audio track is loaded with this file; the option must not stick
            playerPause(m_state.paused, 0);          // wait for the room position before running
            return;
        }
        if (!isHost() && !m_mediaUrl.isEmpty() && (source == m_mediaUrl || path == m_mediaUrl)) {
            // The room link is playing here (the player may have resolved it to another address)
            m_mode = QStringLiteral("link");
            m_linkPath = path;
            m_linkWatch.stop();
            if (!live()) playerPause(m_state.paused, 0);
            emit changed();
            return;
        }
        m_ownPath = path;
        // Web links are announced as such (guests open them too); local paths never leave the device
        m_ownUrl = source.contains(QLatin1String("://")) && !source.startsWith(QLatin1String("file:")) ? source
                 : path.contains(QLatin1String("://")) && !path.startsWith(QLatin1String("file:")) ? path : QString();
        m_announceAt = m_mono.elapsed();
        m_mode = QStringLiteral("own");
        m_localReady = false;
        m_outOfSync = false;
        if (isHost()) {
            m_announcePending = true;                // sent as new room medium once the duration is known
        } else {
            if (m_mediaGen > 0) playerPause(true, 0); // guest waits for "Ready"
            sendReady();
        }
        updateMediaWarning();
        emit changed();
    } else if (name == QLatin1String("media.ended")) {
        // The room link cannot be opened here (e.g. the site plugin is missing): the host relays it instead
        if (m_mode == QLatin1String("link") && m_linkPath.isEmpty() && data.value("reason").toString() == QLatin1String("error")) linkFailed();
    } else if (name == QLatin1String("media.loading")) {
        const QString url = data.value("url").toString();
        if (url != m_streamUrl && url != m_mediaUrl && m_localReady) { m_localReady = false; sendReady(); emit changed(); }
    } else if (name == QLatin1String("player.stopped")) {
        if (isHost() && m_localReady && !m_state.paused)
            send(QJsonObject{{"type", "action"}, {"gen", double(m_mediaGen)}, {"op", "pause"}, {"position", playerState().value("position").toDouble()}});
        else if (m_localReady) { m_localReady = false; sendReady(); }
        emit changed();
    } else if (name == QLatin1String("playback.paused")) {
        tick();                                      // react to the host's pause without waiting for the poll
    }
}

// ------------------------------------------------------------------ share / swarm

bool WatchPartyController::preparing() const
{
    return m_builder->running();
}

QVariantMap WatchPartyController::share() const
{
    if (m_share.isEmpty()) return {{"active", false}, {"ended", m_shareEndedText}};
    QVariantMap m = m_share.toVariantMap();
    m["active"] = true;
    m["mine"] = m_share.value("sourceId").toInt() == m_myId;
    m["forThisMedium"] = qint64(m_share.value("gen").toDouble()) == m_mediaGen;
    m["durationMismatch"] = sync::durationMismatch(m_share.value("duration").toDouble(), m_mediaDuration);
    return m;
}

void WatchPartyController::shareCurrentFile()
{
    if (!m_inRoom || m_builder->running()) return;
    const QVariantMap st = playerState();
    const QString path = localPath(st.value("path").toString());
    if (!isLocalFile(path)) {
        setStatus(text(QStringLiteral("Öffne zuerst die lokale Videodatei, die du für den Raum streamen möchtest."),
                       QStringLiteral("First open the local video file you want to stream to the room.")), true);
        return;
    }
    m_preparePath = path;
    m_prepareProgress = 0;
    m_builder->start(path);
    setStatus(text(QStringLiteral("Datei wird für die Freigabe vorbereitet …"), QStringLiteral("Preparing the file for sharing …")));
}

void WatchPartyController::cancelPrepare()
{
    m_builder->cancel();
    m_preparePath.clear();
    emit changed();
}

void WatchPartyController::onManifestReady(const QString &path, const Manifest &m)
{
    if (!m_inRoom || path != m_preparePath) return;
    m_preparePath.clear();
    m_seedPath = path;
    m_seedManifest = m;
    const QVariantMap st = playerState();
    const double duration = localPath(st.value("path").toString()) == path ? st.value("duration").toDouble() : m_mediaDuration;
    // Only the file name and hints travel to the room – never the local path
    send(QJsonObject{{"type", "share"}, {"op", "offer"}, {"title", QFileInfo(path).fileName().left(200)}, {"duration", duration},
                     {"size", double(m.size)}, {"blockSize", double(m.blockSize)}, {"manifest", m.digestHex()}});
    setStatus(isHost() ? text(QStringLiteral("Freigabe gestartet."), QStringLiteral("Share started."))
                       : text(QStringLiteral("Freigabe angeboten – der Host muss das Medium bestätigen."), QStringLiteral("Share offered – the host has to confirm the medium.")));
}

void WatchPartyController::stopShare()
{
    if (m_share.isEmpty()) return;
    send(QJsonObject{{"type", "share"}, {"op", "stop"}, {"shareId", m_share.value("id").toString()}});
}

void WatchPartyController::confirmShare()
{
    if (!isHost() || m_share.isEmpty()) return;
    send(QJsonObject{{"type", "share"}, {"op", "confirm"}, {"shareId", m_share.value("id").toString()}});
}

void WatchPartyController::watchStream()
{
    if (!m_inRoom || m_share.isEmpty() || m_share.value("status").toString() != QLatin1String("confirmed")) return;
    if (m_share.value("sourceId").toInt() == m_myId) return;
    m_mode = QStringLiteral("stream");
    m_localReady = false;
    m_outOfSync = false;
    sendReady();                 // announces "stream" so the other swarm members connect to us
    updateSwarm();
    emit changed();
}

QList<int> WatchPartyController::swarmParticipants() const
{
    QList<int> ids;
    const int source = m_share.value("sourceId").toInt();
    for (const auto &v : m_rawMembers) {
        const QJsonObject m = v.toObject();
        const int id = m.value("id").toInt();
        if (id == m_myId || !m.value("connected").toBool()) continue;
        if (id == source || m.value("mode").toString() == QLatin1String("stream")) ids << id;
    }
    return ids;
}

SwarmConfig WatchPartyController::swarmConfig() const
{
    SwarmConfig c;
    const QStringList stun = m_settings.value("stunServers").toString().split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
    if (!stun.isEmpty()) for (const auto &u : stun) c.iceServers << IceServerSpec{u, {}, {}};
    else c.iceServers = {IceServerSpec{QStringLiteral("stun:stun.cloudflare.com:3478"), {}, {}},
                         IceServerSpec{QStringLiteral("stun:stun.l.google.com:19302"), {}, {}}};
    const QString turn = m_settings.value("turnUrl").toString().trimmed();
    if (!turn.isEmpty()) c.iceServers << IceServerSpec{turn, m_settings.value("turnUser").toString(), m_settings.value("turnPassword").toString()};
    c.uploadEnabled = m_settings.value("uploadEnabled").toBool();
    c.uploadLimit = qint64(m_settings.value("uploadLimitKBps").toInt()) * 1024;
    c.cacheBytes = qint64(m_settings.value("cacheMB").toInt()) << 20;
    c.cacheDir = QDir(m_host ? m_host->pluginDataDir(QString::fromLatin1(kPluginId)) : QDir::tempPath()).filePath(QStringLiteral("cache"));
    return c;
}

void WatchPartyController::updateSwarm()
{
    const bool usable = !m_share.isEmpty() && m_share.value("status").toString() == QLatin1String("confirmed")
                        && qint64(m_share.value("gen").toDouble()) == m_mediaGen;
    if (!usable) { if (m_swarm || m_tunnel) stopSwarm(); return; }
    if ((m_swarm && m_swarm->shareId() != m_share.value("id").toString()) || (m_tunnel && m_tunnel->shareId() != m_share.value("id").toString())) stopSwarm();
    const bool mine = m_share.value("sourceId").toInt() == m_myId;
    if (m_share.value("kind").toString() == QLatin1String("tunnel")) {
        // Relayed medium: viewers without a working own copy – or everybody when the host relays all – watch it
        const bool force = m_share.value("force").toBool();
        if (!mine && (m_mode.isEmpty() || m_mode == QLatin1String("nolink") || (force && m_mode != QLatin1String("stream"))
                      || (m_mode == QLatin1String("link") && !m_localReady))) {
            m_mode = QStringLiteral("stream");
            m_localReady = false;
            sendReady();
        }
        if (!m_tunnel) {
            if (mine && m_pendingTunnel.video.isValid()) startTunnel(true);
            else if (!mine && m_mode == QLatin1String("stream")) startTunnel(false);
        }
        if (m_tunnel) m_tunnel->setParticipants(mine ? swarmParticipants() : QList<int>{m_share.value("sourceId").toInt()});
        return;
    }
    // Everybody without an own ready copy watches the room stream automatically (no button needed)
    // (a member who opened an own file for this medium keeps it)
    if (!mine && m_mode.isEmpty()) {
        m_mode = QStringLiteral("stream");
        m_localReady = false;
        m_outOfSync = false;
        sendReady();             // announces "stream" so the other swarm members connect to us
    }
    if (!m_swarm) {
        if (mine && m_seedManifest && m_seedManifest->digestHex() == m_share.value("manifest").toString()) startSwarm(true);
        else if (!mine && m_mode == QLatin1String("stream")) startSwarm(false);
    }
    if (m_swarm) m_swarm->setParticipants(swarmParticipants());
}

void WatchPartyController::startSwarm(bool seed)
{
    const QByteArray digest = QByteArray::fromHex(m_share.value("manifest").toString().toLatin1());
    const qint64 size = qint64(m_share.value("size").toDouble());
    const quint32 bs = quint32(m_share.value("blockSize").toDouble());
    if (digest.size() != 32 || size <= 0 || bs == 0) return;
    m_swarm = new Swarm(m_myId, m_share.value("id").toString(), digest, size, bs, swarmConfig(), this);
    connect(m_swarm, &Swarm::signalOut, this, [this](int to, const QString &kind, const QString &data, const QString &mid) {
        if (!m_swarm) return;
        Q_UNUSED(mid);
        const QString pk = pkOf(to);
        if (!pk.isEmpty()) publish(QJsonObject{{"type", "signal"}, {"to", pk}, {"fromId", m_myId}, {"shareId", m_swarm->shareId()}, {"kind", kind}, {"data", data}});
    });
    connect(m_swarm, &Swarm::statusChanged, this, &WatchPartyController::streamChanged);
    if (seed) {
        if (!m_swarm->setSeed(m_seedPath, *m_seedManifest)) {
            stopSwarm();
            setStatus(text(QStringLiteral("Die freigegebene Datei kann nicht gelesen werden."), QStringLiteral("The shared file cannot be read.")), true);
        }
        return;
    }
    m_server = new StreamServer(m_swarm, m_share.value("title").toString(), this);
    if (!m_server->start()) {
        stopSwarm();
        setStatus(text(QStringLiteral("Lokale Wiedergabeschnittstelle konnte nicht gestartet werden."), QStringLiteral("Local playback interface could not be started.")), true);
        return;
    }
    m_streamUrl = m_server->url();
    m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("player.load"), {{"url", m_streamUrl}});
    setStatus(text(QStringLiteral("Raumstream wird verbunden …"), QStringLiteral("Connecting to the room stream …")));
}

void WatchPartyController::stopSwarm()
{
    // Stops transfers, closes the peer connections of the share and frees its temporary cache
    if (m_server) delete m_server.data();
    if (m_swarm) delete m_swarm.data();
    if (m_tunnel) delete m_tunnel.data();
    setAudioFiles({});
    m_streamUrl.clear();
    emit streamChanged();
}

QVariantMap WatchPartyController::stream() const
{
    if (!m_swarm && !m_tunnel) return {{"active", false}};
    QVariantList peers;
    int direct = 0, relay = 0;
    for (const auto &v : (m_swarm ? m_swarm->peerInfo() : m_tunnel->peerInfo())) {
        QVariantMap p = v.toMap();
        QString nick;
        for (const auto &mv : m_members) if (mv.toMap().value("id").toInt() == p.value("id").toInt()) nick = mv.toMap().value("nick").toString();
        p["nick"] = nick;
        const QString s = p.value("state").toString();
        if (s == QLatin1String("direct")) direct++;
        if (s == QLatin1String("relay")) relay++;
        p["stateText"] = s == QLatin1String("direct") ? text(QStringLiteral("direkt"), QStringLiteral("direct"))
                       : s == QLatin1String("relay") ? text(QStringLiteral("über TURN-Relay"), QStringLiteral("via TURN relay"))
                       : s == QLatin1String("failed") ? text(QStringLiteral("keine Verbindung"), QStringLiteral("no connection"))
                       : s == QLatin1String("disconnected") ? text(QStringLiteral("unterbrochen"), QStringLiteral("interrupted"))
                                                            : text(QStringLiteral("verbindet …"), QStringLiteral("connecting …"));
        peers << p;
    }
    QString stall;
    if (m_tunnel) {
        if (m_tunnel->stall() == QLatin1String("unreachable"))
            stall = text(QStringLiteral("Keine Direktverbindung zum Host möglich (die Netze lassen sie nicht zu, kein TURN-Relay eingerichtet)."),
                         QStringLiteral("No direct connection to the host possible (the networks do not allow it, no TURN relay configured)."));
        return {{"active", true}, {"tunnel", true}, {"seed", m_tunnel->isSource()}, {"percent", 100}, {"peers", peers},
                {"direct", direct}, {"relay", relay}, {"upRate", m_tunnel->uploadRate()}, {"downRate", m_tunnel->downloadRate()},
                {"uploaded", m_tunnel->uploaded()}, {"downloaded", m_tunnel->downloaded()}, {"stall", stall}, {"buffering", m_buffering}};
    }
    if (m_swarm->stall() == QLatin1String("unreachable"))
        stall = text(QStringLiteral("Keine Verbindung zu anderen Teilnehmern möglich: Die Netze lassen keine Direktverbindung zu und es ist kein TURN-Relay eingerichtet. Du kannst eine eigene Datei öffnen."),
                     QStringLiteral("No connection to other members possible: the networks do not allow a direct connection and no TURN relay is configured. You can open your own file."));
    else if (m_swarm->stall() == QLatin1String("missing"))
        stall = text(QStringLiteral("Streamquelle nicht verfügbar – die benötigten Teile hat gerade niemand. Warte auf eine erneute Freigabe oder öffne eine eigene Datei."),
                     QStringLiteral("Stream source unavailable – nobody has the needed parts right now. Wait for a new share or open your own file."));
    const int total = qMax(1, m_swarm->blockCount());
    return {{"active", true}, {"seed", m_swarm->isSeed()}, {"have", m_swarm->haveCount()}, {"total", m_swarm->blockCount()},
            {"percent", m_swarm->isSeed() ? 100 : int(100.0 * m_swarm->haveCount() / total)}, {"peers", peers},
            {"direct", direct}, {"relay", relay}, {"upRate", m_swarm->uploadRate()}, {"downRate", m_swarm->downloadRate()},
            {"uploaded", m_swarm->uploaded()}, {"downloaded", m_swarm->downloaded()}, {"manifest", m_swarm->hasManifest()},
            {"stall", stall}, {"buffering", m_buffering}, {"url", m_streamUrl.isEmpty() ? QVariant() : QVariant(true)}};
}

// ------------------------------------------------------------------ host as source for links (tunnel)

void WatchPartyController::setStreamAll(bool on)
{
    saveSettings({{"streamAll", on}});
    if (on && isHost() && m_share.isEmpty()) startHostShare(true);
    else if (!on && isHost() && m_share.value("force").toBool()) stopShare();
}

void WatchPartyController::setAudioFiles(const QStringList &files)
{
    if (!m_host || (files.isEmpty() && !m_audioFilesSet)) return;
    m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("mpv.set"), {{"name", QStringLiteral("audio-files")}, {"value", files}});
    m_audioFilesSet = !files.isEmpty();
}

void WatchPartyController::linkFailed()
{
    if (m_mode != QLatin1String("link") || m_localReady) return;
    m_mode = QStringLiteral("nolink");
    sendReady();
    setStatus(text(QStringLiteral("Dein Player kann den Link nicht öffnen – der Host streamt ihn dir automatisch …"),
                   QStringLiteral("Your player cannot open the link – the host streams it to you automatically …")));
}

// The host makes its own player's medium available: local file → block swarm; link → tunnel
void WatchPartyController::startHostShare(bool force)
{
    if (!isHost() || m_sharing || (!m_share.isEmpty() && m_share.value("sourceId").toInt() == m_myId)) return;
    const QVariantMap st = playerState();
    const QString path = st.value("path").toString();
    if (path.isEmpty() || (!m_streamUrl.isEmpty() && path == m_streamUrl)) return;
    if (isLocalFile(path)) { if (force) shareCurrentFile(); return; }
    const QUrl url(path);
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        setStatus(text(QStringLiteral("Dieses Medium (%1) kann nicht weitergeleitet werden – die anderen öffnen den Link selbst."),
                       QStringLiteral("This medium (%1) cannot be relayed – the others open the link themselves.")).arg(scheme), true);
        return;
    }
    const auto get = [this](const char *name) { return m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("mpv.get"), {{"name", QString::fromLatin1(name)}}); };
    const QString opened = get("stream-open-filename").toString();
    const bool live = st.value("duration").toDouble() <= 0 || !get("seekable").toBool();
    // Address resolved by a site plugin (e.g. UVEBeeg) or by mpv itself: fetch it exactly like the own player does
    // Page addresses (e.g. a YouTube watch URL that mpv resolves via yt-dlp) are not media themselves
    static const QRegularExpression mediaLike(QStringLiteral("\\.(mp4|m4v|webm|mkv|mov|ts|m2ts|m3u8|m3u|mpd|mp3|m4a|aac|ogg|opus|flac|flv|avi)($|[?#/])"),
                                              QRegularExpression::CaseInsensitiveOption);
    const bool pathIsPage = !m_ownUrl.isEmpty() && path == m_ownUrl && !mediaLike.match(QUrl(path).path()).hasMatch();
    const QString direct = opened.startsWith(QLatin1String("http")) ? opened
                         : (opened.startsWith(QLatin1String("edl://")) || pathIsPage) ? QString() : path;
    if (!direct.isEmpty()) {
        QList<QPair<QByteArray, QByteArray>> headers;
        for (const QVariant &h : get("http-header-fields").toList()) {
            const QString s = h.toString();
            const int c = s.indexOf(QLatin1Char(':'));
            if (c > 0) headers << qMakePair(s.left(c).trimmed().toLatin1(), s.mid(c + 1).trimmed().toLatin1());
        }
        offerTunnel(QUrl(direct), QUrl(), get("user-agent").toString().toLatin1(), get("referrer").toString().toLatin1(), headers, live, force);
        return;
    }
    // YouTube & co. (separate video and audio): ask yt-dlp – the one the player uses – for the real addresses
    QString ytdlp;
    for (const QString &opt : get("script-opts").toString().split(QLatin1Char(',')))
        if (opt.startsWith(QLatin1String("ytdl_hook-ytdl_path="))) ytdlp = opt.mid(20);
    if (ytdlp.isEmpty()) ytdlp = QCoreApplication::applicationDirPath() + QStringLiteral("/tools/yt-dlp/yt-dlp")
#ifdef Q_OS_WIN
                                 + QStringLiteral(".exe")
#endif
        ;
    const QString page = m_ownUrl.isEmpty() ? path : m_ownUrl;
    QString format = get("ytdl-format").toString();
    if (format.isEmpty()) format = QStringLiteral("bv*+ba/b");
    auto *p = new QProcess(this);
    m_sharing = true;
    setStatus(text(QStringLiteral("Stream wird für den Raum vorbereitet …"), QStringLiteral("Preparing the stream for the room …")));
    connect(p, &QProcess::finished, this, [this, p, live, force](int code, QProcess::ExitStatus) {
        p->deleteLater();
        m_sharing = false;
        const QJsonObject j = QJsonDocument::fromJson(p->readAllStandardOutput()).object();
        QUrl video, audio;
        QJsonObject headersObj = j.value("http_headers").toObject();
        const QJsonArray req = j.value("requested_formats").toArray();
        for (const auto &v : req) {
            const QJsonObject f = v.toObject();
            const bool hasVideo = f.value("vcodec").toString() != QLatin1String("none");
            if (hasVideo && !video.isValid()) { video = QUrl(f.value("url").toString()); headersObj = f.value("http_headers").toObject(); }
            else if (!hasVideo && !audio.isValid()) audio = QUrl(f.value("url").toString());
        }
        if (!video.isValid()) video = QUrl(j.value("url").toString());
        if (code != 0 || !video.isValid() || video.isEmpty()) {
            setStatus(text(QStringLiteral("Stream konnte nicht vorbereitet werden (yt-dlp)."), QStringLiteral("The stream could not be prepared (yt-dlp).")), true);
            return;
        }
        QList<QPair<QByteArray, QByteArray>> headers;
        QByteArray ua, referrer;
        for (auto it = headersObj.begin(); it != headersObj.end(); ++it) {
            if (it.key().compare(QLatin1String("User-Agent"), Qt::CaseInsensitive) == 0) ua = it.value().toString().toLatin1();
            else if (it.key().compare(QLatin1String("Referer"), Qt::CaseInsensitive) == 0) referrer = it.value().toString().toLatin1();
            else headers << qMakePair(it.key().toLatin1(), it.value().toString().toLatin1());
        }
        offerTunnel(video, audio, ua, referrer, headers, live || j.value("is_live").toBool(), force);
    });
    p->start(ytdlp, {QStringLiteral("-J"), QStringLiteral("--no-playlist"), QStringLiteral("--no-warnings"), QStringLiteral("-f"), format, page});
}

void WatchPartyController::offerTunnel(const QUrl &video, const QUrl &audio, const QByteArray &ua, const QByteArray &referrer,
                                       const QList<QPair<QByteArray, QByteArray>> &headers, bool live, bool force)
{
    if (!m_inRoom || !isHost()) return;
    m_pendingTunnel = {video, audio, ua, referrer, headers};
    QJsonObject offer{{"type", "share"}, {"op", "offer"}, {"kind", "tunnel"}, {"title", m_mediaTitle}, {"duration", m_mediaDuration},
                      {"entry", Tunnel::entryPathFor(video)}, {"live", live}, {"force", force}};
    if (audio.isValid() && !audio.isEmpty()) offer["audio"] = Tunnel::entryPathFor(audio);
    send(offer);
    setStatus(text(QStringLiteral("Du streamst das Medium an den Raum."), QStringLiteral("You are streaming the medium to the room.")));
}

void WatchPartyController::startTunnel(bool source)
{
    const QString shareId = m_share.value("id").toString();
    if (source) {
        Tunnel::Fetch f{m_pendingTunnel.ua, m_pendingTunnel.referrer, m_pendingTunnel.headers};
        m_tunnel = Tunnel::createSource(m_myId, shareId, m_pendingTunnel.video, f, swarmConfig(), this);
        if (m_pendingTunnel.audio.isValid()) m_tunnel->allowUrl(m_pendingTunnel.audio);
    } else {
        m_tunnel = Tunnel::createViewer(m_myId, shareId, m_share.value("sourceId").toInt(), m_share.value("entry").toString(), swarmConfig(), this);
        if (!m_tunnel) {
            setStatus(text(QStringLiteral("Lokale Wiedergabeschnittstelle konnte nicht gestartet werden."), QStringLiteral("Local playback interface could not be started.")), true);
            return;
        }
    }
    connect(m_tunnel, &Tunnel::signalOut, this, [this](int to, const QString &kind, const QString &data) {
        if (!m_tunnel) return;
        const QString pk = pkOf(to);
        if (!pk.isEmpty()) publish(QJsonObject{{"type", "signal"}, {"to", pk}, {"fromId", m_myId}, {"shareId", m_tunnel->shareId()}, {"kind", kind}, {"data", data}});
    });
    connect(m_tunnel, &Tunnel::statusChanged, this, &WatchPartyController::streamChanged);
    if (source) return;
    // Separate audio (YouTube & co.): added as external audio track of the relayed video
    const QString audio = m_share.value("audio").toString();
    if (!audio.isEmpty()) setAudioFiles({m_tunnel->urlFor(audio)});
    m_streamUrl = m_tunnel->url();
    m_host->call(QString::fromLatin1(kPluginId), QStringLiteral("player.load"), {{"url", m_streamUrl}});
    setStatus(text(QStringLiteral("Stream des Hosts wird verbunden …"), QStringLiteral("Connecting to the host's stream …")));
}

// ------------------------------------------------------------------ lifecycle

void WatchPartyController::setActive(bool active)
{
    m_active = active;
}

void WatchPartyController::shutdown()
{
    if (m_inRoom) {
        send(QJsonObject{{"type", "leave"}});
        endSession(QString(), false);
    }
    m_builder->cancel();
    stopSwarm();
}

} // namespace uve::watchparty
