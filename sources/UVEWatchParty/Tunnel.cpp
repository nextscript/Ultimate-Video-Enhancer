#include "Tunnel.h"

#include <rtc/rtc.hpp>

#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <mutex>

namespace uve::watchparty {

namespace {
enum : quint8 { REQ = 1, HEAD = 2, DATA = 3, END = 4, FAIL = 5, CANCEL = 6, CREDIT = 7 };
constexpr int kFragment = 16384;
constexpr qint64 kHighWater = 1 << 20;
constexpr qint64 kWindow = 4 << 20;               // bytes a viewer allows in flight per request
constexpr qint64 kManifestMax = 4 << 20;
const QByteArray kBase("@@UVEWP_BASE@@");

QByteArray hdr(quint8 type, quint32 id) { QByteArray m(5, Qt::Uninitialized); m[0] = char(type); qToBigEndian<quint32>(id, m.data() + 1); return m; }
quint64 key(int peer, quint32 id) { return (quint64(quint32(peer)) << 32) | id; }

QString originOf(const QUrl &u)
{
    QString o = u.scheme().toLower() + QStringLiteral("://") + u.host().toLower();
    if (u.port() > 0) o += QLatin1Char(':') + QString::number(u.port());
    return o;
}

bool privateHost(const QString &host)
{
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0) return true;
    QHostAddress a(host);
    if (a.isNull()) return false;
    return a.isLoopback() || a.isLinkLocal() || a.isSiteLocal() || a.isUniqueLocalUnicast()
           || a.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")), 8) || a.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12)
           || a.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")), 16) || a.isInSubnet(QHostAddress(QStringLiteral("169.254.0.0")), 16);
}

QString tunnelRef(const QUrl &abs, QSet<QString> *origins)
{
    const QString origin = originOf(abs);
    if (origins) origins->insert(origin);
    QString rest = abs.path(QUrl::FullyEncoded);
    if (rest.isEmpty()) rest = QStringLiteral("/");
    if (abs.hasQuery()) rest += QLatin1Char('?') + abs.query(QUrl::FullyEncoded);
    return QString::fromLatin1(kBase) + QStringLiteral("/o/") + QString::fromLatin1(origin.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)) + rest;
}

bool isManifest(const QByteArray &contentType, const QUrl &url, bool *dash)
{
    const QByteArray ct = contentType.toLower();
    const QString p = url.path().toLower();
    *dash = ct.contains("dash+xml") || p.endsWith(QLatin1String(".mpd"));
    return *dash || ct.contains("mpegurl") || p.endsWith(QLatin1String(".m3u8")) || p.endsWith(QLatin1String(".m3u"));
}
}

class TunnelRelay
{
public:
    explicit TunnelRelay(Tunnel *t) : m_target(t) {}
    void post(std::function<void(Tunnel *)> fn)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_target) return;
        Tunnel *t = m_target;
        QMetaObject::invokeMethod(t, [t, fn = std::move(fn)] { fn(t); }, Qt::QueuedConnection);
    }
    void detach() { std::lock_guard<std::mutex> lock(m_mutex); m_target = nullptr; }

private:
    std::mutex m_mutex;
    Tunnel *m_target;
};

// ------------------------------------------------------------------ construction

Tunnel::Tunnel(int myId, const QString &shareId, bool source, const SwarmConfig &cfg, QObject *parent)
    : QObject(parent), m_relay(std::make_shared<TunnelRelay>(this)), m_myId(myId), m_shareId(shareId), m_source(source), m_cfg(cfg)
{
    m_clock.start();
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &Tunnel::tick);
    m_tick.start();
}

Tunnel *Tunnel::createSource(int myId, const QString &shareId, const QUrl &entryUrl, const Fetch &fetch, const SwarmConfig &cfg, QObject *parent)
{
    auto *t = new Tunnel(myId, shareId, true, cfg, parent);
    t->m_fetch = fetch;
    t->m_nam = new QNetworkAccessManager(t);
    t->m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    t->m_origins.insert(originOf(entryUrl));
    return t;
}

Tunnel *Tunnel::createViewer(int myId, const QString &shareId, int sourceId, const QString &entryPath, const SwarmConfig &cfg, QObject *parent)
{
    auto *t = new Tunnel(myId, shareId, false, cfg, parent);
    t->m_sourceId = sourceId;
    if (!t->startServer(entryPath)) { delete t; return nullptr; }
    return t;
}

Tunnel::~Tunnel()
{
    m_relay->detach();
    for (auto &u : m_upstreams) if (u.reply) { u.reply->disconnect(this); u.reply->abort(); u.reply->deleteLater(); }
    for (auto it = m_local.begin(); it != m_local.end(); ++it) { it.key()->disconnect(this); it.key()->abort(); it.key()->deleteLater(); }
    for (auto &p : m_peers) {
        try {
            if (p.dc) { p.dc->resetCallbacks(); p.dc->close(); }
            if (p.pc) { p.pc->resetCallbacks(); p.pc->close(); }
        } catch (...) {}
    }
}

void Tunnel::allowUrl(const QUrl &url)
{
    m_origins.insert(originOf(url));
}

QString Tunnel::entryPathFor(const QUrl &url)
{
    return tunnelRef(url, nullptr).mid(kBase.size());
}

// ------------------------------------------------------------------ manifests

QByteArray Tunnel::rewritePlaylist(const QByteArray &body, const QUrl &base, QSet<QString> *origins)
{
    static const QRegularExpression uriAttr(QStringLiteral("URI=\"([^\"]+)\""));
    QStringList out;
    for (const QString &line : QString::fromUtf8(body).split(QLatin1Char('\n'))) {
        QString l = line;
        if (l.endsWith(QLatin1Char('\r'))) l.chop(1);
        if (l.trimmed().isEmpty()) { out << l; continue; }
        if (l.startsWith(QLatin1Char('#'))) {
            QString r;
            qsizetype last = 0;
            auto it = uriAttr.globalMatch(l);
            while (it.hasNext()) {
                const auto m = it.next();
                r += l.mid(last, m.capturedStart(1) - last) + tunnelRef(base.resolved(QUrl(m.captured(1))), origins);
                last = m.capturedEnd(1);
            }
            out << r + l.mid(last);
            continue;
        }
        out << tunnelRef(base.resolved(QUrl(l.trimmed())), origins);
    }
    return out.join(QLatin1Char('\n')).toUtf8();
}

QByteArray Tunnel::rewriteDash(const QByteArray &body, QSet<QString> *origins)
{
    // Absolute addresses inside the manifest; relative ones resolve inside the tunnel by themselves
    static const QRegularExpression abs(QStringLiteral("https?://[^\\s\"'<>]+"));
    QString s = QString::fromUtf8(body), r;
    qsizetype last = 0;
    auto it = abs.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        r += s.mid(last, m.capturedStart() - last) + tunnelRef(QUrl(m.captured().replace(QLatin1String("&amp;"), QLatin1String("&"))), origins).replace(QLatin1Char('&'), QLatin1String("&amp;"));
        last = m.capturedEnd();
    }
    return (r + s.mid(last)).toUtf8();
}

// ------------------------------------------------------------------ peers (same pattern as the swarm)

Tunnel::Peer *Tunnel::peer(int id, quint64 epoch)
{
    auto it = m_peers.find(id);
    return it != m_peers.end() && it->epoch == epoch ? &*it : nullptr;
}

static std::string iceUrlOf(const IceServerSpec &s)
{
    QString url = s.url;
    if (!s.username.isEmpty() && (url.startsWith(QLatin1String("turn:")) || url.startsWith(QLatin1String("turns:")))) {
        const int c = url.indexOf(u':');
        url = url.left(c + 1) + QString::fromLatin1(QUrl::toPercentEncoding(s.username)) + u':' + QString::fromLatin1(QUrl::toPercentEncoding(s.credential)) + u'@' + url.mid(c + 1);
    }
    return url.toStdString();
}

Tunnel::Peer *Tunnel::createPeer(int id, bool initiator)
{
    removePeer(id);
    rtc::Configuration c;
    for (const auto &s : m_cfg.iceServers) { try { c.iceServers.emplace_back(iceUrlOf(s)); } catch (...) {} }
    Peer p;
    p.id = id;
    p.epoch = m_nextEpoch++;
    p.initiator = initiator;
    p.createdAt = m_clock.elapsed();
    try { p.pc = std::make_shared<rtc::PeerConnection>(c); } catch (...) { return nullptr; }
    std::weak_ptr<TunnelRelay> relay = m_relay;
    const quint64 ep = p.epoch;
    p.pc->onStateChange([relay, id, ep](rtc::PeerConnection::State s) {
        if (auto r = relay.lock()) r->post([id, ep, s](Tunnel *t) { t->handleState(id, ep, int(s)); });
    });
    std::weak_ptr<rtc::PeerConnection> weakPc = p.pc;
    p.pc->onGatheringStateChange([relay, id, ep, weakPc](rtc::PeerConnection::GatheringState s) {
        if (s != rtc::PeerConnection::GatheringState::Complete) return;
        auto pc = weakPc.lock();
        if (!pc) return;
        const auto d = pc->localDescription();
        if (!d) return;
        const QString type = QString::fromStdString(d->typeString()), sdp = QString::fromStdString(std::string(*d));
        if (auto r = relay.lock()) r->post([id, ep, type, sdp](Tunnel *t) { t->handleDescription(id, ep, type, sdp); });
    });
    p.pc->onDataChannel([relay, id, ep](std::shared_ptr<rtc::DataChannel> dc) {
        if (auto r = relay.lock()) r->post([id, ep, dc](Tunnel *t) { t->handleChannel(id, ep, dc); });
    });
    auto &stored = m_peers[id] = std::move(p);
    if (initiator) { try { attach(stored, stored.pc->createDataChannel("uvewp-tunnel")); } catch (...) { stored.state = QStringLiteral("failed"); } }
    emit statusChanged();
    return &stored;
}

void Tunnel::attach(Peer &p, std::shared_ptr<rtc::DataChannel> dc)
{
    p.dc = dc;
    std::weak_ptr<TunnelRelay> relay = m_relay;
    const int id = p.id;
    const quint64 ep = p.epoch;
    dc->setBufferedAmountLowThreshold(256 * 1024);
    dc->onOpen([relay, id, ep] { if (auto r = relay.lock()) r->post([id, ep](Tunnel *t) { t->handleOpen(id, ep); }); });
    dc->onClosed([relay, id, ep] { if (auto r = relay.lock()) r->post([id, ep](Tunnel *t) { t->handleClosed(id, ep); }); });
    dc->onBufferedAmountLow([relay, id, ep] { if (auto r = relay.lock()) r->post([id, ep](Tunnel *t) { t->handleLow(id, ep); }); });
    dc->onMessage([relay, id, ep](rtc::binary data) {
        QByteArray m(reinterpret_cast<const char *>(data.data()), qsizetype(data.size()));
        if (auto r = relay.lock()) r->post([id, ep, m](Tunnel *t) { t->handleMessage(id, ep, m); });
    }, [](rtc::string) {});
    if (dc->isOpen()) handleOpen(id, ep);
}

void Tunnel::setParticipants(const QList<int> &ids)
{
    const QSet<int> want(ids.begin(), ids.end());
    for (const int id : m_peers.keys()) if (!want.contains(id)) removePeer(id);
    for (const int id : want) if (id != m_myId && !m_peers.contains(id) && m_myId < id) createPeer(id, true);
}

void Tunnel::removePeer(int memberId)
{
    auto it = m_peers.find(memberId);
    if (it == m_peers.end()) return;
    for (auto u = m_upstreams.begin(); u != m_upstreams.end();) {
        if (u->peer == memberId) { if (u->reply) { u->reply->disconnect(this); u->reply->abort(); u->reply->deleteLater(); } u = m_upstreams.erase(u); }
        else ++u;
    }
    try {
        if (it->dc) { it->dc->resetCallbacks(); it->dc->close(); }
        if (it->pc) { it->pc->resetCallbacks(); it->pc->close(); }
    } catch (...) {}
    m_peers.erase(it);
    emit statusChanged();
}

void Tunnel::onSignal(int from, const QString &kind, const QString &data)
{
    if (from == m_myId) return;
    try {
        if (kind == QLatin1String("offer")) {
            Peer *p = m_peers.contains(from) ? &m_peers[from] : nullptr;
            if (!p || p->remoteSet || p->initiator) p = createPeer(from, false);
            if (!p) return;
            p->pc->setRemoteDescription(rtc::Description(data.toStdString(), "offer"));
            p->remoteSet = true;
        } else if (kind == QLatin1String("answer")) {
            auto it = m_peers.find(from);
            if (it == m_peers.end() || !it->initiator || it->remoteSet) return;
            it->pc->setRemoteDescription(rtc::Description(data.toStdString(), "answer"));
            it->remoteSet = true;
        } else if (kind == QLatin1String("bye")) {
            removePeer(from);
        }
    } catch (...) {
        if (m_peers.contains(from)) { m_peers[from].state = QStringLiteral("failed"); emit statusChanged(); }
    }
}

void Tunnel::handleState(int id, quint64 epoch, int state)
{
    Peer *p = peer(id, epoch);
    if (!p) return;
    using S = rtc::PeerConnection::State;
    if (S(state) == S::Connected) {
        rtc::Candidate l, r;
        try { if (p->pc->getSelectedCandidatePair(&l, &r)) p->relay = l.type() == rtc::Candidate::Type::Relayed || r.type() == rtc::Candidate::Type::Relayed; } catch (...) {}
        p->state = p->relay ? QStringLiteral("relay") : QStringLiteral("direct");
    } else if (S(state) == S::Failed) {
        p->state = QStringLiteral("failed");
        p->open = false;
    } else if (S(state) == S::Disconnected) {
        p->state = QStringLiteral("disconnected");
    }
    emit statusChanged();
}

void Tunnel::handleDescription(int id, quint64 epoch, const QString &type, const QString &sdp)
{
    if (peer(id, epoch)) emit signalOut(id, type, sdp);
}

void Tunnel::handleChannel(int id, quint64 epoch, std::shared_ptr<rtc::DataChannel> dc)
{
    Peer *p = peer(id, epoch);
    if (p && !p->dc && dc->label() == "uvewp-tunnel") attach(*p, dc);
}

void Tunnel::handleOpen(int id, quint64 epoch)
{
    Peer *p = peer(id, epoch);
    if (!p || p->open) return;
    p->open = true;
    emit statusChanged();
    // Viewer: requests that waited for the connection go out now
    if (!m_source) for (auto it = m_local.begin(); it != m_local.end(); ++it) if (!it->sent && it->id) onLocalData(it.key());
}

void Tunnel::handleClosed(int id, quint64 epoch)
{
    if (Peer *p = peer(id, epoch)) { p->open = false; p->state = QStringLiteral("closed"); emit statusChanged(); }
}

void Tunnel::handleLow(int id, quint64 epoch)
{
    if (!peer(id, epoch)) return;
    for (auto it = m_upstreams.begin(); it != m_upstreams.end(); ++it) if (it->peer == id) pumpUpstream(it.key());
}

void Tunnel::send(int peerId, const QByteArray &m)
{
    auto it = m_peers.find(peerId);
    if (it == m_peers.end() || !it->open || !it->dc) return;
    try { it->dc->send(reinterpret_cast<const std::byte *>(m.constData()), size_t(m.size())); } catch (...) {}
}

size_t Tunnel::buffered(int peerId) const
{
    auto it = m_peers.find(peerId);
    if (it == m_peers.end() || !it->dc) return 0;
    try { return it->dc->bufferedAmount(); } catch (...) { return 0; }
}

void Tunnel::handleMessage(int id, quint64 epoch, const QByteArray &m)
{
    if (!peer(id, epoch) || m.size() < 5) return;
    const quint8 type = quint8(m.at(0));
    const quint32 rid = qFromBigEndian<quint32>(m.constData() + 1);
    const QByteArray payload = m.mid(5);
    if (m_source) {
        if (type == REQ) onRequest(id, rid, QJsonDocument::fromJson(payload).object());
        else if (type == CANCEL) finishUpstream(key(id, rid), false);
        else if (type == CREDIT && payload.size() >= 4) {
            auto it = m_upstreams.find(key(id, rid));
            if (it != m_upstreams.end()) { it->credit += qFromBigEndian<quint32>(payload.constData()); pumpUpstream(key(id, rid)); }
        }
        return;
    }
    // viewer
    if (id != m_sourceId) return;
    QTcpSocket *s = m_byId.value(rid);
    if (!s || !m_local.contains(s)) return;
    Local &l = m_local[s];
    if (type == HEAD) {
        const QJsonObject h = QJsonDocument::fromJson(payload).object();
        l.headSeen = true;
        l.rewrite = h.value("x").toBool();
        const int status = h.value("s").toInt(502);
        QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " " + (status == 200 ? "OK" : status == 206 ? "Partial Content" : status == 416 ? "Range Not Satisfiable" : "Status") + "\r\n";
        const QJsonObject hs = h.value("h").toObject();
        for (auto it = hs.begin(); it != hs.end(); ++it) {
            if (l.rewrite && it.key().compare(QLatin1String("Content-Length"), Qt::CaseInsensitive) == 0) continue;
            out += it.key().toLatin1() + ": " + it.value().toString().toLatin1() + "\r\n";
        }
        out += "Cache-Control: no-store\r\nConnection: close\r\n";
        if (!l.rewrite) { out += "\r\n"; s->write(out); }
        else l.head = out;       // manifest: length is known after the base address is filled in
    } else if (type == DATA) {
        m_down += payload.size();
        if (l.rewrite) { l.body += payload; return; }
        s->write(payload);
        l.written += payload.size();
    } else if (type == END) {
        if (l.rewrite) {
            const QByteArray body = l.body.replace(kBase, m_base.toUtf8());
            s->write(l.head + "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
        }
        m_byId.remove(rid);
        l.id = 0;
        s->disconnectFromHost();
    } else if (type == FAIL) {
        if (!l.headSeen) s->write("HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        m_byId.remove(rid);
        l.id = 0;
        s->disconnectFromHost();
    }
}

// ------------------------------------------------------------------ source: fetch from the origin

QUrl Tunnel::resolvePath(const QString &path) const
{
    // "/o/<base64url(origin)>/rest" → origin + rest, only for allowed origins
    if (!path.startsWith(QLatin1String("/o/"))) return {};
    const qsizetype slash = path.indexOf(QLatin1Char('/'), 3);
    const QString enc = slash < 0 ? path.mid(3) : path.mid(3, slash - 3);
    const QString origin = QString::fromUtf8(QByteArray::fromBase64(enc.toLatin1(), QByteArray::Base64UrlEncoding));
    if (!m_origins.contains(origin)) return {};
    const QUrl u(origin + (slash < 0 ? QStringLiteral("/") : path.mid(slash)), QUrl::StrictMode);
    if (!u.isValid() || (u.scheme() != QLatin1String("http") && u.scheme() != QLatin1String("https"))) return {};
    if (privateHost(u.host()) && !m_origins.contains(originOf(u))) return {};
    return u;
}

void Tunnel::onRequest(int peerId, quint32 id, const QJsonObject &req)
{
    const QUrl url = resolvePath(req.value("p").toString());
    if (!url.isValid() || url.isEmpty()) {
        QByteArray m = hdr(FAIL, id);
        m += QJsonDocument(QJsonObject{{"e", "not part of the shared medium"}}).toJson(QJsonDocument::Compact);
        send(peerId, m);
        return;
    }
    QNetworkRequest r(url);
    if (!m_fetch.userAgent.isEmpty()) r.setRawHeader("User-Agent", m_fetch.userAgent);
    if (!m_fetch.referrer.isEmpty()) r.setRawHeader("Referer", m_fetch.referrer);
    for (const auto &h : m_fetch.headers) r.setRawHeader(h.first, h.second);
    const QString range = req.value("r").toString();
    if (!range.isEmpty()) r.setRawHeader("Range", range.toLatin1());
    r.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    QNetworkReply *reply = req.value("m").toString() == QLatin1String("HEAD") ? m_nam->head(r) : m_nam->get(r);
    reply->setReadBufferSize(2 << 20);            // backpressure: the network is only read as fast as the viewer takes it
    const quint64 k = key(peerId, id);
    Upstream u;
    u.peer = peerId;
    u.id = id;
    u.reply = reply;
    u.url = url;
    m_upstreams.insert(k, u);
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, k] {
        auto it = m_upstreams.find(k);
        if (it == m_upstreams.end() || !it->reply) return;
        bool dash = false;
        it->rewrite = isManifest(it->reply->header(QNetworkRequest::ContentTypeHeader).toByteArray(), it->reply->url(), &dash);
        if (it->rewrite) return;                  // head is sent with the rewritten body
        QJsonObject hs;
        for (const char *name : {"Content-Type", "Content-Length", "Content-Range", "Accept-Ranges"}) {
            const QByteArray v = it->reply->rawHeader(name);
            if (!v.isEmpty()) hs.insert(QString::fromLatin1(name), QString::fromLatin1(v));
        }
        QByteArray m = hdr(HEAD, it->id);
        m += QJsonDocument(QJsonObject{{"s", it->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()}, {"h", hs}}).toJson(QJsonDocument::Compact);
        send(it->peer, m);
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, k] { pumpUpstream(k); });
    connect(reply, &QNetworkReply::finished, this, [this, k] {
        auto it = m_upstreams.find(k);
        if (it == m_upstreams.end() || !it->reply) return;
        const bool ok = it->reply->error() == QNetworkReply::NoError || it->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400;
        pumpUpstream(k);
        if (m_upstreams.contains(k) && (!it->reply || it->reply->bytesAvailable() == 0 || it->rewrite)) finishUpstream(k, ok, it->reply ? it->reply->errorString() : QString());
    });
}

void Tunnel::pumpUpstream(quint64 k)
{
    auto it = m_upstreams.find(k);
    if (it == m_upstreams.end() || !it->reply) return;
    QNetworkReply *reply = it->reply;
    if (it->rewrite) {
        it->body += reply->readAll();
        if (it->body.size() > kManifestMax) { finishUpstream(k, false, QStringLiteral("manifest too large")); return; }
        if (!reply->isFinished()) return;
        bool dash = false;
        isManifest(reply->header(QNetworkRequest::ContentTypeHeader).toByteArray(), reply->url(), &dash);
        const QByteArray body = dash ? rewriteDash(it->body, &m_origins) : rewritePlaylist(it->body, reply->url(), &m_origins);
        QJsonObject hs{{"Content-Type", QString::fromLatin1(reply->rawHeader("Content-Type").isEmpty() ? QByteArray(dash ? "application/dash+xml" : "application/vnd.apple.mpegurl") : reply->rawHeader("Content-Type"))}};
        QByteArray m = hdr(HEAD, it->id);
        m += QJsonDocument(QJsonObject{{"s", reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()}, {"h", hs}, {"x", true}}).toJson(QJsonDocument::Compact);
        send(it->peer, m);
        for (qsizetype off = 0; off < body.size(); off += kFragment) send(it->peer, hdr(DATA, it->id) + body.mid(off, kFragment));
        m_up += body.size();
        finishUpstream(k, true);
        return;
    }
    // Media bytes within the viewer's credit and while the channel is not congested
    while (it->credit > 0 && reply->bytesAvailable() > 0 && buffered(it->peer) < size_t(kHighWater)) {
        const QByteArray d = reply->read(qMin<qint64>(kFragment, it->credit));
        if (d.isEmpty()) break;
        send(it->peer, hdr(DATA, it->id) + d);
        it->credit -= d.size();
        m_up += d.size();
    }
    if (reply->isFinished() && reply->bytesAvailable() == 0) finishUpstream(k, true);
}

void Tunnel::finishUpstream(quint64 k, bool ok, const QString &error)
{
    auto it = m_upstreams.find(k);
    if (it == m_upstreams.end()) return;
    const Upstream u = *it;
    m_upstreams.erase(it);
    if (u.reply) { u.reply->disconnect(this); if (!u.reply->isFinished()) u.reply->abort(); u.reply->deleteLater(); }
    if (ok) send(u.peer, hdr(END, u.id));
    else if (!error.isEmpty()) { QByteArray m = hdr(FAIL, u.id); m += QJsonDocument(QJsonObject{{"e", error.left(200)}}).toJson(QJsonDocument::Compact); send(u.peer, m); }
}

// ------------------------------------------------------------------ viewer: local HTTP for libmpv

bool Tunnel::startServer(const QString &entryPath)
{
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) return false;
    QByteArray k(16, Qt::Uninitialized);
    for (auto &c : k) c = char(QRandomGenerator::system()->bounded(256));
    m_key = QString::fromLatin1(k.toHex());
    m_base = QStringLiteral("http://127.0.0.1:%1/%2").arg(m_server->serverPort()).arg(m_key);
    m_url = m_base + entryPath;
    connect(m_server, &QTcpServer::newConnection, this, &Tunnel::onLocalConnection);
    return true;
}

void Tunnel::onLocalConnection()
{
    while (QTcpSocket *s = m_server->nextPendingConnection()) {
        if (m_local.size() >= 24 || !s->peerAddress().isLoopback()) { s->abort(); s->deleteLater(); continue; }
        Local l;
        l.sock = s;
        l.since = m_clock.elapsed();
        m_local.insert(s, l);
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onLocalData(s); });
        connect(s, &QTcpSocket::bytesWritten, this, [this, s] { if (m_local.contains(s)) grantCredit(m_local[s]); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] { localDone(s); });
    }
}

void Tunnel::onLocalData(QTcpSocket *s)
{
    auto it = m_local.find(s);
    if (it == m_local.end()) return;
    Local &l = *it;
    if (l.sent) { s->readAll(); return; }
    l.head += s->readAll();
    if (l.head.size() > 16384) { localDone(s); return; }
    const int end = l.head.indexOf("\r\n\r\n");
    if (end < 0) return;
    const QList<QByteArray> lines = l.head.left(end).split('\n');
    const QList<QByteArray> rl = lines.value(0).trimmed().split(' ');
    const QByteArray method = rl.value(0);
    QString path = QString::fromLatin1(rl.value(1));
    QByteArray range;
    for (int i = 1; i < lines.size(); ++i) {
        const int c = lines.at(i).indexOf(':');
        if (c > 0 && lines.at(i).left(c).trimmed().toLower() == "range") range = lines.at(i).mid(c + 1).trimmed();
    }
    const QString prefix = QLatin1Char('/') + m_key;
    if (!path.startsWith(prefix + QLatin1Char('/')) || (method != "GET" && method != "HEAD")) {
        s->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        s->disconnectFromHost();
        l.sent = true;
        return;
    }
    if (!l.id) { l.id = m_nextId++; m_byId.insert(l.id, s); }
    auto src = m_peers.find(m_sourceId);
    if (src == m_peers.end() || !src->open) return;          // sent once the connection to the source is open
    l.sent = true;
    l.granted = kWindow;
    QByteArray m = hdr(REQ, l.id);
    m += QJsonDocument(QJsonObject{{"p", path.mid(prefix.size())}, {"m", QString::fromLatin1(method)}, {"r", QString::fromLatin1(range)}}).toJson(QJsonDocument::Compact);
    send(m_sourceId, m);
    QByteArray c = hdr(CREDIT, l.id);
    c.resize(9);
    qToBigEndian<quint32>(quint32(kWindow), c.data() + 5);
    send(m_sourceId, c);
}

// libmpv took data from the socket: allow the source to send the same amount again
void Tunnel::grantCredit(Local &l)
{
    if (!l.id || l.rewrite) return;
    const qint64 inFlight = l.granted - l.written + l.sock->bytesToWrite();
    const qint64 more = kWindow - inFlight;
    if (more < kWindow / 4) return;
    QByteArray c = hdr(CREDIT, l.id);
    c.resize(9);
    qToBigEndian<quint32>(quint32(more), c.data() + 5);
    l.granted += more;
    send(m_sourceId, c);
}

void Tunnel::localDone(QTcpSocket *s)
{
    auto it = m_local.find(s);
    if (it == m_local.end()) return;
    if (it->id) { send(m_sourceId, hdr(CANCEL, it->id)); m_byId.remove(it->id); }
    m_local.erase(it);
    s->disconnect(this);
    s->abort();
    s->deleteLater();
}

void Tunnel::tick()
{
    const qint64 now = m_clock.elapsed();
    if (now - m_rateAt >= 1000) {
        const double dt = double(now - m_rateAt) / 1000.0;
        m_upRate = double(m_up - m_upMark) / dt;
        m_downRate = double(m_down - m_downMark) / dt;
        m_upMark = m_up; m_downMark = m_down; m_rateAt = now;
    }
    QString s;
    if (!m_source) {
        auto src = m_peers.find(m_sourceId);
        const bool open = src != m_peers.end() && src->open;
        if (!open && src != m_peers.end() && (src->state == QLatin1String("failed") || now - src->createdAt > 20000)) s = QStringLiteral("unreachable");
        else if (src == m_peers.end() && now > 20000) s = QStringLiteral("unreachable");
        // Requests without answer for a long time
        for (auto it = m_local.begin(); it != m_local.end(); ++it)
            if (!it->headSeen && now - it->since > 30000) { it->sock->write("HTTP/1.1 504 Gateway Timeout\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); it->sock->disconnectFromHost(); it->headSeen = true; }
    }
    // Failed connections: the offering side tries again after 30 s
    for (const int id : m_peers.keys()) {
        const Peer &p = m_peers[id];
        if (p.state == QLatin1String("failed") && now - p.createdAt > 30000 && p.initiator) createPeer(id, true);
    }
    m_stall = s;
    emit statusChanged();
}

QVariantList Tunnel::peerInfo() const
{
    QVariantList out;
    for (const auto &p : m_peers)
        out << QVariantMap{{"id", p.id}, {"state", p.state}, {"relay", p.relay}, {"seed", p.id == m_sourceId}, {"open", p.open}, {"blocks", 0}, {"up", 0}, {"down", 0}};
    return out;
}

} // namespace uve::watchparty
