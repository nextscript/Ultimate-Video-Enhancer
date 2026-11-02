// Proxy-Konfiguration und lokale Proxy-Brücke – ohne Internet: Mini-SOCKS5-Server und Zielserver laufen im Test.
#include "services/ProxyBridge.h"
#include "services/ProxyConfig.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace uve;

// Minimaler SOCKS5-Server (RFC 1928, ohne Authentifizierung, nur CONNECT mit Domainname/IPv4)
class MiniSocks5 : public QObject
{
public:
    QTcpServer server;
    QStringList requested;   // angefragte Ziele (Hostname so, wie der Client ihn sendet)
    MiniSocks5()
    {
        server.setProxy(QNetworkProxy::NoProxy);
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto *c = server.nextPendingConnection()) handle(c);
        });
    }
    void handle(QTcpSocket *c)
    {
        c->setProperty("stage", 0);
        connect(c, &QTcpSocket::readyRead, this, [this, c] {
            QByteArray buf = c->property("buf").toByteArray() + c->readAll();
            const int stage = c->property("stage").toInt();
            if (stage == 0) {
                if (buf.size() < 2 || buf.size() < 2 + quint8(buf[1])) { c->setProperty("buf", buf); return; }
                c->write(QByteArray("\x05\x00", 2));
                buf = buf.mid(2 + quint8(buf[1]));
                c->setProperty("stage", 1);
            }
            if (c->property("stage").toInt() == 1) {
                if (buf.size() < 5) { c->setProperty("buf", buf); return; }
                QString host; int off = 4;
                if (quint8(buf[3]) == 3) { const int n = quint8(buf[4]); if (buf.size() < 5 + n + 2) { c->setProperty("buf", buf); return; }
                    host = QString::fromLatin1(buf.mid(5, n)); off = 5 + n; }
                else if (quint8(buf[3]) == 1) { if (buf.size() < 10) { c->setProperty("buf", buf); return; }
                    host = QHostAddress(qFromBigEndian<quint32>(buf.constData() + 4)).toString(); off = 8; }
                const quint16 port = (quint8(buf[off]) << 8) | quint8(buf[off + 1]);
                requested << host + ':' + QString::number(port);
                auto *up = new QTcpSocket(c);
                up->setProxy(QNetworkProxy::NoProxy);
                connect(up, &QTcpSocket::connected, c, [c, up, rest = buf.mid(off + 2)] {
                    c->write(QByteArray("\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10));
                    c->setProperty("stage", 2);
                    if (!rest.isEmpty()) up->write(rest);
                    QObject::connect(up, &QTcpSocket::readyRead, c, [c, up] { c->write(up->readAll()); });
                });
                connect(up, &QTcpSocket::disconnected, c, [c] { c->disconnectFromHost(); });
                // "localtarget" wird am Proxy aufgelöst (beweist: kein lokaler DNS-Lookup durch den Client)
                up->connectToHost(host == "localtarget" ? QStringLiteral("127.0.0.1") : host, port);
                c->setProperty("up", QVariant::fromValue<QObject *>(up));
                c->setProperty("buf", QByteArray());
                return;
            }
            if (auto *up = qobject_cast<QTcpSocket *>(c->property("up").value<QObject *>())) up->write(buf);
        });
    }
};

// Einfacher Zielserver: antwortet auf jede Zeile mit "ECHO:<zeile>"
class EchoServer : public QObject
{
public:
    QTcpServer server;
    EchoServer()
    {
        server.setProxy(QNetworkProxy::NoProxy);
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto *c = server.nextPendingConnection())
                connect(c, &QTcpSocket::readyRead, c, [c] { while (c->canReadLine()) c->write("ECHO:" + c->readLine()); });
        });
    }
};

static QByteArray readUntil(QTcpSocket &s, const QByteArray &needle, int ms = 5000)
{
    QByteArray got;
    QElapsedTimer t; t.start();
    while (!got.contains(needle) && t.elapsed() < ms) {
        if (!s.bytesAvailable()) QTest::qWait(10);
        got += s.readAll();
    }
    return got;
}

class TstProxy : public QObject
{
    Q_OBJECT
private slots:
    void validateAndUrl()
    {
        ProxyConfig c;
        QVERIFY(c.validate().isEmpty());          // none
        QVERIFY(c.url().isEmpty());
        c.mode = "manual"; c.type = "socks5"; c.host = "albcloud.example"; c.port = 2888;
        QVERIFY(c.validate().isEmpty());
        QCOMPARE(c.url(), QString("socks5://albcloud.example:2888"));
        c.user = "ich"; c.password = "p@ss:w/rd";
        QCOMPARE(c.url(), QString("socks5://ich:p%40ss%3Aw%2Frd@albcloud.example:2888")); // Sonderzeichen kodiert
        QCOMPARE(c.url(false), QString("socks5://albcloud.example:2888"));                 // für Protokoll/Anzeige
        c.type = "http";
        QVERIFY(c.url().startsWith("http://"));
        c.host = "http://x"; QVERIFY(!c.validate().isEmpty());
        c.host = "x"; c.port = 0; QVERIFY(!c.validate().isEmpty());
        c.port = 80; c.type = "ftp"; QVERIFY(!c.validate().isEmpty());
        QCOMPARE(c.toQNetworkProxy().type(), QNetworkProxy::NoProxy); // ungültig → keiner
    }

    void passwordIsNotStoredInPlainText()
    {
        ProxyConfig c; c.mode = "manual"; c.host = "h"; c.port = 1; c.user = "u"; c.password = "geheim123";
        const auto json = c.toJson();
        QVERIFY(!json.value("password").toString().contains("geheim123"));
#ifdef Q_OS_WIN
        QVERIFY(json.value("password").toString().startsWith("dpapi:"));
#endif
        QCOMPARE(ProxyConfig::fromJson(json).password, QString("geheim123"));
    }

    void bridgeTunnelsThroughSocks5()
    {
        MiniSocks5 socks;
        EchoServer echo;
        ProxyBridge bridge;
        QVERIFY(bridge.start(QNetworkProxy(QNetworkProxy::Socks5Proxy, "127.0.0.1", socks.server.serverPort())));
        const QUrl pu(bridge.proxyUrl());
        QCOMPARE(pu.host(), QString("127.0.0.1"));        // nur Loopback
        QVERIFY(pu.password().size() >= 32);              // Zufallszugang

        // 1) ohne Zugangsdaten → 407
        {
            QTcpSocket s; s.setProxy(QNetworkProxy::NoProxy);
            s.connectToHost("127.0.0.1", quint16(pu.port()));
            QVERIFY(s.waitForConnected(3000));
            s.write("CONNECT localtarget:" + QByteArray::number(echo.server.serverPort()) + " HTTP/1.1\r\n\r\n");
            QVERIFY(readUntil(s, "\r\n\r\n").startsWith("HTTP/1.1 407"));
        }
        // 2) CONNECT mit Zugangsdaten → Tunnel über SOCKS5, Hostname am Proxy aufgelöst
        {
            QTcpSocket s; s.setProxy(QNetworkProxy::NoProxy);
            s.connectToHost("127.0.0.1", quint16(pu.port()));
            QVERIFY(s.waitForConnected(3000));
            const QByteArray auth = (pu.userName() + ':' + pu.password()).toLatin1().toBase64();
            s.write("CONNECT localtarget:" + QByteArray::number(echo.server.serverPort()) + " HTTP/1.1\r\nProxy-Authorization: Basic " + auth + "\r\n\r\n");
            QVERIFY(readUntil(s, "\r\n\r\n").startsWith("HTTP/1.1 200"));
            s.write("hallo\n");
            QVERIFY(readUntil(s, "ECHO:hallo").contains("ECHO:hallo"));
            QVERIFY(socks.requested.contains("localtarget:" + QString::number(echo.server.serverPort())));
        }
        // 3) tcp://-Weiterleitung
        {
            const quint16 lp = bridge.forwardTcp("localtarget", echo.server.serverPort());
            QVERIFY(lp > 0);
            QCOMPARE(bridge.forwardTcp("localtarget", echo.server.serverPort()), lp); // wiederverwendet
            QTcpSocket s; s.setProxy(QNetworkProxy::NoProxy);
            s.connectToHost("127.0.0.1", lp);
            QVERIFY(s.waitForConnected(3000));
            s.write("tcp\n");
            QVERIFY(readUntil(s, "ECHO:tcp").contains("ECHO:tcp"));
        }
        QVERIFY(bridge.stats().connections >= 2);
        QVERIFY(bridge.stats().bytesDown > 0);
    }

    // Optional mit echtem Proxy: UVE_TEST_LIVE_PROXY=socks5://host:port (sonst übersprungen)
    void liveProxyChangesPublicIp()
    {
        const QUrl live(qEnvironmentVariable("UVE_TEST_LIVE_PROXY"));
        if (live.host().isEmpty()) QSKIP("UVE_TEST_LIVE_PROXY nicht gesetzt");
        ProxyConfig c; c.mode = "manual"; c.type = live.scheme() == "http" ? "http" : "socks5"; c.host = live.host(); c.port = live.port();
        auto fetch = [](const QNetworkProxy &p) {
            QNetworkAccessManager nam; nam.setProxy(p);
            QNetworkRequest req(QUrl("https://api.ipify.org")); req.setTransferTimeout(15000);
            auto *r = nam.get(req);
            QSignalSpy done(r, &QNetworkReply::finished);
            done.wait(16000);
            const QByteArray ip = r->readAll().trimmed();
            r->deleteLater();
            return ip;
        };
        const QByteArray direct = fetch(QNetworkProxy::NoProxy), proxied = fetch(c.toQNetworkProxy());
        qInfo() << "öffentliche IP direkt:" << direct << "über Proxy:" << proxied;
        QVERIFY(!proxied.isEmpty());
        QVERIFY(proxied != direct);
    }
};

QTEST_GUILESS_MAIN(TstProxy)
#include "tst_proxy.moc"
