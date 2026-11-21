#include "Swarm.h"

#include <rtc/rtc.hpp>

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QUrl>
#include <QtEndian>
#include <algorithm>
#include <mutex>

namespace uve::watchparty {

namespace {
constexpr quint8 kVersion = 1;
enum : quint8 { HELLO = 1, MREQ = 2, MPART = 3, BITS = 4, HAVE = 5, WANT = 6, PIECE = 7, REJECT = 8, CANCEL = 9, DROP = 10 };
constexpr int kFragment = 16384;             // data per message (small fragments, works with every SCTP stack)
constexpr qint64 kHighWater = 1 << 20;       // stop sending above 1 MiB buffered per channel
constexpr int kQueuePerPeer = 4;             // blocks queued for upload per peer
constexpr int kInflightPerPeer = 4;
constexpr int kInflightTotal = 16;
constexpr int kUrgentBlocks = 6;
constexpr int kAheadBlocks = 30;
constexpr qint64 kUrgentTimeout = 5000;
constexpr qint64 kNormalTimeout = 10000;
constexpr qint64 kManifestMax = 2 << 20;

QByteArray u32(quint32 v) { QByteArray b(4, Qt::Uninitialized); qToBigEndian<quint32>(v, b.data()); return b; }
quint32 rd32(const QByteArray &b, int at) { return qFromBigEndian<quint32>(b.constData() + at); }
QByteArray msg1(quint8 type, quint32 v) { QByteArray m(1, char(type)); m += u32(v); return m; }
quint64 banKey(int block, int peer) { return (quint64(quint32(block)) << 24) | quint32(peer & 0xffffff); }
}

// Bridges libdatachannel threads to the Swarm's thread; detach() under the mutex makes late callbacks no-ops
class SwarmRelay
{
public:
    explicit SwarmRelay(Swarm *s) : m_target(s) {}
    void post(std::function<void(Swarm *)> fn)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_target) return;
        Swarm *t = m_target;
        QMetaObject::invokeMethod(t, [t, fn = std::move(fn)] { fn(t); }, Qt::QueuedConnection);
    }
    void detach() { std::lock_guard<std::mutex> lock(m_mutex); m_target = nullptr; }

private:
    std::mutex m_mutex;
    Swarm *m_target;
};

QList<IceServerSpec> parseIceServers(const QVariantList &servers)
{
    QList<IceServerSpec> out;
    for (const QVariant &v : servers) {
        const QVariantMap m = v.toMap();
        QStringList urls = m.value(QStringLiteral("urls")).toStringList();
        if (urls.isEmpty() && m.value(QStringLiteral("urls")).typeId() == QMetaType::QString) urls << m.value(QStringLiteral("urls")).toString();
        for (const QString &u : urls)
            if (!u.trimmed().isEmpty())
                out << IceServerSpec{u.trimmed(), m.value(QStringLiteral("username")).toString(), m.value(QStringLiteral("credential")).toString()};
    }
    return out;
}

static std::string iceUrl(const IceServerSpec &s)
{
    QString url = s.url;
    if (!s.username.isEmpty() && (url.startsWith(QLatin1String("turn:")) || url.startsWith(QLatin1String("turns:")))) {
        const int c = url.indexOf(u':');
        url = url.left(c + 1) + QString::fromLatin1(QUrl::toPercentEncoding(s.username)) + u':'
              + QString::fromLatin1(QUrl::toPercentEncoding(s.credential)) + u'@' + url.mid(c + 1);
    }
    return url.toStdString();
}

Swarm::Swarm(int myId, const QString &shareId, const QByteArray &digest, qint64 size, quint32 blockSize,
             const SwarmConfig &cfg, QObject *parent)
    : QObject(parent), m_relay(std::make_shared<SwarmRelay>(this)), m_myId(myId), m_shareId(shareId), m_digest(digest),
      m_size(size), m_blockSize(blockSize), m_count(int((size + blockSize - 1) / blockSize)), m_cfg(cfg)
{
    m_have.resize(m_count);
    m_clock.start();
    m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, &Swarm::tick);
    m_tick.start();
}

Swarm::~Swarm()
{
    m_relay->detach();
    for (auto &p : m_peers) {
        try {
            if (p.dc) { p.dc->resetCallbacks(); p.dc->close(); }
            if (p.pc) { p.pc->resetCallbacks(); p.pc->close(); }
        } catch (...) {}
    }
    m_peers.clear();
    m_cache.clear();      // temporary cache is freed with the swarm
}

bool Swarm::setSeed(const QString &path, const Manifest &manifest)
{
    if (manifest.digest != m_digest || qint64(manifest.size) != m_size || manifest.blockSize != m_blockSize) return false;
    m_seedFile.setFileName(path);
    if (!m_seedFile.open(QIODevice::ReadOnly)) return false;
    m_seed = true;
    m_manifest = manifest;
    m_have.fill(true);
    m_haveCount = m_count;
    for (auto &p : m_peers) if (p.open) { sendHello(p); sendBits(p); }
    return true;
}

// ------------------------------------------------------------------ peers

Swarm::Peer *Swarm::peer(int id, quint64 epoch)
{
    auto it = m_peers.find(id);
    return it != m_peers.end() && it->epoch == epoch ? &*it : nullptr;
}

Swarm::Peer *Swarm::createPeer(int id, bool initiator)
{
    removePeer(id);
    rtc::Configuration c;
    for (const auto &s : m_cfg.iceServers) {
        try { c.iceServers.emplace_back(iceUrl(s)); } catch (...) {}
    }
    Peer p;
    p.id = id;
    p.epoch = m_nextEpoch++;
    p.initiator = initiator;
    p.have.resize(m_count);
    p.createdAt = m_clock.elapsed();
    try {
        p.pc = std::make_shared<rtc::PeerConnection>(c);
    } catch (...) {
        return nullptr;
    }
    std::weak_ptr<SwarmRelay> relay = m_relay;
    const quint64 ep = p.epoch;
    p.pc->onStateChange([relay, id, ep](rtc::PeerConnection::State s) {
        if (auto r = relay.lock()) r->post([id, ep, s](Swarm *sw) { sw->handleState(id, ep, int(s)); });
    });
    // Non-trickle ICE: the description goes out once, with all candidates, when gathering is complete –
    // two signaling messages per connection instead of dozens (public relays limit message rates)
    std::weak_ptr<rtc::PeerConnection> weakPc = p.pc;
    p.pc->onGatheringStateChange([relay, id, ep, weakPc](rtc::PeerConnection::GatheringState s) {
        if (s != rtc::PeerConnection::GatheringState::Complete) return;
        auto pc = weakPc.lock();
        if (!pc) return;
        const auto d = pc->localDescription();
        if (!d) return;
        const QString type = QString::fromStdString(d->typeString()), sdp = QString::fromStdString(std::string(*d));
        if (auto r = relay.lock()) r->post([id, ep, type, sdp](Swarm *sw) { sw->handleLocalDescription(id, ep, type, sdp); });
    });
    p.pc->onDataChannel([relay, id, ep](std::shared_ptr<rtc::DataChannel> dc) {
        if (auto r = relay.lock()) r->post([id, ep, dc](Swarm *sw) { sw->handleChannel(id, ep, dc); });
    });
    auto &stored = m_peers[id] = std::move(p);
    if (initiator) {
        try { attachChannel(stored, stored.pc->createDataChannel("uvewp")); } catch (...) { stored.state = QStringLiteral("failed"); }
    }
    emit statusChanged();
    return &stored;
}

void Swarm::attachChannel(Peer &p, std::shared_ptr<rtc::DataChannel> dc)
{
    p.dc = dc;
    std::weak_ptr<SwarmRelay> relay = m_relay;
    const int id = p.id;
    const quint64 ep = p.epoch;
    dc->setBufferedAmountLowThreshold(256 * 1024);
    dc->onOpen([relay, id, ep] { if (auto r = relay.lock()) r->post([id, ep](Swarm *sw) { sw->handleOpen(id, ep); }); });
    dc->onClosed([relay, id, ep] { if (auto r = relay.lock()) r->post([id, ep](Swarm *sw) { sw->handleClosed(id, ep); }); });
    dc->onBufferedAmountLow([relay, id, ep] { if (auto r = relay.lock()) r->post([id, ep](Swarm *sw) { sw->handleBufferedLow(id, ep); }); });
    dc->onMessage(
        [relay, id, ep](rtc::binary data) {
            QByteArray m(reinterpret_cast<const char *>(data.data()), qsizetype(data.size()));
            if (auto r = relay.lock()) r->post([id, ep, m](Swarm *sw) { sw->handleMessage(id, ep, m); });
        },
        [](rtc::string) {});
    if (dc->isOpen()) handleOpen(id, ep);
}

void Swarm::setParticipants(const QList<int> &memberIds)
{
    const QSet<int> want(memberIds.begin(), memberIds.end());
    for (const int id : m_peers.keys()) if (!want.contains(id)) removePeer(id);
    // The member with the lower id offers; the other side answers (no glare)
    for (const int id : want) if (id != m_myId && !m_peers.contains(id) && m_myId < id) createPeer(id, true);
}

void Swarm::removePeer(int memberId)
{
    auto it = m_peers.find(memberId);
    if (it == m_peers.end()) return;
    for (auto pit = m_pending.begin(); pit != m_pending.end();) {
        if (pit->peer == memberId) pit = m_pending.erase(pit); else ++pit;
    }
    try {
        if (it->dc) { it->dc->resetCallbacks(); it->dc->close(); }
        if (it->pc) { it->pc->resetCallbacks(); it->pc->close(); }
    } catch (...) {}
    m_peers.erase(it);
    emit statusChanged();
}

void Swarm::onSignal(int from, const QString &kind, const QString &data, const QString &mid)
{
    if (from == m_myId) return;
    try {
        if (kind == QLatin1String("offer")) {
            Peer *p = m_peers.contains(from) ? &m_peers[from] : nullptr;
            if (!p || p->remoteSet || p->initiator) p = createPeer(from, false);
            if (!p) return;
            p->pc->setRemoteDescription(rtc::Description(data.toStdString(), "offer"));
            p->remoteSet = true;
            for (const auto &c : std::as_const(p->pendingCandidates)) p->pc->addRemoteCandidate(rtc::Candidate(c.first.toStdString(), c.second.toStdString()));
            p->pendingCandidates.clear();
        } else if (kind == QLatin1String("answer")) {
            auto it = m_peers.find(from);
            if (it == m_peers.end() || !it->initiator || it->remoteSet) return;
            it->pc->setRemoteDescription(rtc::Description(data.toStdString(), "answer"));
            it->remoteSet = true;
            for (const auto &c : std::as_const(it->pendingCandidates)) it->pc->addRemoteCandidate(rtc::Candidate(c.first.toStdString(), c.second.toStdString()));
            it->pendingCandidates.clear();
        } else if (kind == QLatin1String("candidate")) {
            auto it = m_peers.find(from);
            if (it == m_peers.end()) return;
            if (!it->remoteSet) { if (it->pendingCandidates.size() < 64) it->pendingCandidates << qMakePair(data, mid); return; }
            it->pc->addRemoteCandidate(rtc::Candidate(data.toStdString(), mid.toStdString()));
        } else if (kind == QLatin1String("bye")) {
            removePeer(from);
        }
    } catch (...) {
        // malformed SDP/candidate from a peer: drop that connection only
        if (m_peers.contains(from)) { m_peers[from].state = QStringLiteral("failed"); emit statusChanged(); }
    }
}

void Swarm::handleState(int id, quint64 epoch, int state)
{
    Peer *p = peer(id, epoch);
    if (!p) return;
    using S = rtc::PeerConnection::State;
    switch (S(state)) {
    case S::Connected: {
        rtc::Candidate l, r;
        try { if (p->pc->getSelectedCandidatePair(&l, &r)) p->relay = l.type() == rtc::Candidate::Type::Relayed || r.type() == rtc::Candidate::Type::Relayed; } catch (...) {}
        p->state = p->relay ? QStringLiteral("relay") : QStringLiteral("direct");
        break;
    }
    case S::Disconnected: p->state = QStringLiteral("disconnected"); break;
    case S::Failed:
        p->state = QStringLiteral("failed");
        p->open = false;
        for (auto it = m_pending.begin(); it != m_pending.end();) { if (it->peer == id) it = m_pending.erase(it); else ++it; }
        p->inflight = 0;
        break;
    case S::Closed: p->open = false; p->state = QStringLiteral("closed"); break;
    default: break;
    }
    emit statusChanged();
}

void Swarm::handleLocalDescription(int id, quint64 epoch, const QString &type, const QString &sdp)
{
    if (peer(id, epoch)) emit signalOut(id, type, sdp, QString());
}

void Swarm::handleLocalCandidate(int id, quint64 epoch, const QString &cand, const QString &mid)
{
    if (peer(id, epoch)) emit signalOut(id, QStringLiteral("candidate"), cand, mid);
}

void Swarm::handleChannel(int id, quint64 epoch, std::shared_ptr<rtc::DataChannel> dc)
{
    Peer *p = peer(id, epoch);
    if (!p || p->dc || dc->label() != "uvewp") return;
    attachChannel(*p, dc);
}

void Swarm::handleOpen(int id, quint64 epoch)
{
    Peer *p = peer(id, epoch);
    if (!p || p->open) return;
    p->open = true;
    sendHello(*p);
    sendBits(*p);
    emit statusChanged();
}

void Swarm::handleClosed(int id, quint64 epoch)
{
    Peer *p = peer(id, epoch);
    if (!p) return;
    p->open = false;
    p->state = QStringLiteral("closed");
    for (auto it = m_pending.begin(); it != m_pending.end();) { if (it->peer == id) it = m_pending.erase(it); else ++it; }
    p->inflight = 0;
    emit statusChanged();
}

void Swarm::handleBufferedLow(int id, quint64 epoch)
{
    if (Peer *p = peer(id, epoch)) pump(*p);
}

// ------------------------------------------------------------------ messages

void Swarm::send(Peer &p, const QByteArray &msg)
{
    if (!p.dc || !p.open) return;
    try { p.dc->send(reinterpret_cast<const std::byte *>(msg.constData()), size_t(msg.size())); } catch (...) {}
}

void Swarm::broadcast(const QByteArray &msg)
{
    for (auto &p : m_peers) if (p.open && p.hello) send(p, msg);
}

void Swarm::sendHello(Peer &p)
{
    QByteArray m(1, char(HELLO));
    m.append(char(kVersion));
    m += m_digest;
    m.append(char((m_seed ? 1 : 0) | (m_manifest ? 2 : 0)));
    send(p, m);
}

void Swarm::sendBits(Peer &p)
{
    if (!m_haveCount) return;
    QByteArray bits((m_count + 7) / 8, '\0');
    for (int i = 0; i < m_count; ++i) if (m_have.testBit(i)) bits[i / 8] = char(bits[i / 8] | (1 << (i % 8)));
    QByteArray m(1, char(BITS));
    m += u32(0) + u32(quint32(m_count)) + bits;
    send(p, m);
}

void Swarm::handleMessage(int id, quint64 epoch, const QByteArray &m)
{
    Peer *pp = peer(id, epoch);
    if (!pp || m.isEmpty()) return;
    Peer &p = *pp;
    const quint8 type = quint8(m.at(0));
    if (type != HELLO && !p.hello) return;
    auto block = [&](int at) { const quint32 v = rd32(m, at); return v < quint32(m_count) ? int(v) : -1; };
    switch (type) {
    case HELLO:
        if (m.size() != 35 || quint8(m.at(1)) != kVersion || m.mid(2, 32) != m_digest) { removePeer(id); return; }   // other medium
        p.hello = true;
        p.seed = m.at(34) & 1;
        p.hasManifest = m.at(34) & 2;
        if (p.seed) p.have.fill(true);
        emit statusChanged();
        break;
    case MREQ:
        if (m_manifest) {
            const QByteArray &b = m_manifest->bytes;
            for (qsizetype off = 0; off < b.size(); off += kFragment) {
                QByteArray part(1, char(MPART));
                part += u32(quint32(off)) + u32(quint32(b.size())) + b.mid(off, kFragment);
                send(p, part);
            }
        }
        break;
    case MPART:
        if (m.size() >= 9) onManifestPart(p, rd32(m, 1), rd32(m, 5), m.mid(9));
        break;
    case BITS: {
        if (m.size() < 9) break;
        const quint32 first = rd32(m, 1), count = rd32(m, 5);
        const QByteArray bits = m.mid(9);
        for (quint32 i = 0; i < count && first + i < quint32(m_count) && qsizetype(i / 8) < bits.size(); ++i)
            p.have.setBit(int(first + i), bits.at(i / 8) & (1 << (i % 8)));
        schedule();
        break;
    }
    case HAVE: if (m.size() == 5 && block(1) >= 0) { p.have.setBit(block(1)); schedule(); } break;
    case DROP: if (m.size() == 5 && block(1) >= 0) p.have.clearBit(block(1)); break;
    case WANT: if (m.size() == 5 && block(1) >= 0) onWant(p, block(1)); break;
    case PIECE: if (m.size() > 9 && block(1) >= 0) onPiece(p, block(1), int(rd32(m, 5)), m.mid(9)); break;
    case CANCEL:
        if (m.size() == 5) {
            const int idx = block(1);
            for (int i = p.queue.size() - 1; i >= 1; --i) if (p.queue.at(i).index == idx) p.queue.removeAt(i);   // [0] is being sent
        }
        break;
    case REJECT:
        if (m.size() >= 5) {
            const int idx = block(1);
            auto it = m_pending.find(idx);
            if (it != m_pending.end() && it->peer == id) {
                m_pending.erase(it);
                p.inflight = qMax(0, p.inflight - 1);
                m_banned.insert(banKey(idx, id), m_clock.elapsed() + 5000);
                if (m.size() >= 6 && m.at(5) == 2) p.have.clearBit(idx);
                schedule();
            }
        }
        break;
    default: break;
    }
}

void Swarm::onManifestPart(Peer &p, quint32 offset, quint32 total, const QByteArray &data)
{
    if (m_manifest || p.id != m_manifestFrom || total > quint32(kManifestMax) || offset != quint32(m_manifestBuf.size())) return;
    m_manifestBuf += data;
    if (quint32(m_manifestBuf.size()) < total) return;
    const QByteArray bytes = m_manifestBuf;
    m_manifestBuf.clear();
    m_manifestFrom = 0;
    // Identity confirmed by the room control: the manifest must hash to exactly that value
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != m_digest) { p.strikes++; return; }
    const auto parsed = Manifest::parse(bytes);
    if (!parsed || qint64(parsed->size) != m_size || parsed->blockSize != m_blockSize) { p.strikes++; return; }
    applyManifest(*parsed);
}

void Swarm::applyManifest(const Manifest &m)
{
    m_manifest = m;
    for (auto &peer : m_peers) if (peer.open) sendHello(peer);
    emit manifestAvailable();
    emit statusChanged();
    schedule();
}

void Swarm::onWant(Peer &p, int index)
{
    const auto reject = [&](quint8 why) { QByteArray r = msg1(REJECT, quint32(index)); r.append(char(why)); send(p, r); };
    if (!m_cfg.uploadEnabled) return reject(1);
    if (!haveBlock(index)) return reject(2);
    if (p.queue.size() >= kQueuePerPeer) return reject(3);
    const QByteArray data = blockData(index);
    if (data.isEmpty()) return reject(2);
    p.queue.append({index, data, 0});
    pump(p);
}

void Swarm::pump(Peer &p)
{
    if (!p.open || !p.dc) return;
    const qint64 now = m_clock.elapsed();
    if (m_cfg.uploadLimit > 0) {
        m_tokens = qMin<double>(qMax<qint64>(m_cfg.uploadLimit / 2, 65536), m_tokens + double(now - m_lastRefill) * m_cfg.uploadLimit / 1000.0);
    }
    m_lastRefill = now;
    while (!p.queue.isEmpty()) {
        size_t buffered = 0;
        try { buffered = p.dc->bufferedAmount(); } catch (...) { return; }
        if (buffered > size_t(kHighWater)) return;             // continues on onBufferedAmountLow
        OutBlock &b = p.queue.first();
        const int n = int(qMin<qsizetype>(kFragment, b.data.size() - b.offset));
        if (m_cfg.uploadLimit > 0) {
            if (m_tokens < n) return;                           // continues on the next tick
            m_tokens -= n;
        }
        QByteArray msg(1, char(PIECE));
        msg += u32(quint32(b.index)) + u32(quint32(b.offset)) + b.data.mid(b.offset, n);
        send(p, msg);
        b.offset += n;
        p.up += n;
        m_up += n;
        if (b.offset >= b.data.size()) p.queue.removeFirst();
    }
}

void Swarm::onPiece(Peer &p, int index, int offset, const QByteArray &data)
{
    auto it = m_pending.find(index);
    if (it == m_pending.end() || it->peer != p.id) return;            // cancelled or from someone else
    if (offset != it->got || it->got + data.size() > it->buf.size()) { p.strikes++; return; }
    memcpy(it->buf.data() + offset, data.constData(), size_t(data.size()));
    it->got += data.size();
    p.down += data.size();
    m_down += data.size();
    if (it->got < it->buf.size()) return;
    const QByteArray blockBytes = it->buf;
    m_pending.erase(it);
    p.inflight = qMax(0, p.inflight - 1);
    // Every block is checked against the confirmed manifest before it is played or passed on
    if (!m_manifest || !m_manifest->verifyBlock(index, blockBytes)) {
        p.strikes++;
        m_banned.insert(banKey(index, p.id), m_clock.elapsed() + 60000);
        if (p.strikes >= 3) removePeer(p.id);
        schedule();
        return;
    }
    completeBlock(index, blockBytes);
    schedule();
}

// ------------------------------------------------------------------ blocks

bool Swarm::haveBlock(int index) const
{
    return index >= 0 && index < m_count && m_have.testBit(index);
}

QByteArray Swarm::blockData(int index)
{
    if (m_seed) {
        const qint64 len = m_manifest ? m_manifest->blockLength(index) : 0;
        if (len <= 0 || !m_seedFile.seek(qint64(index) * m_blockSize)) return {};
        const QByteArray d = m_seedFile.read(len);
        return d.size() == len ? d : QByteArray();
    }
    return m_cache.get(index, m_clock.elapsed());
}

void Swarm::completeBlock(int index, const QByteArray &data)
{
    if (!m_cache.isOpen() && !m_cache.open(m_cfg.cacheDir, m_cfg.cacheBytes, m_blockSize, m_cfg.cacheMaxAgeMs)) return;
    QList<int> evicted;
    if (!m_cache.put(index, data, m_clock.elapsed(), &evicted)) return;
    dropBlocks(evicted);
    if (!m_have.testBit(index)) { m_have.setBit(index); m_haveCount++; }
    broadcast(msg1(HAVE, quint32(index)));
    emit blockAvailable(index);
}

void Swarm::dropBlocks(const QList<int> &indices)
{
    for (const int i : indices) {
        if (i < 0 || i >= m_count || !m_have.testBit(i)) continue;
        m_have.clearBit(i);
        m_haveCount--;
        broadcast(msg1(DROP, quint32(i)));
    }
}

QByteArray Swarm::read(qint64 offset, qint64 maxLen)
{
    if (offset < 0 || offset >= m_size || maxLen <= 0) return {};
    const int idx = int(offset / m_blockSize);
    if (!haveBlock(idx)) return {};
    const QByteArray d = blockData(idx);
    if (d.isEmpty()) return {};
    const qint64 inBlock = offset - qint64(idx) * m_blockSize;
    return d.mid(inBlock, qMin<qint64>(maxLen, d.size() - inBlock));
}

bool Swarm::bufferingAhead(qint64 offset, int blocks) const
{
    const int first = int(offset / m_blockSize);
    for (int i = first; i < qMin(m_count, first + blocks); ++i) if (!haveBlock(i)) return false;
    return true;
}

int Swarm::openReader(qint64 offset)
{
    const int h = m_nextReader++;
    m_readers.insert(h, offset);
    schedule();
    return h;
}

void Swarm::moveReader(int handle, qint64 offset)
{
    if (!m_readers.contains(handle)) return;
    const bool newBlock = m_readers.value(handle) / m_blockSize != offset / m_blockSize;
    m_readers[handle] = offset;
    if (newBlock) schedule();
}

void Swarm::closeReader(int handle)
{
    m_readers.remove(handle);
}

// Next jump of the room (e.g. a host seek): fetch the blocks around the estimated byte position ahead of libmpv
void Swarm::hintPosition(double seconds, double duration)
{
    if (duration <= 0 || m_size <= 0) return;
    const double frac = qBound(0.0, seconds / duration, 1.0);
    m_hintBlock = qint64(frac * double(m_count - 1));
    schedule();
}

void Swarm::schedule()
{
    if (m_seed) return;
    const qint64 now = m_clock.elapsed();
    if (!m_manifest) {
        if (m_manifestFrom && now - m_manifestAskedAt < 5000) return;
        m_manifestBuf.clear();
        m_manifestFrom = 0;
        QList<Peer *> cand;
        for (auto &p : m_peers) if (p.open && p.hello && p.hasManifest) cand << &p;
        if (cand.isEmpty()) return;
        Peer *p = cand.at(int(QRandomGenerator::global()->bounded(cand.size())));
        m_manifestFrom = p->id;
        m_manifestAskedAt = now;
        send(*p, QByteArray(1, char(MREQ)));
        return;
    }
    // Wanted blocks: urgent ones right at the reading positions, then a shuffled window ahead (so several viewers
    // fetch different blocks from the source and exchange them), then the region of the next room jump
    QList<QPair<int, bool>> wanted;
    QSet<int> seen;
    auto add = [&](int i, bool urgent) { if (i >= 0 && i < m_count && !seen.contains(i)) { seen.insert(i); wanted << qMakePair(i, urgent); } };
    QList<int> ahead;
    for (const qint64 off : std::as_const(m_readers)) {
        const int b = int(off / m_blockSize);
        for (int k = 0; k < kUrgentBlocks; ++k) add(b + k, true);
        for (int k = kUrgentBlocks; k < kAheadBlocks; ++k) ahead << b + k;
    }
    std::shuffle(ahead.begin(), ahead.end(), *QRandomGenerator::global());
    for (const int i : ahead) add(i, false);
    if (m_hintBlock >= 0) for (int k = 0; k < 4; ++k) add(int(m_hintBlock) + k, m_readers.isEmpty());

    int total = int(m_pending.size());
    for (const auto &[idx, urgent] : wanted) {
        if (total >= kInflightTotal) break;
        if (haveBlock(idx) || m_pending.contains(idx)) continue;
        Peer *best = nullptr;
        int bestScore = 0;
        for (auto &p : m_peers) {
            if (!p.open || !p.hello || !p.have.testBit(idx) || p.inflight >= kInflightPerPeer) continue;
            const auto ban = m_banned.constFind(banKey(idx, p.id));
            if (ban != m_banned.cend() && *ban > now) continue;
            // Prefer other viewers that already have the block: the original seed is spared for missing blocks
            const int score = (p.seed ? 8 : 0) + p.inflight * 2 + int(QRandomGenerator::global()->bounded(2));
            if (!best || score < bestScore) { best = &p; bestScore = score; }
        }
        if (!best) continue;
        Pending pd;
        pd.peer = best->id;
        pd.urgent = urgent;
        pd.deadline = now + (urgent ? kUrgentTimeout : kNormalTimeout);
        pd.buf = QByteArray(m_manifest->blockLength(idx), Qt::Uninitialized);
        m_pending.insert(idx, pd);
        best->inflight++;
        total++;
        send(*best, msg1(WANT, quint32(idx)));
    }
}

void Swarm::tick()
{
    const qint64 now = m_clock.elapsed();
    // Timeouts: ask another peer
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->deadline > now) { ++it; continue; }
        auto p = m_peers.find(it->peer);
        if (p != m_peers.end()) { p->inflight = qMax(0, p->inflight - 1); send(*p, msg1(CANCEL, quint32(it.key()))); }
        m_banned.insert(banKey(it.key(), it->peer), now + 10000);
        it = m_pending.erase(it);
    }
    for (auto it = m_banned.begin(); it != m_banned.end();) { if (it.value() <= now) it = m_banned.erase(it); else ++it; }
    if (m_cfg.uploadLimit > 0) for (auto &p : m_peers) if (!p.queue.isEmpty()) pump(p);
    // Failed connections are retried after 30 s (the offering side creates a new one)
    for (const int id : m_peers.keys()) {
        const Peer &p = m_peers[id];
        if (p.state == QLatin1String("failed") && now - p.createdAt > 30000 && p.initiator) createPeer(id, true);
    }
    if (now / 30000 != (now - 100) / 30000 && m_cache.isOpen()) dropBlocks(m_cache.expire(now));
    if (now - m_rateAt >= 1000) {
        const double dt = double(now - m_rateAt) / 1000.0;
        m_upRate = double(m_up - m_upMark) / dt;
        m_downRate = double(m_down - m_downMark) / dt;
        m_upMark = m_up; m_downMark = m_down; m_rateAt = now;
        updateStall();
        emit statusChanged();
    }
    schedule();
}

void Swarm::updateStall()
{
    QString s;
    if (!m_seed) {
        int first = -1;
        if (!m_readers.isEmpty()) first = int(m_readers.cbegin().value() / m_blockSize);
        const bool needManifest = !m_manifest;
        const bool needBlock = first >= 0 && !haveBlock(first);
        if (needManifest || needBlock) {
            bool anyOpen = false, anyHas = false;
            for (const auto &p : std::as_const(m_peers)) {
                if (!p.open || !p.hello) continue;
                anyOpen = true;
                if (needManifest ? p.hasManifest : p.have.testBit(first)) anyHas = true;
            }
            if (!anyOpen) s = QStringLiteral("unreachable");
            else if (!anyHas) s = QStringLiteral("missing");
        }
    }
    const qint64 now = m_clock.elapsed();
    if (s.isEmpty()) { m_stallSince = 0; m_stall.clear(); return; }
    if (!m_stallSince) m_stallSince = now;
    // Short gaps are normal while connecting; only report after a while
    if (now - m_stallSince > (s == QLatin1String("unreachable") ? 15000 : 6000)) m_stall = s;
}

QVariantList Swarm::peerInfo() const
{
    QVariantList out;
    for (const auto &p : m_peers) {
        out << QVariantMap{{"id", p.id}, {"state", p.state}, {"relay", p.relay}, {"seed", p.seed}, {"open", p.open},
                           {"blocks", p.have.count(true)}, {"up", p.up}, {"down", p.down}};
    }
    return out;
}

} // namespace uve::watchparty
