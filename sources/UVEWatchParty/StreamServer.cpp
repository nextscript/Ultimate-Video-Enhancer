#include "StreamServer.h"

#include "Swarm.h"
#include "SyncLogic.h"

#include <QDateTime>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

namespace uve::watchparty {

static constexpr qint64 kChunk = 256 * 1024;
static constexpr qint64 kSocketHighWater = 2 * 1024 * 1024;
static constexpr qint64 kWaitTimeoutMs = 120000;

static QString mimeFor(const QString &name)
{
    const QString ext = QFileInfo(name).suffix().toLower();
    if (ext == QLatin1String("mp4") || ext == QLatin1String("m4v")) return QStringLiteral("video/mp4");
    if (ext == QLatin1String("mkv")) return QStringLiteral("video/x-matroska");
    if (ext == QLatin1String("webm")) return QStringLiteral("video/webm");
    if (ext == QLatin1String("avi")) return QStringLiteral("video/x-msvideo");
    if (ext == QLatin1String("mov")) return QStringLiteral("video/quicktime");
    if (ext == QLatin1String("ts") || ext == QLatin1String("m2ts")) return QStringLiteral("video/mp2t");
    return QStringLiteral("application/octet-stream");
}

StreamServer::StreamServer(Swarm *swarm, const QString &fileName, QObject *parent)
    : QObject(parent), m_swarm(swarm)
{
    // Only a safe file name (extension helps libmpv's probing); never a local path
    QString n = QFileInfo(fileName).fileName();
    n.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    if (n.isEmpty()) n = QStringLiteral("stream.bin");
    m_name = n.right(80);
    m_mime = mimeFor(m_name);
    QByteArray key(16, Qt::Uninitialized);
    for (auto &c : key) c = char(QRandomGenerator::system()->bounded(256));
    m_key = QString::fromLatin1(key.toHex());
    connect(swarm, &Swarm::blockAvailable, this, &StreamServer::onBlock);
}

StreamServer::~StreamServer()
{
    for (auto it = m_conns.begin(); it != m_conns.end(); ++it) {
        if (m_swarm && it->reader) m_swarm->closeReader(it->reader);
        it.key()->disconnect(this);
        it.key()->abort();
        it.key()->deleteLater();
    }
    m_conns.clear();
}

bool StreamServer::start()
{
    m_server = new QTcpServer(this);
    m_server->setMaxPendingConnections(16);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) return false;
    connect(m_server, &QTcpServer::newConnection, this, &StreamServer::onNewConnection);
    m_url = QStringLiteral("http://127.0.0.1:%1/%2/%3").arg(m_server->serverPort()).arg(m_key, m_name);
    auto *watchdog = new QTimer(this);
    watchdog->start(5000);
    connect(watchdog, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (auto *s : m_conns.keys())
            if (m_conns[s].waiting && now - m_conns[s].waitSince > kWaitTimeoutMs) finish(s);
    });
    return true;
}

bool StreamServer::waiting() const
{
    for (const auto &c : m_conns) if (c.waiting) return true;
    return false;
}

void StreamServer::onNewConnection()
{
    while (QTcpSocket *s = m_server->nextPendingConnection()) {
        if (m_conns.size() >= 12 || !s->peerAddress().isLoopback()) { s->abort(); s->deleteLater(); continue; }
        Conn c;
        c.sock = s;
        m_conns.insert(s, c);
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadyRead(s); });
        connect(s, &QTcpSocket::bytesWritten, this, [this, s] { pump(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] { finish(s); });
    }
}

void StreamServer::onReadyRead(QTcpSocket *s)
{
    auto it = m_conns.find(s);
    if (it == m_conns.end()) return;
    if (it->started) { s->readAll(); return; }
    it->head += s->readAll();
    if (it->head.size() > 16384) { finish(s); return; }
    if (!it->head.contains("\r\n\r\n")) return;
    it->started = true;
    respond(*it);
}

void StreamServer::respond(Conn &c)
{
    QTcpSocket *s = c.sock;
    const QList<QByteArray> lines = c.head.left(c.head.indexOf("\r\n\r\n")).split('\n');
    const QList<QByteArray> req = lines.value(0).trimmed().split(' ');
    const QByteArray method = req.value(0);
    const QString path = QUrl::fromPercentEncoding(req.value(1));
    QByteArray rangeHeader;
    for (int i = 1; i < lines.size(); ++i) {
        const int col = lines.at(i).indexOf(':');
        if (col > 0 && lines.at(i).left(col).trimmed().toLower() == "range") rangeHeader = lines.at(i).mid(col + 1).trimmed();
    }
    auto plain = [&](const QByteArray &status, const QByteArray &extra = {}) {
        s->write("HTTP/1.1 " + status + "\r\nContent-Length: 0\r\nConnection: close\r\nCache-Control: no-store\r\n" + extra + "\r\n");
        s->disconnectFromHost();
    };
    // Session key in the path: other local programs cannot guess the address
    if (!path.startsWith(QLatin1Char('/') + m_key + QLatin1Char('/'))) return plain("404 Not Found");
    if (method != "GET" && method != "HEAD") return plain("405 Method Not Allowed", "Allow: GET, HEAD\r\n");
    if (!m_swarm) return plain("503 Service Unavailable");
    const qint64 size = m_swarm->size();
    const sync::ByteRange r = sync::parseRange(rangeHeader, size);
    if (r.type == sync::ByteRange::Unsatisfiable) return plain("416 Range Not Satisfiable", "Content-Range: bytes */" + QByteArray::number(size) + "\r\n");
    c.pos = r.type == sync::ByteRange::Range ? r.start : 0;
    c.end = r.type == sync::ByteRange::Range ? r.end : size - 1;
    QByteArray h = r.type == sync::ByteRange::Range ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
    h += "Content-Type: " + m_mime.toLatin1() + "\r\nAccept-Ranges: bytes\r\nContent-Length: " + QByteArray::number(c.end - c.pos + 1)
         + "\r\nCache-Control: no-store\r\nConnection: close\r\n";
    if (r.type == sync::ByteRange::Range)
        h += "Content-Range: bytes " + QByteArray::number(c.pos) + '-' + QByteArray::number(c.end) + '/' + QByteArray::number(size) + "\r\n";
    s->write(h + "\r\n");
    if (method == "HEAD") { s->disconnectFromHost(); return; }
    c.reader = m_swarm->openReader(c.pos);
    pump(s);
}

void StreamServer::pump(QTcpSocket *s)
{
    auto it = m_conns.find(s);
    if (it == m_conns.end() || !it->reader || !m_swarm) return;
    while (it->pos <= it->end && s->bytesToWrite() < kSocketHighWater) {
        const QByteArray d = m_swarm->read(it->pos, qMin(kChunk, it->end - it->pos + 1));
        if (d.isEmpty()) {
            if (!it->waiting) { it->waiting = true; it->waitSince = QDateTime::currentMSecsSinceEpoch(); }
            return;                                      // continues on Swarm::blockAvailable
        }
        it->waiting = false;
        s->write(d);
        it->pos += d.size();
        m_swarm->moveReader(it->reader, it->pos);
    }
    if (it->pos > it->end) {
        m_swarm->closeReader(it->reader);
        it->reader = 0;
        s->disconnectFromHost();
    }
}

void StreamServer::onBlock()
{
    for (auto *s : m_conns.keys()) if (m_conns[s].waiting) pump(s);
}

void StreamServer::finish(QTcpSocket *s)
{
    auto it = m_conns.find(s);
    if (it == m_conns.end()) return;
    if (m_swarm && it->reader) m_swarm->closeReader(it->reader);
    m_conns.erase(it);
    s->disconnect(this);
    s->abort();
    s->deleteLater();
}

} // namespace uve::watchparty
