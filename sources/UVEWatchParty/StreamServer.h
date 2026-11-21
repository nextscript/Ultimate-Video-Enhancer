#pragma once
// Local HTTP interface for libmpv: listens only on 127.0.0.1 with a random port and a random session key in the
// path. HEAD and GET with single byte ranges (206 / 416, RFC 9110) so libmpv can read header, index and file end.
// Bytes come from verified swarm blocks; playback starts while the transfer is still running.
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

class QTcpServer;
class QTcpSocket;

namespace uve::watchparty {

class Swarm;

class StreamServer : public QObject
{
    Q_OBJECT
public:
    StreamServer(Swarm *swarm, const QString &fileName, QObject *parent = nullptr);
    ~StreamServer() override;

    bool start();
    QString url() const { return m_url; }
    int activeRequests() const { return int(m_conns.size()); }
    bool waiting() const;      // a request is waiting for a missing block

private:
    struct Conn {
        QTcpSocket *sock = nullptr;
        QByteArray head;
        bool started = false;
        qint64 pos = 0;
        qint64 end = -1;
        int reader = 0;
        bool waiting = false;
        qint64 waitSince = 0;
    };
    void onNewConnection();
    void onReadyRead(QTcpSocket *s);
    void respond(Conn &c);
    void pump(QTcpSocket *s);
    void finish(QTcpSocket *s);
    void onBlock();

    QPointer<Swarm> m_swarm;
    QTcpServer *m_server = nullptr;
    QString m_key;
    QString m_name;
    QString m_url;
    QString m_mime;
    QHash<QTcpSocket *, Conn> m_conns;
};

} // namespace uve::watchparty
