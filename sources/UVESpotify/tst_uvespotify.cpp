// UVESpotify: Erkennung ohne Netzwerk.
// Mit UVESPOTIFY_LIVE=<Datei mit einem Link pro Zeile> löst live() zusätzlich echte Links auf (ohne Anmeldung).
#include "SpotifyCore.h"
#include "SpotifyNet.h"

#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

using namespace uve::spotify;

// JSON mit ' statt " schreibt sich im Test leichter
static QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object();
}

class TstUveSpotify : public QObject
{
    Q_OBJECT
private slots:
    void links()
    {
        Link l = parseLink("https://open.spotify.com/intl-de/track/5Fim1gaXBgsiFfsQAfQSDS?si=42b2a62aab1447d3");
        QCOMPARE(int(l.kind), int(Kind::Track));
        QCOMPARE(l.id, QString("5Fim1gaXBgsiFfsQAfQSDS"));
        l = parseLink("https://open.spotify.com/playlist/37i9dQZF1DWUW2bvSkjcJ6?si=8e043bbe8bac4dd7");
        QCOMPARE(int(l.kind), int(Kind::Playlist));
        QCOMPARE(l.id, QString("37i9dQZF1DWUW2bvSkjcJ6"));
        QCOMPARE(int(parseLink("https://open.spotify.com/embed/album/4aawyAB9vmqN3uQ7FjRGTy").kind), int(Kind::Album));
        QCOMPARE(int(parseLink("https://open.spotify.com/artist/0TnOYISbd1XYRBk9myaseg").kind), int(Kind::Artist));
        QCOMPARE(int(parseLink("https://open.spotify.com/collection/tracks").kind), int(Kind::Liked));
        QCOMPARE(int(parseLink("https://open.spotify.com/episode/abcdefghijklmnop").kind), int(Kind::Unknown));
        QCOMPARE(int(parseLink("https://open.spotify.com/").kind), int(Kind::Unknown));
        QCOMPARE(int(parseLink("https://example.com/track/5Fim1gaXBgsiFfsQAfQSDS").kind), int(Kind::Unknown));
    }

    void titledTrackUrl()
    {
        Track t;
        t.id = "5Fim1gaXBgsiFfsQAfQSDS";
        t.title = "Back In Black / Live";
        t.artists = QStringList{"AC/DC", "Ünïcødé"};
        const QString url = trackUrl(t);
        // Das Plugin liest weiterhin die ID
        const Link l = parseLink(url);
        QCOMPARE(int(l.kind), int(Kind::Track));
        QCOMPARE(l.id, t.id);
        // So bildet die Wiedergabeliste des Players den Namen (PlaylistModel::displayName)
        const QUrl u(url);
        QCOMPARE(QUrl::fromPercentEncoding(u.path().section('/', -1).toUtf8()), QString("AC∕DC, Ünïcødé – Back In Black ∕ Live"));
        QVERIFY(u.isValid());
        QCOMPARE(u.scheme(), QString("https"));
        Track bare;
        bare.id = "X1234567890";
        QCOMPARE(trackUrl(bare), QString("https://open.spotify.com/track/X1234567890"));
    }

    void embedData()
    {
        const QString html = "<html><script id=\"__NEXT_DATA__\" type=\"application/json\">"
                             "{\"props\":{\"pageProps\":{\"state\":{\"data\":{\"entity\":{\"type\":\"track\",\"id\":\"5Fim\",\"name\":\"Hasta el Amanecer\","
                             "\"artists\":[{\"name\":\"Nicky Jam\"}],\"duration\":198706}}}}}}</script></html>";
        const Track t = embedTrack(embedEntity(html));
        QCOMPARE(t.id, QString("5Fim"));
        QCOMPARE(t.display(), QString("Nicky Jam – Hasta el Amanecer"));
        QCOMPARE(t.query(), QString("Nicky Jam - Hasta el Amanecer"));
        QCOMPARE(t.durationMs, qint64(198706));

        const QJsonObject list = json("{'trackList': ["
                                      " {'uri': 'spotify:track:AAA', 'title': 'Bacio', 'subtitle': 'Domiziana', 'duration': 178475, 'entityType': 'track'},"
                                      " {'uri': 'spotify:track:BBB', 'title': 'BLACK', 'subtitle': 'Bantu Nation,\\u00a0Nu51', 'duration': 173857},"
                                      " {'uri': 'spotify:episode:CCC', 'title': 'Pod', 'subtitle': 'X', 'entityType': 'episode'}]}");
        const QList<Track> tracks = embedTrackList(list);
        QCOMPARE(tracks.size(), 2);
        QCOMPARE(tracks.at(1).id, QString("BBB"));
        QCOMPARE(tracks.at(1).artists, (QStringList{"Bantu Nation", "Nu51"}));
    }

    void apiData()
    {
        const QJsonObject page = json("{'items': ["
                                      " {'item': {'type': 'track', 'id': 'T1', 'name': 'One', 'duration_ms': 1000, 'artists': [{'name': 'A'}]}},"
                                      " {'track': {'type': 'track', 'id': 'T2', 'name': 'Two', 'duration_ms': 2000, 'artists': [{'name': 'B'}, {'name': 'C'}]}},"
                                      " {'track': {'type': 'track', 'id': null, 'name': 'Local', 'is_local': true}},"
                                      " {'track': {'type': 'episode', 'id': 'E1', 'name': 'Podcast'}},"
                                      " {'id': 'T3', 'name': 'Album track', 'duration_ms': 3000, 'artists': [{'name': 'D'}]}]}");
        const QList<Track> tracks = apiItems(page.value("items").toArray());
        QCOMPARE(tracks.size(), 3);
        QCOMPARE(tracks.at(1).display(), QString("B, C – Two"));
        QCOMPARE(tracks.at(2).id, QString("T3"));
    }

    void clock()
    {
        QCOMPARE(parseClock("3:20"), 200);
        QCOMPARE(parseClock("1:02:03"), 3723);
        QCOMPARE(parseClock("LIVE"), 0);
        QCOMPARE(parseClock(""), 0);
    }

    void youtubePick()
    {
        const QString html = "<script>var ytInitialData = {\"contents\":{\"list\":["
                             "{\"videoRenderer\":{\"videoId\":\"vid4m\",\"title\":{\"runs\":[{\"text\":\"Hasta el Amanecer | Video Oficial\"}]},\"lengthText\":{\"simpleText\":\"4:06\"}}},"
                             "{\"videoRenderer\":{\"videoId\":\"lyr\",\"title\":{\"runs\":[{\"text\":\"Hasta el Amanecer (Letra)\"}]},\"lengthText\":{\"simpleText\":\"3:20\"}}},"
                             "{\"videoRenderer\":{\"videoId\":\"mix\",\"title\":{\"runs\":[{\"text\":\"Mix\"}]},\"lengthText\":{\"simpleText\":\"1:21:07\"}}}"
                             "]}};</script>";
        const auto results = youtubeResults(html);
        QCOMPARE(results.size(), 3);
        QCOMPARE(results.at(0).seconds, 246);
        QCOMPARE(pickYoutube(results, 198706, false), QString("lyr"));     // passende Länge
        QCOMPARE(pickYoutube(results, 198706, true), QString("vid4m"));    // Musikvideo gewünscht
        QCOMPARE(pickYoutube(results, 0, false), QString("vid4m"));        // Länge unbekannt: erster normaler Treffer
        QCOMPARE(pickYoutube({}, 1000, false), QString());
    }

    void directStreams()
    {
        // Nur Ton: eine Zeile
        QCOMPARE(directStream({"https://rr2.googlevideo.com/videoplayback?expire=1794798250&id=a", ""}),
                 QString("https://rr2.googlevideo.com/videoplayback?expire=1794798250&id=a"));
        // Bild + Ton getrennt: eine EDL-Quelle
        QCOMPARE(directStream({"https://v.example/a;b", "https://a.example/c,d"}),
                 QString("edl://!no_clip;%21%https://v.example/a;b;!new_stream;!no_clip;%21%https://a.example/c,d"));
        QCOMPARE(directStream({"ERROR: nope", ""}), QString());
        QCOMPARE(streamExpiry("https://rr2.googlevideo.com/videoplayback?expire=1794798250&ei=x"), qint64(1794798250));
        QCOMPARE(streamExpiry("https://manifest.googlevideo.com/api/manifest/hls/expire/1794798251/ei/x"), qint64(1794798251));
        QCOMPARE(streamExpiry("https://example.com/a"), qint64(0));
    }

    void pkce()
    {
        // Beispiel aus RFC 7636, Anhang B
        QCOMPARE(pkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), QString("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
        const QString token = randomToken(96);
        QCOMPARE(token.size(), 96);
        QVERIFY(token != randomToken(96));
    }

    void live()
    {
        const QString list = qEnvironmentVariable("UVESPOTIFY_LIVE");
        if (list.isEmpty()) QSKIP("UVESPOTIFY_LIVE not set");
        QFile file(list);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        for (const QString &raw : QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts)) {
            const QString url = raw.trimmed();
            if (url.isEmpty()) continue;
            ResolveJob job(url, nullptr, qEnvironmentVariable("UVESPOTIFY_MODE") == "video");
            QSignalSpy ok(&job, &ResolveJob::finished);
            QSignalSpy bad(&job, &ResolveJob::failed);
            job.start();
            QTRY_VERIFY_WITH_TIMEOUT(ok.count() + bad.count() > 0, 60000);
            if (ok.count()) {
                const auto r = ok.first().first().value<Resolution>();
                printf("OK   %s\n     -> %s\n     title=%s collection=%s count=%d more=%d first-more=%s\n", qPrintable(url), qPrintable(r.url),
                       r.title.toUtf8().constData(), r.collection.toUtf8().constData(), r.count, int(r.more.size()), qPrintable(r.more.value(0)));
            } else {
                printf("FAIL %s: %s\n", qPrintable(url), qPrintable(bad.first().first().toString()));
            }
            fflush(stdout);
        }
    }
};

QTEST_GUILESS_MAIN(TstUveSpotify)
#include "tst_uvespotify.moc"
