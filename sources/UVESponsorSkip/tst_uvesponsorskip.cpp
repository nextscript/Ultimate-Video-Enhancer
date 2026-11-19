#include "SponsorCore.h"

#include <QtTest>

using namespace uve::sponsor;

class TstUveSponsorSkip : public QObject
{
    Q_OBJECT

    static Segment seg(double a, double b, const char *cat, const char *uuid, const char *action = "skip")
    {
        return {a, b, QString::fromLatin1(cat), QString::fromLatin1(action), QString::fromLatin1(uuid)};
    }

private slots:
    void videoIds_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("id");
        QTest::newRow("watch") << "https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=42s" << "dQw4w9WgXcQ";
        QTest::newRow("mobile") << "https://m.youtube.com/watch?feature=share&v=dQw4w9WgXcQ" << "dQw4w9WgXcQ";
        QTest::newRow("music") << "https://music.youtube.com/watch?v=dQw4w9WgXcQ&list=RD" << "dQw4w9WgXcQ";
        QTest::newRow("short") << "https://youtu.be/dQw4w9WgXcQ?si=abc" << "dQw4w9WgXcQ";
        QTest::newRow("shorts") << "https://www.youtube.com/shorts/dQw4w9WgXcQ" << "dQw4w9WgXcQ";
        QTest::newRow("live") << "https://www.youtube.com/live/dQw4w9WgXcQ?feature=share" << "dQw4w9WgXcQ";
        QTest::newRow("embed") << "https://www.youtube-nocookie.com/embed/dQw4w9WgXcQ" << "dQw4w9WgXcQ";
        QTest::newRow("no id") << "https://www.youtube.com/@channel" << "";
        QTest::newRow("bad id") << "https://www.youtube.com/watch?v=abc" << "";
        QTest::newRow("other") << "https://example.com/watch?v=dQw4w9WgXcQ" << "";
        QTest::newRow("file") << "C:/Videos/clip.mp4" << "";
    }
    void videoIds()
    {
        QFETCH(QString, url);
        QFETCH(QString, id);
        QCOMPARE(videoIdFromUrl(url), id);
    }

    void hashAndUrls()
    {
        QCOMPARE(hashPrefix(QStringLiteral("dQw4w9WgXcQ")), QStringLiteral("5f6b"));
        const QString u = skipSegmentsUrl(QStringLiteral("https://sponsor.ajay.app/"), QStringLiteral("5f6b"));
        QVERIFY(u.startsWith(QLatin1String("https://sponsor.ajay.app/api/skipSegments/5f6b?categories=")));
        QVERIFY(u.contains(QLatin1String("sponsor")));
        QVERIFY(u.contains(QLatin1String("actionTypes=")));
        QVERIFY(!u.contains(QLatin1String("dQw4w9WgXcQ")));   // die Video-ID selbst geht nie raus
        QCOMPARE(viewedUrl(QStringLiteral("https://sponsor.ajay.app"), QStringLiteral("abc")),
                 QStringLiteral("https://sponsor.ajay.app/api/viewedVideoSponsorTime?UUID=abc"));
    }

    void parse()
    {
        const QByteArray body = R"([
            {"videoID":"otherVideo1","segments":[{"category":"sponsor","actionType":"skip","segment":[1,2],"UUID":"x"}]},
            {"videoID":"dQw4w9WgXcQ","segments":[
                {"category":"outro","actionType":"skip","segment":[200,212.5],"UUID":"o"},
                {"category":"sponsor","actionType":"skip","segment":[10.5,40],"UUID":"s"},
                {"category":"poi_highlight","actionType":"poi","segment":[50,50],"UUID":"p"},
                {"category":"selfpromo","actionType":"mute","segment":[60,70],"UUID":"m"},
                {"category":"chapter","actionType":"chapter","segment":[0,100],"UUID":"c"}]}])";
        const QList<Segment> s = segmentsFromJson(body, QStringLiteral("dQw4w9WgXcQ"));
        QCOMPARE(s.size(), 3);
        QCOMPARE(s.at(0).uuid, QStringLiteral("s"));
        QCOMPARE(s.at(0).start, 10.5);
        QCOMPARE(s.at(1).actionType, QStringLiteral("mute"));
        QCOMPARE(s.at(2).category, QStringLiteral("outro"));
        QVERIFY(segmentsFromJson("not json", QStringLiteral("dQw4w9WgXcQ")).isEmpty());
    }

    void autoSkipChainsAdjacent()
    {
        SkipEngine e;
        e.setMode(QStringLiteral("intro"), Mode::Auto);
        e.setSegments({seg(10, 20, "sponsor", "a"), seg(20.1, 30, "intro", "b"), seg(28, 35, "sponsor", "c")});
        QVERIFY(!e.tick(5).jump.valid);
        const SkipEngine::Tick t = e.tick(10.2);
        QVERIFY(t.jump.valid);
        QCOMPARE(t.jump.to, 35.0);
        QCOMPARE(t.jump.uuids.size(), 3);
        QCOMPARE(t.jump.category, QStringLiteral("sponsor"));
        QVERIFY(!e.tick(34.8).jump.valid);   // fast am Ende: kein Sprung mehr
    }

    void manualPromptsOnce()
    {
        SkipEngine e;   // intro: Standard "Per Taste"
        e.setSegments({seg(0, 8, "intro", "i")});
        SkipEngine::Tick t = e.tick(0.0);
        QVERIFY(!t.jump.valid);
        QCOMPARE(t.prompt, QStringLiteral("intro"));
        QVERIFY(e.tick(1.0).prompt.isEmpty());   // nur einmal je Betreten
        const SkipEngine::Jump j = e.manualJump(2.0);
        QVERIFY(j.valid);
        QCOMPARE(j.to, 8.0);
        QVERIFY(e.tick(9).prompt.isEmpty());
        QCOMPARE(e.tick(3).prompt, QStringLiteral("intro"));   // erneut betreten → erneut Hinweis
    }

    void offIgnored()
    {
        SkipEngine e;
        e.setMode(QStringLiteral("sponsor"), Mode::Off);
        e.setSegments({seg(10, 20, "sponsor", "a")});
        QVERIFY(!e.tick(12).jump.valid);
        QVERIFY(e.tick(12).prompt.isEmpty());
        QVERIFY(!e.manualJump(12).valid);
    }

    void undoSuppressesAutoSkip()
    {
        SkipEngine e;
        e.setSegments({seg(10, 20, "sponsor", "a")});
        const SkipEngine::Tick t = e.tick(10.1);
        QVERIFY(t.jump.valid);
        e.suppress(t.jump.uuids);   // Sprung rückgängig
        const SkipEngine::Tick again = e.tick(10.3);
        QVERIFY(!again.jump.valid);
        QVERIFY(again.prompt.isEmpty());
        QVERIFY(e.manualJump(11).valid);   // per Taste geht es weiterhin
        e.setSegments(e.segments());       // neues Video / neu geladen: wieder automatisch
        QVERIFY(e.tick(10.1).jump.valid);
    }

    void muteSegments()
    {
        SkipEngine e;
        e.setMode(QStringLiteral("selfpromo"), Mode::Auto);
        e.setSegments({seg(10, 20, "selfpromo", "m", "mute")});
        QVERIFY(!e.tick(5).mute);
        const SkipEngine::Tick t = e.tick(12);
        QVERIFY(t.mute);
        QVERIFY(!t.jump.valid);
        QVERIFY(!e.tick(21).mute);
    }

    void time()
    {
        QCOMPARE(formatTime(65.4), QStringLiteral("1:05"));
        QCOMPARE(formatTime(3725), QStringLiteral("1:02:05"));
    }
};

QTEST_GUILESS_MAIN(TstUveSponsorSkip)
#include "tst_uvesponsorskip.moc"
