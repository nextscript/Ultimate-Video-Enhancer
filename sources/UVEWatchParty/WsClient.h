#pragma once
// Minimal WebSocket client (RFC 6455) on QSslSocket for the signaling connection.
// The UVE Player distribution ships Qt Network (with TLS backends) but not the Qt WebSockets module, so – like the
// Twitch/Kick chat plugins – the plugin carries its own small client instead of QWebSocket.
// wss:// with full certificate verification; ws:// only when explicitly allowed for a loopback address.
#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <QUrl>

class QTcpSocket;

namespace uve::watchparty {

class WsClient : public QObject
{
    Q_OBJECT
public:
    explicit WsClient(QObject *parent = nullptr);
    ~WsClient() override;

    // false (+ reason) for URLs that are not allowed (e.g. ws:// to a non-loopback host)
    // tolerateCertErrors: connect even if the certificate cannot be verified (wrong PC clock, outdated root
    // certificates, HTTPS scanning antivirus). Only for channels whose content is end-to-end protected anyway.
    bool open(const QUrl &url, bool allowInsecureLoopback, QString *why = nullptr, bool tolerateCertErrors = false);
    void close(quint16 code = 1000);
    bool isOpen() const { return m_state == Open; }
    bool sendText(const QByteArray &utf8);

    static bool isLoopbackHost(const QString &host);
    // UTC seconds from the Date header of the handshake response (0 = none)
    qint64 serverTime() const { return m_serverTime; }
    bool certErrorsTolerated() const { return m_certTolerated; }

signals:
    void opened();
    void textMessage(const QByteArray &utf8);
    // httpStatus: status of a refused handshake (e.g. 503 when the free quota is used up), otherwise 0
    void closed(int code, const QString &reason, int httpStatus);

private:
    enum State { Idle, Connecting, Handshake, Open, Closing, Closed };
    void onConnected();
    void onReadyRead();
    void onSocketError();
    bool parseFrames();
    void writeFrame(quint8 opcode, const QByteArray &payload);
    void finish(int code, const QString &reason, int httpStatus = 0);

    QTcpSocket *m_sock = nullptr;
    QUrl m_url;
    QByteArray m_buf, m_frag, m_key;
    State m_state = Idle;
    bool m_fragActive = false;
    bool m_finished = true;
    qint64 m_serverTime = 0;
    bool m_certTolerated = false;
    QTimer m_handshakeTimer;
    QTimer m_pingTimer;
};

} // namespace uve::watchparty
