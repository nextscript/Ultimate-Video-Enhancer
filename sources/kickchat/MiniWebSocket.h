#pragma once
// Minimaler WebSocket-Client (RFC 6455) auf Basis von QSslSocket – bewusst OHNE Qt-WebSockets-Modul,
// weil die Player-Distribution nur Qt6Network (mit TLS-Backends) enthält.
// Unterstützt: wss-Handshake, Text-Frames (auch fragmentiert), Ping/Pong, Close, maskierte Client-Frames.

#include <QByteArray>
#include <QObject>
#include <QSslSocket>
#include <QString>
#include <QTimer>

class MiniWebSocket : public QObject
{
    Q_OBJECT
public:
    explicit MiniWebSocket(QObject *parent = nullptr);
    ~MiniWebSocket() override;

    void open(const QString &host, quint16 port, const QString &path = QStringLiteral("/"));
    void close(quint16 code = 1000);
    bool isOpen() const { return m_state == Open; }
    bool sendText(const QString &text);

    // Diagnose für Testprogramme
    QString stateName() const;
    int socketState() const { return m_sock ? int(m_sock->state()) : -1; }
    int pendingBytes() const { return int(m_buf.size()); }

signals:
    void opened();
    void textMessage(const QString &text);
    void closed(const QString &reason);
    void failed(const QString &reason);

private:
    enum State { Idle, TlsConnecting, Handshake, Open, Closing, Closed };

    void onEncrypted();
    void onReadyRead();
    void onSocketError();
    void onSslErrors(const QList<QSslError> &errors);

    bool parseFrames();                    // true = weitermachen erlaubt, false = Verbindung beendet
    void writeFrame(quint8 opcode, const QByteArray &payload);
    void fail(const QString &why);
    void abortQuiet();
    void emitClosed(const QString &why);

    void newSocket();                    // pro Verbindungsversuch ein frischer Socket

    QSslSocket *m_sock = nullptr;
    QByteArray m_buf, m_frag, m_key;
    QString m_host, m_path, m_tlsError;
    State m_state = Idle;
    bool m_fragActive = false;
    bool m_closeEmitted = false;
    QTimer m_handshakeTimer, m_pingTimer;
};
