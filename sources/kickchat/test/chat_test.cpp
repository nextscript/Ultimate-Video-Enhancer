// Konsolentest des KickChat-Controllers:
//  1) offline: Kanalerkennung aus URLs, Segment-Aufbau mit [emote:id:name], Antworten, Ereignisse
//  2) online:  echter Kanal (Standard „jomotic“) – Kanal-Infos, Emotes, Verlauf und Live-Chat
// Aufruf: chat_test.exe [kanal] [sekunden]

#include "KickChatController.h"
#include "fakehost.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QTimer>
#include <cstdio>

static int g_fail = 0;
#define CHECK(cond, what) do { if (cond) std::printf("  ok   %s\n", what); else { std::printf("  FAIL %s\n", what); ++g_fail; } } while (0)

static QString segText(const QVariantList &segs)
{
    QString out;
    for (const QVariant &v : segs) {
        const QVariantMap s = v.toMap();
        out += s.value(QStringLiteral("t")).toString() == QLatin1String("emote")
                   ? QStringLiteral("<%1>").arg(s.value(QStringLiteral("name")).toString())
                   : s.value(QStringLiteral("s")).toString();
    }
    return out;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString channel = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("jomotic");
    const int seconds = argc > 2 ? atoi(argv[2]) : 40;

    std::printf("== offline ==\n");
    CHECK(KickChatController::guessChannel(QStringLiteral("https://kick.com/jomotic")) == QLatin1String("jomotic"), "kick.com/jomotic");
    CHECK(KickChatController::guessChannel(QStringLiteral("https://www.kick.com/Jomotic?x=1")) == QLatin1String("jomotic"), "www + Query + Großschreibung");
    CHECK(KickChatController::guessChannel(QStringLiteral("https://kick.com/popout/abc_def/chat")) == QLatin1String("abc_def"), "popout");
    CHECK(KickChatController::guessChannel(QStringLiteral("https://kick.com/video/123")).isEmpty(), "video ist kein Kanal");
    CHECK(KickChatController::guessChannel(QStringLiteral("https://twitch.tv/jomotic")).isEmpty(), "twitch ignoriert");
    CHECK(KickChatController::guessChannel(QStringLiteral("https://notkick.com/jomotic")).isEmpty(), "fremde Domain");

    FakeHost host;
    KickChatController ctrl(&host, QStringLiteral("cc.ts3x.kick-chat"), QDir::currentPath() + QStringLiteral("/testdata_kick"));

    const QVariantList segs = ctrl.buildSegments(QStringLiteral("Hallo [emote:4193302:jomoticsalut] Welt https://kick.com/x a[emote:1:b]c"));
    std::printf("  segments: %s\n", segText(segs).toUtf8().constData());
    CHECK(segText(segs) == QLatin1String("Hallo<jomoticsalut> Welt https://kick.com/x a<b> c"), "Segmente mit Emotes");
    bool link = false;
    for (const QVariant &v : segs) if (!v.toMap().value(QStringLiteral("url")).toString().isEmpty()) link = true;
    CHECK(link, "Link erkannt");

    const QByteArray ev = R"({"event":"App\\Events\\ChatMessageEvent","data":"{\"id\":\"abc\",\"chatroom_id\":1,\"content\":\"hi [emote:37226:KEKW]\",\"type\":\"reply\",\"created_at\":\"2026-11-09T20:00:00+00:00\",\"sender\":{\"id\":5,\"username\":\"Tester\",\"slug\":\"tester\",\"identity\":{\"color\":\"#FF0000\",\"badges\":[{\"type\":\"moderator\",\"text\":\"Moderator\"},{\"type\":\"subscriber\",\"text\":\"Subscriber\",\"count\":3}]}},\"metadata\":{\"original_sender\":{\"id\":6,\"username\":\"Other\"},\"original_message\":{\"id\":\"x\",\"content\":\"frage [emote:1:pog]\"}}}","channel":"chatrooms.1.v2"})";
    QVariantMap got;
    QObject::connect(&ctrl, &KickChatController::newMessage, [&](const QVariantMap &m) { got = m; });
    ctrl.handlePusherFrame(QString::fromUtf8(ev));
    CHECK(got.value(QStringLiteral("name")).toString() == QLatin1String("Tester"), "Pusher-Nachricht: Name");
    CHECK(got.value(QStringLiteral("type")).toString() == QLatin1String("reply"), "Antwort erkannt");
    CHECK(got.value(QStringLiteral("replyText")).toString() == QLatin1String("frage pog"), "Antwort-Text ohne Emote-Markup");
    CHECK(got.value(QStringLiteral("raw")).toString() == QLatin1String("hi KEKW"), "Rohtext");
    CHECK(got.value(QStringLiteral("badges")).toList().size() == 2, "zwei Abzeichen");

    got.clear();
    ctrl.handlePusherFrame(QString::fromUtf8(ev));
    CHECK(got.isEmpty(), "doppelte Nachricht verworfen");

    QString updated;
    QObject::connect(&ctrl, &KickChatController::messageUpdated, [&](const QString &id, const QVariantMap &) { updated = id; });
    ctrl.handlePusherFrame(QStringLiteral(R"({"event":"App\\Events\\MessageDeletedEvent","data":"{\"id\":\"q\",\"message\":{\"id\":\"abc\"}}"})"));
    CHECK(updated == QLatin1String("abc") && ctrl.messageById(QStringLiteral("abc")).value(QStringLiteral("deleted")).toBool(), "Löschen markiert Nachricht");

    ctrl.handlePusherFrame(QStringLiteral(R"({"event":"App\\Events\\GiftedSubscriptionsEvent","data":"{\"chatroom_id\":1,\"gifted_usernames\":[\"a\",\"b\",\"c\"],\"gifter_username\":\"Santa\"}"})"));
    CHECK(got.value(QStringLiteral("kind")).toString() == QLatin1String("gift"), "Geschenk-Abos als Systemmeldung");
    ctrl.clearHistory();

    {
        // LanguageTool online: ab Werk an, altes automatisch gespeichertes „aus“ zählt nicht,
        // selbst gewähltes „aus“ bleibt
        FakeHost h1, h2, h3;
        h2.preloadSettings(QStringLiteral("cc.ts3x.kick-chat"), QJsonObject{{QStringLiteral("grammarOnline"), false}});
        h3.preloadSettings(QStringLiteral("cc.ts3x.kick-chat"), QJsonObject{{QStringLiteral("grammarOnline"), false},
                                                                           {QStringLiteral("grammarOnlineChosen"), true}});
        KickChatController c1(&h1, QStringLiteral("cc.ts3x.kick-chat"), QDir::currentPath() + QStringLiteral("/testdata_kick"));
        KickChatController c2(&h2, QStringLiteral("cc.ts3x.kick-chat"), QDir::currentPath() + QStringLiteral("/testdata_kick"));
        KickChatController c3(&h3, QStringLiteral("cc.ts3x.kick-chat"), QDir::currentPath() + QStringLiteral("/testdata_kick"));
        CHECK(c1.grammarOnline(), "LanguageTool ab Werk an");
        CHECK(c2.grammarOnline(), "altes gespeichertes Aus aus 1.3.0 wird an");
        CHECK(!c3.grammarOnline(), "selbst gewähltes Aus bleibt aus");
        c1.setGrammarOnline(false);
        CHECK(h1.storedSettings(QStringLiteral("cc.ts3x.kick-chat")).value(QStringLiteral("grammarOnlineChosen")).toBool(),
              "Umschalten merkt sich die Wahl");
    }

    if (seconds <= 0) {
        std::printf("\n%s (%d Fehler)\n", g_fail ? "FEHLGESCHLAGEN" : "BESTANDEN", g_fail);
        return g_fail ? 1 : 0;
    }

    std::printf("\n== online: %s (%d s) ==\n", channel.toUtf8().constData(), seconds);
    int liveCount = 0, total = 0;
    bool connected = false;
    QObject::connect(&ctrl, &KickChatController::newMessage, [&](const QVariantMap &m) {
        ++total;
        const bool isLive = m.value(QStringLiteral("ts")).toDouble() > QDateTime::currentMSecsSinceEpoch() - 15000;
        if (connected && isLive) ++liveCount;
        std::printf("  [%s] %s%s: %s\n", m.value(QStringLiteral("time")).toString().toUtf8().constData(),
                    m.value(QStringLiteral("type")).toString() == QLatin1String("system") ? "* " : "",
                    m.value(QStringLiteral("name")).toString().toUtf8().constData(),
                    segText(m.value(QStringLiteral("segments")).toList()).toUtf8().constData());
    });
    QObject::connect(&ctrl, &KickChatController::statusChanged, [&]() {
        std::printf("  STATUS %s %s\n", ctrl.statusKey().toUtf8().constData(), ctrl.statusDetail().toUtf8().constData());
        if (ctrl.statusKey() == QLatin1String("connected")) connected = true;
    });
    int images = 0;
    QObject::connect(&ctrl, &KickChatController::emoteImageReady, [&]() { ++images; });

    ctrl.setChannel(channel);
    QTimer::singleShot(seconds * 1000, &app, &QCoreApplication::quit);
    app.exec();

    std::printf("\n  Chatroom %lld, Kanal %lld, User %lld, Zuschauer %d, live %d, Emotes %d, Bilder %d\n",
                ctrl.chatroomId(), ctrl.channelId(), ctrl.channelUserId(), ctrl.viewerCount(), int(ctrl.streamLive()),
                ctrl.emoteCount(), images);
    std::printf("  Titel: %s\n", ctrl.streamTitle().toUtf8().constData());
    CHECK(ctrl.chatroomId() > 0, "Chatroom-ID geholt");
    CHECK(connected, "Pusher abonniert");
    CHECK(total > 0, "Nachrichten (Verlauf/Live) empfangen");
    CHECK(ctrl.emoteCount() > 50, "Emote-Katalog geladen");
    std::printf("  Live-Nachrichten während des Tests: %d\n", liveCount);
    std::printf("\n%s (%d Fehler)\n", g_fail ? "FEHLGESCHLAGEN" : "BESTANDEN", g_fail);
    return g_fail ? 1 : 0;
}
