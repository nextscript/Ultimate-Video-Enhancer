// Tests für UVEinetRadio mit reproduzierbaren API-Testdaten (tests/fixtures) und einem lokalen HTTP-Server.
// Keine Verbindung ins Internet, kein Fenster (QT_QPA_PLATFORM=offscreen).
#include "Caches.h"
#include "FakeHost.h"
#include "RadioBrowser.h"
#include "RadioController.h"
#include "RadioTypes.h"
#include "StationStore.h"
#include "StreamProbe.h"
#include "TestHttpServer.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>
#include <QQuickWindow>
#include <QSGRendererInterface>

using namespace uve::radio;

static QByteArray fixture(const char *name)
{
    QFile f(QStringLiteral(UVEINETRADIO_SOURCE_DIR "/tests/fixtures/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly)) qFatal("fixture missing: %s", name);
    return f.readAll();
}

// Radio-Browser-Nachbildung: liefert die Fixtures und prüft dabei die Parameter
static TestResponse apiHandler(const QString &path, const QUrlQuery &q, const QByteArray &)
{
    TestResponse r;
    if (path == QLatin1String("/json/stations/search")) {
        if (q.queryItemValue("hidebroken") != QLatin1String("true")) return TestResponse{400};
        if (q.queryItemValue("name") == QLatin1String("nichts")) { r.body = "[]"; return r; }
        r.body = q.queryItemValue("offset").toInt() >= 30 ? fixture("search_page2.json") : fixture("search_page1.json");
        if (q.queryItemValue("name") == QLatin1String("langsam")) r.delayMs = 600;
        return r;
    }
    if (path == QLatin1String("/json/countries")) { r.body = fixture("countries.json"); return r; }
    if (path == QLatin1String("/json/languages")) { r.body = fixture("languages.json"); return r; }
    if (path == QLatin1String("/json/tags")) { r.body = fixture("tags.json"); return r; }
    if (path == QLatin1String("/json/stations/byuuid")) { r.body = fixture("byuuid.json"); return r; }
    if (path.startsWith(QLatin1String("/json/url/"))) { r.body = "{\"ok\":true}"; return r; }
    return TestResponse{404, "text/plain", "not found"};
}

// Audio-/Webseiten-Nachbildung für die Stream-Prüfung
static TestResponse streamHandler(const QString &path, const QUrlQuery &, const QByteArray &)
{
    if (path == QLatin1String("/live.mp3")) {
        TestResponse r{200, "audio/mpeg", QByteArray("ID3\x04\x00\x00\x00\x00\x00\x00", 10) + QByteArray(2000, '\xFF')};
        r.stream = true;
        return r;
    }
    if (path == QLatin1String("/slow.mp3")) {
        TestResponse r{200, "audio/mpeg", QByteArray(2000, '\xFF')};
        r.stream = true;
        r.delayMs = 700;
        return r;
    }
    if (path == QLatin1String("/aac")) {   // falscher Typ, aber Audio-Inhalt (ADTS)
        TestResponse r{200, "text/html", QByteArray("\xFF\xF1\x50\x80", 4) + QByteArray(500, '\x11')};
        r.stream = true;
        return r;
    }
    if (path == QLatin1String("/page")) return TestResponse{200, "text/html; charset=utf-8", "<!DOCTYPE html><html><head><title>Sender</title></head><body>Hallo</body></html>"};
    if (path == QLatin1String("/hls.m3u8"))
        return TestResponse{200, "application/vnd.apple.mpegurl", "#EXTM3U\n#EXT-X-VERSION:3\n#EXTINF:10,\nseg1.aac\n"};
    if (path == QLatin1String("/to-ftp")) {
        TestResponse r{302, "text/plain", ""};
        r.headers << qMakePair(QByteArray("Location"), QByteArray("ftp://example.invalid/stream"));
        return r;
    }
    if (path == QLatin1String("/to-live")) {
        TestResponse r{302, "text/plain", ""};
        r.headers << qMakePair(QByteArray("Location"), QByteArray("/live.mp3"));
        return r;
    }
    return TestResponse{404, "text/plain", "gone"};
}

static int closedPort()
{
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    return s.serverPort();   // nach dem Schließen frei → Verbindung abgelehnt
}

class TstUveInetRadio : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<TestHttpServer> m_api;
    std::unique_ptr<TestHttpServer> m_streams;

    std::unique_ptr<RadioController> makeController(FakeHost &host, const QString &dataDir = {})
    {
        host.dataDir = dataDir.isEmpty() ? m_dir.filePath(QStringLiteral("data-%1").arg(++m_n)) : dataDir;
        QDir().mkpath(host.dataDir);
        auto c = std::make_unique<RadioController>(&host, host.dataDir);
        c->browser()->setMirrors({m_api->baseUrl()});
        c->browser()->setTimeoutMs(1500);
        c->setSearchDebounceMs(30);
        c->setProbeTimeoutMs(1500);
        c->setMetadataIntervalMs(50);
        return c;
    }
    int m_n = 0;

    QVariantMap station(const QString &id, const QString &name, const QString &url)
    {
        Station s;
        s.id = id; s.name = name; s.url = url;
        return s.toVariant();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_api = std::make_unique<TestHttpServer>(apiHandler);
        m_streams = std::make_unique<TestHttpServer>(streamHandler);
    }

    // ------------------------------------------------------------------ Datentypen
    void urlChecks()
    {
        QCOMPARE(checkStreamUrl(QString()), UrlCheck::Empty);
        QCOMPARE(checkStreamUrl("ftp://x.example/stream"), UrlCheck::BadScheme);
        QCOMPARE(checkStreamUrl("javascript:alert(1)"), UrlCheck::BadScheme);
        QCOMPARE(checkStreamUrl("file:///C:/x.mp3"), UrlCheck::BadScheme);
        QCOMPARE(checkStreamUrl("https://stream.example.com/live.mp3"), UrlCheck::Ok);
        QCOMPARE(checkStreamUrl("http://1.2.3.4:8000/;"), UrlCheck::Ok);
        QCOMPARE(checkStreamUrl("https://ice.example.org/radio.m3u8"), UrlCheck::Ok);
        QCOMPARE(checkStreamUrl("https://www.example.com/listen.html"), UrlCheck::WebPage);
        QCOMPARE(checkStreamUrl("https://www.youtube.com/watch?v=abc"), UrlCheck::WebPage);
        QCOMPARE(checkStreamUrl("https://www.radio.de/s/1live"), UrlCheck::WebPage);
        QCOMPARE(checkStreamUrl("https://stream.radio.de/abc.mp3"), UrlCheck::Ok);
        QCOMPARE(checkStreamUrl("https://www.sender.de/"), UrlCheck::WebPage);
        QVERIFY(sameUrl("HTTP://Example.com:80/live/", "http://example.com/live"));
        QVERIFY(!sameUrl("http://example.com/a", "http://example.com/b"));
    }

    void stationFromApi()
    {
        const QJsonArray a = QJsonDocument::fromJson(fixture("search_page1.json")).array();
        const Station s0 = Station::fromApi(a.at(0).toObject());
        QCOMPARE(s0.id, QStringLiteral("00000000-0000-0000-0000-000000001000"));
        QCOMPARE(s0.playUrl(), QStringLiteral("https://stream0.example.invalid/live.mp3"));   // url_resolved bevorzugt
        QCOMPARE(s0.bitrate, 128);
        const Station s1 = Station::fromApi(a.at(1).toObject());
        QCOMPARE(s1.playUrl(), QStringLiteral("http://stream1.example.invalid/live"));        // Rückfall url
        QVERIFY(s1.toVariant().value("insecure").toBool());
        const Station s3 = Station::fromApi(a.at(3).toObject());
        QCOMPARE(s3.name, QStringLiteral("Steuer zeichen Radio"));                            // Steuerzeichen entfernt
        QVERIFY(s3.codec.isEmpty());                                                           // UNKNOWN
        const Station s4 = Station::fromApi(a.at(4).toObject());
        QCOMPARE(s4.playUrl(), QStringLiteral("https://ok4.example.invalid/x"));              // ftp url_resolved ungültig
    }

    void metadataParsing()
    {
        TrackInfo t = trackFromMetadata({{"icy-title", "Daft Punk - One More Time"}}, "Sender");
        QCOMPARE(t.artist, QStringLiteral("Daft Punk"));
        QCOMPARE(t.title, QStringLiteral("One More Time"));
        t = trackFromMetadata({{"ICY-Title", "Nachrichten"}}, "Sender");
        QCOMPARE(t.title, QStringLiteral("Nachrichten"));
        QVERIFY(t.artist.isEmpty());
        QVERIFY(trackFromMetadata({{"icy-title", " - "}}, "Sender").isEmpty());
        QVERIFY(trackFromMetadata({{"icy-title", "Sender"}}, "Sender").isEmpty());
        QVERIFY(trackFromMetadata({{"icy-name", "Sender"}, {"icy-genre", "Pop"}}, "Sender").isEmpty());
        t = trackFromMetadata({{"artist", "A"}, {"title", "<b>B</b>\n"}}, "S");
        QCOMPARE(t.title, QStringLiteral("<b>B</b>"));   // nur Text – angezeigt wird PlainText
    }

    // ------------------------------------------------------------------ Suche und Filter
    void searchAndFilters()
    {
        FakeHost host;
        auto c = makeController(host);
        QSignalSpy spy(c.get(), &RadioController::resultsChanged);
        c->search("Test", "DE", "german", "pop", "name");
        QTRY_VERIFY(spy.count() > 0 && !c->searchBusy());
        QCOMPARE(c->results().size(), 30);                      // Doppelter Eintrag ausgefiltert
        QVERIFY(c->hasMore());
        QVERIFY(c->searchError().isEmpty());
        const QString req = m_api->log().last();
        for (const char *p : {"name=Test", "countrycode=DE", "language=german", "tag=pop", "hidebroken=true", "limit=30",
                              "offset=0", "order=name"})
            QVERIFY2(req.contains(QLatin1String(p)), qPrintable(req));
        QVERIFY(m_api->lastHeaders().contains("User-Agent: UVEinetRadio/1.0.1"));

        // Seitenweise: zweite Seite anhängen
        c->loadMore();
        QTRY_VERIFY(!c->searchBusy() && c->results().size() == 35);
        QVERIFY(m_api->log().last().contains("offset=30"));
        QVERIFY(!c->hasMore());

        // Beliebtheit
        c->search("Test", {}, {}, {}, "popularity");
        QTRY_VERIFY(!c->searchBusy());
        QVERIFY(m_api->log().last().contains("order=clickcount"));
        QVERIFY(m_api->log().last().contains("reverse=true"));

        // Leere Trefferliste
        c->search("nichts", {}, {}, {}, "name");
        QTRY_VERIFY(!c->searchBusy() && c->searchDone());
        QCOMPARE(c->results().size(), 0);
        QVERIFY(c->searchError().isEmpty());
    }

    void facets()
    {
        FakeHost host;
        auto c = makeController(host);
        c->loadFacets();
        QTRY_VERIFY(!c->facetsBusy());
        QCOMPARE(c->countries().size(), 2);   // leere Namen und 0 Sender ausgelassen
        QCOMPARE(c->countries().at(0).toMap().value("name").toString(), QStringLiteral("Austria"));   // alphabetisch
        QCOMPARE(c->countries().at(0).toMap().value("code").toString(), QStringLiteral("AT"));
        QCOMPARE(c->languages().size(), 2);
        QCOMPARE(c->tags().size(), 3);
    }

    void debounceAndStaleResults()
    {
        FakeHost host;
        auto c = makeController(host);
        c->setSearchDebounceMs(200);
        const int before = m_api->hits("/json/stations/search");
        // Tippen: nur die letzte Eingabe geht raus
        c->search("T", {}, {}, {}, "name");
        c->search("Te", {}, {}, {}, "name");
        c->search("Tes", {}, {}, {}, "name");
        QTRY_VERIFY(!c->searchBusy());
        QCOMPARE(m_api->hits("/json/stations/search") - before, 1);
        QVERIFY(m_api->log().last().contains("name=Tes"));

        // Überholte langsame Antwort wird verworfen
        c->setSearchDebounceMs(0);
        c->search("langsam", {}, {}, {}, "name");
        QTest::qWait(100);   // Anfrage läuft
        c->search("nichts", {}, {}, {}, "name");
        QTRY_VERIFY(!c->searchBusy());
        QTest::qWait(800);   // langsame Antwort wäre jetzt da
        QCOMPARE(c->results().size(), 0);
    }

    // ------------------------------------------------------------------ Spiegel
    void mirrorFailover()
    {
        TestHttpServer down([](const QString &, const QUrlQuery &, const QByteArray &) { return TestResponse{503, "text/plain", "busy"}; });
        TestHttpServer hang([](const QString &, const QUrlQuery &, const QByteArray &) { TestResponse r; r.hang = true; return r; });
        QNetworkAccessManager nam;
        RadioBrowser rb(&nam, nullptr);
        rb.setTimeoutMs(400);
        rb.setMaxAttempts(4);
        rb.setMirrors({QUrl(QStringLiteral("http://127.0.0.1:%1").arg(closedPort())), down.baseUrl(), hang.baseUrl(), m_api->baseUrl()});
        QSignalSpy mirrorSpy(&rb, &RadioBrowser::mirrorChanged);
        bool done = false;
        RadioBrowser::Result res;
        rb.search(SearchQuery{}, [&](const RadioBrowser::Result &r) { res = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY2(res.ok, qPrintable(res.error));
        QCOMPARE(res.data.array().size(), 31);
        // Qt kann eine Anfrage nach abgebrochener Verbindung intern einmal wiederholen – daher „mindestens einmal“
        QVERIFY(down.hits("/json/stations/search") >= 1);
        QVERIFY(hang.hits("/json/stations/search") >= 1);
        QCOMPARE(mirrorSpy.count(), 3);
        QCOMPARE(rb.currentMirror(), QStringLiteral("127.0.0.1"));
        // Der funktionierende Spiegel bleibt gewählt
        done = false;
        const int apiHits = m_api->hits("/json/stations/search");
        rb.search(SearchQuery{}, [&](const RadioBrowser::Result &r) { res = r; done = true; });
        QTRY_VERIFY(done);
        const int downHits = down.hits("/json/stations/search");
        QCOMPARE(m_api->hits("/json/stations/search"), apiHits + 1);
        QCOMPARE(down.hits("/json/stations/search"), downHits);   // kaputter Spiegel wird nicht erneut gefragt
    }

    void limitedRetriesAndOfflineCache()
    {
        TestHttpServer down([](const QString &, const QUrlQuery &, const QByteArray &) { return TestResponse{500, "text/plain", "x"}; });
        QNetworkAccessManager nam;
        CatalogCache cache(m_dir.filePath(QStringLiteral("cachetest")));
        RadioBrowser rb(&nam, &cache);
        rb.setTimeoutMs(500);
        rb.setMaxAttempts(3);
        rb.setMirrors({down.baseUrl(), down.baseUrl(), down.baseUrl(), down.baseUrl(), down.baseUrl()});
        bool done = false;
        RadioBrowser::Result res;
        rb.countries([&](const RadioBrowser::Result &r) { res = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!res.ok);
        QCOMPARE(down.hits("/json/countries"), 3);   // begrenzt

        // Mit gefülltem Cache: bei Ausfall gespeicherte Antwort
        rb.setMirrors({m_api->baseUrl()});
        done = false;
        rb.languages([&](const RadioBrowser::Result &r) { res = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(res.ok && !res.fromCache);
        // frischer Cache-Treffer: kein Netz
        const int hits = m_api->hits("/json/languages");
        done = false;
        rb.languages([&](const RadioBrowser::Result &r) { res = r; done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(m_api->hits("/json/languages"), hits);
        // Suche (TTL 1 h) bei Ausfall aus dem Cache
        done = false;
        rb.search(SearchQuery{}, [&](const RadioBrowser::Result &r) { res = r; done = true; });
        QTRY_VERIFY(done);
        rb.setMirrors({down.baseUrl()});
        cache.clear();
        done = false;
        rb.search(SearchQuery{}, [&](const RadioBrowser::Result &r) { res = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!res.ok);   // Cache geleert → Fehler
    }

    void searchErrorShown()
    {
        FakeHost host;
        auto c = makeController(host);
        c->browser()->setMirrors({QUrl(QStringLiteral("http://127.0.0.1:%1").arg(closedPort()))});
        c->search("Fehler", {}, {}, {}, "name");
        QTRY_VERIFY(!c->searchBusy());
        QVERIFY(!c->searchError().isEmpty());
        QCOMPARE(c->results().size(), 0);
    }

    // ------------------------------------------------------------------ Favoriten und Verlauf
    void favoritesSurviveRestart()
    {
        const QString dir = m_dir.filePath(QStringLiteral("persist"));
        QVariantMap s = station("uuid-1", "Sender Eins", m_streams->base() + "/live.mp3");
        {
            FakeHost host;
            auto c = makeController(host, dir);
            // Katalog offline – Favorit geht trotzdem
            c->browser()->setMirrors({QUrl(QStringLiteral("http://127.0.0.1:%1").arg(closedPort()))});
            QVERIFY(c->toggleFavorite(s));
            QVERIFY(c->playCustom("Mein Stream", m_streams->base() + "/aac", true));
            QCOMPARE(c->favorites().size(), 2);
        }
        QFile f(QDir(dir).filePath("library.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(root.value("version").toInt(), 1);
        QCOMPARE(root.value("format").toString(), QStringLiteral("uveinetradio-library"));
        QVERIFY(QDir(dir).entryList({"*.tmp", "*~"}, QDir::Files).isEmpty());   // atomar geschrieben

        FakeHost host2;
        auto c2 = makeController(host2, dir);   // „Neustart“
        QCOMPARE(c2->favorites().size(), 2);
        QVERIFY(c2->isFavorite("uuid-1"));
        // Direkt abspielen
        c2->play(c2->favorites().last().toMap(), "favorite");
        QTRY_COMPARE(c2->state(), QStringLiteral("connecting"));
        QCOMPARE(host2.loadedUrls().last(), m_streams->base() + "/live.mp3");
        QVERIFY(!c2->toggleFavorite(s));
        QCOMPARE(c2->favorites().size(), 1);
    }

    void brokenLibraryIsKept()
    {
        const QString dir = m_dir.filePath(QStringLiteral("broken"));
        QDir().mkpath(dir);
        QFile f(QDir(dir).filePath("library.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ kaputt");
        f.close();
        FakeHost host;
        auto c = makeController(host, dir);
        QVERIFY(!c->storageError().isEmpty());
        QCOMPARE(QDir(dir).entryList({"library.json.broken-*"}, QDir::Files).size(), 1);
    }

    void historyLimit()
    {
        StationStore store(m_dir.filePath(QStringLiteral("hist")));
        for (int i = 0; i < 25; ++i) {
            Station s; s.id = QString::number(i); s.name = QStringLiteral("S%1").arg(i); s.url = "https://x.example/" + s.id;
            store.addHistory(s);
        }
        QCOMPARE(store.history().size(), 20);
        QCOMPARE(store.history().first().id, QStringLiteral("24"));
        Station again; again.id = "10"; again.name = "S10"; again.url = "https://x.example/10";
        store.addHistory(again);
        QCOMPARE(store.history().size(), 20);
        QCOMPARE(store.history().first().id, QStringLiteral("10"));
        store.removeHistory("10");
        QCOMPARE(store.history().size(), 19);
        QVERIFY(store.save());
        StationStore again2(m_dir.filePath(QStringLiteral("hist")));
        QVERIFY(again2.load());
        QCOMPARE(again2.history().size(), 19);
        again2.clearHistory();
        QVERIFY(again2.history().isEmpty());
    }

    // ------------------------------------------------------------------ Wiedergabe
    void noPlaybackWithoutPlay()
    {
        FakeHost host;
        auto c = makeController(host);
        c->search("Test", {}, {}, {}, "name");
        QTRY_VERIFY(!c->searchBusy());
        c->toggleFavorite(c->results().first().toMap());
        c->loadFacets();
        QTRY_VERIFY(!c->facetsBusy());
        QTest::qWait(200);
        QCOMPARE(host.loadedUrls().size(), 0);
        QCOMPARE(host.count("player.stop"), 0);
        QCOMPARE(c->state(), QStringLiteral("idle"));
    }

    void playCustomStream()
    {
        FakeHost host;
        auto c = makeController(host);
        const QString url = m_streams->base() + "/live.mp3";
        host.playlist << "C:/Videos/film.mkv";   // vorhandener Eintrag bleibt
        QVERIFY(c->playCustom("Eigener Sender", url, false));
        QCOMPARE(c->state(), QStringLiteral("checking"));
        QTRY_COMPARE(c->state(), QStringLiteral("connecting"));
        QCOMPARE(host.count("playlist.add"), 1);
        const auto add = std::find_if(host.calls.begin(), host.calls.end(), [](const auto &x) { return x.name == "playlist.add"; });
        QCOMPARE(add->args.value("urls").toStringList(), QStringList{url});
        QCOMPARE(add->args.value("titles").toList().value(0).toString(), QStringLiteral("Eigener Sender"));
        QVERIFY(add->args.value("play").toBool());
        QCOMPARE(host.playlist.size(), 2);
        QCOMPARE(host.playlist.first(), QStringLiteral("C:/Videos/film.mkv"));
        QVERIFY(c->history().isEmpty());   // erst nach media.loaded

        // „Läuft“ erst, wenn genau diese Quelle geladen ist
        c->handleEvent("media.loading", {{"url", url}});
        QCOMPARE(c->state(), QStringLiteral("connecting"));
        host.loadedSource = url;
        c->handleEvent("media.loaded", {{"path", url}, {"source", url}, {"title", "x"}});
        QCOMPARE(c->state(), QStringLiteral("playing"));
        QCOMPARE(c->history().size(), 1);
        QCOMPARE(c->favorites().size(), 0);   // nicht als Favorit gewählt

        // Fehlende Titelmetadaten: Sendername bleibt, kein Titel
        QTest::qWait(150);
        QVERIFY(c->title().isEmpty() && c->artist().isEmpty());
        QCOMPARE(c->current().value("name").toString(), QStringLiteral("Eigener Sender"));
        // Metadaten kommen an
        host.mpvValues.insert("metadata", QVariantMap{{"icy-title", "Künstler - Lied"}});
        host.mpvValues.insert("audio-codec-name", "mp3");
        host.mpvValues.insert("audio-bitrate", 128000);
        QTRY_COMPARE(c->title(), QStringLiteral("Lied"));
        QCOMPARE(c->artist(), QStringLiteral("Künstler"));
        QCOMPARE(c->liveBitrate(), 128);

        // Nochmals abspielen: kein doppelter Eintrag, sondern der vorhandene
        c->stop();
        QCOMPARE(c->state(), QStringLiteral("stopped"));
        QVERIFY(c->title().isEmpty());
        QCOMPARE(host.count("player.stop"), 1);
        c->playCustom("Eigener Sender", url, false);
        QTRY_COMPARE(c->state(), QStringLiteral("connecting"));
        QCOMPARE(host.count("playlist.add"), 1);
        QCOMPARE(host.count("playlist.play"), 1);
    }

    void rejectsWebPages()
    {
        FakeHost host;
        auto c = makeController(host);
        QCOMPARE(c->customUrlError("https://www.example.com/radio.html"), QStringLiteral("webpage"));
        QCOMPARE(c->customUrlError("ftp://x"), QStringLiteral("scheme"));
        QVERIFY(!c->playCustom("x", "https://www.example.com/radio.html", false));
        // Erkennung am Inhalt: HTML-Antwort
        QVERIFY(c->playCustom("Seite", m_streams->base() + "/page", false));
        QTRY_COMPARE(c->state(), QStringLiteral("error"));
        QCOMPARE(c->errorCode(), QStringLiteral("webpage"));
        QCOMPARE(host.loadedUrls().size(), 0);
        // text/html mit Audio-Inhalt ist ein Stream
        QVERIFY(c->playCustom("AAC", m_streams->base() + "/aac", false));
        QTRY_COMPARE(c->state(), QStringLiteral("connecting"));
        // HLS-Playlist
        QVERIFY(c->playCustom("HLS", m_streams->base() + "/hls.m3u8", false));
        QTRY_COMPARE(c->state(), QStringLiteral("connecting"));
        QCOMPARE(host.loadedUrls().size(), 2);
    }

    void redirectChecks()
    {
        QNetworkAccessManager nam;
        StreamProbe probe(&nam);
        bool done = false;
        ProbeResult res;
        probe.probe(m_streams->base() + "/to-ftp", [&](const ProbeResult &r) { res = r; done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(res.kind, ProbeResult::BadRedirect);
        done = false;
        probe.probe(m_streams->base() + "/to-live", [&](const ProbeResult &r) { res = r; done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(res.kind, ProbeResult::Ok);
        QVERIFY(res.contentType.startsWith("audio/mpeg"));
    }

    void unreachableStream()
    {
        FakeHost host;
        auto c = makeController(host);
        c->setProbeTimeoutMs(6000);   // Windows wiederholt den Verbindungsaufbau zu einem geschlossenen Port ~2 s
        // Verbindung abgelehnt: Fehler im Panel, Player bekommt nichts
        c->play(station("uuid-x", "Toter Sender", QStringLiteral("http://127.0.0.1:%1/live").arg(closedPort())), "favorite");
        QTRY_COMPARE(c->state(), QStringLiteral("error"));
        QCOMPARE(c->errorCode(), QStringLiteral("unreachable"));
        QCOMPARE(host.loadedUrls().size(), 0);
        QVERIFY(host.count("osd.show") >= 1);   // Player-Fehleranzeige
        // 404
        c->play(station("uuid-y", "Weg", m_streams->base() + "/gibtsnicht"), "favorite");
        QTRY_COMPARE(c->state(), QStringLiteral("error"));
        QCOMPARE(c->errorCode(), QStringLiteral("unreachable"));

        // Player meldet den Fehler (z. B. Format) → Status Fehler, Aktualisieren angeboten
        const QString url = m_streams->base() + "/live.mp3";
        c->play(station("00000000-0000-0000-0000-000000001000", "Testsender 00", url), "favorite");
        QTRY_COMPARE(c->state(), QStringLiteral("connecting"));
        c->handleEvent("media.ended", {{"reason", "error"}, {"path", url}, {"source", url}, {"error", "unrecognized file format"}});
        QCOMPARE(c->state(), QStringLiteral("error"));
        QCOMPARE(c->errorCode(), QStringLiteral("stream"));
        QVERIFY(c->errorDetail().contains("unrecognized"));
        QVERIFY(c->canRefresh());
        QVERIFY(c->history().isEmpty());

        // Katalogeintrag aktualisieren → neue Adresse
        c->refreshCurrent();
        QTRY_VERIFY(!c->refreshing());
        QTRY_COMPARE(c->state(), QStringLiteral("error"));   // fresh.example.invalid ist nicht auflösbar
        QCOMPARE(c->current().value("name").toString(), QStringLiteral("Testsender 00 (neu)"));
        QCOMPARE(m_api->hits("/json/stations/byuuid") >= 1, true);
    }

    void fastStationSwitch()
    {
        FakeHost host;
        auto c = makeController(host);
        const QString slow = m_streams->base() + "/slow.mp3";
        const QString fast = m_streams->base() + "/live.mp3";
        c->play(station("a", "Sender A", slow), "favorite");
        c->play(station("b", "Sender B", fast), "favorite");
        QTRY_COMPARE(c->state(), QStringLiteral("connecting"));
        QTest::qWait(900);   // Prüfung von A wäre jetzt fertig – darf nichts mehr laden
        QCOMPARE(host.loadedUrls(), QStringList{fast});
        QCOMPARE(c->current().value("id").toString(), QStringLiteral("b"));

        // Verspätetes media.loaded einer alten Quelle während B verbindet: B bleibt „verbinde“ … und
        // der Player spielt jetzt etwas anderes → nicht mehr unser Sender
        c->handleEvent("media.loaded", {{"path", slow}, {"source", slow}});
        QCOMPARE(c->state(), QStringLiteral("idle"));

        // Neu: B → C schnell, C lädt
        c->play(station("b", "Sender B", fast), "favorite");
        QTRY_COMPARE(c->state(), QStringLiteral("connecting"));
        const QString hls = m_streams->base() + "/hls.m3u8";
        c->play(station("c", "Sender C", hls), "favorite");
        QTRY_COMPARE(c->state(), QStringLiteral("connecting"));
        c->handleEvent("media.ended", {{"reason", "stop"}, {"source", fast}, {"path", fast}});   // alter Sender endet
        QCOMPARE(c->state(), QStringLiteral("connecting"));
        c->handleEvent("media.loaded", {{"path", hls}, {"source", hls}});
        QCOMPARE(c->state(), QStringLiteral("playing"));
        QCOMPARE(c->history().first().toMap().value("id").toString(), QStringLiteral("c"));
        QCOMPARE(c->history().size(), 1);   // A und B liefen nie

        // Metadaten des alten Senders werden nicht übernommen: Player meldet andere Quelle
        host.loadedSource = fast;
        host.mpvValues.insert("metadata", QVariantMap{{"icy-title", "Alt - Falsch"}});
        QTest::qWait(200);
        QVERIFY(c->title().isEmpty());
        host.loadedSource = hls;
        host.mpvValues.insert("metadata", QVariantMap{{"title", "Neu"}});
        QTRY_COMPARE(c->title(), QStringLiteral("Neu"));
        // Senderwechsel setzt Titel zurück
        c->play(station("a", "Sender A", fast), "favorite");
        QVERIFY(c->title().isEmpty());

        // Stop während der Prüfung: kein späteres Laden
        const int loads = host.loadedUrls().size();
        c->play(station("s", "Sender S", slow), "favorite");
        c->stop();
        QTest::qWait(900);
        QCOMPARE(host.loadedUrls().size(), loads);
        QCOMPARE(c->state(), QStringLiteral("stopped"));

        // Stopp im Player
        c->play(station("b", "Sender B", fast), "favorite");
        QTRY_COMPARE(c->state(), QStringLiteral("connecting"));
        c->handleEvent("player.stopped", {{"path", fast}});
        QCOMPARE(c->state(), QStringLiteral("stopped"));
    }

    void clickOnlyWhenEnabled()
    {
        FakeHost host;
        auto c = makeController(host);
        const QString url = m_streams->base() + "/live.mp3";
        const int before = m_api->hits("/json/url/clicktest");
        c->play(station("clicktest", "Klick", url), "search");
        QTRY_COMPARE(c->state(), QStringLiteral("connecting"));
        c->toggleFavorite(station("clicktest", "Klick", url));
        c->play(station("clicktest", "Klick", url), "favorite");
        QTRY_COMPARE(c->state(), QStringLiteral("connecting"));
        QTest::qWait(200);
        QCOMPARE(m_api->hits("/json/url/clicktest"), before);   // aus = nie
        c->setSendClicks(true);
        QVERIFY(host.settings.value("sendClicks").toBool());
        c->play(station("clicktest", "Klick", url), "favorite");
        QTest::qWait(200);
        QCOMPARE(m_api->hits("/json/url/clicktest"), before);   // Favorit meldet nicht
        c->playCustom("x", url, false);
        QTest::qWait(200);
        QCOMPARE(m_api->hits("/json/url/clicktest"), before);
        c->play(station("clicktest", "Klick", url), "search");
        QTRY_COMPARE(m_api->hits("/json/url/clicktest"), before + 1);
    }

    void lastWorkingUrlRemembered()
    {
        FakeHost host;
        auto c = makeController(host);
        Station s; s.id = "w1"; s.name = "W"; s.url = m_streams->base() + "/to-live"; s.urlResolved = m_streams->base() + "/live.mp3";
        c->toggleFavorite(s.toVariant());
        c->play(s.toVariant(), "favorite");
        QTRY_COMPARE(c->state(), QStringLiteral("connecting"));
        const QString used = host.loadedUrls().last();
        QCOMPARE(used, s.urlResolved);
        c->handleEvent("media.loaded", {{"path", used}, {"source", used}});
        QCOMPARE(c->favorites().first().toMap().value("lastWorkingUrl").toString(), used);
    }

    void logoCache()
    {
        TestHttpServer img([](const QString &path, const QUrlQuery &, const QByteArray &) {
            if (path == "/logo.png") return TestResponse{200, "image/png", QByteArray("\x89PNG\r\n\x1a\n", 8) + QByteArray(100, 'x')};
            return TestResponse{200, "text/html", "<html>nope</html>"};
        });
        FakeHost host;
        auto c = makeController(host);
        const QString logo = img.base() + "/logo.png";
        QVERIFY(c->logoUrl(logo).isEmpty());   // erst laden, nicht blockieren
        QTRY_VERIFY(!c->logoUrl(logo).isEmpty());
        QVERIFY(c->logoUrl(logo).startsWith("file:"));
        QVERIFY(c->logoCacheBytes() > 0);
        QVERIFY(c->logoUrl(img.base() + "/fake.png").isEmpty());
        QTest::qWait(300);
        QVERIFY(c->logoUrl(img.base() + "/fake.png").isEmpty());   // HTML ist kein Logo
        c->toggleFavorite(station("keep", "Bleibt", "https://x.example/s"));
        c->clearLogoCache();
        c->clearCatalogCache();
        QCOMPARE(c->logoCacheBytes(), 0);
        QCOMPARE(c->favorites().size(), 1);   // Favoriten bleiben
    }


    // Nachladen und Stern dürfen die Liste nicht neu aufbauen – sonst springt sie nach oben (1.0.1)
    void scrollPositionKept()
    {
        FakeHost host;
        auto c = makeController(host);
        c->search("Test", {}, {}, {}, "name");
        QTRY_VERIFY(!c->searchBusy() && c->results().size() == 30);
        QQmlEngine engine;
        QQmlComponent comp(&engine);
        comp.setData("import QtQuick\nListView { width: 300; height: 200; property var c\n"
                     " model: c.resultsModel\n delegate: Item { required property var modelData; width: 300; height: 50 } }", QUrl());
        std::unique_ptr<QObject> view(comp.createWithInitialProperties({{"c", QVariant::fromValue<QObject *>(c.get())}}));
        QVERIFY2(view, qPrintable(comp.errorString()));
        QTRY_COMPARE(view->property("count").toInt(), 30);
        view->setProperty("contentY", 1100.0);   // weit unten
        auto *model = qobject_cast<QAbstractItemModel *>(c->resultsModel());
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        QSignalSpy inserts(model, &QAbstractItemModel::rowsInserted);
        c->loadMore();
        QTRY_COMPARE(view->property("count").toInt(), 35);
        QCOMPARE(resets.count(), 0);
        QVERIFY(inserts.count() > 0);
        QCOMPARE(view->property("contentY").toDouble(), 1100.0);
        // Favorit setzen ändert die Trefferliste nicht
        c->toggleFavorite(c->results().at(25).toMap());
        QCOMPARE(view->property("contentY").toDouble(), 1100.0);

        // Favoriten-/Verlaufsmodell: Entfernen/Hinzufügen ohne Neuaufbau, Reihenfolge stimmt
        auto *fav = qobject_cast<QAbstractItemModel *>(c->favoritesModel());
        QSignalSpy favResets(fav, &QAbstractItemModel::modelReset);
        for (int i = 0; i < 5; ++i) c->toggleFavorite(c->results().at(i).toMap());
        c->removeFavorite(c->results().at(2).toMap().value("id").toString());
        QCOMPARE(fav->rowCount(), 5);
        QCOMPARE(favResets.count(), 0);
        const QVariantList expect = c->favorites();
        for (int i = 0; i < fav->rowCount(); ++i)
            QCOMPARE(fav->data(fav->index(i, 0), Qt::UserRole).toMap().value("id"), expect.at(i).toMap().value("id"));
    }
    // ------------------------------------------------------------------ Oberfläche

    void englishTranslation()
    {
        QTranslator tr;
        QVERIFY(tr.load(QStringLiteral(UVEINETRADIO_SOURCE_DIR "/lang/uveinetradio_en.qm")));
        QCoreApplication::installTranslator(&tr);
        QQmlEngine engine;
        QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(UVEINETRADIO_SOURCE_DIR "/qml/RadioTexts.qml")));
        std::unique_ptr<QObject> o(comp.create());
        QVERIFY2(o, qPrintable(comp.errorString()));
        QVariant ret;
        QMetaObject::invokeMethod(o.get(), "stateText", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, "playing"),
                                  Q_ARG(QVariant, false), Q_ARG(QVariant, false));
        QCOMPARE(ret.toString(), QStringLiteral("Playing"));
        QMetaObject::invokeMethod(o.get(), "errorText", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, "webpage"), Q_ARG(QVariant, ""));
        QVERIFY(ret.toString().startsWith("This address is a web page"));
        QCoreApplication::removeTranslator(&tr);
    }

    // Bild des Panels (nur mit UVEINETRADIO_SNAPSHOT=<Ordner>): Layout prüfen, ohne Fenster auf dem Desktop
    void snapshot()
    {
        const QString out = qEnvironmentVariable("UVEINETRADIO_SNAPSHOT");
        if (out.isEmpty()) QSKIP("UVEINETRADIO_SNAPSHOT nicht gesetzt");
        FakeHost host;
        auto c = makeController(host);
        Station fav; fav.id = "f1"; fav.name = "Mein Lieblingssender"; fav.url = m_streams->base() + "/live.mp3";
        fav.country = "Germany"; fav.codec = "MP3"; fav.bitrate = 192;
        c->toggleFavorite(fav.toVariant());
        c->play(fav.toVariant(), "favorite");
        QTRY_COMPARE(c->state(), QStringLiteral("connecting"));
        host.loadedSource = fav.url;
        c->handleEvent("media.loaded", {{"path", fav.url}, {"source", fav.url}});
        host.mpvValues.insert("metadata", QVariantMap{{"icy-title", "Daft Punk - One More Time"}});
        host.mpvValues.insert("audio-codec-name", "mp3");
        host.mpvValues.insert("audio-bitrate", 192000);
        QTRY_VERIFY(!c->title().isEmpty());
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QQmlEngine engine;
        QQmlComponent t(&engine);
        t.setData("import QtQuick\nQtObject { property bool dark: true; property color bg: '#0b0d10'; property color panel: '#14171c';"
                  "property color panel2: '#1b1f26'; property color line: '#262b34'; property color text: '#e6e8eb';"
                  "property color muted: '#8b93a1'; property color accent: '#ff8a1f'; property color accent2: '#ffb15c';"
                  "property color focus: '#4da3ff'; property color error: '#ff6b6b' }", QUrl());
        std::unique_ptr<QObject> theme(t.create());
        for (const char *file : {"RadioPanel.qml", "RadioSettings.qml"}) {
            QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(UVEINETRADIO_SOURCE_DIR "/qml/") + QLatin1String(file)));
            auto *item = qobject_cast<QQuickItem *>(comp.createWithInitialProperties(
                {{"controller", QVariant::fromValue<QObject *>(c.get())}, {"theme", QVariant::fromValue(theme.get())}}));
            QVERIFY2(item, qPrintable(comp.errorString()));
            QQuickWindow win;
            win.setColor(QColor("#14171c"));
            win.resize(380, 720);
            item->setParentItem(win.contentItem());
            item->setWidth(380);
            item->setHeight(720);
            win.show();
            QTest::qWait(1500);
            QVERIFY(win.grabWindow().save(QDir(out).filePath(QString::fromLatin1(file).replace(".qml", ".png"))));
            delete item;
        }
    }
    void qmlPagesLoad_data()
    {
        QTest::addColumn<QString>("file");
        for (const char *f : {"RadioPanel.qml", "RadioSettings.qml", "NowPlaying.qml", "StationRow.qml"}) QTest::newRow(f) << f;
    }
    void qmlPagesLoad()
    {
        QFETCH(QString, file);
        FakeHost host;
        auto c = makeController(host);
        QQmlEngine engine;
        QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(UVEINETRADIO_SOURCE_DIR "/qml/") + file));
        QObject *theme = nullptr;
        {
            QQmlComponent t(&engine);
            t.setData("import QtQuick\nQtObject { property bool dark: true; property color bg: '#0b0d10'; property color panel: '#14171c';"
                      "property color panel2: '#1b1f26'; property color line: '#262b34'; property color text: '#e6e8eb';"
                      "property color muted: '#8b93a1'; property color accent: '#ff8a1f'; property color accent2: '#ffb15c';"
                      "property color focus: '#4da3ff'; property color error: '#ff6b6b' }", QUrl());
            theme = t.create();
            QVERIFY(theme);
        }
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError> &w) { warnings << w; });
        QObject *obj = comp.createWithInitialProperties({{"controller", QVariant::fromValue<QObject *>(c.get())},
                                                         {"theme", QVariant::fromValue(theme)}});
        QVERIFY2(obj, qPrintable(comp.errorString()));
        if (auto *item = qobject_cast<QQuickItem *>(obj)) { item->setWidth(380); item->setHeight(700); }
        QTest::qWait(300);   // Startsuche läuft gegen den Test-Server
        QStringList w;
        for (const auto &e : warnings) w << e.toString();
        QVERIFY2(warnings.isEmpty(), qPrintable(w.join('\n')));
        delete obj;
        delete theme;
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    TstUveInetRadio t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_uveinetradio.moc"
