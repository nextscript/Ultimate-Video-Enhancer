#include "services/MediaFiles.h"
#include "services/PlaylistModel.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace uve;

class TstPlaylist : public QObject
{
    Q_OBJECT
    static QStringList names(const PlaylistModel &p) { QStringList l; for (int i = 0; i < p.count(); ++i) l << p.pathAt(i); return l; }
private slots:
    void addRemove()
    {
        PlaylistModel p;
        p.add({"a.mp4", "b.mp4", "c.mp4"});
        QCOMPARE(p.count(), 3);
        p.setCurrentIndex(1);
        p.remove({0});
        QCOMPARE(p.currentIndex(), 0);
        QCOMPARE(p.pathAt(0), QString("b.mp4"));
        p.remove({0});
        QCOMPARE(p.currentIndex(), -1);
    }
    void insertNext()
    {
        PlaylistModel p;
        p.add({"a", "b", "c"});
        p.setCurrentIndex(0);
        p.add({"x"}, true);
        QCOMPARE(names(p), QStringList({"a", "x", "b", "c"}));
    }
    void nextWithRepeat()
    {
        PlaylistModel p;
        p.add({"a", "b"});
        p.setCurrentIndex(1);
        QCOMPARE(p.nextIndex(true), -1);
        p.setRepeat("all");
        QCOMPARE(p.nextIndex(true), 0);
        p.setRepeat("one");
        QCOMPARE(p.nextIndex(true), 1);   // automatisch: gleicher Titel
        QCOMPARE(p.nextIndex(false), -1); // manuell "Weiter" am Listenende: kein Umlauf (wie VLC)
        p.setCurrentIndex(0);
        QCOMPARE(p.nextIndex(false), 1);  // manuell "Weiter": nächster Titel trotz "eins wiederholen"
    }
    void shuffleNeverRepeatsCurrent()
    {
        PlaylistModel p;
        p.add({"a", "b", "c", "d"});
        p.setCurrentIndex(2);
        p.setShuffle(true);
        for (int i = 0; i < 50; ++i) QVERIFY(p.nextIndex(false) != 2);
    }
    void moveRowsUpDown()
    {
        PlaylistModel p;
        p.add({"a", "b", "c", "d"});
        p.moveRows({2, 3}, "up");
        QCOMPARE(names(p), QStringList({"a", "c", "d", "b"}));
        p.moveRows({0, 1}, "up"); // Block am Anfang bleibt stehen
        QCOMPARE(names(p), QStringList({"a", "c", "d", "b"}));
        p.moveRows({0}, "down");
        QCOMPARE(names(p), QStringList({"c", "a", "d", "b"}));
    }
    void moveRowsTopBottomNext()
    {
        PlaylistModel p;
        p.add({"a", "b", "c", "d", "e"});
        p.setCurrentIndex(1); // b
        p.moveRows({3, 4}, "next");
        QCOMPARE(names(p), QStringList({"a", "b", "d", "e", "c"}));
        QCOMPARE(p.currentIndex(), 1);
        p.moveRows({4}, "top");
        QCOMPARE(names(p), QStringList({"c", "a", "b", "d", "e"}));
        QCOMPARE(p.currentIndex(), 2);
        p.moveRows({0}, "bottom");
        QCOMPARE(names(p), QStringList({"a", "b", "d", "e", "c"}));
    }
    void removeErrorsAndOthers()
    {
        PlaylistModel p;
        p.add({"a", "b", "c"});
        p.setError(1, true);
        p.removeErrors();
        QCOMPARE(names(p), QStringList({"a", "c"}));
        p.removeOthers({1});
        QCOMPARE(names(p), QStringList({"c"}));
    }
    void m3uRoundtrip()
    {
        QTemporaryDir dir;
        (void)QFile(dir.filePath("x.mp4")).open(QIODevice::WriteOnly);
        (void)QFile(dir.filePath("y.mkv")).open(QIODevice::WriteOnly);
        PlaylistModel p;
        p.add({dir.filePath("x.mp4"), dir.filePath("y.mkv"), "https://example.com/stream.m3u8"});
        QVERIFY(p.saveM3U(dir.filePath("list.m3u")));
        const auto back = media::parsePlaylistFile(dir.filePath("list.m3u"));
        QCOMPARE(back.size(), 3);
        QCOMPARE(back[2], QString("https://example.com/stream.m3u8"));
        QCOMPARE(media::expand({dir.filePath("list.m3u")}).size(), 3);
    }
    void urlValidation()
    {
        QVERIFY(media::isAllowedUrlScheme("tcp://hls.ts3x.cc:8113"));
        QVERIFY(media::isAllowedUrlScheme("https://www.twitch.tv/mihanaplays"));
        QVERIFY(!media::isAllowedUrlScheme("javascript://x"));
        QVERIFY(!media::isAllowedUrlScheme("https://"));
        QVERIFY(!media::isUrl("C:\\Filme\\a.mkv"));
    }
    void folderExpansionSorted()
    {
        QTemporaryDir dir;
        for (const char *n : {"Folge 10.mkv", "Folge 2.mkv", "notes.txt", "Folge 1.mkv"})
            (void)QFile(dir.filePath(n)).open(QIODevice::WriteOnly);
        const auto e = media::expand({dir.path()});
        QCOMPARE(e.size(), 3);
        QVERIFY(e[0].endsWith("Folge 1.mkv"));
        QVERIFY(e[1].endsWith("Folge 2.mkv"));
        QVERIFY(e[2].endsWith("Folge 10.mkv"));
    }
};

QTEST_GUILESS_MAIN(TstPlaylist)
#include "tst_playlist.moc"
