#include "ProxyBridge.h"
#include "../core/Logger.h"

#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

namespace uve {

namespace {
constexpr int kMaxHeader = 32 * 1024;
constexpr qint64 kMaxBuffered = 8 * 1024 * 1024; // Gegendruck: Lesen pausieren, wenn die Gegenseite hinterherhinkt

QByteArray randomToken(int bytes)
{
    QByteArray b(bytes, Qt::Uninitialized);
    for (auto &c : b) c = char(QRandomGenerator::system()->bounded(256));
    return b.toHex();
}
} // namespace

ProxyBridge::ProxyBridge(QObject *parent) : QObject(parent) {}
ProxyBridge::~ProxyBridge() { stop(); }

bool ProxyBridge::start(const QNetworkProxy &upstream, QString *error)
{
    stop();
    m_upstream = upstream;
    // Upstream-Hostnamen am Proxy auflösen lassen
    m_upstream.setCapabilities(m_upstream.capabilities() | QNetworkProxy::HostNameLookupCapability | QNetworkProxy::TunnelingCapability);
    m_user = "uve";
    m_token = randomToken(16);
    m_server = new QTcpServer(this);
    m_server->setProxy(QNetworkProxy::NoProxy);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        if (error) *error = m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QTcpServer::newConnection, this, &ProxyBridge::onHttpClient);
    Logger::write("proxy", QStringLiteral("Brücke aktiv auf 127.0.0.1:%1 → %2 %3:%4")
                               .arg(m_server->serverPort())
                               .arg(upstream.type() == QNetworkProxy::Socks5Proxy ? "SOCKS5" : "HTTP")
                               .arg(upstream.hostName()).arg(upstream.port()));
    return true;
}

void ProxyBridge::stop()
{
    for (auto *f : m_forwarders) f->deleteLater();
    m_forwarders.clear();
    m_forwardPorts.clear();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
}

QString ProxyBridge::proxyUrl() const
{
    if (!m_server) return {};
    return QStringLiteral("http://%1:%2@127.0.0.1:%3").arg(QString::fromLatin1(m_user), QString::fromLatin1(m_token)).arg(m_server->serverPort());
}

quint16 ProxyBridge::forwardTcp(const QString &host, quint16 port)
{
    if (!m_server) return 0;
    const QString key = host + ':' + QString::number(port);
    if (m_forwardPorts.contains(key)) return m_forwardPorts.value(key);
    auto *srv = new QTcpServer(this);
    srv->setProxy(QNetworkProxy::NoProxy);
    if (!srv->listen(QHostAddress::LocalHost, 0)) { delete srv; return 0; }
    connect(srv, &QTcpServer::newConnection, this, [this, srv, host, port] {
        while (auto *client = srv->nextPendingConnection()) pipe(client, host, port, {}, {});
    });
    m_forwarders << srv;
    m_forwardPorts.insert(key, srv->serverPort());
    Logger::write("proxy", QStringLiteral("TCP-Weiterleitung 127.0.0.1:%1 → %2 (über Proxy)").arg(srv->serverPort()).arg(key));
    return srv->serverPort();
}

void ProxyBridge::onHttpClient()
{
    while (auto *client = m_server->nextPendingConnection()) {
        client->setProperty("hdr", QByteArray());
        connect(client, &QTcpSocket::readyRead, this, [this, client] { handleHttpRequest(client); });
        connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
        // Kopf muss zügig kommen
        QTimer::singleShot(15000, client, [client] { if (!client->property("piped").toBool()) client->abort(); });
    }
}

void ProxyBridge::handleHttpRequest(QTcpSocket *client)
{
    if (client->property("piped").toBool()) return;
    QByteArray buf = client->property("hdr").toByteArray() + client->readAll();
    const int end = buf.indexOf("\r\n\r\n");
    if (end < 0) {
        if (buf.size() > kMaxHeader) client->abort();
        else client->setProperty("hdr", buf);
        return;
    }
    client->setProperty("piped", true);
    disconnect(client, &QTcpSocket::readyRead, this, nullptr);
    const QByteArray head = buf.left(end);
    const QByteArray rest = buf.mid(end + 4);
    QList<QByteArray> lines = head.split('\n');
    for (auto &l : lines) l = l.trimmed();
    const QList<QByteArray> req = lines.value(0).split(' ');
    auto reply = [client](const QByteArray &status) {
        client->write("HTTP/1.1 " + status + "\r\nProxy-Authenticate: Basic realm=\"UVE Player\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        client->disconnectFromHost();
    };
    if (req.size() < 3) { reply("400 Bad Request"); return; }

    // Zugang nur mit den Zufallsdaten dieser Sitzung
    const QByteArray expected = "Basic " + (m_user + ':' + m_token).toBase64();
    bool authed = false;
    QList<QByteArray> forward;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray &l = lines[i];
        const int c = l.indexOf(':');
        const QByteArray name = l.left(c).trimmed().toLower();
        if (name == "proxy-authorization") { authed = l.mid(c + 1).trimmed() == expected; continue; }
        if (name == "proxy-connection") continue;
        forward << l;
    }
    if (!authed) { reply("407 Proxy Authentication Required"); return; }

    const QByteArray method = req[0];
    if (method == "CONNECT") {
        const QByteArray target = req[1];
        const int colon = target.lastIndexOf(':');
        const QString host = QString::fromLatin1(target.left(colon)).remove('[').remove(']');
        const quint16 port = quint16(target.mid(colon + 1).toUInt());
        if (colon <= 0 || port == 0) { reply("400 Bad Request"); return; }
        pipe(client, host, port, rest, "HTTP/1.1 200 Connection established\r\n\r\n");
        return;
    }
    // absolute-form: GET http://host[:port]/pfad HTTP/1.1 → origin-form an den Zielserver
    const QUrl url(QString::fromLatin1(req[1]));
    if (url.scheme() != QLatin1String("http") || url.host().isEmpty()) { reply("400 Bad Request"); return; }
    QByteArray path = url.path(QUrl::FullyEncoded).toLatin1();
    if (path.isEmpty()) path = "/";
    if (url.hasQuery()) path += '?' + url.query(QUrl::FullyEncoded).toLatin1();
    QByteArray out = method + ' ' + path + ' ' + req[2] + "\r\n";
    for (const auto &l : forward) out += l + "\r\n";
    out += "\r\n" + rest;
    pipe(client, url.host(), quint16(url.port(80)), out, {});
}

void ProxyBridge::pipe(QTcpSocket *client, const QString &host, quint16 port, const QByteArray &firstUpstreamBytes,
                       const QByteArray &clientReplyOnConnect)
{
    auto *up = new QTcpSocket(client);           // Lebensdauer an den Client gebunden
    up->setProxy(m_upstream);
    client->setProperty("piped", true);
    const QString target = host + ':' + QString::number(port);
    ++m_stats.connections;
    m_stats.hosts[host]++;
    QPointer<QTcpSocket> pc(client), pu(up);

    auto flow = [this](QTcpSocket *from, QTcpSocket *to, bool down) {
        if (to->bytesToWrite() > kMaxBuffered) return;   // Gegendruck; bytesWritten setzt fort
        const QByteArray d = from->readAll();
        if (d.isEmpty()) return;
        (down ? m_stats.bytesDown : m_stats.bytesUp) += d.size();
        to->write(d);
    };
    connect(up, &QTcpSocket::connected, client, [pc, pu, firstUpstreamBytes, clientReplyOnConnect, flow] {
        if (!pc || !pu) return;
        if (!clientReplyOnConnect.isEmpty()) pc->write(clientReplyOnConnect);
        if (!firstUpstreamBytes.isEmpty()) pu->write(firstUpstreamBytes);
        flow(pc, pu, false); // bereits gepuffert
        QObject::connect(pc, &QTcpSocket::readyRead, pu, [pc, pu, flow] { flow(pc, pu, false); });
        QObject::connect(pu, &QTcpSocket::readyRead, pc, [pc, pu, flow] { flow(pu, pc, true); });
        QObject::connect(pc, &QTcpSocket::bytesWritten, pu, [pc, pu, flow] { flow(pu, pc, true); });
        QObject::connect(pu, &QTcpSocket::bytesWritten, pc, [pc, pu, flow] { flow(pc, pu, false); });
    });
    connect(up, &QTcpSocket::errorOccurred, client, [this, pc, pu, target, clientReplyOnConnect](QAbstractSocket::SocketError e) {
        if (!pc) return;
        if (e != QAbstractSocket::RemoteHostClosedError) {
            const QString msg = pu ? pu->errorString() : QString();
            Logger::write("proxy", QStringLiteral("Verbindung zu %1 fehlgeschlagen: %2").arg(target, msg));
            emit connectionFailed(target, msg);
            if (!clientReplyOnConnect.isEmpty() && pu && pu->state() != QAbstractSocket::ConnectedState) {
                pc->write("HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            }
        }
        pc->disconnectFromHost();
    });
    // Schließen einer Seite schließt die andere, nachdem ausstehende Daten geschrieben sind
    connect(up, &QTcpSocket::disconnected, client, [pc, pu] { if (pc) { if (pu) pc->write(pu->readAll()); pc->disconnectFromHost(); } });
    connect(client, &QTcpSocket::disconnected, up, [pu] { if (pu) pu->disconnectFromHost(); });
    up->connectToHost(host, port);
}

} // namespace uve
