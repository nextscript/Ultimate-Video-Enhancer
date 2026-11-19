#include "MiniWebSocket.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

static const char kWsGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"; // RFC 6455

MiniWebSocket::MiniWebSocket(QObject *parent) : QObject(parent)
{
    m_handshakeTimer.setSingleShot(true);
    m_handshakeTimer.setInterval(15000);
    connect(&m_handshakeTimer, &QTimer::timeout, this, [this] {
        if (m_state != Open) fail(QStringLiteral("Zeitüberschreitung beim Verbindungsaufbau"));
    });

    m_pingTimer.setInterval(30000);
    connect(&m_pingTimer, &QTimer::timeout, this, [this] {
        if (m_state == Open) writeFrame(0x9, QByteArray());
    });
}

MiniWebSocket::~MiniWebSocket() { abortQuiet(); }

// Pro Verbindungsversuch ein brandneuer QSslSocket: wiederverwendete Sockets nach abort()
// behalten in Qt6 gern hängende TLS-/Schreibzustände.
void MiniWebSocket::newSocket()
{
    if (m_sock) {
        m_sock->disconnect(this);
        m_sock->abort();
        m_sock->deleteLater();
    }
    m_sock = new QSslSocket(this);
    connect(m_sock, &QSslSocket::encrypted,        this, &MiniWebSocket::onEncrypted);
    connect(m_sock, &QAbstractSocket::readyRead,   this, &MiniWebSocket::onReadyRead);
    connect(m_sock, &QAbstractSocket::errorOccurred, this, &MiniWebSocket::onSocketError);
    connect(m_sock, &QSslSocket::sslErrors,        this, &MiniWebSocket::onSslErrors);
}

void MiniWebSocket::open(const QString &host, quint16 port, const QString &path)
{
    abortQuiet(); // Timer/Fuffer zurücksetzen, alte Verbindung entsorgen
    newSocket();  // frischer Socket pro Verbindungsversuch
    m_host = host; m_path = path;
    m_buf.clear(); m_frag.clear(); m_fragActive = false; m_tlsError.clear(); m_closeEmitted = false;
    m_state = TlsConnecting;

    QByteArray keyBytes(16, '\0');
    for (int i = 0; i < 16; ++i) keyBytes[i] = char(quint8(QRandomGenerator::global()->bounded(256)));
    m_key = keyBytes.toBase64();

    m_handshakeTimer.start();
    m_sock->connectToHostEncrypted(host, port);
}

void MiniWebSocket::close(quint16 code)
{
    if (m_state == Open) {
        QByteArray payload;
        payload.append(char((code >> 8) & 0xFF));
        payload.append(char(code & 0xFF));
        writeFrame(0x8, payload);
        m_state = Closing;
        m_pingTimer.stop();
        m_sock->flush();
        emitClosed(QStringLiteral("lokal geschlossen"));
    }
    abortQuiet();
}

bool MiniWebSocket::sendText(const QString &text)
{
    if (m_state != Open) return false;
    writeFrame(0x1, text.toUtf8());
    return true;
}

QString MiniWebSocket::stateName() const
{
    switch (m_state) {
    case Idle:          return QStringLiteral("Idle");
    case TlsConnecting: return QStringLiteral("TlsConnecting");
    case Handshake:     return QStringLiteral("Handshake");
    case Open:          return QStringLiteral("Open");
    case Closing:       return QStringLiteral("Closing");
    case Closed:        return QStringLiteral("Closed");
    }
    return QStringLiteral("?");
}

// ------------------------------------------------------------ TLS / Handshake
void MiniWebSocket::onEncrypted()
{
    if (m_state != TlsConnecting) return;
    m_state = Handshake;

    QByteArray req;
    req += "GET " + m_path.toUtf8() + " HTTP/1.1\r\n";
    req += "Host: " + m_host.toUtf8() + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "Sec-WebSocket-Key: " + m_key + "\r\n";
    req += "\r\n";
    m_sock->write(req);
}

void MiniWebSocket::onReadyRead()
{
    m_buf += m_sock->readAll();

    if (m_state == Handshake) {
        const int sep = m_buf.indexOf("\r\n\r\n");
        if (sep < 0) {
            if (m_buf.size() > 16384) fail(QStringLiteral("Handshake-Antwort zu groß"));
            return;
        }
        const QByteArray head = m_buf.left(sep);
        m_buf.remove(0, sep + 4);

        const QList<QByteArray> lines = head.split('\n');
        if (lines.isEmpty()) { fail(QStringLiteral("leere Handshake-Antwort")); return; }
        const QString status = QString::fromLatin1(lines.first()).trimmed();
        if (!status.contains(QLatin1String(" 101"))) {
            fail(QStringLiteral("WebSocket-Handshake abgelehnt: %1").arg(status));
            return;
        }
        QString accept;
        for (int i = 1; i < lines.size(); ++i) {
            const QString line = QString::fromLatin1(lines[i]).trimmed();
            const int c = line.indexOf(QLatin1Char(':'));
            if (c <= 0) continue;
            const QString key = line.left(c).toLower();
            const QString val = line.mid(c + 1).trimmed();
            if (key == QLatin1String("sec-websocket-accept")) accept = val;
        }
        const QByteArray expected = QCryptographicHash::hash(m_key + QByteArray(kWsGuid),
                                                             QCryptographicHash::Sha1).toBase64();
        if (accept != QString::fromLatin1(expected)) {
            fail(QStringLiteral("ungültiges Sec-WebSocket-Accept"));
            return;
        }

        m_state = Open;
        m_handshakeTimer.stop();
        m_pingTimer.start();
        emit opened();
        if (!m_buf.isEmpty() && !parseFrames()) return;
        return;
    }

    if (m_state == Open || m_state == Closing) parseFrames();
}

// ------------------------------------------------------------ Frame-Layer
bool MiniWebSocket::parseFrames()
{
    while (m_state == Open || m_state == Closing) {
        if (m_buf.size() < 2) return true;
        const quint8 b0 = quint8(m_buf.at(0)), b1 = quint8(m_buf.at(1));
        const bool fin = b0 & 0x80;
        const quint8 op = b0 & 0x0F;
        if (b1 & 0x80) { fail(QStringLiteral("maskierte Server-Frames sind unzulässig")); return false; }

        quint64 len = b1 & 0x7F;
        int hdr = 2;
        if (len == 126) {
            if (m_buf.size() < 4) return true;
            len = (quint64(quint8(m_buf.at(2))) << 8) | quint8(m_buf.at(3));
            hdr = 4;
        } else if (len == 127) {
            if (m_buf.size() < 10) return true;
            len = 0;
            for (int i = 0; i < 8; ++i) len = (len << 8) | quint8(m_buf.at(2 + i));
            hdr = 10;
        }
        if (len > (16ULL << 20)) { fail(QStringLiteral("Frame zu groß")); return false; }
        if (quint64(m_buf.size() - hdr) < len) return true;

        const QByteArray payload = m_buf.mid(hdr, int(len));
        m_buf.remove(0, hdr + int(len));

        switch (op) {
        case 0x1: // Text
            if (m_fragActive) { fail(QStringLiteral("Frame-Fehler (verschachtelte Textframes)")); return false; }
            if (fin) emit textMessage(QString::fromUtf8(payload));
            else { m_fragActive = true; m_frag = payload; }
            break;
        case 0x0: // Fortsetzung
            if (!m_fragActive) break;
            m_frag += payload;
            if (fin) { m_fragActive = false; emit textMessage(QString::fromUtf8(m_frag)); m_frag.clear(); }
            break;
        case 0x2: // Binärdaten – für IRC nicht erwartet, ignorieren
            break;
        case 0x8: // Close
        {
            quint16 code = payload.size() >= 2
                ? quint16((quint16(quint8(payload.at(0))) << 8) | quint8(payload.at(1))) : 1005;
            if (m_state == Open) { // Close-Rahmen zurückgeben
                QByteArray echo;
                echo.append(char((code >> 8) & 0xFF)); echo.append(char(code & 0xFF));
                writeFrame(0x8, echo);
            }
            m_state = Closed;
            m_pingTimer.stop();
            m_handshakeTimer.stop();
            emitClosed(QStringLiteral("Gegenüber geschlossen (Code %1)").arg(code));
            return false;
        }
        case 0x9: // Ping → Pong
            writeFrame(0xA, payload);
            break;
        case 0xA: // Pong
            break;
        default:
            fail(QStringLiteral("unbekannter Opcode %1").arg(op));
            return false;
        }
    }
    return true;
}

void MiniWebSocket::writeFrame(quint8 opcode, const QByteArray &payload)
{
    if (m_state != Open && m_state != Closing) return;

    QByteArray out;
    out.append(char(0x80 | opcode));
    const int n = payload.size();
    if (n < 126) {
        out.append(char(0x80 | n));
    } else if (n < 65536) {
        out.append(char(0x80 | 126));
        out.append(char((n >> 8) & 0xFF));
        out.append(char(n & 0xFF));
    } else {
        out.append(char(0x80 | 127));
        for (int i = 7; i >= 0; --i) out.append(char((qint64(n) >> (8 * i)) & 0xFF));
    }
    char mask[4];
    for (int i = 0; i < 4; ++i) mask[i] = char(quint8(QRandomGenerator::global()->bounded(256)));
    out.append(mask, 4);
    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i) masked[i] = char(quint8(masked.at(i)) ^ quint8(mask[i % 4]));
    out.append(masked);
    m_sock->write(out);
}

// ------------------------------------------------------------ Fehler / Ende
void MiniWebSocket::onSocketError()
{
    if (m_state == Closed || m_state == Idle) return;
    const QString why = m_tlsError.isEmpty() ? m_sock->errorString() : m_tlsError;
    if (m_state == Open || m_state == Closing) emitClosed(why);
    else fail(why);
}

void MiniWebSocket::onSslErrors(const QList<QSslError> &errors)
{
    // Zertifikatsfehler NICHT ignorieren – Text für die Fehlermeldung merken, Qt bricht dann ab.
    if (!errors.isEmpty()) m_tlsError = QStringLiteral("TLS-Fehler: ") + errors.first().errorString();
}

void MiniWebSocket::fail(const QString &why)
{
    if (m_state == Closed && m_closeEmitted) return;
    m_state = Closed;
    m_handshakeTimer.stop();
    m_pingTimer.stop();
    m_sock->abort();
    emit failed(why);
}

void MiniWebSocket::emitClosed(const QString &why)
{
    if (m_closeEmitted) return;
    m_closeEmitted = true;
    m_handshakeTimer.stop();
    m_pingTimer.stop();
    emit closed(why);
}

void MiniWebSocket::abortQuiet()
{
    m_handshakeTimer.stop();
    m_pingTimer.stop();
    m_buf.clear(); m_frag.clear(); m_fragActive = false;
    if (m_sock && m_state != Idle) m_sock->abort();
    m_state = Idle;
    m_closeEmitted = false;
    m_tlsError.clear();
}
