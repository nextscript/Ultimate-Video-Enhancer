// UVESuno: Erkennung ohne Netzwerk.
// Mit UVESUNO_LIVE=<Datei mit einem Link pro Zeile> löst live() zusätzlich echte Links auf.
#include "SunoCore.h"
#include "SunoNet.h"

#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

using namespace uve::suno;

// JSON mit ' statt " schreibt sich im Test leichter
static QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object();
}

class TstUveSuno : public QObject
{
    Q_OBJECT
private slots:
    void links()
    {
        Link l = parseLink("https://suno.com/song/94cf05ef-1549-4f27-ae3c-533900476b4c");
        QCOMPARE(int(l.kind), int(Kind::Song));
        QCOMPARE(l.id, QString("94cf05ef-1549-4f27-ae3c-533900476b4c"));
        QCOMPARE(int(parseLink("https://suno.com/song/94CF05EF-1549-4f27-ae3c-533900476b4c/Name%20%E2%80%93%20Titel").kind), int(Kind::Song));
        QCOMPARE(int(parseLink("https://app.suno.ai/song/94cf05ef-1549-4f27-ae3c-533900476b4c/").kind), int(Kind::Song));
        QCOMPARE(int(parseLink("https://suno.com/embed/94cf05ef-1549-4f27-ae3c-533900476b4c").kind), int(Kind::Song));
        l = parseLink("https://suno.com/playlist/990fd5fe-70d2-449b-8a4d-3cb0a7d3e805");
        QCOMPARE(int(l.kind), int(Kind::Playlist));
        l = parseLink("https://suno.com/@freak288?page=songs");
        QCOMPARE(int(l.kind), int(Kind::Profile));
        QCOMPARE(l.id, QString("freak288"));
        l = parseLink("https://suno.com/s/AbCdEf123");
        QCOMPARE(int(l.kind), int(Kind::Short));
        QCOMPARE(int(parseLink("https://suno.com/song/not-a-uuid").kind), int(Kind::Unknown));
        QCOMPARE(int(parseLink("https://suno.com/create").kind), int(Kind::Unknown));
        QCOMPARE(int(parseLink("https://example.com/song/94cf05ef-1549-4f27-ae3c-533900476b4c").kind), int(Kind::Unknown));
    }

    void clipSources()
    {
        // Download gesperrt: offizielles MP4
        Clip c = clipFromJson(json("{'id': 'A', 'title': ' Χίλια  χιλιόμετρα ', 'display_name': 'Freak288', 'status': 'complete',"
                                   " 'audio_url': 'https://studio-api.prod.suno.com/api/forbidden',"
                                   " 'video_url': 'https://cdn1.suno.ai/A.mp4', 'metadata': {'duration': '174.6'},"
                                   " 'media_urls': [{'url': 'https://x.cloudfront.net/1/clip/A.m4a', 'encoding': '1.0.0'}]}"));
        QCOMPARE(c.url, QString("https://cdn1.suno.ai/A.mp4"));
        QCOMPARE(c.display(), QString("Freak288 – Χίλια χιλιόμετρα"));
        QCOMPARE(c.duration, 174.6);
        QVERIFY(c.isVideo);
        // Download erlaubt: MP3
        c = clipFromJson(json("{'id': 'B', 'title': 'T', 'handle': 'h', 'status': 'complete', 'audio_url': 'https://cdn1.suno.ai/B.mp3',"
                              " 'video_url': 'https://cdn1.suno.ai/B.mp4'}"));
        QCOMPARE(c.url, QString("https://cdn1.suno.ai/B.mp3"));
        QCOMPARE(c.artist, QString("h"));
        // Noch nicht fertig: nicht abspielbar
        c = clipFromJson(json("{'id': 'C', 'title': 'T', 'status': 'streaming', 'audio_url': 'https://cdn1.suno.ai/C.mp3'}"));
        QVERIFY(c.url.isEmpty());
    }

    void coverOptions()
    {
        Clip mp4 = clipFromJson(json("{'id': 'A', 'title': 'T', 'status': 'complete', 'audio_url': 'https://studio-api.prod.suno.com/api/forbidden',"
                                     " 'video_url': 'https://cdn1.suno.ai/A.mp4', 'image_url': 'https://cdn2.suno.ai/image_A.jpeg',"
                                     " 'image_large_url': 'https://cdn2.suno.ai/image_large_A.jpeg'}"));
        // Bild nur aus dem Cover, Ton nur aus dem MP4 – mit Längenangaben in Bytes
        QCOMPARE(playbackUrl(mp4), QString("edl://!no_clip;!delay_open,media_type=video;%39%https://cdn2.suno.ai/image_large_A.jpeg"
                                           ";!new_stream;!no_clip;!delay_open,media_type=audio;%26%https://cdn1.suno.ai/A.mp4"));
        Clip mp3 = clipFromJson(json("{'id': 'B', 'title': 'T', 'status': 'complete', 'audio_url': 'https://cdn1.suno.ai/B.mp3',"
                                     " 'image_url': 'https://cdn2.suno.ai/image_B.jpeg'}"));
        QVERIFY(playbackUrl(mp3).contains("%33%https://cdn2.suno.ai/image_B.jpeg;!new_stream;"));
        QVERIFY(playbackUrl(mp3).endsWith("media_type=audio;%26%https://cdn1.suno.ai/B.mp3"));
        Clip bare = clipFromJson(json("{'id': 'C', 'title': 'T', 'status': 'complete', 'video_url': 'https://cdn1.suno.ai/C.mp4'}"));
        QCOMPARE(playbackUrl(bare), QString("https://cdn1.suno.ai/C.mp4"));   // ohne Cover: die Datei selbst
    }

    void lists()
    {
        const QJsonObject playlist = json("{'name': 'Staff Picks', 'playlist_clips': ["
                                          " {'clip': {'id': 'A', 'title': 'One', 'status': 'complete', 'video_url': 'https://cdn1.suno.ai/A.mp4'}},"
                                          " {'clip': {'id': 'B', 'title': 'Two', 'status': 'error'}},"
                                          " {'clip': {'id': 'C', 'title': 'Three', 'status': 'complete', 'audio_url': 'https://cdn1.suno.ai/C.mp3'}}]}");
        const QList<Clip> clips = playlistClips(playlist);
        QCOMPARE(clips.size(), 2);
        QCOMPARE(clips.at(1).id, QString("C"));
        const QJsonObject profile = json("{'clips': [{'id': 'D', 'title': 'Four', 'status': 'complete', 'video_url': 'https://cdn1.suno.ai/D.mp4'}]}");
        QCOMPARE(profileClips(profile).size(), 1);
    }

    void titledSongUrl()
    {
        Clip c;
        c.id = "94cf05ef-1549-4f27-ae3c-533900476b4c";
        c.title = "AC/DC Tribute";
        c.artist = "Freak288";
        const QString url = songUrl(c);
        const Link l = parseLink(url);
        QCOMPARE(int(l.kind), int(Kind::Song));
        QCOMPARE(l.id, c.id);
        // So bildet die Wiedergabeliste des Players den Namen (PlaylistModel::displayName)
        QCOMPARE(QUrl::fromPercentEncoding(QUrl(url).path().section('/', -1).toUtf8()), QString("Freak288 – AC∕DC Tribute"));
    }

    void live()
    {
        const QString list = qEnvironmentVariable("UVESUNO_LIVE");
        if (list.isEmpty()) QSKIP("UVESUNO_LIVE not set");
        QFile file(list);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        for (const QString &raw : QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts)) {
            const QString url = raw.trimmed();
            if (url.isEmpty()) continue;
            ResolveJob job(url);
            QSignalSpy ok(&job, &ResolveJob::finished);
            QSignalSpy bad(&job, &ResolveJob::failed);
            job.start();
            QTRY_VERIFY_WITH_TIMEOUT(ok.count() + bad.count() > 0, 120000);
            if (ok.count()) {
                const auto r = ok.first().first().value<Resolution>();
                printf("OK   %s\n     -> %s\n     title=%s collection=%s count=%d more=%d\n", qPrintable(url), qPrintable(r.url.left(140)),
                       r.title.toUtf8().constData(), r.collection.toUtf8().constData(), r.count, int(r.more.size()));
            } else {
                printf("FAIL %s: %s (definitive=%d)\n", qPrintable(url), qPrintable(bad.first().first().toString()), int(bad.first().at(1).toBool()));
            }
            fflush(stdout);
        }
    }
};

QTEST_GUILESS_MAIN(TstUveSuno)
#include "tst_uvesuno.moc"
