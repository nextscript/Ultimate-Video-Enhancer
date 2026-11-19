// chat_test.cpp – v1.2, vier Teile in einem Durchlauf:
//  1) Kanalerkennung: Twitch-Kanal aus URL/Playlist (offline, tischtestbar)
//  2) Emoticon-Pipeline: erfundener IRC-Frame mit emotes-Tag → Bild wird geladen,
//     Nachricht wird als Segment mit src neu gerendert (messageUpdated)
//  3) Live-Join: echter Beitritt über wss://irc-ws.chat.twitch.tv
//     (Standard-Kanal: eternal_pestilence – großer 7tv-Kanal, deckt Teil 4 mit ab)
//  4) 7tv-Pipeline: Katalog anonym (Global + Raum-ID), erfundener Frame OHNE Tag
//     ("Nous test" – Name aus dem Kanal-Set von eternal_pestilence) → 7tv-Name wird
//     erkannt, GIF/WebP lädt in emotes/7tv/, die
//     fertige Zeile wird nachgeladen (rebuild nach Bild + nach Katalog)
// Aufruf: chat_test.exe [kanal]   – aus out/test starten!
// Exit 0 = alles OK (joined && emoteOk && sevenOk && guessFail==0), sonst 1.

#include "fakehost.h"
#include "TwitchChatController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QVariantList>
#include <cstdio>

static int guessFail = 0;

static void expectChannel(const QString &input, const QString &want)
{
    const QString got = TwitchChatController::guessChannel(input);
    const bool ok = (got == want);
    if (!ok) ++guessFail;
    std::printf("GUESS %-4s in='%s' got='%s' want='%s'\n", ok ? "OK" : "FAIL",
                input.toUtf8().constData(), got.toUtf8().constData(), want.toUtf8().constData());
    std::fflush(stdout);
}

// Hat die Zeile schon ein echtes Emoticon-Bild (Segment mit Datei-URL)?
static bool segmentsHaveImage(const QVariantMap &m)
{
    const QVariantList segs = m.value(QStringLiteral("segments")).toList();
    for (const QVariant &sv : segs) {
        const QVariantMap s = sv.toMap();
        if (s.value(QStringLiteral("t")).toString() == QLatin1String("emote")
            && !s.value(QStringLiteral("src")).toString().isEmpty())
            return true;
    }
    return false;
}

static bool renderedWithImage(const TwitchChatController &c, const QString &rawNeedle)
{
    const QVariantList h = c.history();
    for (const QVariant &v : h) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("raw")).toString() == rawNeedle && segmentsHaveImage(m))
            return true;
    }
    return false;
}

// Wie oben, aber das Emote-Bild muss aus der 7tv-Ecke kommen (Schlüssel „7tv_…“):
// beweist Namen-Erkennung ohne Tag UND CDN-Download in einem Schritt.
static bool renderedWith7tvImage(const TwitchChatController &c, const QString &rawNeedle)
{
    const QVariantList h = c.history();
    for (const QVariant &v : h) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("raw")).toString() != rawNeedle) continue;
        const QVariantList segs = m.value(QStringLiteral("segments")).toList();
        for (const QVariant &sv : segs) {
            const QVariantMap s = sv.toMap();
            if (s.value(QStringLiteral("t")).toString() == QLatin1String("emote")
                && s.value(QStringLiteral("key")).toString().startsWith(QLatin1String("7tv_"))
                && !s.value(QStringLiteral("src")).toString().isEmpty())
                return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ———— Teil 1: Kanalerkennung (offline) ————
    std::printf("==== Teil 1: Kanalerkennung ====\n");
    expectChannel(QStringLiteral("https://usher.ttvnw.net/api/channel/hls/xnowski.m3u8?token=abc&sig=DEADBEEF"),
                  QStringLiteral("xnowski"));
    expectChannel(QStringLiteral("usher.jtlnw.net/api/channel/hls/SchodyMP4.m3u8"),
                  QStringLiteral("schodymp4"));
    expectChannel(QStringLiteral("https://usher.twitch.tv/v2/OldKanal/1080p?token=x"),
                  QStringLiteral("oldkanal"));
    expectChannel(QStringLiteral("https://player.twitch.tv/?channel=Testkanal&parent=hls.ts3x.cc"),
                  QStringLiteral("testkanal"));
    expectChannel(QStringLiteral("https://www.twitch.tv/embed/foo123/chat?darkpopout&parent=hls.ts3x.cc"),
                  QStringLiteral("foo123"));
    expectChannel(QStringLiteral("https://www.twitch.tv/xnowski"), QStringLiteral("xnowski"));
    expectChannel(QStringLiteral("twitch.tv/directory/games"), QString());
    expectChannel(QStringLiteral("https://clips.twitch.tv/GoodBraveHungryLizardPulse"), QString());
    expectChannel(QStringLiteral("https://mytwitch.tv/fake"), QString());

    // Lokale Playlist-Datei: Kanal aus dem Dateiinhalt …
    {
        QFile f(QStringLiteral("testsample.strm"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write("https://usher.ttvnw.net/api/channel/hls/xnowski.m3u8?token=xyz&sig=42\n");
            f.close();
        }
        expectChannel(QStringLiteral("testsample.strm"), QStringLiteral("xnowski"));
    }
    // … oder (reserviertes Wort) aus dem Dateinamen:
    {
        QFile f(QStringLiteral("videos.m3u8"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { f.write("#EXTM3U\n"); f.close(); }
        expectChannel(QStringLiteral("videos.m3u8"), QString());
        QFile g(QStringLiteral("meinstream.m3u8"));
        if (g.open(QIODevice::WriteOnly | QIODevice::Truncate)) { g.write("#EXTM3U\n#EXT-X-VERSION:3\n"); g.close(); }
        expectChannel(QStringLiteral("meinstream.m3u8"), QStringLiteral("meinstream"));
    }

    // ———— Controller (Fake-Host, Emote-Cache im aktuellen Verzeichnis) ————
    FakeHost host;
    TwitchChatController c(&host, QStringLiteral("cc.ts3x.twitch-chat"));
    c.setAutoChannel(false);

    // ———— Teil 2: Emoticon-Pipeline mit erfundenem Frame (2 IRC-Zeilen gebündelt) ————
    std::printf("\n==== Teil 2: Emoticons ====\n");
    int updates = 0;
    bool emoteOk = false;
    const QString fakeRaw = QStringLiteral("Kappa test");

    QObject::connect(&c, &TwitchChatController::messageUpdated, [&](int) {
        ++updates;
        if (renderedWithImage(c, fakeRaw)) emoteOk = true;
        if (renderedWith7tvImage(c, QStringLiteral("Nous test"))) { /* Teil-4-Signal, Poll prüft weiter */ }
        std::fflush(stdout);
    });

    const QString fakeFrame = QString::fromLatin1(
        "@badges=subscriber/6;color=#FF0000;display-name=Tester;emotes=25:0-4;mod=0;room-id=22578024;"
        "subscriber=1;turbo=0;user-id=123456789 "
        ":tester!tester@tmi.twitch.tv PRIVMSG #chan :Kappa test\r\n"
        "PING\r\n");
    QMetaObject::invokeMethod(&c, "onText", Q_ARG(QString, fakeFrame));
    const int firstSegs = c.history().isEmpty() ? 0
                        : int(c.history().first().toMap().value(QStringLiteral("segments")).toList().size());
    std::printf("Fake-Frame verarbeitet: Verlauf=%d (erwartet 1), erste Segmente=%d\n",
                int(c.history().size()), firstSegs);
    std::fflush(stdout);

    // ———— Teil 3: Live-Join ————
    const QString channel = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("eternal_pestilence");
    std::printf("\n==== Teil 3: Live-Join #%s ====\n", channel.toUtf8().constData());
    bool joined = false;
    int messageCount = 0;

    QObject::connect(&c, &TwitchChatController::statusChanged, [&]() {
        std::printf("STATUS: %s %s\n", c.statusKey().toUtf8().constData(),
                    c.statusDetail().toUtf8().constData());
        if (c.statusKey() == QLatin1String("connected")) joined = true;
        std::fflush(stdout);
    });
    QObject::connect(&c, &TwitchChatController::newMessage, [&](const QVariantMap &m) {
        ++messageCount;
        std::printf("MSG [%s] %s: %s\n", m.value(QStringLiteral("type")).toString().toUtf8().constData(),
                    m.value(QStringLiteral("name")).toString().toUtf8().constData(),
                    m.value(QStringLiteral("raw")).toString().toUtf8().constData());
        std::fflush(stdout);
    });

    c.setChannel(channel);       // verbindet automatisch (autoChannel spielt keine Rolle fürs Manuelle)

    // ———— Teil 4: 7tv ohne Tag: Katalog anonym, Name aus dem Nachrichtentext ————
    std::printf("\n==== Teil 4: 7tv-Emotes ====\n");
    bool sevenOk = false;
    const QString sevenRaw = QStringLiteral("Nous test");
    if (channel == QLatin1String("eternal_pestilence"))
        c.fetchThirdPartyChannel(channel, QStringLiteral("49455794"));   // fester Startpunkt; die echte room-id markiert denselben Kanal
    const QString sevenFrame = QString::fromLatin1(
        ":viewer9!viewer9@tmi.twitch.tv PRIVMSG #eternal_pestilence :Nous test\r\n");
    QMetaObject::invokeMethod(&c, "onText", Q_ARG(QString, sevenFrame));
    std::printf("7tv-Frame verarbeitet: Verlauf=%d (erwartet 2)\n", int(c.history().size()));
    std::fflush(stdout);

    // ———— Teil 5: Stream-Proxy (tcp:// bzw. play.php ohne Kanalnamen) → Kanal aus dem Medientitel,
    //      danach echte angeheftete Nachricht des Kanals (Standard: montanablack88)
    const QString proxyChannel = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("montanablack88");
    std::printf("\n==== Teil 5: Proxy-Adresse -> #%s ====\n", proxyChannel.toUtf8().constData());
    FakeHost proxyHost;
    TwitchChatController pc(&proxyHost, QStringLiteral("cc.ts3x.twitch-chat"));
    pc.setAutoChannel(true);
    bool proxyJoined = false, proxyPinned = false, localIgnored = false;
    // lokale Datei / normale Adresse: kein Titel-Raten
    proxyHost.setMpvProperty(QStringLiteral("media-title"), QStringLiteral("urlaubsvideo"));
    proxyHost.setCurrentMediaPath(QStringLiteral("C:/Videos/urlaubsvideo.mkv"));
    pc.onMediaPath(QStringLiteral("C:/Videos/urlaubsvideo.mkv"));
    QTimer::singleShot(1200, &app, [&] {
        localIgnored = pc.channel() != QLatin1String("urlaubsvideo");
        std::printf("Lokale Datei ignoriert: %d\n",localIgnored ? 1 : 0);
        const QString hlsPath = QStringLiteral("https://hls.example.invalid/play.php?hls=1&s=abc123");
        proxyHost.setMpvProperty(QStringLiteral("media-title"), QStringLiteral("play.php?hls=1&s=abc123"));
        proxyHost.setCurrentMediaPath(hlsPath);
        pc.onMediaPath(hlsPath);
        // Proxy setzt den Titel erst nach dem Laden
        QTimer::singleShot(600, &app, [&] {
            proxyHost.setMpvProperty(QStringLiteral("file-local-options/force-media-title"), proxyChannel);
        });
        std::fflush(stdout);
    });
    QObject::connect(&pc, &TwitchChatController::statusChanged, [&] {
        if (pc.statusKey() == QLatin1String("connected") && pc.channel() == proxyChannel && !proxyJoined) {
            proxyJoined = true;
            std::printf("Proxy: #%s beigetreten\n",proxyChannel.toUtf8().constData());
            std::fflush(stdout);
        }
    });
    QObject::connect(&pc, &TwitchChatController::pinnedChanged, [&] {
        const QVariantMap p = pc.pinnedMessage();
        if (p.value(QStringLiteral("id")).toString().isEmpty()) return;
        proxyPinned = true;
        std::printf("Proxy: angeheftet von %s: %s\n",p.value(QStringLiteral("sender")).toString().toUtf8().constData(),
                    p.value(QStringLiteral("text")).toString().toUtf8().constData());
        std::fflush(stdout);
    });

    // Polling: Emote-Bild(er) und Kataloge können jederzeit eintreffen
    QTimer poll;
    poll.start(500);
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        if (!emoteOk && renderedWithImage(c, fakeRaw)) emoteOk = true;
        // auch Live-Nachrichten mit Emote zählen als Pipeline-Beweis
        if (!emoteOk) {
            const QVariantList h = c.history();
            for (const QVariant &v : h) {
                const QVariantMap m = v.toMap();
                if (!m.value(QStringLiteral("emoteIds")).toList().isEmpty() && segmentsHaveImage(m)) {
                    emoteOk = true;
                    break;
                }
            }
        }
        if (!sevenOk && renderedWith7tvImage(c, sevenRaw)) {
            sevenOk = true;
            std::printf("7tv-Emote in Zeile eingebaut (Name ohne Tag erkannt + CDN-Bild da)\n");
            std::fflush(stdout);
        }
    });

    QTimer::singleShot(20000, &app, [&]() {
        QDir emDir(QStringLiteral("testdata_cc.ts3x.twitch-chat/emotes"));
        const int cached = emDir.entryList({QStringLiteral("*.png")}, QDir::Files).size();
        QDir sevenDir(QStringLiteral("testdata_cc.ts3x.twitch-chat/emotes/7tv"));
        const int cached7 = sevenDir.entryList({QStringLiteral("*.gif"), QStringLiteral("*.webp")}, QDir::Files).size();
        std::printf("\nERGEBNIS: joined=%d emoteOk=%d sevenOk=%d updates=%d guessFail=%d "
                    "messages=%d emoteCache-Dateien=%d 7tv-Cache-Dateien=%d\n",
                    joined ? 1 : 0, emoteOk ? 1 : 0, sevenOk ? 1 : 0, updates, guessFail,
                    messageCount, cached, cached7);
        std::fflush(stdout);
        std::printf("PROXY: localIgnored=%d joined=%d pinned=%d\n",localIgnored ? 1 : 0, proxyJoined ? 1 : 0, proxyPinned ? 1 : 0);
        std::fflush(stdout);
        const bool ok = joined && emoteOk && sevenOk && guessFail == 0 && cached > 0 && cached7 > 0
                        && localIgnored && proxyJoined && proxyPinned;
        QCoreApplication::exit(ok ? 0 : 1);
    });

    return app.exec();
}
