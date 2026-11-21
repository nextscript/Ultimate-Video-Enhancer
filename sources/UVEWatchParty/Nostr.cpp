#include "Nostr.h"

#include "WsClient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

namespace uve::watchparty {

QStringList NostrPool::defaultRelays()
{
    // Large public relays without registration; several at once so one outage does not matter
    // offchain.pub is not used: it refuses keys outside its "web of trust" (our keys are new per session)
    return {QStringLiteral("wss://nos.lol"), QStringLiteral("wss://relay.primal.net"), QStringLiteral("wss://relay.snort.social"),
            QStringLiteral("wss://nostr.bitcoiner.social")};
}

NostrPool::NostrPool(QObject *parent) : QObject(parent), m_keys(crypto::generateKeyPair())
{
}

NostrPool::~NostrPool()
{
    stop();
}

QJsonObject NostrPool::makeEvent(const crypto::KeyPair &keys, int kind, const QJsonArray &tags, const QString &content, qint64 createdAt)
{
    const QString pub = QString::fromLatin1(keys.pub.toHex());
    // NIP-01: id = sha256 of [0, pubkey, created_at, kind, tags, content] (compact JSON)
    const QJsonArray ser{0, pub, double(createdAt), kind, tags, content};
    const QByteArray id = crypto::sha256(QJsonDocument(ser).toJson(QJsonDocument::Compact));
    return QJsonObject{{"id", QString::fromLatin1(id.toHex())}, {"pubkey", pub}, {"created_at", double(createdAt)}, {"kind", kind},
                       {"tags", tags}, {"content", content}, {"sig", QString::fromLatin1(crypto::schnorrSign(keys.secret, id).toHex())}};
}

bool NostrPool::verifyEvent(const QJsonObject &ev)
{
    const QJsonArray ser{0, ev.value("pubkey").toString(), ev.value("created_at").toDouble(), ev.value("kind").toInt(),
                         ev.value("tags").toArray(), ev.value("content").toString()};
    const QByteArray id = crypto::sha256(QJsonDocument(ser).toJson(QJsonDocument::Compact));
    if (QString::fromLatin1(id.toHex()) != ev.value("id").toString()) return false;
    return crypto::schnorrVerify(QByteArray::fromHex(ev.value("pubkey").toString().toLatin1()), id,
                                 QByteArray::fromHex(ev.value("sig").toString().toLatin1()));
}

void NostrPool::start(const QStringList &relays, const QString &topic)
{
    stop();
    m_topic = topic;
    m_subId = QString::fromLatin1(crypto::randomBytes(8).toHex());
    for (const QString &url : relays) {
        auto *r = new Relay;
        r->url = url.trimmed();
        r->retry = new QTimer(this);
        r->retry->setSingleShot(true);
        connect(r->retry, &QTimer::timeout, this, [this, r] { openRelay(*r); });
        m_relays << r;
        openRelay(*r);
    }
}

void NostrPool::stop()
{
    for (Relay *r : std::as_const(m_relays)) {
        if (r->ws) { r->ws->disconnect(this); r->ws->close(); r->ws->deleteLater(); }
        r->retry->deleteLater();
        delete r;
    }
    m_relays.clear();
    m_queue.clear();
}

void NostrPool::openRelay(Relay &r)
{
    if (r.ws) { r.ws->disconnect(this); r.ws->deleteLater(); }
    r.ws = new WsClient(this);
    r.open = false;
    Relay *rp = &r;
    connect(r.ws, &WsClient::opened, this, [this, rp] {
        rp->open = true;
        rp->attempt = 0;
        // No time filter: ephemeral events are not stored anyway, and a "since" based on the local clock would
        // hide the messages of members whose PC clock is off
        const QJsonObject filter{{"kinds", QJsonArray{kKind}}, {"#t", QJsonArray{m_topic}}};
        // The relay's clock (Date header) – events with a created_at far off the relay time are refused by many relays
        if (const qint64 t = rp->ws->serverTime(); t > 0 && !m_clockKnown) {
            m_clockOffset = t - QDateTime::currentSecsSinceEpoch();
            m_clockKnown = true;
            if (qAbs(m_clockOffset) > 30) emit diagnostic(QStringLiteral("local clock differs from the relays by %1 s – corrected").arg(m_clockOffset));
        }
        emit diagnostic(QStringLiteral("relay connected: %1%2").arg(rp->url, rp->ws->certErrorsTolerated()
            ? QStringLiteral(" (certificate could not be verified – content stays end-to-end encrypted)") : QString()));
        rp->ws->sendText(QJsonDocument(QJsonArray{"REQ", m_subId, filter}).toJson(QJsonDocument::Compact));
        // Queued contents are signed only now, with the relay-corrected clock
        for (const QByteArray &content : std::as_const(m_queue)) rp->ws->sendText(signedEvent(content));
        m_queue.clear();
        emit connectionChanged();
    });
    connect(r.ws, &WsClient::textMessage, this, [this, rp](const QByteArray &m) { onText(rp, m); });
    connect(r.ws, &WsClient::closed, this, [this, rp](int code, const QString &reason, int http) {
        if (qEnvironmentVariableIsSet("WP_NOSTR_DEBUG")) qInfo("nostr %s closed %d %s http %d", qPrintable(rp->url), code, qPrintable(reason), http);
        const bool was = rp->open;
        rp->open = false;
        if (!was) m_lastError = reason + (http ? QStringLiteral(" (HTTP %1)").arg(http) : QString());
        // Certificate problem on this PC: retry at once, accepting the certificate (messages are E2E encrypted + signed)
        if (!was && reason.startsWith(QLatin1String("tls")) && !rp->tolerateCert) { rp->tolerateCert = true; rp->attempt = 0; rp->retry->start(200); emit diagnostic(QStringLiteral("relay %1: %2 – retrying without certificate check").arg(rp->url, reason)); return; }
        emit diagnostic(QStringLiteral("relay %1: %2%3").arg(rp->url, was ? QStringLiteral("connection lost") : QStringLiteral("cannot connect (") + reason + QLatin1Char(')'),
                                                             http ? QStringLiteral(" HTTP %1").arg(http) : QString()));
        static constexpr int kDelays[] = {2, 4, 8, 15, 30, 60};
        rp->retry->start(kDelays[qMin(rp->attempt++, 5)] * 1000);
        if (was) emit connectionChanged();
    });
    QString why;
    if (!r.ws->open(QUrl(r.url), m_allowLoopback, &why, r.tolerateCert)) { m_lastError = why; r.retry->start(60000); }
}

void NostrPool::onText(Relay *r, const QByteArray &utf8)
{
    const QJsonArray a = QJsonDocument::fromJson(utf8).array();
    static const bool debug = qEnvironmentVariableIsSet("WP_NOSTR_DEBUG");
    if (debug && a.at(0).toString() != QLatin1String("EVENT")) qInfo("nostr %s <- %s", qPrintable(r->url), utf8.left(300).constData());
    if (a.size() >= 4 && a.at(0).toString() == QLatin1String("OK") && !a.at(2).toBool()) {
        emit diagnostic(QStringLiteral("relay %1 refused an event: %2").arg(r->url, a.at(3).toString().left(200)));
        return;
    }
    if (a.size() >= 2 && (a.at(0).toString() == QLatin1String("NOTICE") || a.at(0).toString() == QLatin1String("CLOSED"))) {
        emit diagnostic(QStringLiteral("relay %1: %2").arg(r->url, QString::fromUtf8(utf8.left(200))));
        return;
    }
    if (a.size() < 3 || a.at(0).toString() != QLatin1String("EVENT") || a.at(1).toString() != m_subId) return;
    const QJsonObject ev = a.at(2).toObject();
    const QString id = ev.value("id").toString();
    if (id.isEmpty() || m_seen.contains(id)) return;          // the same event arrives from several relays
    if (ev.value("kind").toInt() != kKind || !verifyEvent(ev)) return;   // never trust a relay blindly
    m_seen.insert(id);
    m_seenOrder << id;
    if (m_seenOrder.size() > 4000) m_seen.remove(m_seenOrder.takeFirst());
    if (ev.value("pubkey").toString() == publicKeyHex()) return;          // our own echo
    emit received(ev.value("pubkey").toString(), ev.value("content").toString().toUtf8());
}

void NostrPool::publish(const QByteArray &content)
{
    if (connectedCount() == 0) { if (m_queue.size() < 50) m_queue << content; return; }
    const QByteArray msg = signedEvent(content);
    for (Relay *r : std::as_const(m_relays)) if (r->open) r->ws->sendText(msg);
}

QByteArray NostrPool::signedEvent(const QByteArray &content)
{
    // Built with append: QJsonArray{QJsonArray{…}} would copy the inner array instead of nesting it
    QJsonArray tags;
    tags.append(QJsonArray{"t", m_topic});
    const QJsonObject ev = makeEvent(m_keys, kKind, tags, QString::fromUtf8(content), QDateTime::currentSecsSinceEpoch() + m_clockOffset);
    m_seen.insert(ev.value("id").toString());
    return QJsonDocument(QJsonArray{"EVENT", ev}).toJson(QJsonDocument::Compact);
}

int NostrPool::connectedCount() const
{
    int n = 0;
    for (const Relay *r : m_relays) if (r->open) n++;
    return n;
}

} // namespace uve::watchparty
