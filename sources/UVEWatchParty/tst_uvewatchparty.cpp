// Tests: sync math, room ids, ranges, manifest, cache, local HTTP interface and a three-peer swarm over real
// WebRTC DataChannels (libdatachannel) on this machine: A has the file, B fetches a block from A, C the same block from B.
#include "BlockCache.h"
#include "Crypto.h"
#include "Nostr.h"
#include "RoomHost.h"
#include "Manifest.h"
#include "StreamServer.h"
#include "Swarm.h"
#include "SyncLogic.h"
#include "Tunnel.h"
#include "WsClient.h"
#include "WatchPartyController.h"
#include "PluginApi.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QCryptographicHash>
#include <QDateTime>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace uve::watchparty;

// Small HTTP origin standing in for a video site / CDN (checks the headers the source must send)
static bool pollUntil(const std::function<bool()> &cond, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) { if (t.elapsed() > ms) return false; QTest::qWait(20); }
    return true;
}
    // ---------------------------------------------------------------- media tunnel (links)
struct Origin : QTcpServer {
        QByteArray video;
        QList<QByteArray> requests;
        Origin() { connect(this, &QTcpServer::newConnection, this, [this] { while (auto *s = nextPendingConnection()) serve(s); }); }
        QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(serverPort()); }
        void serve(QTcpSocket *s)
        {
            connect(s, &QTcpSocket::readyRead, s, [this, s] {
                static QHash<QTcpSocket *, QByteArray> bufs;
                QByteArray &b = bufs[s];
                b += s->readAll();
                if (!b.contains("\r\n\r\n")) return;
                const QByteArray head = b;
                bufs.remove(s);
                requests << head;
                const QByteArray path = head.split(' ').value(1);
                auto reply = [s](const QByteArray &status, const QByteArray &type, const QByteArray &body, const QByteArray &extra = {}) {
                    s->write("HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n" + extra + "Connection: close\r\n\r\n" + body);
                    s->disconnectFromHost();
                };
                // the source must fetch with the headers of its own player
                if (!head.contains("User-Agent: UVETest/1") || !head.contains("Referer: https://site.example/watch") || !head.contains("Cookie: s=1")) return reply("403 Forbidden", "text/plain", "headers missing");
                if (path.startsWith("/video.mp4")) {
                    QByteArray range;
                    for (const auto &l : head.split('\n')) if (l.toLower().startsWith("range:")) range = l.mid(6).trimmed();
                    const auto r = sync::parseRange(range, video.size());
                    if (r.type == sync::ByteRange::Range)
                        return reply("206 Partial Content", "video/mp4", video.mid(r.start, r.end - r.start + 1),
                                     "Accept-Ranges: bytes\r\nContent-Range: bytes " + QByteArray::number(r.start) + "-" + QByteArray::number(r.end) + "/" + QByteArray::number(video.size()) + "\r\n");
                    return reply("200 OK", "video/mp4", video, "Accept-Ranges: bytes\r\n");
                }
                if (path == "/hls/master.m3u8") return reply("200 OK", "application/vnd.apple.mpegurl", "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nlow/v.m3u8\n");
                if (path == "/hls/low/v.m3u8")
                    return reply("200 OK", "application/vnd.apple.mpegurl",
                                 "#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:2,\nseg1.ts\n#EXTINF:2,\n/hls/low/seg2.ts\n#EXTINF:2,\n" + base().toUtf8() + "/hls/low/seg3.ts\n#EXT-X-ENDLIST\n");
                if (path.startsWith("/hls/low/")) return reply("200 OK", "video/mp2t", "SEGMENT:" + path);
                reply("404 Not Found", "text/plain", "");
            });
        }
    };

static QByteArray httpGet(const QString &url, const QByteArray &range = {})
    {
        const QUrl u(url);
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, quint16(u.port()));
        if (!pollUntil([&] { return s.state() == QAbstractSocket::ConnectedState; }, 3000)) return {};
        s.write("GET " + u.path(QUrl::FullyEncoded).toUtf8() + (u.hasQuery() ? "?" + u.query(QUrl::FullyEncoded).toUtf8() : QByteArray())
                + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" + (range.isEmpty() ? QByteArray() : "Range: " + range + "\r\n") + "\r\n");
        QByteArray all;
        pollUntil([&] { all += s.readAll(); return s.state() == QAbstractSocket::UnconnectedState; }, 20000);
        return all + s.readAll();
    }
static QByteArray bodyOf(const QByteArray &r) { return r.mid(r.indexOf("\r\n\r\n") + 4); }


class TstWatchParty : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_tmp;
    QString m_file;
    Manifest m_manifest;
    QByteArray m_data;

    // Wire two swarms' signaling directly (stands in for the signaling service)
    static void link(Swarm *a, int idA, Swarm *b, int idB)
    {
        QObject::connect(a, &Swarm::signalOut, b, [b, idA, idB](int to, const QString &k, const QString &d, const QString &m) { if (to == idB) b->onSignal(idA, k, d, m); });
        QObject::connect(b, &Swarm::signalOut, a, [a, idA, idB](int to, const QString &k, const QString &d, const QString &m) { if (to == idA) a->onSignal(idB, k, d, m); });
    }

    SwarmConfig config(const QString &name) const
    {
        SwarmConfig c;
        c.cacheDir = m_tmp.filePath(name);
        c.cacheBytes = 64LL << 20;
        return c;
    }

    static bool waitFor(const std::function<bool()> &cond, int ms)
    {
        QElapsedTimer t;
        t.start();
        while (!cond()) {
            if (t.elapsed() > ms) return false;
            QTest::qWait(20);
        }
        return true;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        m_data.resize(3 * (1 << 20) + 12345);           // 4 blocks, the last one short
        for (auto &c : m_data) c = char(QRandomGenerator::global()->bounded(256));
        m_file = m_tmp.filePath(QStringLiteral("film.mkv"));
        QFile f(m_file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(m_data);
        f.close();
        ManifestBuilder b;
        QSignalSpy done(&b, &ManifestBuilder::finished);
        b.start(m_file);
        QVERIFY(done.wait(10000));
        m_manifest = done.at(0).at(1).value<Manifest>();
    }

    void roomIds()
    {
        QCOMPARE(sync::normalizeRoomId(QStringLiteral("k7m3qx")), QStringLiteral("K7M3QX"));
        QCOMPARE(sync::normalizeRoomId(QStringLiteral(" K7M3QX ")), QStringLiteral("K7M3QX"));
        QCOMPARE(sync::normalizeRoomId(QStringLiteral("k7m-3qx")), QStringLiteral("K7M3QX"));    // pasted with a dash
        QVERIFY(sync::normalizeRoomId(QStringLiteral("K7M3Q0")).isEmpty());                     // 0 not in the alphabet
        QVERIFY(sync::normalizeRoomId(QStringLiteral("K7M3Q")).isEmpty());
        QVERIFY(sync::normalizeRoomId(QStringLiteral("K7M3QXA")).isEmpty());
    }

    void targetPosition()
    {
        sync::RoomState s;
        s.paused = false; s.position = 10; s.ref = 1000;
        QCOMPARE(sync::targetPosition(s, 6000, 100), 15.0);
        QCOMPARE(sync::targetPosition(s, 600000, 100), 100.0);
        s.paused = true;
        QCOMPARE(sync::targetPosition(s, 6000, 100), 10.0);
        QVERIFY(sync::durationMismatch(5400, 5300));
        QVERIFY(!sync::durationMismatch(5400, 5401.5));
        QVERIFY(!sync::durationMismatch(0, 5400));
    }

    void driftCorrection()
    {
        sync::DriftController d;
        QCOMPARE(d.update(0.2, true).kind, sync::DriftController::Keep);      // within 250 ms tolerance
        QCOMPARE(d.update(0.6, true).kind, sync::DriftController::Keep);      // debounce: first sample
        auto r = d.update(0.6, true);
        QCOMPARE(r.kind, sync::DriftController::SetSpeed);
        QVERIFY(r.speed < 1.0);                                              // ahead → slower
        QCOMPARE(d.update(0.3, true).kind, sync::DriftController::Keep);
        r = d.update(0.05, true);
        QCOMPARE(r.kind, sync::DriftController::SetSpeed);
        QCOMPARE(r.speed, 1.0);                                              // settled → 1×
        d.update(5.0, true);
        QCOMPARE(d.update(5.0, true).kind, sync::DriftController::Seek);     // large drift → seek
        r = d.update(-0.8, true); r = d.update(-0.8, true);
        QVERIFY(r.speed > 1.0);                                              // behind → faster
        QCOMPARE(d.update(0, false).speed, 1.0);
    }

    void playerObserver()
    {
        sync::PlayerObserver o;
        o.reset(10, false, 0);
        auto c = o.update(10.25, false, 1.0, 250);                            // normal progress
        QVERIFY(!c.seeked && !c.paused && !c.played);
        c = o.update(60, false, 1.0, 500);                                   // user seek
        QVERIFY(c.seeked);
        o.expect(120, true, 750, true);                                      // plugin's own seek + pause
        c = o.update(120, true, 1.0, 1000);
        QVERIFY(!c.seeked && !c.paused);                                     // no feedback to the server
        c = o.update(120, false, 1.0, 5000);                                 // user presses play later
        QVERIFY(c.played);
    }

    void ranges()
    {
        using R = sync::ByteRange;
        QCOMPARE(sync::parseRange("", 1000).type, R::Full);
        auto r = sync::parseRange("bytes=0-99", 1000); QCOMPARE(r.type, R::Range); QCOMPARE(r.start, 0); QCOMPARE(r.end, 99);
        r = sync::parseRange("bytes=500-", 1000); QCOMPARE(r.start, 500); QCOMPARE(r.end, 999);
        r = sync::parseRange("bytes=-100", 1000); QCOMPARE(r.start, 900); QCOMPARE(r.end, 999);
        r = sync::parseRange("bytes=900-5000", 1000); QCOMPARE(r.end, 999);
        QCOMPARE(sync::parseRange("bytes=1000-", 1000).type, R::Unsatisfiable);
        QCOMPARE(sync::parseRange("bytes=-0", 1000).type, R::Unsatisfiable);
        QCOMPARE(sync::parseRange("bytes=5-1", 1000).type, R::Full);
        QCOMPARE(sync::parseRange("bytes=0-1,5-6", 1000).type, R::Full);
    }

    void manifest()
    {
        QCOMPARE(m_manifest.blockCount(), 4);
        QCOMPARE(m_manifest.blockLength(3), qint64(12345));
        QVERIFY(m_manifest.verifyBlock(0, m_data.left(1 << 20)));
        QByteArray bad = m_data.left(1 << 20); bad[5] = char(bad[5] ^ 1);
        QVERIFY(!m_manifest.verifyBlock(0, bad));
        const auto parsed = Manifest::parse(m_manifest.bytes);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->digest, m_manifest.digest);
        QVERIFY(!Manifest::parse(m_manifest.bytes.left(30)).has_value());
        QCOMPARE(Manifest::chooseBlockSize(100), quint32(1 << 20));
        QVERIFY(quint64(200) * 1024 * 1024 * 1024 / Manifest::chooseBlockSize(quint64(200) * 1024 * 1024 * 1024) < 32768);
    }

    void cache()
    {
        BlockCache c;
        QVERIFY(c.open(m_tmp.filePath(QStringLiteral("cachetest")), 8 * 16384, 16384, 1000));
        QList<int> evicted;
        for (int i = 0; i < 8; ++i) QVERIFY(c.put(i, QByteArray(16384, char('a' + i)), i, &evicted));
        QVERIFY(evicted.isEmpty());
        QCOMPARE(c.get(0, 100).at(0), 'a');                                  // touch block 0
        QVERIFY(c.put(8, QByteArray(100, 'z'), 101, &evicted));
        QCOMPARE(evicted, QList<int>{1});                                    // least recently used
        QCOMPARE(c.get(8, 102), QByteArray(100, 'z'));
        const QList<int> expired = c.expire(5000);
        QCOMPARE(expired.size(), c.count() == 0 ? 8 : int(expired.size()));
        const QString path = c.path();
        c.clear();
        QVERIFY(!QFile::exists(path));                                       // cache freed
    }

    void localHttp()
    {
        Swarm seed(1, QStringLiteral("s1"), m_manifest.digest, m_data.size(), m_manifest.blockSize, config(QStringLiteral("h")));
        QVERIFY(seed.setSeed(m_file, m_manifest));
        StreamServer server(&seed, QStringLiteral("Film 1.mkv"));
        QVERIFY(server.start());
        const QUrl u(server.url());
        QCOMPARE(u.host(), QStringLiteral("127.0.0.1"));
        auto request = [&](const QByteArray &method, const QByteArray &path, const QByteArray &range) {
            // The server lives in this thread: only wait through the event loop (no blocking waitFor*)
            QTcpSocket s;
            s.connectToHost(QHostAddress::LocalHost, quint16(u.port()));
            if (!waitFor([&] { return s.state() == QAbstractSocket::ConnectedState; }, 3000)) return QByteArray();
            s.write(method + ' ' + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" + (range.isEmpty() ? QByteArray() : "Range: " + range + "\r\n") + "\r\n");
            QByteArray all;
            waitFor([&] { all += s.readAll(); return s.state() == QAbstractSocket::UnconnectedState; }, 5000);
            all += s.readAll();
            return all;
        };
        const QByteArray path = u.path(QUrl::FullyEncoded).toUtf8();
        QByteArray r = request("HEAD", path, {});
        QVERIFY(r.startsWith("HTTP/1.1 200"));
        QVERIFY(r.contains("Content-Length: " + QByteArray::number(m_data.size())));
        QVERIFY(r.contains("Content-Type: video/x-matroska"));
        r = request("GET", path, "bytes=1048570-1048585");                     // across a block boundary
        QVERIFY(r.startsWith("HTTP/1.1 206"));
        QVERIFY(r.contains("Content-Range: bytes 1048570-1048585/" + QByteArray::number(m_data.size())));
        QCOMPARE(r.mid(r.indexOf("\r\n\r\n") + 4), m_data.mid(1048570, 16));
        r = request("GET", path, "bytes=-10");                                 // file end (index)
        QCOMPARE(r.mid(r.indexOf("\r\n\r\n") + 4), m_data.right(10));
        r = request("GET", path, "bytes=99999999-");
        QVERIFY(r.startsWith("HTTP/1.1 416"));
        QVERIFY(r.contains("Content-Range: bytes */"));
        r = request("GET", path, {});
        QVERIFY(r.startsWith("HTTP/1.1 200"));
        QCOMPARE(r.mid(r.indexOf("\r\n\r\n") + 4), m_data);
        QVERIFY(request("GET", "/wrongkey/film.mkv", {}).startsWith("HTTP/1.1 404"));   // session key required
    }

    void swarmThreePeers()
    {
        // A = source (1), B (2) and C (3) watch the stream
        Swarm a(1, QStringLiteral("share"), m_manifest.digest, m_data.size(), m_manifest.blockSize, config(QStringLiteral("a")));
        Swarm b(2, QStringLiteral("share"), m_manifest.digest, m_data.size(), m_manifest.blockSize, config(QStringLiteral("b")));
        Swarm c(3, QStringLiteral("share"), m_manifest.digest, m_data.size(), m_manifest.blockSize, config(QStringLiteral("c")));
        QVERIFY(a.setSeed(m_file, m_manifest));
        link(&a, 1, &b, 2);
        link(&a, 1, &c, 3);
        link(&b, 2, &c, 3);

        // Step 1: only A and B know each other – B gets the manifest and block 0 from A
        a.setParticipants({2});
        b.setParticipants({1});
        const int rb = b.openReader(0);
        QVERIFY2(waitFor([&] { return b.hasManifest() && !b.read(0, 16).isEmpty(); }, 30000), "B did not receive block 0 from A");
        QCOMPARE(b.read(0, 1 << 20), m_data.left(1 << 20));
        // B keeps reading ahead (blocks 1–3) from A: let that finish before counting A's upload
        QVERIFY(waitFor([&] { return b.haveCount() == b.blockCount(); }, 30000));
        QTest::qWait(300);
        b.closeReader(rb);
        const qint64 aUpBefore = a.uploaded();

        // Step 2: C only connects to B (A is out of reach for C) and gets the verified block from B
        b.setParticipants({1, 3});
        c.setParticipants({2});
        const int rc = c.openReader(0);
        QVERIFY2(waitFor([&] { return c.hasManifest() && !c.read(0, 16).isEmpty(); }, 30000), "C did not receive block 0 from B");
        QCOMPARE(c.read(0, 1 << 20), m_data.left(1 << 20));
        QVERIFY(b.uploaded() >= (1 << 20));
        QCOMPARE(a.uploaded(), aUpBefore);                                  // the seed did not send it again
        c.closeReader(rc);

        // Seek near the end (short last block) works through the swarm too
        const int rs = c.openReader(m_data.size() - 100);
        QVERIFY(waitFor([&] { return !c.read(m_data.size() - 100, 100).isEmpty(); }, 30000));
        QCOMPARE(c.read(m_data.size() - 100, 100), m_data.right(100));
        c.closeReader(rs);
        for (const auto &v : c.peerInfo()) {
            const QString st = v.toMap().value("state").toString();
            QVERIFY2(st == QLatin1String("direct") || st == QLatin1String("relay"), qPrintable(st));
        }
    }

    void tunnelRewrite()
    {
        QSet<QString> origins;
        const QByteArray out = Tunnel::rewritePlaylist("#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"https://keys.cdn.example/k\"\nseg1.ts\n/abs/seg2.ts\nhttps://other.example/seg3.ts?x=1\n",
                                                       QUrl("https://cdn.example/path/v.m3u8"), &origins);
        QVERIFY(!out.contains("https://"));                                  // nothing points past the tunnel
        QVERIFY(origins.contains("https://cdn.example") && origins.contains("https://other.example") && origins.contains("https://keys.cdn.example"));
        QVERIFY(out.contains("/path/seg1.ts") && out.contains("/abs/seg2.ts") && out.contains("/seg3.ts?x=1"));
        const QByteArray mpd = Tunnel::rewriteDash("<BaseURL>https://dash.example/a/</BaseURL><SegmentTemplate media=\"seg-$Number$.m4s\"/>", &origins);
        QVERIFY(!mpd.contains("https://dash.example") && mpd.contains("seg-$Number$.m4s"));
        QVERIFY(Tunnel::entryPathFor(QUrl("https://cdn.example/v.mp4?t=1")).startsWith("/o/"));
    }

    void tunnelRelaysLikeThePlayer()
    {
        Origin origin;
        QVERIFY(origin.listen(QHostAddress::LocalHost, 0));
        origin.video = m_data.left(700000);
        const Tunnel::Fetch fetch{"UVETest/1", "https://site.example/watch", {qMakePair(QByteArray("Cookie"), QByteArray("s=1"))}};
        const QUrl video(origin.base() + "/video.mp4");
        std::unique_ptr<Tunnel> src(Tunnel::createSource(1, "t1", video, fetch, config("ta"), nullptr));
        src->allowUrl(QUrl(origin.base() + "/hls/master.m3u8"));
        std::unique_ptr<Tunnel> view(Tunnel::createViewer(2, "t1", 1, Tunnel::entryPathFor(video), config("tb"), nullptr));
        QVERIFY(view);
        connect(src.get(), &Tunnel::signalOut, view.get(), [&](int to, const QString &k, const QString &d) { if (to == 2) view->onSignal(1, k, d); });
        connect(view.get(), &Tunnel::signalOut, src.get(), [&](int to, const QString &k, const QString &d) { if (to == 1) src->onSignal(2, k, d); });
        src->setParticipants({2});
        view->setParticipants({1});
        QVERIFY(view->url().startsWith("http://127.0.0.1:"));
        // byte ranges (seeking) and the complete file
        QByteArray r = httpGet(view->url(), "bytes=100000-100099");
        QVERIFY2(r.startsWith("HTTP/1.1 206"), r.left(200).constData());
        QCOMPARE(bodyOf(r), origin.video.mid(100000, 100));
        r = httpGet(view->url());
        QVERIFY(r.startsWith("HTTP/1.1 200"));
        QCOMPARE(bodyOf(r), origin.video);
        // HLS: playlists rewritten into the tunnel, segments (relative, root-relative, absolute) fetched through it
        const QString master = view->urlFor(Tunnel::entryPathFor(QUrl(origin.base() + "/hls/master.m3u8")));
        r = httpGet(master);
        QVERIFY(r.startsWith("HTTP/1.1 200"));
        const QByteArray variantLine = bodyOf(r).split('\n').value(2).trimmed();
        QVERIFY2(variantLine.startsWith("http://127.0.0.1:"), variantLine.constData());
        const QByteArray media = bodyOf(httpGet(QString::fromUtf8(variantLine)));
        int segs = 0;
        for (const QByteArray &l : media.split('\n')) {
            const QByteArray line = l.trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            QVERIFY2(line.startsWith("http://127.0.0.1:"), line.constData());
            QVERIFY(bodyOf(httpGet(QString::fromUtf8(line))).startsWith("SEGMENT:/hls/low/seg"));
            segs++;
        }
        QCOMPARE(segs, 3);
        QVERIFY(media.contains("URI=\"http://127.0.0.1:"));
        // the source never fetches addresses outside the shared medium
        const QString foreign = view->urlFor("/o/" + QString::fromLatin1(QByteArray("http://127.0.0.1:1").toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)) + "/admin");
        QVERIFY(httpGet(foreign).startsWith("HTTP/1.1 502"));
        for (const auto &req : origin.requests) QVERIFY(!req.contains("/admin"));
    }

    // Opt-in (network): YouTube with separate video and audio through the tunnel, addresses from yt-dlp
    void tunnelYouTube()
    {
        if (qEnvironmentVariable("WP_REAL_RELAYS") != QLatin1String("1")) QSKIP("set WP_REAL_RELAYS=1 for network tests");
        const QString ytdlp = qEnvironmentVariable("WP_YTDLP", "C:/Program Files/UVE Player/tools/yt-dlp/yt-dlp.exe");
        if (!QFileInfo::exists(ytdlp)) QSKIP("yt-dlp not found (WP_YTDLP)");
        QProcess p;
        p.start(ytdlp, {"-J", "--no-playlist", "--no-warnings", "-f", "bv*+ba/b", "https://www.youtube.com/watch?v=jNQXAC9IVRw"});
        QVERIFY(p.waitForFinished(120000));
        const QJsonArray req = QJsonDocument::fromJson(p.readAllStandardOutput()).object().value("requested_formats").toArray();
        QCOMPARE(req.size(), 2);
        const QUrl video(req.at(0).toObject().value("url").toString()), audio(req.at(1).toObject().value("url").toString());
        const QJsonObject h = req.at(0).toObject().value("http_headers").toObject();
        Tunnel::Fetch fetch{h.value("User-Agent").toString().toLatin1(), {}, {}};
        std::unique_ptr<Tunnel> src(Tunnel::createSource(1, "yt", video, fetch, config("ya"), nullptr));
        src->allowUrl(audio);
        std::unique_ptr<Tunnel> view(Tunnel::createViewer(2, "yt", 1, Tunnel::entryPathFor(video), config("yb"), nullptr));
        connect(src.get(), &Tunnel::signalOut, view.get(), [&](int to, const QString &k, const QString &d) { if (to == 2) view->onSignal(1, k, d); });
        connect(view.get(), &Tunnel::signalOut, src.get(), [&](int to, const QString &k, const QString &d) { if (to == 1) src->onSignal(2, k, d); });
        src->setParticipants({2});
        view->setParticipants({1});
        for (const QString &u : {view->url(), view->urlFor(Tunnel::entryPathFor(audio))}) {
            const QByteArray r = httpGet(u, "bytes=0-999");
            QVERIFY2(r.startsWith("HTTP/1.1 206"), r.left(300).constData());
            QCOMPARE(bodyOf(r).size(), 1000);
        }
    }

    void corruptedBlockRejected()
    {
        // A seed whose file content does not match the manifest: the viewer must never accept the block
        const QString bad = m_tmp.filePath(QStringLiteral("bad.mkv"));
        QFile f(bad);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QByteArray d = m_data; d[10] = char(d[10] ^ 0xff);
        f.write(d);
        f.close();
        Swarm a(1, QStringLiteral("x"), m_manifest.digest, m_data.size(), m_manifest.blockSize, config(QStringLiteral("xa")));
        Swarm b(2, QStringLiteral("x"), m_manifest.digest, m_data.size(), m_manifest.blockSize, config(QStringLiteral("xb")));
        QVERIFY(a.setSeed(bad, m_manifest));
        link(&a, 1, &b, 2);
        a.setParticipants({2});
        b.setParticipants({1});
        b.openReader(0);
        QVERIFY(waitFor([&] { return b.hasManifest(); }, 30000));
        QTest::qWait(3000);
        QVERIFY(b.read(0, 16).isEmpty());
        QVERIFY(!b.read(1 << 20, 16).isEmpty() || b.haveCount() >= 0);      // other (intact) blocks may arrive
    }
};

// ---------------------------------------------------------------- end to end (needs the signaling service)
// Fake UVE host: a simulated player clock plus the documented commands the plugin uses
class FakeHost : public uve::IPlayerHost
{
public:
    explicit FakeHost(const QString &dataDir) : m_dataDir(dataDir) { m_clock.start(); }
    WatchPartyController *controller = nullptr;
    QString path;
    double duration = 0, base = 0, speed = 1.0;
    bool paused = true;
    qint64 since = 0;
    int seeks = 0;
    QJsonObject settings;
    double position() const { return paused ? base : qMin(duration, base + double(m_clock.elapsed() - since) / 1000.0 * speed); }
    void rebase() { base = position(); since = m_clock.elapsed(); }
    void load(const QString &p, double d, const QString &src = {})
    {
        path = p; duration = d; base = 0; paused = false; since = m_clock.elapsed();
        const QString s = src.isEmpty() ? p : src;
        source = s;
        QTimer::singleShot(50, [this, p, d, s] { if (controller) controller->onEvent("media.loaded", {{"path", p}, {"source", s}, {"title", QFileInfo(p).fileName()}, {"duration", d}}); });
    }
    QString hostVersion() const override { return "1.3.2"; }
    int apiVersion() const override { return 1; }
    QJsonObject loadPluginSettings(const QString &) const override { return settings; }
    void savePluginSettings(const QString &, const QJsonObject &s) override { settings = s; }
    QString pluginDataDir(const QString &) const override { return m_dataDir; }
    void setShaderStage(const QString &, const QString &) override {}
    void showOsd(const QString &, int) override {}
    void log(const QString &, const QString &, const QString &) override {}
    QString currentMediaPath() const override { return path; }
    double currentPosition() const override { return position(); }
    bool isPaused() const override { return paused; }
    void setPaused(bool p) override { rebase(); paused = p; }
    QImage grabVideoFrame(int) override { return {}; }
    bool hasCommand(const QString &) const override { return true; }
    QVariant call(const QString &, const QString &cmd, const QVariantMap &a, QString *) override
    {
        if (cmd == "player.state") return QVariantMap{{"path", path}, {"source", source.isEmpty() ? path : source}, {"position", position()}, {"duration", duration}, {"paused", paused}, {"title", QFileInfo(path).fileName()}};
        if (cmd == "player.pause") { rebase(); paused = a.value("paused").toBool(); return paused; }
        if (cmd == "player.seek") { rebase(); base = a.value("seconds").toDouble(); seeks++; return true; }
        if (cmd == "player.load") { lastLoad = a.value("url").toString(); return true; }
        if (cmd == "player.stop") { path.clear(); return true; }
        if (cmd == "mpv.get") {
            const QString n = a.value("name").toString();
            if (n == "speed") return speed;
            if (mpv.contains(n)) return mpv.value(n);
            return n == "seekable" ? QVariant(seekable) : QVariant(false);
        }
        if (cmd == "mpv.set") { if (a.value("name") == "speed") { rebase(); speed = a.value("value").toDouble(); } return true; }
        if (cmd == "app.info") return QVariantMap{{"language", "de"}};
        return {};
    }
    QString lastLoad;
    bool seekable = true;
    QString source;
    QVariantMap mpv;

private:
    QString m_dataDir;
    QElapsedTimer m_clock;
};

// Minimal Nostr relay on 127.0.0.1 for deterministic end-to-end tests (REQ / EVENT / OK, #t and kinds filters)
class FakeRelay : public QObject
{
public:
    QTcpServer server;
    struct Conn { QByteArray buf; bool open = false; QMap<QString, QJsonObject> subs; };
    QHash<QTcpSocket *, Conn> conns;
    int events = 0;
    qint64 skew = 0;          // seconds the relay clock is ahead of this machine
    int refused = 0;
    ~FakeRelay() { for (auto *s : conns.keys()) s->disconnect(this); server.close(); }
    bool start() { connect(&server, &QTcpServer::newConnection, this, [this] { while (auto *s = server.nextPendingConnection()) accept(s); }); return server.listen(QHostAddress::LocalHost, 0); }
    QString url() const { return QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort()); }
    static void sendText(QTcpSocket *s, const QByteArray &p)
    {
        QByteArray f; f.append(char(0x81));
        if (p.size() < 126) f.append(char(p.size()));
        else if (p.size() < 65536) { f.append(char(126)); f.append(char(p.size() >> 8)); f.append(char(p.size() & 0xff)); }
        else { f.append(char(127)); for (int i = 7; i >= 0; --i) f.append(char((quint64(p.size()) >> (8 * i)) & 0xff)); }
        s->write(f + p);
    }
    void accept(QTcpSocket *s)
    {
        conns.insert(s, Conn{});
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onData(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] { conns.remove(s); s->deleteLater(); });
    }
    void onData(QTcpSocket *s)
    {
        Conn &c = conns[s];
        c.buf += s->readAll();
        if (!c.open) {
            const int e = c.buf.indexOf("\r\n\r\n");
            if (e < 0) return;
            QByteArray key;
            for (const auto &l : c.buf.left(e).split('\n')) if (l.toLower().startsWith("sec-websocket-key:")) key = l.mid(18).trimmed();
            c.buf.remove(0, e + 4);
            const QByteArray date = QDateTime::currentDateTimeUtc().addSecs(skew).toString(Qt::RFC2822Date).toLatin1();
            s->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nDate: " + date + "\r\nSec-WebSocket-Accept: "
                     + QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64() + "\r\n\r\n");
            c.open = true;
        }
        for (;;) {
            if (c.buf.size() < 6) return;
            const auto *p = reinterpret_cast<const uchar *>(c.buf.constData());
            const int op = p[0] & 0x0f;
            quint64 len = p[1] & 0x7f;
            int pos = 2;
            if (len == 126) { len = (quint64(p[2]) << 8) | p[3]; pos = 4; }
            else if (len == 127) { len = 0; for (int i = 0; i < 8; ++i) len = (len << 8) | p[2 + i]; pos = 10; }
            if (quint64(c.buf.size()) < pos + 4 + len) return;
            const QByteArray mask = c.buf.mid(pos, 4);
            QByteArray data = c.buf.mid(pos + 4, int(len));
            for (int i = 0; i < data.size(); ++i) data[i] = char(data[i] ^ mask[i % 4]);
            c.buf.remove(0, pos + 4 + int(len));
            if (op == 0x8) { s->disconnectFromHost(); return; }
            if (op == 0x9) { QByteArray f; f.append(char(0x8A)); f.append(char(data.size())); s->write(f + data); continue; }
            if (op == 0x1) onMessage(s, data);
        }
    }
    void onMessage(QTcpSocket *s, const QByteArray &text)
    {
        const QJsonArray a = QJsonDocument::fromJson(text).array();
        const QString t = a.at(0).toString();
        if (t == "REQ") { conns[s].subs.insert(a.at(1).toString(), a.at(2).toObject()); sendText(s, QJsonDocument(QJsonArray{"EOSE", a.at(1)}).toJson(QJsonDocument::Compact)); }
        else if (t == "CLOSE") conns[s].subs.remove(a.at(1).toString());
        else if (t == "EVENT") {
            const QJsonObject ev = a.at(1).toObject();
            const qint64 relayNow = QDateTime::currentSecsSinceEpoch() + skew;
            const bool inTime = std::abs(qint64(ev.value("created_at").toDouble()) - relayNow) <= 60;
            if (!inTime) refused++;
            const bool valid = NostrPool::verifyEvent(ev) && inTime;
            sendText(s, QJsonDocument(QJsonArray{"OK", ev.value("id"), valid, valid ? "" : "invalid: bad signature"}).toJson(QJsonDocument::Compact));
            if (!valid) return;
            events++;
            QString tag;
            for (const auto &tv : ev.value("tags").toArray()) if (tv.toArray().at(0).toString() == "t") tag = tv.toArray().at(1).toString();
            for (auto it = conns.begin(); it != conns.end(); ++it)
                for (auto sub = it->subs.cbegin(); sub != it->subs.cend(); ++sub)
                    if (sub->value("kinds").toArray().contains(ev.value("kind")) && sub->value("#t").toArray().contains(tag))
                        sendText(it.key(), QJsonDocument(QJsonArray{"EVENT", sub.key(), ev}).toJson(QJsonDocument::Compact));
        }
    }
};

class TstRoomHost : public QObject
{
    Q_OBJECT
    qint64 t = 1700000000000;
    RoomHost::Clock clock() { return [this] { return t; }; }
    static QJsonObject find(const QList<RoomHost::Out> &outs, const QString &type)
    {
        for (int i = outs.size() - 1; i >= 0; --i) if (outs.at(i).msg.value("type").toString() == type) return outs.at(i).msg;
        return {};
    }
private slots:
    void rightsAndState()
    {
        RoomHost h("ABCD-EFGH-JKMN", "Freitag Kino", true, "hostpk", "Ana", clock());
        auto out = h.handle("guestpk", QJsonObject{{"type", "hello"}, {"nick", "Ben"}});
        const QJsonObject w = find(out, "welcome");
        QCOMPARE(w.value("memberId").toInt(), 2);
        QCOMPARE(out.first().toPk, QStringLiteral("guestpk"));
        // Guests cannot control playback or change the medium
        QVERIFY(h.handle("guestpk", QJsonObject{{"type", "media"}, {"title", "X"}, {"duration", 10}}).isEmpty());
        h.handle("hostpk", QJsonObject{{"type", "media"}, {"title", "Film"}, {"duration", 600}});
        QVERIFY(h.handle("guestpk", QJsonObject{{"type", "action"}, {"gen", 1}, {"op", "play"}, {"position", 0}}).isEmpty());
        out = h.handle("hostpk", QJsonObject{{"type", "action"}, {"gen", 1}, {"op", "play"}, {"position", 10}});
        const QJsonObject st = find(out, "state").value("state").toObject();
        QCOMPARE(st.value("paused").toBool(), false);
        QCOMPARE(st.value("position").toDouble(), 10.0);
        t += 5000;
        QCOMPARE(h.positionNow(), 15.0);
        // Stale generation and unknown ops are ignored
        QVERIFY(h.handle("hostpk", QJsonObject{{"type", "action"}, {"gen", 0}, {"op", "pause"}, {"position", 1}}).isEmpty());
        // Ready of an earlier medium is ignored
        h.handle("guestpk", QJsonObject{{"type", "ready"}, {"gen", 0}, {"ready", true}});
        QCOMPARE(h.snapshot().value("members").toArray().at(1).toObject().value("ready").toBool(), false);
        // Ping → pong to the sender with the host clock
        out = h.handle("guestpk", QJsonObject{{"type", "ping"}, {"t", 5}});
        QCOMPARE(out.first().toPk, QStringLiteral("guestpk"));
        QCOMPARE(out.first().msg.value("server").toDouble(), double(t));
        // Unknown member → asked to join again
        QCOMPARE(h.handle("strangerpk", QJsonObject{{"type", "ready"}}).first().msg.value("type").toString(), QStringLiteral("rejoin"));
    }

    void shareAndHandOver()
    {
        RoomHost h("ABCD-EFGH-JKMN", "Kino", false, "hostpk", "Ana", clock());
        h.handle("guestpk", QJsonObject{{"type", "hello"}, {"nick", "Ben"}});
        const QString manifest(64, 'a');
        auto out = h.handle("guestpk", QJsonObject{{"type", "share"}, {"op", "offer"}, {"title", "f.mkv"}, {"duration", 600}, {"size", 5e6}, {"blockSize", 1048576}, {"manifest", manifest}});
        QJsonObject share = h.snapshot().value("share").toObject();
        QCOMPARE(share.value("status").toString(), QStringLiteral("pending"));
        QVERIFY(h.handle("guestpk", QJsonObject{{"type", "share"}, {"op", "confirm"}, {"shareId", share.value("id")}}).isEmpty());   // only the host
        h.handle("hostpk", QJsonObject{{"type", "share"}, {"op", "confirm"}, {"shareId", share.value("id")}});
        share = h.snapshot().value("share").toObject();
        QCOMPARE(share.value("status").toString(), QStringLiteral("confirmed"));
        QCOMPARE(h.snapshot().value("media").toObject().value("gen").toInt(), 1);        // room had no medium → share defines it
        // New medium ends the share
        out = h.handle("hostpk", QJsonObject{{"type", "media"}, {"title", "Neu"}, {"duration", 10}});
        QCOMPARE(find(out, "share-ended").value("shareId"), share.value("id"));
        // Explicit hand-over: snapshot with the new host and a higher epoch
        out = h.handle("hostpk", QJsonObject{{"type", "host"}, {"memberId", 2}});
        const QJsonObject snap = find(out, "snapshot");
        QCOMPARE(snap.value("hostPk").toString(), QStringLiteral("guestpk"));
        QCOMPARE(snap.value("epoch").toInt(), 2);
        QVERIFY(h.handedOver());
        // The new host continues from the snapshot
        RoomHost h2 = RoomHost::fromSnapshot(snap, "guestpk", clock(), false);
        QCOMPARE(h2.hostId(), 2);
        QCOMPARE(h2.snapshot().value("media").toObject().value("title").toString(), QStringLiteral("Neu"));
    }

    void chatAndLinks()
    {
        RoomHost h("ABCD-EFGH-JKMN", "Kino", false, "hostpk", "Ana", clock());
        h.handle("hostpk", QJsonObject{{"type", "chat"}, {"text", "  Hallo!  "}});
        auto out = h.handle("guestpk", QJsonObject{{"type", "hello"}, {"nick", "Ben"}});
        // Joiner gets the history in the welcome; everybody sees the join note
        const QJsonArray hist = find(out, "welcome").value("snapshot").toObject().value("chat").toArray();
        QCOMPARE(hist.size(), 1);
        QCOMPARE(hist.at(0).toObject().value("text").toString(), QStringLiteral("Hallo!"));
        QCOMPARE(find(out, "chat").value("system").toBool(), true);
        out = h.handle("guestpk", QJsonObject{{"type", "chat"}, {"text", "Hi Ana"}});
        const QJsonObject m = find(out, "chat");
        QCOMPARE(m.value("nick").toString(), QStringLiteral("Ben"));
        QVERIFY(out.first().toPk.isEmpty());                                   // to the whole room
        QVERIFY(h.handle("guestpk", QJsonObject{{"type", "chat"}, {"text", "   "}}).isEmpty());
        // Web links become the room medium; local paths and loopback addresses never do
        h.handle("hostpk", QJsonObject{{"type", "media"}, {"title", "Clip"}, {"duration", 212}, {"url", "https://www.youtube.com/watch?v=abc"}});
        QCOMPARE(h.snapshot().value("media").toObject().value("url").toString(), QStringLiteral("https://www.youtube.com/watch?v=abc"));
        h.handle("hostpk", QJsonObject{{"type", "media"}, {"title", "x"}, {"duration", 1}, {"url", "C:/Filme/geheim.mkv"}});
        QVERIFY(h.snapshot().value("media").toObject().value("url").toString().isEmpty());
        h.handle("hostpk", QJsonObject{{"type", "media"}, {"title", "x"}, {"duration", 1}, {"url", "http://127.0.0.1:5000/stream"}});
        QVERIFY(h.snapshot().value("media").toObject().value("url").toString().isEmpty());
    }

    void failoverAndTimeouts()
    {
        RoomHost h("ABCD-EFGH-JKMN", "Kino", false, "hostpk", "Ana", clock());
        h.handle("guestpk", QJsonObject{{"type", "hello"}, {"nick", "Ben"}});
        h.handle("hostpk", QJsonObject{{"type", "media"}, {"title", "Film"}, {"duration", 600}});
        h.handle("hostpk", QJsonObject{{"type", "action"}, {"gen", 1}, {"op", "play"}, {"position", 100}});
        const QJsonObject snap = h.snapshot();
        t += 50000;   // host gone for 50 s
        RoomHost f = RoomHost::fromSnapshot(snap, "guestpk", clock(), true);
        QCOMPARE(f.epoch(), 2);
        const QJsonObject st = f.snapshot().value("state").toObject();
        QCOMPARE(st.value("paused").toBool(), true);                 // the room pauses after a host failure
        QVERIFY(std::abs(st.value("position").toDouble() - 150.0) < 0.01);
        // Silent members are removed after the resume window, the room keeps running
        t += RoomHost::kRemoveMs + 1000;
        const auto out = f.tick();
        QCOMPARE(find(out, "peer-left").value("memberId").toInt(), 1);
        QCOMPARE(f.snapshot().value("members").toArray().size(), 1);
        // Host leaves with a ready successor → hand-over; without one → room closes
        RoomHost g("ABCD-EFGH-JKMN", "Kino", false, "hostpk", "Ana", clock());
        g.handle("guestpk", QJsonObject{{"type", "hello"}, {"nick", "Ben"}});
        g.handle("hostpk", QJsonObject{{"type", "leave"}});
        QVERIFY(g.handedOver());
        RoomHost solo("ABCD-EFGH-JKMN", "Kino", false, "hostpk", "Ana", clock());
        solo.handle("hostpk", QJsonObject{{"type", "leave"}});
        QVERIFY(solo.closed());
    }
};

class TstWatchPartyE2E : public QObject
{
    Q_OBJECT
private slots:
    // Both pages load without QML errors with a real controller (offscreen, no desktop window)
    void qmlPages()
    {
        QTemporaryDir tmp;
        FakeHost h(tmp.path());
        WatchPartyController c(&h);
        for (const char *page : {"WatchPartyPanel.qml", "WatchPartySettings.qml"}) {
            QQmlEngine engine;
            QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(UVEWP_SOURCE_DIR "/") + QLatin1String(page)));
            QObject *o = comp.createWithInitialProperties({{"controller", QVariant::fromValue<QObject *>(&c)}});
            QVERIFY2(o, qPrintable(comp.errorString()));
            QVERIFY(o->property("implicitHeight").toDouble() > 50);
            delete o;
        }
    }

    void syncAndStream()
    {
        // WP_REAL_RELAYS=1 runs the same flow over the public Nostr relays instead of the local test relay
        FakeRelay relay;
        QVERIFY(relay.start());
        const bool real = qEnvironmentVariable("WP_REAL_RELAYS") == QLatin1String("1");
        const QString url = real ? QString() : relay.url();
        QTemporaryDir tmp;
        QByteArray data(2 * (1 << 20) + 777, Qt::Uninitialized);
        for (auto &c : data) c = char(QRandomGenerator::global()->bounded(256));
        const QString file = tmp.filePath("Film.mkv");
        { QFile f(file); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(data); }

        const QJsonObject cfg{{"relays", url}, {"allowInsecureLoopback", true}};
        FakeHost hh(tmp.filePath("h")), gh(tmp.filePath("g"));
        hh.settings = cfg; gh.settings = cfg;
        WatchPartyController host(&hh), guest(&gh);
        hh.controller = &host; gh.controller = &guest;
        const auto wait = [](const std::function<bool()> &c, int ms) { QElapsedTimer t; t.start(); while (!c()) { if (t.elapsed() > ms) return false; QTest::qWait(20); } return true; };

        host.createRoom("Freitag Kino", "Ana", true, "popcorn");
        QVERIFY2(wait([&] { return host.inRoom(); }, 10000), qPrintable(host.status()));
        const QString roomId = host.roomId();
        QVERIFY2(!sync::normalizeRoomId(roomId).isEmpty(), qPrintable("room id: '" + roomId + "' status: " + host.status()));
        guest.joinRoom(roomId.toLower(), "Ben", "falsch");
        QVERIFY2(wait([&] { return guest.statusError() && !guest.busy(); }, 30000), qPrintable(guest.status()));
        QVERIFY2(guest.status().contains("Passwort"), qPrintable(guest.status()));
        guest.joinRoom(roomId, "Ben", "popcorn");
        QVERIFY2(wait([&] { return guest.inRoom() && host.members().size() == 2; }, 30000), qPrintable(guest.status()));
        QVERIFY(!guest.isHost() && host.isHost());

        // Host opens the film: becomes the room medium; guest opens an own copy and confirms with "Ready"
        hh.load(file, 600);
        QVERIFY(wait([&] { return guest.mediaGen() == 1 && host.localReady(); }, 10000));
        gh.load(tmp.filePath("copy-of-film.mkv"), 600);
        QVERIFY(wait([&] { return guest.localMode() == "own"; }, 5000));
        guest.markReady();
        // Host jumps to 120 s while playing: guest follows to ≈ the same position
        hh.rebase(); hh.base = 120; hh.paused = false;
        QVERIFY2(wait([&] { return std::abs(gh.position() - hh.position()) < 0.5 && !gh.paused; }, 8000),
                 qPrintable(QString("host %1 guest %2").arg(hh.position()).arg(gh.position())));
        const int seeksBefore = gh.seeks;
        QTest::qWait(3000);                                               // stable: no repeated corrections
        const QString diag = QString("host %1 guest %2 speed %3 seeks %4").arg(hh.position()).arg(gh.position()).arg(gh.speed).arg(gh.seeks - seeksBefore);
        QVERIFY2(gh.seeks - seeksBefore <= 1, qPrintable(diag));
        QVERIFY2(std::abs(gh.position() - hh.position()) < 0.4, qPrintable(diag));
        // Host pauses: guest pauses at the same place
        hh.setPaused(true);
        host.onEvent("playback.paused", {{"paused", true}});
        QVERIFY(wait([&] { return gh.paused && std::abs(gh.position() - hh.position()) < 0.5; }, 5000));

        // Chat in both directions
        host.sendChat("Hallo Ben");
        QVERIFY(wait([&] { for (const auto &v : guest.chat()) if (v.toMap().value("text") == "Hallo Ben") return true; return false; }, 10000));
        guest.sendChat("Hi Ana");
        QVERIFY(wait([&] { for (const auto &v : host.chat()) if (v.toMap().value("text") == "Hi Ana") return true; return false; }, 10000));

        // Host opens a web link (YouTube): the guest's player opens the same link and follows the host
        const QString link = QStringLiteral("https://www.youtube.com/watch?v=dQw4w9WgXcQ");
        gh.lastLoad.clear();
        hh.load(QStringLiteral("https://rr1.googlevideo.com/videoplayback?id=1"), 212, link);
        QVERIFY2(wait([&] { return gh.lastLoad == link; }, 15000), qPrintable(gh.lastLoad));
        QCOMPARE(guest.mediaUrl(), link);
        gh.load(QStringLiteral("https://rr5.googlevideo.com/videoplayback?id=2"), 212, link);   // resolved differently here
        QVERIFY(wait([&] { return guest.localMode() == "link" && guest.localReady(); }, 10000));
        hh.rebase(); hh.base = 100; hh.paused = false;
        QVERIFY2(wait([&] { return std::abs(gh.position() - hh.position()) < 0.5 && !gh.paused; }, 10000),
                 qPrintable(QString("host %1 guest %2").arg(hh.position()).arg(gh.position())));

        // New medium the guest does not have; the host shares it and the guest's room stream starts by itself
        hh.load(file, 600);
        QVERIFY(wait([&] { return guest.mediaGen() == 3 && host.localReady(); }, 15000));
        QVERIFY(guest.localMode().isEmpty());
        gh.lastLoad.clear();
        // no button: the host's new local file is shared to the room automatically
        QVERIFY2(wait([&] { return host.share().value("active").toBool() && host.stream().value("active").toBool(); }, 15000), qPrintable(host.status()));
        // No button: the stream starts automatically once the share reaches the guest
        QVERIFY(wait([&] { return !gh.lastLoad.isEmpty(); }, 20000));
        QCOMPARE(guest.localMode(), QStringLiteral("stream"));
        const QUrl su(gh.lastLoad);
        QCOMPARE(su.host(), QStringLiteral("127.0.0.1"));
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, quint16(su.port()));
        QVERIFY(wait([&] { return s.state() == QAbstractSocket::ConnectedState; }, 3000));
        s.write("GET " + su.path(QUrl::FullyEncoded).toUtf8() + " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=1048000-1049999\r\n\r\n");
        QByteArray all;
        QVERIFY2(wait([&] { all += s.readAll(); return s.state() == QAbstractSocket::UnconnectedState; }, 30000), "stream read timed out");
        all += s.readAll();
        QVERIFY(all.startsWith("HTTP/1.1 206"));
        QCOMPARE(all.mid(all.indexOf("\r\n\r\n") + 4), data.mid(1048000, 2000));
        QVERIFY(guest.stream().value("direct").toInt() + guest.stream().value("relay").toInt() >= 1);

        // Host closes the room: guest session ends, stream and cache are released
        host.closeRoom();
        QVERIFY(wait([&] { return !guest.inRoom(); }, 10000));
        QVERIFY(!guest.stream().value("active").toBool());
        if (!real) QVERIFY(relay.events > 10);
    }

    // Host relays a site link (resolved by a plugin, with headers) – the guest needs no plugin; local deviations resync by themselves
    void hostRelaysLink()
    {
        FakeRelay relay;
        QVERIFY(relay.start());
        Origin origin;
        QVERIFY(origin.listen(QHostAddress::LocalHost, 0));
        QByteArray video(900000, Qt::Uninitialized);
        for (auto &ch : video) ch = char(QRandomGenerator::global()->bounded(256));
        origin.video = video;
        QTemporaryDir tmp;
        const QJsonObject cfg{{"relays", relay.url()}, {"allowInsecureLoopback", true}};
        FakeHost hh(tmp.filePath("h")), gh(tmp.filePath("g"));
        hh.settings = cfg; gh.settings = cfg;
        hh.mpv = {{"user-agent", "UVETest/1"}, {"referrer", "https://site.example/watch"}, {"http-header-fields", QStringList{"Cookie: s=1"}}, {"seekable", true}};
        WatchPartyController host(&hh), guest(&gh);
        hh.controller = &host; gh.controller = &guest;
        const auto wait = [](const std::function<bool()> &c, int ms) { QElapsedTimer t; t.start(); while (!c()) { if (t.elapsed() > ms) return false; QTest::qWait(50); } return true; };
        host.createRoom("Kino", "Ana", false, QString());
        QVERIFY(wait([&] { return host.inRoom(); }, 5000));
        guest.joinRoom(host.roomId(), "Ben", QString());
        QVERIFY(wait([&] { return guest.inRoom(); }, 15000));
        host.setStreamAll(true);
        // site page opened through a plugin: the player plays the resolved address
        hh.load(origin.base() + "/video.mp4", 300, QStringLiteral("https://site.example/watch?v=1"));
        QVERIFY2(wait([&] { return gh.lastLoad.startsWith("http://127.0.0.1:") && gh.lastLoad != origin.base() + "/video.mp4"; }, 20000), qPrintable(gh.lastLoad + " / " + host.status()));
        QCOMPARE(guest.localMode(), QStringLiteral("stream"));
        const QByteArray r = httpGet(gh.lastLoad, "bytes=500000-500099");
        QVERIFY2(r.startsWith("HTTP/1.1 206"), r.left(200).constData());
        QCOMPARE(bodyOf(r), video.mid(500000, 100));
        // guest plays the relayed stream; host jumps; then the guest wanders off and is brought back without a button
        gh.load(gh.lastLoad, 300);
        QVERIFY(wait([&] { return guest.localReady(); }, 10000));
        hh.rebase(); hh.base = 80; hh.paused = false;
        QVERIFY(wait([&] { return std::abs(gh.position() - hh.position()) < 0.5; }, 10000));
        gh.rebase(); gh.base = 200;                                            // local seek by the guest
        QVERIFY2(wait([&] { return std::abs(gh.position() - hh.position()) < 0.5; }, 8000),
                 qPrintable(QString("host %1 guest %2").arg(hh.position()).arg(gh.position())));
    }

    // Twitch already playing when the room is created: the room takes it over, guests open it and follow pause/play
    void liveStreamAlreadyPlaying()
    {
        FakeRelay relay;
        QVERIFY(relay.start());
        QTemporaryDir tmp;
        const QJsonObject cfg{{"relays", relay.url()}, {"allowInsecureLoopback", true}};
        FakeHost hh(tmp.filePath("h")), gh(tmp.filePath("g"));
        hh.settings = cfg; gh.settings = cfg;
        WatchPartyController host(&hh), guest(&gh);
        hh.controller = &host; gh.controller = &guest;
        const auto wait = [](const std::function<bool()> &c, int ms) { QElapsedTimer t; t.start(); while (!c()) { if (t.elapsed() > ms) return false; QTest::qWait(50); } return true; };
        const QString channel = QStringLiteral("https://www.twitch.tv/therealknossi");
        // HLS live: the player reports the DVR window as "duration" and the stream is not really seekable
        hh.path = QStringLiteral("https://video-weaver.fra05.hls.ttvnw.net/v1/playlist/abc.m3u8");
        hh.source = channel; hh.duration = 30; hh.paused = false; hh.seekable = false; hh.rebase();
        host.createRoom("Twitch Abend", "Ana", false, QString());
        QVERIFY(wait([&] { return host.mediaGen() == 1 && host.property("mediaLive").toBool(); }, 10000));
        QCOMPARE(host.mediaUrl(), channel);
        guest.joinRoom(host.roomId(), "Ben", QString());
        QVERIFY2(wait([&] { return gh.lastLoad == channel; }, 20000), qPrintable(guest.status() + " / " + gh.lastLoad));
        QVERIFY(guest.property("mediaLive").toBool());
        gh.seekable = false;
        gh.load(QStringLiteral("https://video-weaver.ams02.hls.ttvnw.net/v1/playlist/xyz.m3u8"), 30, channel);
        QVERIFY(wait([&] { return guest.localMode() == "link" && guest.localReady(); }, 10000));
        const int seeks = gh.seeks;
        // Host pauses → guest pauses; host plays → guest plays; never a seek for live
        hh.setPaused(true);
        host.onEvent("playback.paused", {{"paused", true}});
        QVERIFY(wait([&] { return gh.paused; }, 8000));
        hh.setPaused(false);
        host.onEvent("playback.paused", {{"paused", false}});
        QVERIFY(wait([&] { return !gh.paused; }, 8000));
        QTest::qWait(1500);
        QCOMPARE(gh.seeks, seeks);
    }

    // A new medium of the host: it waits at the beginning until the guest is ready, then both start from 0
    void startTogether()
    {
        FakeRelay relay;
        QVERIFY(relay.start());
        QTemporaryDir tmp;
        QByteArray data(1 << 20, 'x');
        const QString file = tmp.filePath("Film.mkv");
        { QFile f(file); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(data); }
        const QJsonObject cfg{{"relays", relay.url()}, {"allowInsecureLoopback", true}};
        FakeHost hh(tmp.filePath("h")), gh(tmp.filePath("g"));
        hh.settings = cfg; gh.settings = cfg;
        WatchPartyController host(&hh), guest(&gh);
        hh.controller = &host; gh.controller = &guest;
        const auto wait = [](const std::function<bool()> &c, int ms) { QElapsedTimer t; t.start(); while (!c()) { if (t.elapsed() > ms) return false; QTest::qWait(50); } return true; };
        host.createRoom("Kino", "Ana", false, QString());
        QVERIFY(wait([&] { return host.inRoom(); }, 5000));
        guest.joinRoom(host.roomId(), "Ben", QString());
        QVERIFY(wait([&] { return guest.inRoom(); }, 15000));
        hh.load(file, 600);                                                     // starts playing at once …
        QVERIFY(wait([&] { return guest.mediaGen() == 1; }, 10000));
        QVERIFY(wait([&] { return hh.paused && hh.position() < 0.5; }, 5000));  // … and waits at the beginning
        QVERIFY(wait([&] { return !gh.lastLoad.isEmpty(); }, 20000));           // the guest gets the room stream
        QTest::qWait(1500);
        QVERIFY2(hh.paused && hh.position() < 0.5, qPrintable(QString("host started early at %1").arg(hh.position())));
        gh.load(gh.lastLoad, 600);                                              // guest player has loaded it
        QVERIFY(wait([&] { return !hh.paused; }, 10000));                      // now both start
        QVERIFY(wait([&] { return !gh.paused; }, 5000));
        QTest::qWait(1000);
        QVERIFY2(hh.position() < 3.0 && std::abs(gh.position() - hh.position()) < 0.5,
                 qPrintable(QString("host %1 guest %2").arg(hh.position()).arg(gh.position())));
    }

    // Members whose PC clock is off: events are stamped with the relay time, so relays accept them
    void clockSkew()
    {
        FakeRelay relay;
        relay.skew = 600;
        QVERIFY(relay.start());
        QTemporaryDir tmp;
        const QJsonObject cfg{{"relays", relay.url()}, {"allowInsecureLoopback", true}};
        FakeHost hh(tmp.filePath("h")), gh(tmp.filePath("g"));
        hh.settings = cfg; gh.settings = cfg;
        WatchPartyController host(&hh), guest(&gh);
        hh.controller = &host; gh.controller = &guest;
        const auto wait = [](const std::function<bool()> &c, int ms) { QElapsedTimer t; t.start(); while (!c()) { if (t.elapsed() > ms) return false; QTest::qWait(50); } return true; };
        host.createRoom("Kino", "Ana", false, QString());
        QVERIFY(wait([&] { return host.inRoom(); }, 5000));
        guest.joinRoom(host.roomId(), "Ben", QString());
        QVERIFY2(wait([&] { return guest.inRoom(); }, 15000), qPrintable(guest.status()));
        QCOMPARE(relay.refused, 0);
    }

    // Host disappears without leaving (crash / network): the guest takes over and the room continues
    void hostFailover()
    {
        FakeRelay relay;
        QVERIFY(relay.start());
        QTemporaryDir tmp;
        const QJsonObject cfg{{"relays", relay.url()}, {"allowInsecureLoopback", true}};
        FakeHost hh(tmp.filePath("h")), gh(tmp.filePath("g"));
        hh.settings = cfg; gh.settings = cfg;
        auto *host = new WatchPartyController(&hh);
        WatchPartyController guest(&gh);
        hh.controller = host; gh.controller = &guest;
        const auto wait = [](const std::function<bool()> &c, int ms) { QElapsedTimer t; t.start(); while (!c()) { if (t.elapsed() > ms) return false; QTest::qWait(50); } return true; };
        host->createRoom("Kino", "Ana", false, QString());
        QVERIFY(wait([&] { return host->inRoom(); }, 5000));
        guest.joinRoom(host->roomId(), "Ben", QString());
        QVERIFY2(wait([&] { return guest.inRoom(); }, 15000), qPrintable(guest.status()));
        const QString roomId = host->roomId();
        hh.controller = nullptr;
        delete host;                                   // gone without a word
        QVERIFY2(wait([&] { return guest.isHost(); }, 70000), "guest did not take over");
        QCOMPARE(guest.roomId(), roomId);
        QVERIFY(guest.inRoom());
    }
};

class TstWatchPartyCrypto : public QObject
{
    Q_OBJECT
private slots:
    void bip340Vectors()
    {
        // BIP340 test vector 0
        const QByteArray sk = QByteArray::fromHex("0000000000000000000000000000000000000000000000000000000000000003");
        QCOMPARE(crypto::xOnlyPublicKey(sk).toHex().toUpper(), QByteArray("F9308A019258C31049344F85F89D5229B531C845836F99B08601F113BCE036F9"));
        const QByteArray sig = crypto::schnorrSign(sk, QByteArray(32, '\0'), QByteArray(32, '\0'));
        QCOMPARE(sig.toHex().toUpper(), QByteArray("E907831F80848D1069A5371B402410364BDF1C5F8307B0084C55F1CE2DCA821525F66A4A85EA8B71E482A74F382D2CE5EBEEE8FDB2172F477DF4900D310536C0"));
        QVERIFY(crypto::schnorrVerify(crypto::xOnlyPublicKey(sk), QByteArray(32, '\0'), sig));
        // Random key pairs: sign/verify round trip, tampering detected
        for (int i = 0; i < 5; ++i) {
            const auto kp = crypto::generateKeyPair();
            const QByteArray m = crypto::randomBytes(32);
            const QByteArray s = crypto::schnorrSign(kp.secret, m);
            QVERIFY(crypto::schnorrVerify(kp.pub, m, s));
            QByteArray bad = s; bad[40] = char(bad[40] ^ 1);
            QVERIFY(!crypto::schnorrVerify(kp.pub, m, bad));
        }
    }

    void roomKeyAndAead()
    {
        const QByteArray k1 = crypto::deriveRoomKey("ABCD-EFGH-JKMN", "popcorn");
        QCOMPARE(k1, crypto::deriveRoomKey("ABCD-EFGH-JKMN", "popcorn"));
        QVERIFY(k1 != crypto::deriveRoomKey("ABCD-EFGH-JKMN", "falsch"));
        QVERIFY(k1 != crypto::deriveRoomKey("ABCD-EFGH-JKMP", "popcorn"));
        const QByteArray ct = crypto::encrypt(k1, "{\"type\":\"hello\"}");
        QByteArray pt;
        QVERIFY(crypto::decrypt(k1, ct, &pt));
        QCOMPARE(pt, QByteArray("{\"type\":\"hello\"}"));
        QVERIFY(!crypto::decrypt(crypto::deriveRoomKey("ABCD-EFGH-JKMN", "falsch"), ct, &pt));   // wrong password
        QVERIFY(!crypto::roomTopic("ABCD-EFGH-JKMN").contains("ABCD"));
    }

    void nostrEvents()
    {
        const auto kp = crypto::generateKeyPair();
        QJsonObject ev = NostrPool::makeEvent(kp, NostrPool::kKind, QJsonArray{QJsonArray{"t", "abc"}}, "hello", 1700000000);
        QVERIFY(NostrPool::verifyEvent(ev));
        ev["content"] = "hellO";
        QVERIFY(!NostrPool::verifyEvent(ev));
    }

    // Opt-in (network): certificate problems on a PC are tolerated only when asked, a wrong host name never
    void tlsFallback()
    {
        if (qEnvironmentVariable("WP_REAL_RELAYS") != QLatin1String("1")) QSKIP("set WP_REAL_RELAYS=1 for network tests");
        auto attempt = [](const QString &url, bool tolerate) {
            WsClient ws;
            int http = -1;
            QString reason;
            QObject::connect(&ws, &WsClient::closed, [&](int, const QString &r, int h) { reason = r; http = h; });
            ws.open(QUrl(url), false, nullptr, tolerate);
            QElapsedTimer t; t.start();
            while (http < 0 && t.elapsed() < 20000) QTest::qWait(50);
            return qMakePair(reason, http);
        };
        auto r = attempt("wss://self-signed.badssl.com/", false);
        QVERIFY2(r.first.startsWith("tls"), qPrintable(r.first));
        r = attempt("wss://self-signed.badssl.com/", true);
        QVERIFY2(!r.first.startsWith("tls") && r.second > 0, qPrintable(QString("%1 http %2").arg(r.first).arg(r.second)));   // TLS passed, no WebSocket there
        r = attempt("wss://wrong.host.badssl.com/", true);
        QVERIFY2(r.first.startsWith("tls"), qPrintable(r.first));
    }

    // Opt-in check against the real public relays: WP_REAL_RELAYS=1
    void publicRelays()
    {
        if (qEnvironmentVariable("WP_REAL_RELAYS") != QLatin1String("1")) QSKIP("set WP_REAL_RELAYS=1 to test the public relays");
        const QString topic = crypto::roomTopic(QString::fromLatin1(crypto::randomBytes(6).toHex()));
        const QStringList list = qEnvironmentVariableIsSet("WP_RELAYS") ? qEnvironmentVariable("WP_RELAYS").split(',') : NostrPool::defaultRelays();
        for (const QString &relay : list) {
            NostrPool a, b;
            QStringList got;
            connect(&b, &NostrPool::received, this, [&](const QString &, const QByteArray &c) { got << QString::fromUtf8(c); });
            a.start({relay}, topic);
            b.start({relay}, topic);
            QElapsedTimer t; t.start();
            while ((a.connectedCount() < 1 || b.connectedCount() < 1) && t.elapsed() < 10000) QTest::qWait(50);
            QTest::qWait(500);
            const QElapsedTimer sent = [] { QElapsedTimer e; e.start(); return e; }();
            a.publish("ping-" + relay.toUtf8());
            while (got.isEmpty() && t.elapsed() < 20000) QTest::qWait(20);
            qInfo("%s: connected %d/%d, received %s after %lld ms", qPrintable(relay), a.connectedCount(), b.connectedCount(),
                  got.isEmpty() ? "NOTHING" : "ok", got.isEmpty() ? -1LL : sent.elapsed());
        }
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    // WP_SUITE=core|crypto|e2e runs one suite (each suite writes its own -o output)
    const QString only = qEnvironmentVariable("WP_SUITE");
    int rc = 0;
    if (only.isEmpty() || only == "core") { TstWatchParty t; rc |= QTest::qExec(&t, argc, argv); }
    if (only.isEmpty() || only == "crypto") { TstWatchPartyCrypto t; rc |= QTest::qExec(&t, argc, argv); }
    if (only.isEmpty() || only == "room") { TstRoomHost t; rc |= QTest::qExec(&t, argc, argv); }
    if (only.isEmpty() || only == "e2e") { TstWatchPartyE2E t; rc |= QTest::qExec(&t, argc, argv); }
    return rc;
}

#include "tst_uvewatchparty.moc"
