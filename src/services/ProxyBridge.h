#pragma once
// Lokale Proxy-Brücke: mpv/FFmpeg kann nur HTTP-Proxys. Die Brücke nimmt auf 127.0.0.1 HTTP-Proxy-Anfragen
// (CONNECT und absolute-form GET) sowie reine TCP-Weiterleitungen (für tcp://-Streams) an und baut die
// Verbindungen über den eingestellten Upstream-Proxy (SOCKS5 oder HTTP-CONNECT) auf. Hostnamen werden –
// bei SOCKS5 – am Proxy aufgelöst (kein lokaler DNS-Leak).
//
// Sicherheit: nur Loopback, zufälliger Port, Zugang nur mit zufälligen Zugangsdaten (Proxy-Authorization Basic).
#include <QByteArray>
#include <QHash>
#include <QNetworkProxy>
#include <QObject>
#include <QPointer>

class QTcpServer;
class QTcpSocket;

namespace uve {

class ProxyBridge : public QObject
{
    Q_OBJECT
public:
    explicit ProxyBridge(QObject *parent = nullptr);
    ~ProxyBridge() override;

    bool start(const QNetworkProxy &upstream, QString *error = nullptr);
    void stop();
    bool running() const { return m_server != nullptr; }

    // http://user:token@127.0.0.1:port – für die mpv-Option http-proxy
    QString proxyUrl() const;
    // tcp://host:port über den Upstream-Proxy erreichbar machen → lokaler Port (0 bei Fehler)
    quint16 forwardTcp(const QString &host, quint16 port);

    struct Stats { qint64 connections = 0; qint64 bytesDown = 0; qint64 bytesUp = 0; QHash<QString, qint64> hosts; };
    const Stats &stats() const { return m_stats; }

signals:
    void connectionFailed(const QString &target, const QString &error);

private:
    void onHttpClient();
    void handleHttpRequest(QTcpSocket *client);
    void pipe(QTcpSocket *client, const QString &host, quint16 port, const QByteArray &firstUpstreamBytes,
              const QByteArray &clientReplyOnConnect);

    QTcpServer *m_server = nullptr;
    QList<QTcpServer *> m_forwarders;
    QHash<QString, quint16> m_forwardPorts;   // "host:port" → lokaler Port
    QNetworkProxy m_upstream;
    QByteArray m_user, m_token;
    Stats m_stats;
};

} // namespace uve
