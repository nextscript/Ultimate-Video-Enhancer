#pragma once
// Minimal Nostr client (NIP-01) for the serverless room channel: several public relays at once (redundancy),
// ephemeral events (kind 20777, relays forward but do not store them) filtered by one topic tag.
// Each session uses a fresh random key pair – no account, no registration.
#include "Crypto.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

namespace uve::watchparty {

class WsClient;

class NostrPool : public QObject
{
    Q_OBJECT
public:
    static constexpr int kKind = 20777;   // ephemeral range 20000–29999, own kind for UVE Watch Party
    static QStringList defaultRelays();

    explicit NostrPool(QObject *parent = nullptr);
    ~NostrPool() override;

    void setAllowInsecureLoopback(bool on) { m_allowLoopback = on; }
    void start(const QStringList &relays, const QString &topic);
    void stop();
    void publish(const QByteArray &content);
    int connectedCount() const;
    int relayCount() const { return int(m_relays.size()); }
    QString lastError() const { return m_lastError; }
    QString publicKeyHex() const { return QString::fromLatin1(m_keys.pub.toHex()); }
    // Seconds the local clock is behind the relays (from their Date headers); used for created_at
    qint64 clockOffset() const { return m_clockOffset; }

    // Signed event with id (exposed for tests)
    static QJsonObject makeEvent(const crypto::KeyPair &keys, int kind, const QJsonArray &tags, const QString &content, qint64 createdAt);
    static bool verifyEvent(const QJsonObject &ev);

signals:
    void received(const QString &pubkeyHex, const QByteArray &content);
    void connectionChanged();
    void diagnostic(const QString &text);    // for the player log (connection problems, refused events)

private:
    struct Relay {
        QString url;
        WsClient *ws = nullptr;
        bool open = false;
        int attempt = 0;
        bool tolerateCert = false;     // after a certificate failure: content is end-to-end protected anyway
        QTimer *retry = nullptr;
    };
    void openRelay(Relay &r);
    void onText(Relay *r, const QByteArray &utf8);

    crypto::KeyPair m_keys;
    QList<Relay *> m_relays;
    QString m_topic;
    QString m_subId;
    QList<QByteArray> m_queue;          // contents published while no relay was connected (signed when sent)
    QByteArray signedEvent(const QByteArray &content);
    QSet<QString> m_seen;
    QList<QString> m_seenOrder;
    bool m_allowLoopback = false;
    qint64 m_clockOffset = 0;
    QString m_lastError;
    bool m_clockKnown = false;
};

} // namespace uve::watchparty
