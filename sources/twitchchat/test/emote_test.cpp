// emote_test.cpp – BTTV/FFZ/7TV von der API bis auf den Bildschirm.
//  Teil 1 (offline): Katalog-Parser mit festen Antworten (Alias, Zero-Width, Modifier, Sets).
//  Teil 2 (live):    echte Kataloge + CDN-Bilder; Nachrichten → Segmente: Wortgrenzen,
//                    Groß-/Kleinschreibung, Vorrang, Zero-Width, Emoji vor Twitch-Emote.
//  Teil 3 (live):    echtes Panel im Software-Renderer; jedes animierte Emote muss in
//                    mehreren Bildern laufen, statische müssen geladen sein. Screenshots.
// Aufruf: emote_test.exe <Ausgabeordner für Screenshots>   (Qt-bin auf PATH)
// Exit 0 nur, wenn alle Prüfungen bestanden sind.

#include "fakehost.h"
#include "TwitchChatController.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QLibraryInfo>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>
#include <functional>

static int g_fail = 0;
static int g_pass = 0;
static QStringList g_qmlProblems;

static void check(bool ok, const QString &what)
{
    ok ? ++g_pass : ++g_fail;
    std::printf("%-5s %s\n", ok ? "OK" : "FEHLER", what.toUtf8().constData());
    std::fflush(stdout);
}

static void grabMessages(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (type == QtInfoMsg || type == QtDebugMsg) return;
    // Der Software-Renderer und die Plattform melden Unkritisches; zählen sollen QML-Probleme.
    if (msg.contains(QLatin1String(".qml")) || msg.contains(QLatin1String("QML"))
        || msg.contains(QLatin1String("AnimatedImage")) || msg.contains(QLatin1String("Image"))) {
        g_qmlProblems << msg;
        std::printf("QML-MELDUNG: %s\n", msg.toUtf8().constData());
        std::fflush(stdout);
    }
}

static void spin(int ms, const std::function<bool()> &done = {})
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms && !(done && done()))
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

// ———————————————————————————————————————————————— Teil 1: Parser
static void testParsers()
{
    std::printf("\n==== Teil 1: Katalog-Parser (offline) ====\n");

    const ThirdPartyCatalog seven = TwitchChatController::parseCatalog(Provider7tv, R"({
      "emote_set": {"emotes": [
        {"id":"AAA","name":"MeinAlias","flags":0,"data":{"name":"Original","flags":0,"animated":true}},
        {"id":"BBB","name":"ZweiteEbene","flags":1,"data":{"flags":256,"animated":false}},
        {"id":"CCC","name":"NurDatenKennung","flags":0,"data":{"flags":256}}
      ]}})", false);
    check(seven.value("MeinAlias").key == "7tv_AAA" && seven.value("MeinAlias").animated,
          "7TV: Kanal-Alias zählt, animiert erkannt");
    check(!seven.contains("Original"), "7TV: Originalname gilt nicht neben dem Alias");
    check(seven.value("ZweiteEbene").zeroWidth && seven.value("NurDatenKennung").zeroWidth,
          "7TV: Zero-Width aus beiden Kennungen");
    check(!seven.value("MeinAlias").zeroWidth, "7TV: normales Emote ist nicht Zero-Width");
    check(TwitchChatController::parseCatalog(Provider7tv,
              R"({"emotes":[{"id":"G","name":"Glob","data":{}}]})", true).contains("Glob"),
          "7TV: globales Set");

    const ThirdPartyCatalog bttvGlobal = TwitchChatController::parseCatalog(ProviderBttv, R"([
        {"id":"54fa8f1401e468494b85b537","code":":tf:","animated":false,"modifier":false},
        {"id":"6468f845aee1f7f47567709b","code":"h!","animated":false,"modifier":true},
        {"id":"5e76d399d6581c3724c0f0b8","code":"cvMask","animated":false,"modifier":false}
      ])", true);
    check(bttvGlobal.contains(":tf:"), "BTTV: Sonderzeichen im Namen");
    check(!bttvGlobal.contains("h!"), "BTTV: Modifier-Befehl ist kein Bild");
    check(bttvGlobal.value("cvMask").zeroWidth, "BTTV: bekannte Überlagerung ist Zero-Width");
    const ThirdPartyCatalog bttvChannel = TwitchChatController::parseCatalog(ProviderBttv, R"({
        "channelEmotes":[{"id":"a1","code":"Eigen","animated":true}],
        "sharedEmotes":[{"id":"b2","code":"Geteilt","animated":false}]})", false);
    check(bttvChannel.value("Eigen").animated && bttvChannel.contains("Geteilt"),
          "BTTV: eigene und geteilte Kanal-Emotes");

    const QByteArray ffzJson = R"({
        "default_sets":[3],
        "sets":{
          "3":{"emoticons":[
            {"id":9,"name":"ffzFest","animated":null,"modifier":false,"modifier_flags":0},
            {"id":10,"name":"ffzAnim","animated":{"1":"x"},"modifier":false,"modifier_flags":0},
            {"id":11,"name":"ffzSpin","modifier":true,"modifier_flags":129},
            {"id":12,"name":"ffzHut","modifier":true,"modifier_flags":0}]},
          "77":{"emoticons":[{"id":13,"name":"NurAndereGruppe","modifier":false}]}
        }})";
    const ThirdPartyCatalog ffzGlobal = TwitchChatController::parseCatalog(ProviderFfz, ffzJson, true);
    check(ffzGlobal.value("ffzFest").key == "ffz_9" && !ffzGlobal.value("ffzFest").animated,
          "FFZ: statisches Emote, Zahl-ID");
    check(ffzGlobal.value("ffzAnim").animated, "FFZ: animiert erkannt");
    check(!ffzGlobal.contains("ffzSpin"), "FFZ: versteckter Modifier ist kein Bild");
    check(ffzGlobal.value("ffzHut").zeroWidth, "FFZ: sichtbarer Modifier ist Zero-Width");
    check(!ffzGlobal.contains("NurAndereGruppe"), "FFZ global: nur freigeschaltete Sets");
    check(TwitchChatController::parseCatalog(ProviderFfz, ffzJson, false).contains("NurAndereGruppe"),
          "FFZ Kanal: alle Sets des Raums");
    check(TwitchChatController::parseCatalog(ProviderBttv, "<html>kaputt</html>", true).isEmpty(),
          "Kaputte Antwort ergibt leeren Katalog");
}

// ———————————————————————————————————————————————— Hilfen für Teil 2/3
static void sendLine(TwitchChatController &c, const QString &text, const QString &emoteTag = QString())
{
    const QString frame = QStringLiteral("@display-name=Tester;color=#1E90FF;emotes=%1 "
                                         ":tester!tester@tmi.twitch.tv PRIVMSG #sodapoppin :%2\r\n")
                              .arg(emoteTag, text);
    QMetaObject::invokeMethod(&c, "onText", Q_ARG(QString, frame));
}

static QVariantList segmentsOf(const TwitchChatController &c, const QString &raw)
{
    const QVariantList h = c.history();
    for (const QVariant &v : h) {
        const QVariantMap m = v.toMap();
        if (m.value("raw").toString() == raw) return m.value("segments").toList();
    }
    return {};
}

static QStringList emoteKeysOf(const QVariantList &segs)
{
    QStringList out;
    for (const QVariant &v : segs) {
        const QVariantMap s = v.toMap();
        if (s.value("t").toString() == QLatin1String("emote")) out << s.value("key").toString();
    }
    return out;
}

static void collectItems(QQuickItem *item, const char *className, QList<QQuickItem *> &out)
{
    if (!item) return;
    if (QLatin1String(item->metaObject()->className()).startsWith(QLatin1String(className)))
        out << item;
    const auto children = item->childItems();
    for (QQuickItem *child : children) collectItems(child, className, out);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");   // Screenshots ohne Grafiktreiber
    qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");   // offscreen findet die Systemschriften sonst nicht
    QGuiApplication app(argc, argv);
    qInstallMessageHandler(grabMessages);
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();

    testParsers();

    // ———————————————————————————————————————————— Teil 2: echte Daten
    std::printf("\n==== Teil 2: Live-Kataloge und Segmente ====\n");
    FakeHost host;
    QDir(host.pluginDataDir(QStringLiteral("cc.ts3x.twitch-chat")) + QStringLiteral("/emotes")).removeRecursively();
    TwitchChatController c(&host, QStringLiteral("cc.ts3x.twitch-chat"));
    c.setAutoChannel(false);
    c.closeChat();                              // keine echten IRC-Nachrichten – nur erfundene Zeilen
    c.setChannel(QStringLiteral("sodapoppin"));

    const auto nameKnown = [&c](const QString &name, const QString &provider) {
        const QVariantList all = c.emoteList(QStringLiteral("all"));
        for (const QVariant &v : all) {
            const QVariantMap e = v.toMap();
            if (e.value("name").toString() == name && e.value("provider").toString() == provider) return true;
        }
        return false;
    };
    c.fetchThirdPartyGlobal();
    c.fetchThirdPartyChannel(QStringLiteral("sodapoppin"), QStringLiteral("26301881"));
    spin(30000, [&] { return nameKnown("SourPls", "bttv") && nameKnown("RossPls", "bttv")
                             && nameKnown("SadWeeb", "ffz") && nameKnown("RainTime", "7tv"); });
    check(nameKnown("SourPls", "bttv"), "BTTV global geladen (SourPls)");
    check(nameKnown("RossPls", "bttv"), "BTTV Kanal geladen (RossPls)");
    check(nameKnown("SadWeeb", "ffz"), "FFZ Kanal geladen (SadWeeb)");
    check(nameKnown("RainTime", "7tv"), "7TV global geladen (RainTime)");
    check(nameKnown("LUL", "ffz") || nameKnown("ZrehplaR", "ffz") || !c.emoteList("ffz").isEmpty(),
          "Globale Listen vorhanden");

    // Abzeichen: echter Katalog und echter Download (der Cache wurde oben geleert).
    c.fetchBadges();
    spin(30000, [&] { return !c.badgeImage(QStringLiteral("moderator/1")).isEmpty()
                             && !c.badgeImage(QStringLiteral("subscriber/0")).isEmpty(); });
    const QImage modBadge(QUrl(c.badgeImage(QStringLiteral("moderator/1"))).toLocalFile());
    check(modBadge.width() == 36 && modBadge.height() == 36,
          QStringLiteral("Moderator-Abzeichen geladen (%1x%2)").arg(modBadge.width()).arg(modBadge.height()));
    check(!c.badgeImage(QStringLiteral("subscriber/0")).isEmpty(), "Abo-Abzeichen geladen");

    // Twitch-Listen kommen ohne Login: Kanal und Global – jeweils im richtigen Abschnitt.
    // HypeTrain-Emotes erscheinen nur, wenn das Konto sie hat, hier also keine.
    const auto sectionCount = [&c](const QString &section) {
        int count = 0;
        const QVariantList all = c.emoteList(QStringLiteral("twitch"));
        for (const QVariant &v : all) count += v.toMap().value("section").toString() == section ? 1 : 0;
        return count;
    };
    c.fetchEmoteCatalogs();
    spin(20000, [&] { return sectionCount("twitch-channel") > 0 && sectionCount("twitch-global") >= 100; });
    check(sectionCount("twitch-channel") > 0, QStringLiteral("Twitch Kanal ohne Login: %1").arg(sectionCount("twitch-channel")));
    check(sectionCount("twitch-global") >= 100, QStringLiteral("Twitch Global ohne Login: %1").arg(sectionCount("twitch-global")));
    check(sectionCount("twitch-hype") == 0 && !nameKnown("HypeLol", "twitch"),
          "TwitchHypeTrain: ohne Konto keine Emotes im Browser");
    check(nameKnown("Kappa", "twitch"), "Kappa ist bekannt");
    check(c.ownEmoteTag(QStringLiteral("Kappa HypeLol")) == QLatin1String("25:0-4"),
          "Eigene Nachricht: Global-Emote wird Bild, HypeTrain-Emote ohne Besitz bleibt Text");

    // Zusatz-Katalog: In diesem Kanal gibt es kein animiertes FFZ-Emote – dieses Emote
    // (öffentlich, „youFlat“) deckt den animierten FFZ-Weg ab. Der Vorrang-Fall bekommt
    // einen Namen, den FFZ und BTTV zugleich führen.
    ThirdPartyCatalog extraFfz = TwitchChatController::parseCatalog(ProviderFfz, [] {
        return QByteArray(R"({"sets":{"1":{"emoticons":[
            {"id":291125,"name":"SadWeeb","modifier":false},
            {"id":781221,"name":"youFlat","animated":{"1":"x"},"modifier":false},
            {"id":291125,"name":"RossPls","modifier":false}]}}})");
    }(), false);
    c.setThirdPartyCatalog(ProviderFfz, false, extraFfz);

    const QString lineBttvStatic  = QStringLiteral("statisch :tf: und D: Ende");
    const QString lineBttvAnim    = QStringLiteral("SourPls RossPls bttv");
    const QString lineFfz         = QStringLiteral("SadWeeb youFlat ffz");
    const QString line7tv         = QStringLiteral("JUICING UNLUCKY 7tv");
    const QString lineZero        = QStringLiteral("RossPls RainTime ueberlagert");
    const QString lineZeroAlone   = QStringLiteral("RainTime steht allein");
    const QString lineBoundaries  = QStringLiteral("xSourPls SourPlsx sourpls SourPls, h! normal");
    const QString lineEmoji       = QStringLiteral("😀 Kappa danach");

    sendLine(c, lineBttvStatic);
    sendLine(c, lineBttvAnim);
    sendLine(c, lineFfz);
    sendLine(c, line7tv);
    sendLine(c, lineZero);
    sendLine(c, lineZeroAlone);
    sendLine(c, lineBoundaries);
    sendLine(c, lineEmoji, QStringLiteral("25:2-6"));   // Twitch zählt 😀 als EIN Zeichen
    // HypeTrain- und Global-Emote aus einer fremden Nachricht: Bild muss von Twitch kommen
    const QString lineHypeGlobal = QStringLiteral("HypeLol Kappa");
    sendLine(c, lineHypeGlobal, QStringLiteral("304420818:0-6/25:8-12"));

    const auto allShown = [&] {
        return emoteKeysOf(segmentsOf(c, lineHypeGlobal)).size() == 2
            && emoteKeysOf(segmentsOf(c, lineBttvStatic)).size() == 2
            && emoteKeysOf(segmentsOf(c, lineBttvAnim)).size() == 2
            && emoteKeysOf(segmentsOf(c, lineFfz)).size() == 2
            && emoteKeysOf(segmentsOf(c, line7tv)).size() == 2
            && emoteKeysOf(segmentsOf(c, lineEmoji)).size() == 1
            && !segmentsOf(c, lineZero).isEmpty()
            && emoteKeysOf(segmentsOf(c, lineZeroAlone)).size() == 1;
    };
    spin(40000, allShown);

    check(emoteKeysOf(segmentsOf(c, lineHypeGlobal)) == QStringList({ "304420818", "25" }),
          "Chat: HypeTrain- und Global-Emote erscheinen als Bild");
    const QStringList bttvStatic = emoteKeysOf(segmentsOf(c, lineBttvStatic));
    check(bttvStatic.size() == 2 && bttvStatic[0].startsWith("bttv_"),
          "BTTV: Namen mit Satzzeichen (:tf:, D:) werden Bilder");
    const QStringList bttvAnim = emoteKeysOf(segmentsOf(c, lineBttvAnim));
    check(bttvAnim.size() == 2, "BTTV: zwei animierte Emotes in einer Zeile");
    check(bttvAnim.size() == 2 && bttvAnim[1].startsWith("bttv_"),
          "Vorrang: BTTV schlägt FFZ bei gleichem Namen (RossPls)");
    const QStringList ffz = emoteKeysOf(segmentsOf(c, lineFfz));
    check(ffz == QStringList({ "ffz_291125", "ffz_781221" }), "FFZ: statisch und animiert");
    const QStringList seven = emoteKeysOf(segmentsOf(c, line7tv));
    check(seven.size() == 2 && seven[0].startsWith("7tv_"), "7TV: Kanal-Aliasse (JUICING, UNLUCKY)");

    const QVariantList zero = segmentsOf(c, lineZero);
    const QVariantMap zeroBase = zero.isEmpty() ? QVariantMap() : zero.first().toMap();
    check(zeroBase.value("overlays").toList().size() == 1
              && zeroBase.value("overlays").toList().first().toMap().value("name") == "RainTime",
          "Zero-Width: RainTime liegt über RossPls statt daneben");
    check(zero.size() == 2, "Zero-Width: nimmt keinen eigenen Platz in der Zeile ein");
    check(emoteKeysOf(segmentsOf(c, lineZeroAlone)).size() == 1,
          "Zero-Width ohne Emote davor: steht für sich");

    check(emoteKeysOf(segmentsOf(c, lineBoundaries)).isEmpty(),
          "Wortgrenzen: xSourPls, SourPlsx, sourpls, „SourPls,“ und h! bleiben Text");
    const QVariantList emoji = segmentsOf(c, lineEmoji);
    const QStringList emojiKeys = emoteKeysOf(emoji);
    check(emojiKeys == QStringList({ "25" }) && emoji.size() == 3
              && emoji[1].toMap().value("name") == "Kappa",
          "Emoji vor Twitch-Emote: Bereich trifft genau „Kappa“");

    // ———————————————————————————————————————————— Teil 3: echtes Panel
    std::printf("\n==== Teil 3: Darstellung im Panel ====\n");
    g_qmlProblems.clear();
    QQuickView view;
    view.engine()->addImportPath(QLibraryInfo::path(QLibraryInfo::QmlImportsPath));
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(460, 620);
    view.setSource(QUrl::fromLocalFile(QStringLiteral("M:/LAPS/twitchchat/src/ChatPanel.qml")));
    check(view.status() == QQuickView::Ready, "Panel geladen");
    QQuickItem *rootItem = view.rootObject();
    if (!rootItem) return 1;
    view.setColor(QColor(QStringLiteral("#18181b")));
    rootItem->setProperty("controller", QVariant::fromValue<QObject *>(&c));
    view.show();
    spin(3000);

    QList<QQuickItem *> animated;
    collectItems(rootItem, "QQuickAnimatedImage", animated);
    QList<QQuickItem *> images;
    collectItems(rootItem, "QQuickImage", images);
    check(!animated.isEmpty(), QStringLiteral("Animierte Emotes im Panel: %1").arg(animated.size()));

    const auto pathKind = [](const QString &src) {
        for (const char *p : { "/bttv/", "/ffz/", "/7tv/" })
            if (src.contains(QLatin1String(p))) return QString::fromLatin1(p).remove('/');
        return QStringLiteral("twitch");
    };

    // Bildwechsel über drei Sekunden messen – nur laufende Animationen ändern currentFrame
    QHash<QQuickItem *, int> changes;
    QHash<QQuickItem *, int> lastFrame;
    for (QQuickItem *a : animated) lastFrame.insert(a, a->property("currentFrame").toInt());
    const QImage frameA = view.grabWindow();
    frameA.save(outDir + QStringLiteral("/chat_bild1.png"));
    QElapsedTimer clock;
    clock.start();
    QImage frameB;
    while (clock.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        for (QQuickItem *a : animated) {
            const int f = a->property("currentFrame").toInt();
            if (f != lastFrame.value(a)) { changes[a]++; lastFrame[a] = f; }
        }
        if (frameB.isNull() && clock.elapsed() > 400) frameB = view.grabWindow();
    }
    frameB.save(outDir + QStringLiteral("/chat_bild2.png"));
    check(frameA != frameB, "Screenshots unterscheiden sich: Bewegung kommt wirklich auf dem Bildschirm an");

    QHash<QString, int> runningByProvider;
    QHash<QString, double> soloRate;                     // Bildwechsel/s je Emote mit wenig Last
    for (QQuickItem *a : animated) {
        const QString src = a->property("source").toUrl().toString();
        soloRate[src] = qMax(soloRate.value(src), changes.value(a) / 3.0);
        const int frames = a->property("frameCount").toInt();
        const int perSecond = changes.value(a) / 3;
        std::printf("  %-7s Bilder gesamt=%-4d Wechsel/s=%-3d %dx%d  %s\n",
                    pathKind(src).toUtf8().constData(), frames, perSecond,
                    int(a->width()), int(a->height()), src.section('/', -1).toUtf8().constData());
        if (frames > 1 && changes.value(a) >= 3) runningByProvider[pathKind(src)]++;
        check(a->property("status").toInt() == 1 && frames > 1 && changes.value(a) >= 3,
              QStringLiteral("läuft: %1").arg(src.section('/', -2)));
    }
    std::fflush(stdout);
    check(runningByProvider.value("bttv") >= 1, "Animiert laufend: BTTV");
    check(runningByProvider.value("ffz") >= 1, "Animiert laufend: FFZ");
    check(runningByProvider.value("7tv") >= 1, "Animiert laufend: 7TV");

    int loadedStatic = 0, brokenStatic = 0;
    for (QQuickItem *i : images) {
        const QString src = i->property("source").toUrl().toString();
        if (src.isEmpty() || !src.contains(QLatin1String("/emotes/"))) continue;
        i->property("status").toInt() == 1 ? ++loadedStatic : ++brokenStatic;
    }
    check(loadedStatic >= 3 && brokenStatic == 0,
          QStringLiteral("Statische Emotes geladen: %1, fehlerhaft: %2").arg(loadedStatic).arg(brokenStatic));
    check(g_qmlProblems.isEmpty(), QStringLiteral("Keine QML-Meldungen im Chat (%1)").arg(g_qmlProblems.size()));

    // Browser: Bilder kommen hier direkt aus dem Netz
    g_qmlProblems.clear();
    QObject *native = rootItem->findChild<QObject *>(QString(), Qt::FindDirectChildrenOnly);
    Q_UNUSED(native)
    QList<QQuickItem *> stack{ rootItem };
    QQuickItem *panel = nullptr;
    while (!stack.isEmpty() && !panel) {
        QQuickItem *it = stack.takeFirst();
        if (it->property("emotesOpen").isValid()) panel = it;
        stack += it->childItems();
    }
    check(panel != nullptr, "Panel-Inhalt gefunden");
    if (panel) {
        panel->setProperty("browserKind", QStringLiteral("7tv"));
        panel->setProperty("emotesOpen", true);
        QList<QQuickItem *> tiles;
        int ready = 0, loading = 0, failed = 0, netAnimated = 0;
        const auto countTiles = [&] {
            tiles.clear();
            collectItems(panel, "QQuickAnimatedImage", tiles);
            collectItems(panel, "QQuickImage", tiles);
            ready = loading = failed = netAnimated = 0;
            for (QQuickItem *t : tiles) {
                if (!t->property("source").toUrl().scheme().startsWith(QLatin1String("http"))) continue;
                const int st = t->property("status").toInt();   // 1 fertig, 2 lädt, 3 Fehler
                if (st == 1) ++ready;
                else if (st == 3) ++failed;
                else ++loading;
                if (st == 1 && t->property("frameCount").toInt() > 1) ++netAnimated;
            }
            return ready > 0 && loading == 0;
        };
        spin(30000, countTiles);
        countTiles();
        for (QQuickItem *t : tiles) {
            const int st = t->property("status").toInt();
            if (st != 1 && t->property("source").toUrl().scheme().startsWith(QLatin1String("http")))
                std::printf("  Kachel nicht fertig (Status %d, Fortschritt %.2f, %s, y=%d): %s\n", st,
                            t->property("progress").toDouble(), t->metaObject()->className(),
                            int(t->mapToScene(QPointF(0, 0)).y()),
                            t->property("source").toUrl().toString().toUtf8().constData());
        }
        std::printf("  Kacheln: fertig=%d laedt=%d Fehler=%d davon animiert=%d\n", ready, loading, failed, netAnimated);
        check(netAnimated > 0, QStringLiteral("Browser: animierte Kacheln aus dem Netz laufen: %1").arg(netAnimated));
        // Noch ladende Kacheln hängen nur vom Tempo des CDN ab; als Fehler zählt nur ein Fehler.
        check(ready > 0 && failed == 0, QStringLiteral("Browser: keine Kachel mit Ladefehler"));
        spin(500);
        QQuickItem *chatList = panel->findChild<QQuickItem *>(QStringLiteral("chatView"));
        check(chatList && chatList->height() >= 100,
              QStringLiteral("Chat bleibt bei offenem Browser sichtbar (Höhe %1)").arg(chatList ? int(chatList->height()) : -1));
        view.grabWindow().save(outDir + QStringLiteral("/browser.png"));

        // Kanal-Emotes ohne Abo sind gesperrt, mit Abo frei; die Kachel zeigt beides
        const auto channelLocks = [&c](int &locked, int &open, QString &firstId) {
            locked = open = 0;
            const QVariantList list = c.emoteList(QStringLiteral("twitch"));
            for (const QVariant &v : list) {
                const QVariantMap e = v.toMap();
                if (e.value("section").toString() != QLatin1String("twitch-channel")) continue;
                if (firstId.isEmpty()) firstId = e.value("id").toString();
                e.value("locked").toBool() ? ++locked : ++open;
            }
        };
        int locked = 0, open = 0;
        QString firstId;
        channelLocks(locked, open, firstId);
        check(locked > 0 && open == 0, QStringLiteral("Kanal-Emotes ohne Abo gesperrt: %1").arg(locked));
        const int lockedBefore = locked;
        c.applySubscriptionEmotes(QStringLiteral(R"({"data":[{"id":"%1","name":"ProbeAbo","emote_type":"subscriptions","format":["static"]}]})")
                                      .arg(firstId).toUtf8());
        channelLocks(locked, open, firstId);
        check(open == 1 && locked == lockedBefore - 1, "Kanal-Emote mit Abo wird frei");
        panel->setProperty("browserKind", QStringLiteral("twitch"));
        spin(1500);
        int dimmed = 0, bright = 0;
        QList<QQuickItem *> walk{ panel };                 // Kacheln hängen nur im Elementbaum
        while (!walk.isEmpty()) {
            QQuickItem *t = walk.takeLast();
            walk += t->childItems();
            if (t->objectName() != QLatin1String("emoteTileImage") || !t->isVisible()) continue;
            t->opacity() < 0.5 ? ++dimmed : ++bright;
        }
        check(dimmed > 0, QStringLiteral("Browser: gesperrte Kacheln sind abgeblendet (%1 von %2)").arg(dimmed).arg(dimmed + bright));
        c.setEmoteScopeMissing(true);
        spin(200);
        QQuickItem *scopeHint = panel->findChild<QQuickItem *>(QStringLiteral("emoteScopeHint"));
        check(scopeHint && scopeHint->isVisible(), "Browser: Hinweis auf fehlendes Token-Recht sichtbar");
        view.grabWindow().save(outDir + QStringLiteral("/browser_twitch.png"));
        c.setEmoteScopeMissing(false);
        check(g_qmlProblems.isEmpty(), QStringLiteral("Keine QML-Meldungen im Browser (%1)").arg(g_qmlProblems.size()));
    }

    // ———————————————————————————————————————————— Teil 4: voller Chat
    // Abspieltempo unter Last: viele Zeilen mit je mehreren animierten Emotes. Mit Software-
    // Renderer ist das strenger als im Player, der die Grafikkarte benutzt.
    std::printf("\n==== Teil 4: Abspieltempo bei vollem Chat ====\n");
    if (panel) panel->setProperty("emotesOpen", false);
    for (int i = 0; i < 120; ++i)
        sendLine(c, QStringLiteral("SourPls RossPls youFlat RainTime Zeile%1").arg(i));
    spin(2500);
    QList<QQuickItem *> busy;
    collectItems(rootItem, "QQuickAnimatedImage", busy);
    QHash<QQuickItem *, int> busyChanges, busyLast;
    for (QQuickItem *a : busy) busyLast.insert(a, a->property("currentFrame").toInt());
    clock.restart();
    while (clock.elapsed() < 4000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        for (QQuickItem *a : busy) {
            const int f = a->property("currentFrame").toInt();
            if (f != busyLast.value(a)) { busyChanges[a]++; busyLast[a] = f; }
        }
    }
    QHash<QString, QPair<double, int>> busyRate;         // Summe, Anzahl
    for (QQuickItem *a : busy) {
        auto &r = busyRate[a->property("source").toUrl().toString()];
        r.first += busyChanges.value(a) / 4.0;
        r.second += 1;
    }
    std::printf("  gleichzeitig laufende Animationen: %d\n", int(busy.size()));
    check(busy.size() >= 20, QStringLiteral("Lasttest hat genug Animationen (%1)").arg(busy.size()));
    for (auto it = busyRate.constBegin(); it != busyRate.constEnd(); ++it) {
        const double rate = it->first / qMax(1, it->second);
        const double solo = soloRate.value(it.key());
        if (solo <= 0) continue;
        check(rate >= solo * 0.7, QStringLiteral("%1: %2 Bilder/s unter Last, %3 ohne Last (%4x)")
                  .arg(it.key().section('/', -2)).arg(rate, 0, 'f', 1).arg(solo, 0, 'f', 1).arg(it->second));
    }
    view.grabWindow().save(outDir + QStringLiteral("/last.png"));

    std::printf("\nERGEBNIS: bestanden=%d fehlgeschlagen=%d\n", g_pass, g_fail);
    std::fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
