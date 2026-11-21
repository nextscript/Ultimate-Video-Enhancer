// Live-Test (nur mit UVEINETRADIO_LIVE=1): echter Radio-Browser-Katalog (Spiegel per DNS) und echte Wiedergabe mit
// libmpv (ao=null, vo=null – kein Fenster, kein Ton). Ein Host-Ersatz übersetzt die Plugin-Befehle in mpv-Aufrufe und
// mpv-Ereignisse in media.loaded / media.ended – wie der Player. Sucht je einen MP3-, AAC- und HLS-Sender.
#include "FakeHost.h"
#include "RadioController.h"
#include "RadioTypes.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <mpv/client.h>

using namespace uve::radio;

static QVariant fromNode(const mpv_node &n)
{
    switch (n.format) {
    case MPV_FORMAT_STRING: return QString::fromUtf8(n.u.string);
    case MPV_FORMAT_FLAG: return bool(n.u.flag);
    case MPV_FORMAT_INT64: return qlonglong(n.u.int64);
    case MPV_FORMAT_DOUBLE: return n.u.double_;
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap m;
        for (int i = 0; i < n.u.list->num; ++i) m.insert(QString::fromUtf8(n.u.list->keys[i]), fromNode(n.u.list->values[i]));
        return m;
    }
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList l;
        for (int i = 0; i < n.u.list->num; ++i) l << fromNode(n.u.list->values[i]);
        return l;
    }
    default: return {};
    }
}

class MpvHost : public QObject
{
public:
    FakeHost host;
    mpv_handle *mpv = nullptr;
    RadioController *controller = nullptr;
    QString source;
    QTimer pump;

    MpvHost()
    {
        mpv = mpv_create();
        mpv_set_option_string(mpv, "vo", "null");
        mpv_set_option_string(mpv, "ao", "null");
        mpv_set_option_string(mpv, "ytdl", "no");
        mpv_set_option_string(mpv, "idle", "yes");
        mpv_set_option_string(mpv, "network-timeout", "15");
        mpv_initialize(mpv);
        host.override = [this](const QString &cmd, const QVariantMap &a) -> QVariant {
            if (cmd == "playlist.add" || cmd == "player.load") {
                const QString url = cmd == "player.load" ? a.value("url").toString() : a.value("urls").toStringList().value(0);
                host.playlist << url;
                load(url);
                return int(host.playlist.size());
            }
            if (cmd == "playlist.play") { load(host.playlist.value(a.value("index").toInt())); return true; }
            if (cmd == "player.stop") { const char *c[] = {"stop", nullptr}; mpv_command(mpv, c); return true; }
            if (cmd == "player.state") return QVariantMap{{"source", source}, {"path", source}};
            if (cmd == "mpv.get") {
                mpv_node n;
                if (mpv_get_property(mpv, a.value("name").toString().toUtf8().constData(), MPV_FORMAT_NODE, &n) < 0)
                    return QVariant(QString());
                const QVariant v = fromNode(n);
                mpv_free_node_contents(&n);
                return v.isValid() ? v : QVariant(QString());
            }
            return {};
        };
        connect(&pump, &QTimer::timeout, this, [this] { drain(); });
        pump.start(20);
    }
    ~MpvHost() override { mpv_terminate_destroy(mpv); }

    void load(const QString &url)
    {
        source = url;
        controller->handleEvent("media.loading", {{"url", url}});
        const QByteArray u = url.toUtf8();
        const char *c[] = {"loadfile", u.constData(), "replace", nullptr};
        mpv_command(mpv, c);
    }

    void drain()
    {
        while (true) {
            mpv_event *e = mpv_wait_event(mpv, 0);
            if (e->event_id == MPV_EVENT_NONE) return;
            if (e->event_id == MPV_EVENT_FILE_LOADED) {
                host.loadedSource = source;
                controller->handleEvent("media.loaded", {{"path", source}, {"source", source}});
            } else if (e->event_id == MPV_EVENT_END_FILE) {
                auto *ef = static_cast<mpv_event_end_file *>(e->data);
                const QString reason = ef->reason == MPV_END_FILE_REASON_ERROR ? "error"
                                     : ef->reason == MPV_END_FILE_REASON_EOF ? "eof" : "stop";
                controller->handleEvent("media.ended", {{"reason", reason}, {"source", source}, {"path", source},
                                                        {"error", ef->reason == MPV_END_FILE_REASON_ERROR ? QString::fromUtf8(mpv_error_string(ef->error)) : QString()}});
            }
        }
    }
};

class TstLive : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QJsonArray m_top;

    QJsonArray fetch(RadioController &c, const QUrlQuery &q)
    {
        bool done = false;
        RadioBrowser::Result res;
        c.browser()->get("/json/stations/search", q, 0, [&](const RadioBrowser::Result &r) { res = r; done = true; });
        if (!QTest::qWaitFor([&] { return done; }, 30000)) return {};
        if (!res.ok) qWarning() << "API:" << res.error;
        return res.data.array();
    }

    // Spielt Kandidaten nacheinander, bis einer läuft. Rückgabe: Name des Senders oder leer
    QString playOne(const QList<Station> &candidates, const char *label)
    {
        MpvHost m;
        m.host.dataDir = m_dir.filePath(label);
        RadioController c(&m.host, m.host.dataDir);
        m.controller = &c;
        c.setMetadataIntervalMs(1000);
        for (const Station &s : candidates) {
            c.play(s.toVariant(), "favorite");
            const bool ok = QTest::qWaitFor([&] { return c.state() == "playing" || c.state() == "error"; }, 25000);
            if (!ok || c.state() != "playing") {
                qInfo().noquote() << label << "nicht abspielbar:" << s.name << s.playUrl() << c.state() << c.errorCode() << c.errorDetail();
                continue;
            }
            // Etwas laufen lassen: Metadaten, Codec
            QTest::qWaitFor([&] { return !c.title().isEmpty(); }, 12000);
            QTest::qWait(1500);
            qInfo().noquote() << QStringLiteral("%1 LÄUFT: %2 | %3 | codec=%4 %5 kbit/s | Titel: %6")
                                     .arg(QLatin1String(label), s.name, s.playUrl(), c.liveCodec()).arg(c.liveBitrate())
                                     .arg(c.title().isEmpty() ? QStringLiteral("(keine Titelmetadaten – Sendername bleibt)")
                                                              : (c.artist().isEmpty() ? c.title() : c.artist() + " – " + c.title()));
            if (c.history().isEmpty()) return {};
            c.stop();
            return s.name;
        }
        return {};
    }

private slots:
    void initTestCase()
    {
        if (qEnvironmentVariable("UVEINETRADIO_LIVE") != QLatin1String("1")) QSKIP("UVEINETRADIO_LIVE=1 setzen");
        QVERIFY(m_dir.isValid());
    }

    void discoveryAndSearch()
    {
        FakeHost host;
        host.dataDir = m_dir.filePath("search");
        RadioController c(&host, host.dataDir);
        c.search("jazz", {}, {}, {}, "popularity");
        QVERIFY(QTest::qWaitFor([&] { return c.searchDone() && !c.searchBusy(); }, 30000));
        QVERIFY2(c.searchError().isEmpty(), qPrintable(c.searchError()));
        qInfo() << "Spiegel:" << c.mirror() << "Spiegelliste:" << c.browser()->mirrors() << "Treffer:" << c.results().size();
        QVERIFY(c.results().size() > 5);
        c.loadFacets();
        QVERIFY(QTest::qWaitFor([&] { return !c.facetsBusy(); }, 30000));
        qInfo() << "Länder" << c.countries().size() << "Sprachen" << c.languages().size() << "Genres" << c.tags().size();
        QVERIFY(c.countries().size() > 50);

        // Spiegelwechsel live: erster Spiegel tot
        QList<QUrl> mirrors = c.browser()->mirrors();
        mirrors.prepend(QUrl("https://127.0.0.1:1"));
        c.browser()->setMirrors(mirrors);
        c.search("rock", "DE", {}, {}, "popularity");
        QVERIFY(QTest::qWaitFor([&] { return !c.searchBusy(); }, 30000));
        QVERIFY2(c.searchError().isEmpty(), qPrintable(c.searchError()));
        qInfo() << "nach Spiegelwechsel:" << c.mirror() << c.results().size() << "Treffer";

        QUrlQuery q;
        for (auto [k, v] : {std::pair{"order", "clickcount"}, {"reverse", "true"}, {"hidebroken", "true"}, {"limit", "500"}})
            q.addQueryItem(k, v);
        m_top = fetch(c, q);
        QVERIFY(m_top.size() > 100);
    }

    void playMp3() { play([](const Station &s) { return s.codec.compare("MP3", Qt::CaseInsensitive) == 0 && !s.hls; }, "MP3"); }
    void playAac() { play([](const Station &s) { return s.codec.startsWith("AAC", Qt::CaseInsensitive) && !s.hls && !s.playUrl().contains(".m3u8"); }, "AAC"); }
    void playHls() { play([](const Station &s) { return s.hls || s.playUrl().contains(".m3u8"); }, "HLS"); }

private:
    void play(const std::function<bool(const Station &)> &want, const char *label)
    {
        QList<Station> cands;
        for (const auto &v : std::as_const(m_top)) {
            const Station s = Station::fromApi(v.toObject());
            if (s.isValid() && want(s)) cands << s;
            if (cands.size() >= 6) break;
        }
        if (cands.isEmpty()) QSKIP(qPrintable(QStringLiteral("kein %1-Sender im Katalog gefunden").arg(QLatin1String(label))));
        const QString name = playOne(cands, label);
        if (name.isEmpty()) QSKIP(qPrintable(QStringLiteral("kein %1-Sender erreichbar").arg(QLatin1String(label))));
    }
};

QTEST_GUILESS_MAIN(TstLive)
#include "tst_live.moc"
