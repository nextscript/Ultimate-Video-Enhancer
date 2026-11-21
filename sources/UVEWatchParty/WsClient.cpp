#include "WsClient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHostAddress>
#include <QNetworkProxyFactory>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QtEndian>

namespace uve::watchparty {

static constexpr qint64 kMaxMessage = 4 * 1024 * 1024;

WsClient::WsClient(QObject *parent) : QObject(parent)
{
    m_handshakeTimer.setSingleShot(true);
    connect(&m_handshakeTimer, &QTimer::timeout, this, [this] { finish(1006, QStringLiteral("timeout")); });
    // Keeps NAT/proxy mappings alive; the server answers pings with pongs
    m_pingTimer.setInterval(25000);
    connect(&m_pingTimer, &QTimer::timeout, this, [this] { if (m_state == Open) writeFrame(0x9, {}); });
}

WsClient::~WsClient()
{
    if (m_sock) { m_sock->disconnect(this); m_sock->abort(); }
}

bool WsClient::isLoopbackHost(const QString &host)
{
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0) return true;
    QHostAddress a(host);
    return !a.isNull() && a.isLoopback();
}

bool WsClient::open(const QUrl &url, bool allowInsecureLoopback, QString *why, bool tolerateCertErrors)
{
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("wss") && scheme != QLatin1String("ws")) { if (why) *why = QStringLiteral("scheme"); return false; }
    if (url.host().isEmpty()) { if (why) *why = QStringLiteral("host"); return false; }
    const bool tls = scheme == QLatin1String("wss");
    if (!tls && !(allowInsecureLoopback && isLoopbackHost(url.host()))) { if (why) *why = QStringLiteral("insecure"); return false; }

    if (m_sock) { m_sock->disconnect(this); m_sock->abort(); m_sock->deleteLater(); }
    m_url = url;
    m_buf.clear(); m_frag.clear(); m_fragActive = false; m_finished = false;
    m_state = Connecting;
    m_certTolerated = false;
    // Networks that only allow traffic through a proxy (school, company): use the proxy configured in the system
    QNetworkProxy proxy = QNetworkProxy::NoProxy;
    if (!isLoopbackHost(url.host())) {
        QUrl probe = url;
        probe.setScheme(tls ? QStringLiteral("https") : QStringLiteral("http"));
        const auto list = QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(probe));
        for (const auto &p : list) if (p.type() == QNetworkProxy::HttpProxy || p.type() == QNetworkProxy::Socks5Proxy) { proxy = p; break; }
    }
    if (tls) {
        auto *s = new QSslSocket(this);
        m_sock = s;
        s->setProxy(proxy);
        connect(s, &QSslSocket::encrypted, this, &WsClient::onConnected);
        connect(s, &QSslSocket::sslErrors, this, [this, s, tolerateCertErrors](const QList<QSslError> &errs) {
            // A wrong host name is always fatal; validity/chain problems only when the caller tolerates them
            bool hostMismatch = false;
            for (const auto &e : errs) if (e.error() == QSslError::HostNameMismatch) hostMismatch = true;
            if (tolerateCertErrors && !hostMismatch) {
                m_certTolerated = true;
                s->ignoreSslErrors(errs);
                return;
            }
            finish(1015, errs.isEmpty() ? QStringLiteral("tls") : QStringLiteral("tls: ") + errs.first().errorString());
        });
        s->connectToHostEncrypted(url.host(), quint16(url.port(443)));
    } else {
        m_sock = new QTcpSocket(this);
        m_sock->setProxy(proxy);
        connect(m_sock, &QTcpSocket::connected, this, &WsClient::onConnected);
        m_sock->connectToHost(url.host(), quint16(url.port(80)));
    }
    connect(m_sock, &QTcpSocket::readyRead, this, &WsClient::onReadyRead);
    connect(m_sock, &QTcpSocket::errorOccurred, this, &WsClient::onSocketError);
    connect(m_sock, &QTcpSocket::disconnected, this, [this] { finish(1006, QStringLiteral("disconnected")); });
    m_handshakeTimer.start(15000);
    return true;
}

void WsClient::onConnected()
{
    QByteArray raw(16, Qt::Uninitialized);
    for (auto &c : raw) c = char(QRandomGenerator::system()->bounded(256));
    m_key = raw.toBase64();
    QByteArray path = m_url.path(QUrl::FullyEncoded).toUtf8();
    if (path.isEmpty()) path = "/";
    if (m_url.hasQuery()) path += '?' + m_url.query(QUrl::FullyEncoded).toUtf8();
    QByteArray host = m_url.host(QUrl::FullyEncoded).toUtf8();
    const int defPort = m_url.scheme() == QLatin1String("wss") ? 443 : 80;
    if (m_url.port(defPort) != defPort) host += ':' + QByteArray::number(m_url.port());
    QByteArray req = "GET " + path + " HTTP/1.1\r\nHost: " + host
                     + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + m_key
                     + "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: UVE-WatchParty/1.0\r\n\r\n";
    m_state = Handshake;
    m_sock->write(req);
}

void WsClient::onReadyRead()
{
    m_buf += m_sock->readAll();
    if (m_state == Handshake) {
        const int end = m_buf.indexOf("\r\n\r\n");
        if (end < 0) { if (m_buf.size() > 16384) finish(1002, QStringLiteral("handshake")); return; }
        const QByteArray head = m_buf.left(end);
        m_buf.remove(0, end + 4);
        const QList<QByteArray> lines = head.split('\n');
        const QList<QByteArray> status = lines.value(0).trimmed().split(' ');
        const int code = status.value(1).toInt();
        if (code != 101) { finish(1006, QStringLiteral("http %1").arg(code), code); return; }
        const QByteArray expect = QCryptographicHash::hash(m_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
        bool accepted = false;
        for (const auto &l : lines) {
            const int c = l.indexOf(':');
            if (c > 0 && l.left(c).trimmed().toLower() == "sec-websocket-accept") accepted = l.mid(c + 1).trimmed() == expect;
            if (c > 0 && l.left(c).trimmed().toLower() == "date") {
                const QDateTime d = QDateTime::fromString(QString::fromLatin1(l.mid(c + 1).trimmed()), Qt::RFC2822Date);
                if (d.isValid()) m_serverTime = d.toSecsSinceEpoch();
            }
        }
        if (!accepted) { finish(1002, QStringLiteral("handshake")); return; }
        m_handshakeTimer.stop();
        m_state = Open;
        m_pingTimer.start();
        emit opened();
    }
    if (m_state == Open || m_state == Closing) parseFrames();
}

bool WsClient::parseFrames()
{
    for (;;) {
        if (m_buf.size() < 2) return true;
        const auto *p = reinterpret_cast<const uchar *>(m_buf.constData());
        const bool fin = p[0] & 0x80;
        const quint8 op = p[0] & 0x0f;
        const bool masked = p[1] & 0x80;
        quint64 len = p[1] & 0x7f;
        int pos = 2;
        if (len == 126) { if (m_buf.size() < 4) return true; len = qFromBigEndian<quint16>(p + 2); pos = 4; }
        else if (len == 127) { if (m_buf.size() < 10) return true; len = qFromBigEndian<quint64>(p + 2); pos = 10; }
        if (len > quint64(kMaxMessage)) { finish(1009, QStringLiteral("too big")); return false; }
        const int maskPos = pos;
        if (masked) pos += 4;
        if (quint64(m_buf.size()) < pos + len) return true;
        QByteArray payload = m_buf.mid(pos, int(len));
        if (masked) for (int i = 0; i < payload.size(); ++i) payload[i] = char(payload[i] ^ m_buf[maskPos + (i % 4)]);
        m_buf.remove(0, pos + int(len));
        switch (op) {
        case 0x0: case 0x1: case 0x2:
            if (op != 0) { m_frag.clear(); m_fragActive = true; }
            if (!m_fragActive) break;
            m_frag += payload;
            if (m_frag.size() > kMaxMessage) { finish(1009, QStringLiteral("too big")); return false; }
            if (fin) { m_fragActive = false; const QByteArray msg = m_frag; m_frag.clear(); emit textMessage(msg); }
            break;
        case 0x8: {
            const int code = payload.size() >= 2 ? qFromBigEndian<quint16>(payload.constData()) : 1005;
            const QString reason = QString::fromUtf8(payload.mid(2));
            if (m_state == Open) writeFrame(0x8, payload.left(2));
            m_sock->flush();
            finish(code, reason);
            return false;
        }
        case 0x9: writeFrame(0xA, payload); break;
        default: break;
        }
        if (m_finished) return false;
    }
}

void WsClient::writeFrame(quint8 opcode, const QByteArray &payload)
{
    if (!m_sock) return;
    QByteArray f;
    f.append(char(0x80 | opcode));
    const qint64 n = payload.size();
    if (n < 126) f.append(char(0x80 | n));
    else if (n < 65536) { f.append(char(0x80 | 126)); quint16 be = qToBigEndian<quint16>(quint16(n)); f.append(reinterpret_cast<const char *>(&be), 2); }
    else { f.append(char(0x80 | 127)); quint64 be = qToBigEndian<quint64>(quint64(n)); f.append(reinterpret_cast<const char *>(&be), 8); }
    const quint32 mask = QRandomGenerator::system()->generate();
    const auto *m = reinterpret_cast<const char *>(&mask);
    f.append(m, 4);
    QByteArray data = payload;
    for (int i = 0; i < data.size(); ++i) data[i] = char(data[i] ^ m[i % 4]);
    f += data;
    m_sock->write(f);
}

bool WsClient::sendText(const QByteArray &utf8)
{
    if (m_state != Open) return false;
    writeFrame(0x1, utf8);
    return true;
}

void WsClient::close(quint16 code)
{
    if (m_state == Open) {
        QByteArray p(2, Qt::Uninitialized);
        qToBigEndian<quint16>(code, p.data());
        writeFrame(0x8, p);
        m_state = Closing;
        if (m_sock) m_sock->flush();
        QTimer::singleShot(1500, this, [this, code] { finish(code, QStringLiteral("closed")); });
        return;
    }
    finish(code, QStringLiteral("closed"));
}

void WsClient::onSocketError()
{
    finish(1006, m_sock ? m_sock->errorString() : QStringLiteral("error"));
}

void WsClient::finish(int code, const QString &reason, int httpStatus)
{
    if (m_finished) return;
    m_finished = true;
    m_state = Closed;
    m_handshakeTimer.stop();
    m_pingTimer.stop();
    if (m_sock) { m_sock->disconnect(this); m_sock->abort(); m_sock->deleteLater(); m_sock = nullptr; }
    emit closed(code, reason, httpStatus);
}

} // namespace uve::watchparty
