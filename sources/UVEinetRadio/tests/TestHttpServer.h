#pragma once
// Kleiner HTTP/1.1-Server für reproduzierbare Tests (Radio-Browser-Antworten, Audio-/HTML-Antworten, Ausfälle).
#include <QHash>
#include <QList>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

struct TestResponse
{
    int status = 200;
    QByteArray contentType = "application/json";
    QByteArray body;
    QList<QPair<QByteArray, QByteArray>> headers;
    int delayMs = 0;
    bool hang = false;        // nie antworten (Zeitlimit)
    bool stream = false;      // Kopf + Daten senden, Verbindung offen lassen (Live-Stream)
};

class TestHttpServer : public QObject
{
public:
    using Handler = std::function<TestResponse(const QString &path, const QUrlQuery &query, const QByteArray &headers)>;

    explicit TestHttpServer(Handler h, QObject *parent = nullptr) : QObject(parent), m_handler(std::move(h))
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s, buf] {
                    *buf += s->readAll();
                    const int end = buf->indexOf("\r\n\r\n");
                    if (end < 0) return;
                    const QByteArray head = buf->left(end);
                    buf->clear();
                    const QList<QByteArray> lines = head.split('\n');
                    const QList<QByteArray> req = lines.value(0).trimmed().split(' ');
                    const QUrl url(QStringLiteral("http://x") + QString::fromLatin1(req.value(1)));
                    ++m_hits[url.path()];
                    m_log << url.path() + QLatin1Char('?') + url.query();
                    m_lastHeaders = head;
                    const TestResponse r = m_handler(url.path(), QUrlQuery(url), head);
                    if (r.hang) return;
                    QPointer<QTcpSocket> ps(s);
                    auto send = [ps, r] {
                        if (!ps) return;
                        QByteArray out = "HTTP/1.1 " + QByteArray::number(r.status) + " X\r\n";
                        if (!r.contentType.isEmpty()) out += "Content-Type: " + r.contentType + "\r\n";
                        for (const auto &h : r.headers) out += h.first + ": " + h.second + "\r\n";
                        if (!r.stream) out += "Content-Length: " + QByteArray::number(r.body.size()) + "\r\nConnection: close\r\n";
                        out += "\r\n" + r.body;
                        ps->write(out);
                        if (!r.stream) ps->disconnectFromHost();
                    };
                    if (r.delayMs > 0) QTimer::singleShot(r.delayMs, s, send);
                    else send();
                });
                QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }

    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    QUrl baseUrl() const { return QUrl(base()); }
    int hits(const QString &path) const { return m_hits.value(path); }
    int totalHits() const { int n = 0; for (int v : m_hits) n += v; return n; }
    QStringList log() const { return m_log; }
    QByteArray lastHeaders() const { return m_lastHeaders; }
    void setHandler(Handler h) { m_handler = std::move(h); }

private:
    QTcpServer m_server;
    Handler m_handler;
    QHash<QString, int> m_hits;
    QStringList m_log;
    QByteArray m_lastHeaders;
};
