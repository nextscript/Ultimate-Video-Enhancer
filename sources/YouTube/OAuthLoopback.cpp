#include "OAuthLoopback.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>

namespace {
QString base64Url(const QByteArray &data)
{
    return QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
QByteArray randomBytes(int n)
{
    QByteArray b(n, Qt::Uninitialized);
    QRandomGenerator *g = QRandomGenerator::system();
    for (int i = 0; i < n; ++i) b[i] = char(g->bounded(256));
    return b;
}
} // namespace

OAuthLoopback::OAuthLoopback(QObject *parent) : QObject(parent) {}
OAuthLoopback::~OAuthLoopback() { close(); }

int OAuthLoopback::port() const { return m_server && m_server->isListening() ? m_server->serverPort() : 0; }
bool OAuthLoopback::listening() const { return m_server && m_server->isListening(); }

int OAuthLoopback::listen()
{
    close();
    m_server = new QTcpServer(this);
    // nur lokal erreichbar (RFC 8252 Abschnitt 7.3)
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        delete m_server;
        emit portChanged();
        return 0;
    }
    connect(m_server, &QTcpServer::newConnection, this, &OAuthLoopback::onNewConnection);
    emit portChanged();
    return m_server->serverPort();
}

void OAuthLoopback::close()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
        emit portChanged();
    }
}

QString OAuthLoopback::randomVerifier(int bytes) const { return base64Url(randomBytes(qBound(32, bytes, 96))); }
QString OAuthLoopback::randomState() const { return base64Url(randomBytes(24)); }
QString OAuthLoopback::challenge(const QString &verifier) const
{
    return base64Url(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256));
}

void OAuthLoopback::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *s = m_server->nextPendingConnection();
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        connect(s, &QTcpSocket::readyRead, this, [this, s] { handle(s); });
        QTimer::singleShot(10000, s, [s] { s->abort(); });   // hängende Verbindungen beenden
    }
}

void OAuthLoopback::handle(QTcpSocket *socket)
{
    QByteArray buf = socket->property("buf").toByteArray() + socket->readAll();
    if (buf.size() > 16384) { socket->abort(); return; }
    socket->setProperty("buf", buf);
    if (!buf.contains("\r\n\r\n") && !buf.contains("\n\n")) return;   // Kopfzeilen noch nicht vollständig

    const QList<QByteArray> line = buf.left(buf.indexOf('\n')).trimmed().split(' ');
    QVariantMap params;
    bool relevant = false;
    if (line.size() >= 2 && line[0] == "GET") {
        const QUrl url(QString::fromLatin1(line[1]));
        const QUrlQuery q(url);
        for (const auto &it : q.queryItems(QUrl::FullyDecoded)) params.insert(it.first, it.second);
        relevant = params.contains(QStringLiteral("code")) || params.contains(QStringLiteral("error"));
    }
    const bool ok = params.contains(QStringLiteral("code"));
    QByteArray body = (relevant ? (ok ? m_successHtml : m_errorHtml) : QStringLiteral("<!doctype html><title>UVE Player</title>")).toUtf8();
    if (body.isEmpty()) body = ok ? "OK" : "Error";
    QByteArray resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "
                      + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    socket->write(resp);
    socket->flush();
    socket->disconnectFromHost();
    if (relevant) {
        emit redirected(params);
        close();   // nur eine Weiterleitung annehmen
    }
}
