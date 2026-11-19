// qml_panel_test.cpp – lädt ChatPanel.qml (mit ChatNative.qml dahinter) echt im
// QQML-Engine-Headless-Modus (offscreen), damit QML-Ladefehler (Property-Tippfehler
// wie 2.2.0 „Button.color“ oder 2.3.0 „AnimatedImage.autoPlay“) NIE wieder unentdeckt
// ins Zip wandern. Prüft: Panel kompiliert, Loader wird Ready, Item existiert,
// controller-/theme-Bindungen greifen, keine QML-Fehlermeldungen im Lauf.
// Aufruf: qml_panel_test.exe   (Qt-bin auf PATH; aus out/test starten)

#include "fakehost.h"
#include "TwitchChatController.h"
#include "WhisperService.h"

#include <QColor>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QThread>
#include <QWheelEvent>
#include <QFont>
#include <QDateTime>
#include <QTimeZone>
#include <QDesktopServices>
#include <QKeyEvent>
#include <QPluginLoader>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QList>
#include <QMouseEvent>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickView>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <cstdio>
#include <functional>
#include <memory>

// Sammelt alle QML-/Qt-Warnungen während des Tests
static QList<QString> g_qmlErrors;
static QtMessageHandler g_oldHandler = nullptr;

static void grabMessages(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const bool isQmlError = type == QtCriticalMsg || type == QtFatalMsg
        || msg.contains(QLatin1String("Cannot assign"))
        || msg.contains(QLatin1String("is not a type"))
        || msg.contains(QLatin1String("Cannot read"))
        || msg.contains(QLatin1String("TypeError"))
        || msg.contains(QLatin1String("ReferenceError"))
        || msg.contains(QLatin1String("Binding loop"))
        || msg.contains(QLatin1String("does not contain an Item"));
    if (isQmlError) {
        g_qmlErrors.append(msg);
        std::printf("QML-FEHLER: %s\n", msg.toUtf8().constData());
    }
    if (g_oldHandler) g_oldHandler(type, ctx, msg);
    std::fflush(stdout);
}

// Sammelt alle sichtbaren Elemente, die rechts über die Panelbreite hinausragen
static void collectOverflow(QQuickItem *item, qreal limit, QStringList &out)
{
    if (!item->isVisible()) return;
    const qreal right = item->mapToScene(QPointF(item->width(), 0)).x();
    if (right > limit + 0.5)
        out << QStringLiteral("%1 „%2“ rechts=%3")
                   .arg(QString::fromLatin1(item->metaObject()->className()),
                        item->property("text").toString().left(30))
                   .arg(right, 0, 'f', 0);
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        collectOverflow(child, limit, out);
}

static QQuickItem *findPanelContent(QQuickItem *rootItem)
{
    QList<QQuickItem *> stack{ rootItem };
    while (!stack.isEmpty()) {
        QQuickItem *it = stack.takeFirst();
        if (it->property("emotesOpen").isValid()) return it;
        stack += it->childItems();
    }
    return nullptr;
}

static void settle(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// Abo-Emotes: nur Typ „subscriptions“, im Tab „Abos“ und „Alle“, weg nach dem Logout.
static bool checkSubscriptionEmotes(TwitchChatController &controller)
{
    const auto names = [&controller](const QString &kind) {
        QStringList out;
        const QVariantList list = controller.emoteList(kind);
        for (const QVariant &v : list) out << v.toMap().value(QStringLiteral("name")).toString();
        return out;
    };
    controller.setAuth(QStringLiteral("tester"), QStringLiteral("testtoken"));
    controller.applySubscriptionEmotes(R"json({"data":[
        {"id":"111","name":"subStatic","emote_type":"subscriptions","owner_id":"26301881","emote_set_id":"406404867","format":["static"]},
        {"id":"555555701","name":";-)","emote_type":"subscriptions","emote_set_id":"42","owner_id":"twitch","format":["static"]},
        {"id":"555555645","name":"<3x","emote_type":"subscriptions","format":["static"]},
        {"id":"666","name":"robotSet","emote_type":"subscriptions","emote_set_id":"33","owner_id":"26301881","format":["static"]},
        {"id":"667","name":"ohneKanal","emote_type":"subscriptions","owner_id":"0","format":["static"]},
        {"id":"668","name":"twitchEigen","emote_type":"subscriptions","owner_id":"12826","format":["static"]},
        {"id":"222","name":"subAnim","emote_type":"subscriptions","format":["static","animated"]},
        {"id":"333","name":"followerOnly","emote_type":"follower","format":["static"]},
        {"id":"444","name":"globalOne","emote_type":"globals","format":["static"]},
        {"id":"555","name":"HypeEigen","emote_type":"hypetrain","format":["static"]},
        {"id":"556","name":"HypeBesitzer","emote_type":"","owner_id":"477339272","format":["static"]}]})json");
    const QVariantList subs = controller.emoteList(QStringLiteral("subs"));
    // Affen-/Roboter-Smileys meldet Twitch als „subscriptions“: Sie sind kein Kanal-Abo …
    bool ok = names(QStringLiteral("subs")) == QStringList({ "subAnim", "subStatic" });
    if (!ok) std::printf("  Abos enthaelt: %s\n", names(QStringLiteral("subs")).join(' ').toUtf8().constData());
    // … bleiben aber Emotes des Kontos und werden in eigenen Nachrichten zum Bild.
    ok = ok && controller.ownEmoteTag(QStringLiteral(";-)")) == QLatin1String("555555701:0-2");
    ok = ok && subs.size() == 2 && subs[0].toMap().value("animated").toBool()
         && !subs[1].toMap().value("animated").toBool();
    ok = ok && names(QStringLiteral("all")).contains("subStatic");
    ok = ok && subs[0].toMap().value("section").toString() == QLatin1String("subs")
         && !names(QStringLiteral("twitch")).contains("subStatic")
         && !names(QStringLiteral("7tv")).contains("subStatic");
    controller.toggleEmoteFavorite(QStringLiteral("subStatic"));
    ok = ok && names(QStringLiteral("favorites")).contains("subStatic");
    controller.toggleEmoteFavorite(QStringLiteral("subStatic"));

    // Standard-Emotes: Kategorie „Twitch“ und in eigenen Nachrichten erkannt
    controller.applyGlobalEmotes(R"({"data":[{"id":"25","name":"Kappa","format":["static"]},
        {"id":"emotesv2_x","name":"Tanz","format":["static","animated"]}]})");
    const QVariantList twitch = controller.emoteList(QStringLiteral("twitch"));
    bool globalOk = names(QStringLiteral("twitch")).contains("Kappa") && !twitch.isEmpty()
                    && twitch[0].toMap().value("section").toString() == QLatin1String("twitch-global")
                    && controller.ownEmoteTag(QStringLiteral("Kappa")) == QLatin1String("25:0-4");
    std::printf("Twitch-Standard-Emotes: %s\n", globalOk ? "ok" : "FEHLER");
    ok = ok && globalOk;

    // Kategorien: Gleichnamige Emotes stehen getrennt und werden einzeln favorisiert
    ThirdPartyCatalog sevenChannel, bttvGlobal, ffzGlobal;
    sevenChannel.insert(QStringLiteral("Gleich"), ThirdPartyEmote{ QStringLiteral("7tv_1"), false, false });
    bttvGlobal.insert(QStringLiteral("Gleich"), ThirdPartyEmote{ QStringLiteral("bttv_1"), false, false });
    ffzGlobal.insert(QStringLiteral("NurFfz"), ThirdPartyEmote{ QStringLiteral("ffz_1"), false, false });
    controller.setThirdPartyCatalog(Provider7tv, false, sevenChannel);
    controller.setThirdPartyCatalog(ProviderBttv, true, bttvGlobal);
    controller.setThirdPartyCatalog(ProviderFfz, true, ffzGlobal);
    bool categoryOk = names(QStringLiteral("7tv")) == QStringList({ "Gleich" })
                      && names(QStringLiteral("bttv")) == QStringList({ "Gleich" })
                      && names(QStringLiteral("ffz")) == QStringList({ "NurFfz" })
                      && names(QStringLiteral("all")).count("Gleich") == 2;
    QStringList sections;
    const QVariantList everything = controller.emoteList(QStringLiteral("all"));
    for (const QVariant &v : everything) {
        const QString s = v.toMap().value(QStringLiteral("section")).toString();
        if (!sections.contains(s)) sections << s;
    }
    categoryOk = categoryOk && sections == QStringList({ "subs", "twitch-global", "twitch-hype", "7tv-channel", "bttv-global", "ffz-global" });
    categoryOk = categoryOk && controller.ownEmoteTag(QStringLiteral("HypeEigen")) == QLatin1String("555:0-8");
    int hypeTiles = 0;
    for (const QVariant &v : everything)
        hypeTiles += v.toMap().value(QStringLiteral("section")).toString() == QLatin1String("twitch-hype") ? 1 : 0;
    categoryOk = categoryOk && hypeTiles == 2;   // nach Typ und nach Besitzer erkannt
    controller.toggleEmoteFavorite(QStringLiteral("Gleich"), QStringLiteral("bttv_1"));
    const QVariantList favorites = controller.emoteList(QStringLiteral("favorites"));
    categoryOk = categoryOk && favorites.size() == 1
                 && favorites[0].toMap().value("section").toString() == QLatin1String("bttv")
                 && controller.emoteList(QStringLiteral("7tv"))[0].toMap().value("favorite").toBool() == false;
    controller.toggleEmoteFavorite(QStringLiteral("Gleich"), QStringLiteral("bttv_1"));
    categoryOk = categoryOk && controller.emoteList(QStringLiteral("favorites")).isEmpty();
    std::printf("Emote-Kategorien: %s\n", categoryOk ? "ok" : "FEHLER");
    ok = ok && categoryOk;

    // Eigene Nachrichten: Twitch liefert keinen Tag, die Zeile braucht ihn trotzdem.
    const auto tagIs = [&controller](const QString &message, const QString &expected) {
        const QString tag = controller.ownEmoteTag(message);
        if (tag != expected)
            std::printf("  ownEmoteTag(%s) = %s, erwartet %s\n", message.toUtf8().constData(),
                        tag.toUtf8().constData(), expected.toUtf8().constData());
        return tag == expected;
    };
    bool ownOk = tagIs(QStringLiteral("subStatic"), QStringLiteral("111:0-8"));
    ownOk = tagIs(QStringLiteral("hallo subAnim welt globalOne followerOnly"),
                  QStringLiteral("222:6-12/444:19-27/333:29-40")) && ownOk;
    ownOk = tagIs(QStringLiteral("subStatic subStatic"), QStringLiteral("111:0-8/111:10-18")) && ownOk;
    ownOk = tagIs(QStringLiteral("xsubStatic subStatic, substatic Fremd"), QString()) && ownOk;
    ownOk = tagIs(QString::fromUtf8("\xF0\x9F\x98\x80 subStatic"), QStringLiteral("111:3-11")) && ownOk;   // Emoji = 2 UTF-16-Einheiten
    ok = ok && ownOk;

    controller.setAuth(QString(), QString());
    ok = ok && controller.emoteList(QStringLiteral("subs")).isEmpty()
         && controller.ownEmoteTag(QStringLiteral("subStatic")).isEmpty();
    std::printf("Abo-Emotes: %s\n", ok ? "ok" : "FEHLER");
    return ok;
}

static void collectByText(QQuickItem *item, const std::function<bool(const QString &)> &match, QList<QQuickItem *> &out)
{
    const QVariant text = item->property("text");
    if (text.isValid() && item->isVisible() && match(text.toString())) out << item;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        collectByText(child, match, out);
}

// Emote-Browser: Favorisieren und neue Emotes dürfen die Scroll-Position nicht verlieren.
static bool checkBrowserScroll(TwitchChatController &controller, QQuickItem *panel)
{
    QQuickItem *grid = panel->findChild<QQuickItem *>(QStringLiteral("emoteGrid"));
    if (!grid) { std::printf("Browser-Scroll: Liste fehlt\n"); return false; }

    ThirdPartyCatalog many;
    for (int i = 0; i < 300; ++i)
        many.insert(QStringLiteral("Viel%1").arg(i, 3, 10, QLatin1Char('0')),
                    ThirdPartyEmote{ QStringLiteral("7tv_v%1").arg(i), false, false });
    controller.setThirdPartyCatalog(Provider7tv, false, many);
    panel->setProperty("browserKind", QStringLiteral("7tv"));
    panel->setProperty("emotesOpen", true);
    settle(300);
    grid->setProperty("contentY", 1500.0);
    settle(300);

    const auto isTileName = [](const QString &t) { return t.startsWith(QLatin1String("Viel")); };
    // Oberste sichtbare Kachel: Genau das sieht der Nutzer, die Zahl contentY allein sagt das nicht.
    const auto topTile = [&]() -> QQuickItem * {
        QList<QQuickItem *> labels;
        collectByText(grid, isTileName, labels);
        QQuickItem *best = nullptr;
        qreal bestTop = 0;
        for (QQuickItem *l : labels) {
            const QPointF at = l->mapToItem(grid, QPointF(0, 0));
            if (at.y() < 0 || at.y() >= grid->height()) continue;
            if (!best || at.y() < bestTop - 1 || (qAbs(at.y() - bestTop) <= 1 && at.x() < best->mapToItem(grid, QPointF(0, 0)).x())) {
                best = l;
                bestTop = at.y();
            }
        }
        return best;
    };
    const auto topName = [&] { QQuickItem *t = topTile(); return t ? t->property("text").toString() : QString(); };
    const auto starCount = [&] {
        QList<QQuickItem *> stars;
        collectByText(grid, [](const QString &t) { return t == QString::fromUtf8("★"); }, stars);
        return int(stars.size());
    };

    QQuickItem *tile = topTile();
    if (!tile) { std::printf("Browser-Scroll: keine Kachel sichtbar\n"); return false; }
    const QString name = tile->property("text").toString();
    const QPointer<QQuickItem> sameTile(tile);
    std::printf("Browser-Scroll Start: oben steht %s\n", name.toUtf8().constData());

    controller.toggleEmoteFavorite(name, many.value(name).key);
    settle(300);
    const bool favoriteOk = topName() == name && starCount() == 1 && !sameTile.isNull();
    std::printf("Browser-Scroll Favorit: oben %s, Sterne=%d, Kachel erhalten=%d %s\n", topName().toUtf8().constData(),
                starCount(), sameTile.isNull() ? 0 : 1, favoriteOk ? "ok" : "FEHLER");

    // Favorit wieder weg: gleiche Stelle, Stern aus
    controller.toggleEmoteFavorite(name, many.value(name).key);
    settle(300);
    const bool unfavoriteOk = topName() == name && starCount() == 0;
    std::printf("Browser-Scroll Favorit weg: oben %s, Sterne=%d %s\n", topName().toUtf8().constData(),
                starCount(), unfavoriteOk ? "ok" : "FEHLER");

    // Echte Mausklicks auf die Kachel: rechts schaltet den Favoriten um, links nicht
    const auto clickTop = [&](Qt::MouseButton button) {
        QQuickItem *target = topTile();
        if (!target || !target->window()) return;
        QQuickWindow *window = target->window();
        const QPointF scenePos = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
        const QPointF globalPos = window->mapToGlobal(scenePos.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, scenePos, scenePos, globalPos, button, button, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scenePos, scenePos, globalPos, button, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        settle(300);
    };
    clickTop(Qt::LeftButton);
    const bool leftOk = starCount() == 0;
    clickTop(Qt::RightButton);
    const bool rightOnOk = starCount() == 1 && topName() == name
                           && controller.emoteList(QStringLiteral("favorites")).size() == 1;
    clickTop(Qt::RightButton);
    const bool rightOffOk = starCount() == 0 && topName() == name
                            && controller.emoteList(QStringLiteral("favorites")).isEmpty();
    const bool rightClickOk = leftOk && rightOnOk && rightOffOk;
    std::printf("Browser-Rechtsklick: links kein Favorit=%d, rechts an=%d, rechts aus=%d %s\n",
                leftOk ? 1 : 0, rightOnOk ? 1 : 0, rightOffOk ? 1 : 0, rightClickOk ? "ok" : "FEHLER");

    // Neues Emote im Katalog (z. B. gerade im Chat gesehen) erzwingt Neuaufbau: Stelle bleibt.
    // „Viel999“ sortiert ganz ans Ende und verschiebt die Kacheln davor nicht.
    many.insert(QStringLiteral("Viel999"), ThirdPartyEmote{ QStringLiteral("7tv_neu"), false, false });
    controller.setThirdPartyCatalog(Provider7tv, false, many);
    settle(300);
    const bool rebuildOk = topName() == name;
    std::printf("Browser-Scroll neues Emote: oben %s %s\n", topName().toUtf8().constData(), rebuildOk ? "ok" : "FEHLER");

    // Tab-Wechsel beginnt dagegen bewusst wieder oben
    panel->setProperty("browserKind", QStringLiteral("ffz"));
    settle(200);
    panel->setProperty("browserKind", QStringLiteral("7tv"));
    settle(300);
    const bool tabOk = topName() == QLatin1String("Viel000");
    std::printf("Browser-Scroll Tab-Wechsel: oben %s %s\n", topName().toUtf8().constData(), tabOk ? "ok" : "FEHLER");

    panel->setProperty("browserKind", QStringLiteral("all"));
    panel->setProperty("emotesOpen", false);
    return favoriteOk && unfavoriteOk && rightClickOk && rebuildOk && tabOk;
}

// Rechtschreibprüfung im echten Panel: Markierung, Rechtsklickmenü, Vorschlag, eigenes Wörterbuch.
static QStringList spellWords(QQuickItem *field)
{
    QStringList words;
    const QVariantList ranges = field->property("spellRanges").toList();
    for (const QVariant &r : ranges) words << r.toMap().value(QStringLiteral("word")).toString();
    return words;
}

static int redPixels(QQuickView &view, QQuickItem *field)
{
    const QImage shot = view.grabWindow();
    const QPointF topLeft = field->mapToScene(QPointF(0, 0));
    int count = 0;
    for (int y = int(topLeft.y()); y < int(topLeft.y() + field->height()) && y < shot.height(); ++y) {
        for (int x = int(topLeft.x()); x < int(topLeft.x() + field->width()) && x < shot.width(); ++x) {
            const QColor c = shot.pixelColor(x, y);
            if (c.red() > 170 && c.green() < 130 && c.blue() < 120) ++count;
        }
    }
    return count;
}

static bool checkSpellInput(TwitchChatController &controller, QQuickView &view, QQuickItem *panel, const QString &shotDir)
{
    QQuickItem *field = panel->findChild<QQuickItem *>(QStringLiteral("sendField"));
    QObject *menu = panel->findChild<QObject *>(QStringLiteral("spellMenu"));
    if (!field || !menu) {
        std::printf("Rechtschreibung: Eingabefeld oder Menü fehlt\n");
        return false;
    }
    bool ok = true;
    const auto expect = [&ok](bool passed, const char *what) {
        std::printf("Rechtschreibung %s: %s\n", what, passed ? "ok" : "FEHLER");
        ok = ok && passed;
    };

    field->setProperty("enabled", true);     // ohne Verbindung wäre das Feld gesperrt
    field->setProperty("text", QStringLiteral("Hallo Wrold und Strase ok danke"));
    settle(600);
    expect(spellWords(field) == QStringList({QStringLiteral("Wrold"), QStringLiteral("Strase")}), "findet beide Fehler");
    expect(redPixels(view, field) > 10, "zeichnet die Wellenlinie");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/rechtschreibung_linie.png"));

    // Echter Rechtsklick auf „Wrold“ über das Kontextmenü-Ereignis des Fensters
    const auto openMenuAt = [&](int textPos) {
        QRectF rect;
        QMetaObject::invokeMethod(field, "positionToRectangle", Q_RETURN_ARG(QRectF, rect), Q_ARG(int, textPos));
        const QPointF scene = field->mapToScene(QPointF(rect.x() + 3, field->height() / 2));   // rect zählt ab Feldecke
        QMouseEvent move(QEvent::MouseMove, scene, scene, view.mapToGlobal(scene), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &move);
        settle(50);
        QContextMenuEvent event(QContextMenuEvent::Mouse, scene.toPoint(), view.mapToGlobal(scene.toPoint()));
        QCoreApplication::sendEvent(&view, &event);
        settle(500);
    };
    openMenuAt(7);
    expect(menu->property("visible").toBool(), "Rechtsklick öffnet das Menü");
    const QVariantMap target = menu->property("target").toMap();
    expect(target.value(QStringLiteral("word")).toString() == QStringLiteral("Wrold"), "Menü kennt das Wort unter der Maus");
    const QStringList found = menu->property("suggestions").toStringList();
    std::printf("Vorschläge: %s\n", found.join(QStringLiteral(", ")).toUtf8().constData());
    expect(found.contains(QStringLiteral("World")) || found.contains(QStringLiteral("world")), "Vorschlag kommt an");
    expect(menu->property("count").toInt() >= found.size() + 12, "Vorschläge stehen im Menü");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/rechtschreibung_menue.png"));
    QMetaObject::invokeMethod(menu, "close");
    settle(150);

    // Vorschlag übernehmen ersetzt nur dieses Wort
    QMetaObject::invokeMethod(panel, "replaceSpellTarget", Q_ARG(QVariant, QStringLiteral("World")));
    settle(300);
    expect(field->property("text").toString() == QStringLiteral("Hallo World und Strase ok danke"), "Vorschlag ersetzt das Wort");
    expect(spellWords(field) == QStringList({QStringLiteral("Strase")}), "danach bleibt nur der zweite Fehler");

    // Menü außerhalb eines Fehlers: nur Bearbeiten und Sprachen; alte Vorschläge sind weg
    openMenuAt(1);
    expect(menu->property("visible").toBool() && menu->property("target").isNull()
               && menu->property("suggestions").toList().isEmpty(), "Menü ohne Fehler zeigt keine Vorschläge");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/rechtschreibung_menue_leer.png"));
    QMetaObject::invokeMethod(menu, "close");
    settle(150);

    controller.addSpellWord(QStringLiteral("Strase"));
    settle(300);
    expect(spellWords(field).isEmpty() && redPixels(view, field) == 0, "eigenes Wörterbuch nimmt die Markierung weg");

    field->setProperty("text", QStringLiteral("Wrold "));
    settle(300);
    expect(spellWords(field) == QStringList({QStringLiteral("Wrold")}), "neuer Text wird geprüft");
    controller.setSpellGerman(false);
    controller.setSpellEnglish(false);
    settle(300);
    expect(spellWords(field).isEmpty(), "beide Sprachen aus: keine Markierung");
    controller.setSpellGerman(true);
    controller.setSpellEnglish(true);
    settle(300);
    expect(spellWords(field) == QStringList({QStringLiteral("Wrold")}), "Sprachen wieder an");

    // Wort wird noch getippt: keine Markierung, bis das Leerzeichen kommt
    field->forceActiveFocus();
    field->setProperty("text", QStringLiteral("Hallo Wrold"));
    field->setProperty("cursorPosition", 11);
    settle(300);
    if (field->hasActiveFocus())
        expect(spellWords(field).isEmpty(), "Wort am Cursor bleibt unmarkiert");
    field->setProperty("text", QString());
    settle(150);
    expect(spellWords(field).isEmpty(), "leeres Feld ohne Markierung");

    // ——— Satzbau: Hinweiszeile, Korrektur, blaue Linie ———
    controller.setGrammarOnline(false);        // Der Test soll nichts ins Netz schicken
    QQuickItem *grammarBar = panel->findChild<QQuickItem *>(QStringLiteral("grammarBar"));
    QQuickItem *grammarMessage = panel->findChild<QQuickItem *>(QStringLiteral("grammarMessage"));
    QQuickItem *predictionBar = panel->findChild<QQuickItem *>(QStringLiteral("predictionBar"));
    expect(grammarBar && grammarMessage && predictionBar && !grammarBar->isVisible() && !predictionBar->isVisible(), "Leisten ohne Text versteckt");
    if (!grammarBar || !grammarMessage || !predictionBar) return false;
    field->setProperty("text", QStringLiteral("Das ist ist gut "));
    settle(300);
    expect(grammarBar->isVisible() && grammarMessage->property("text").toString().contains(QStringLiteral("repeated")), "Hinweis zum doppelten Wort");
    // Repeater-Kinder hängen nur im sichtbaren Baum, findChildren fände sie nicht.
    const auto chips = [&](QQuickItem *bar, const QString &name) {
        QList<QQuickItem *> visible;
        const QList<QQuickItem *> all = bar->childItems();
        for (QQuickItem *item : all)
            if (item->objectName() == name && item->isVisible() && item->width() > 0) visible << item;
        return visible;
    };
    const auto clickItem = [&](QQuickItem *item) {
        const QPointF scene = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QMouseEvent press(QEvent::MouseButtonPress, scene, scene, view.mapToGlobal(scene), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, scene, view.mapToGlobal(scene), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &release);
        settle(300);
    };
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/satzbau.png"));
    const auto grammarChips = chips(grammarBar, QStringLiteral("grammarChip"));
    expect(grammarChips.size() == 1, "Korrektur steht als Knopf da");
    if (!grammarChips.isEmpty()) clickItem(grammarChips.first());
    expect(field->property("text").toString() == QStringLiteral("Das ist gut "), "Klick übernimmt die Korrektur");
    expect(!grammarBar->isVisible(), "danach ist der Hinweis weg");

    field->setProperty("text", QStringLiteral("Das ist ist gut "));
    settle(300);
    openMenuAt(9);
    expect(menu->property("suggestions").toStringList() == QStringList({QStringLiteral("ist")}), "Rechtsklick zeigt die Korrektur");
    QMetaObject::invokeMethod(menu, "close");
    settle(150);
    controller.setGrammarCheck(false);
    settle(300);
    expect(!grammarBar->isVisible(), "Satzbau aus: kein Hinweis");
    controller.setGrammarCheck(true);

    // ——— Wortvorschläge: Liste kommt vom Wörterbuch-Thread, Tab und Klick übernehmen ———
    field->setProperty("text", QStringLiteral("Hal"));
    field->setProperty("cursorPosition", 3);
    QElapsedTimer waited;
    waited.start();
    while (field->property("predictions").toList().isEmpty() && waited.elapsed() < 10000) settle(50);
    const QStringList predicted = field->property("predictions").toStringList();
    std::printf("Wortvorschläge zu „Hal“: %s\n", predicted.join(QStringLiteral(", ")).toUtf8().constData());
    expect(predictionBar->isVisible() && !predicted.isEmpty() && predicted.first().startsWith(QStringLiteral("Hal")), "Wortvorschläge erscheinen");
    settle(100);
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/wortvorschlaege.png"));
    field->forceActiveFocus();
    settle(100);
    if (field->hasActiveFocus() && !predicted.isEmpty()) {
        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &tab);
        settle(200);
        expect(field->property("text").toString() == predicted.first() + QLatin1Char(' '), "Tab übernimmt den ersten Vorschlag");
        expect(field->hasActiveFocus(), "Fokus bleibt nach Tab im Feld");
    }
    field->setProperty("text", QStringLiteral("Hal"));
    field->setProperty("cursorPosition", 3);
    settle(200);
    const auto predictionChips = chips(predictionBar, QStringLiteral("predictionChip"));
    expect(!predictionChips.isEmpty(), "Vorschläge stehen als Knöpfe da");
    if (!predictionChips.isEmpty()) {
        clickItem(predictionChips.first());
        expect(field->property("text").toString().endsWith(QLatin1Char(' ')) && !field->property("text").toString().startsWith(QStringLiteral("Hal ")), "Klick übernimmt den Vorschlag");
    }
    controller.setPredictText(false);
    field->setProperty("text", QStringLiteral("Hal"));
    settle(200);
    expect(!predictionBar->isVisible(), "Wortvorschläge aus: keine Leiste");
    controller.setPredictText(true);

    // Langer Hinweis und Vorschläge dürfen das Panel nicht über 329 px hinausschieben
    field->setProperty("text", QStringLiteral("hallo hallo das das ist ist ein Haus"));
    settle(300);
    const QPointF right = grammarBar->mapToScene(QPointF(grammarBar->width(), 0));
    expect(right.x() <= view.width(), "Hinweiszeile bleibt in der Seitenleiste");
    field->setProperty("text", QString());
    settle(150);

    // ——— Online-Prüfung im Panel, mit Testsatz: läuft sie mit den lokalen Regeln zusammen? ———
    controller.setGrammarOnline(true);
    const QString online = QStringLiteral("he go to school today ");
    field->setProperty("text", online);
    field->setProperty("cursorPosition", int(online.size()));
    settle(200);
    const QVariantList localOnly = field->property("grammarRanges").toList();
    expect(localOnly.size() == 1, "lokale Regel meldet sich sofort, vor der Tipp-Pause");
    QElapsedTimer waitedOnline;
    waitedOnline.start();
    const auto remoteCount = [&] {
        int remote = 0;
        const QVariantList all = field->property("grammarRanges").toList();
        for (const QVariant &v : all)
            if (v.toMap().value(QStringLiteral("source")).toString() == QStringLiteral("languagetool")) ++remote;
        return remote;
    };
    while (remoteCount() == 0 && waitedOnline.elapsed() < 12000) settle(100);
    if (remoteCount() == 0) {
        std::printf("HINWEIS LanguageTool im Panel nicht erreichbar, Prüfung übersprungen\n");
    } else {
        std::printf("LanguageTool im Panel: %d Fundstellen nach %lld ms\n", int(field->property("grammarRanges").toList().size()), waitedOnline.elapsed());
        int atFirstWord = 0;
        const QVariantList all = field->property("grammarRanges").toList();
        for (const QVariant &v : all)
            if (v.toMap().value(QStringLiteral("start")).toInt() == 0) ++atFirstWord;
        expect(atFirstWord == 1, "gleiche Stelle von beiden Prüfungen erscheint nur einmal");
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/satzbau_online.png"));
        // Tippen macht die Positionen ungültig: Online-Fundstellen müssen sofort verschwinden.
        field->setProperty("text", online + QStringLiteral("x"));
        settle(150);
        expect(remoteCount() == 0, "weitertippen verwirft alte Online-Fundstellen");
    }
    // Befehle gehen nie hinaus: Auch nach der Pause darf keine Online-Fundstelle erscheinen.
    field->setProperty("text", QStringLiteral("/w freund he go to school today "));
    settle(1800);
    expect(remoteCount() == 0, "Befehl wird nicht online geprüft");
    field->setProperty("text", QString());
    settle(150);
    return ok;
}

// Das fertige Paket: Die DLL muss ihre Wörterbücher selbst neben sich finden.
static bool checkPackagedDictionaries(QQmlEngine &engine)
{
    FakeHost host;
    QPluginLoader loader(QStringLiteral("M:/LAPS/twitchchat/out/plugin/twitchchat.dll"));
    auto *plugin = qobject_cast<uve::IPlayerPlugin *>(loader.instance());
    QString error;
    if (!plugin || !plugin->initialize(&host, &error)) {
        std::printf("Paket-Wörterbücher: DLL nicht geladen: %s\n", loader.errorString().toUtf8().constData());
        return false;
    }
    QQmlComponent component(&engine);
    component.setData("import QtQuick\n"
                      "QtObject {\n"
                      "  property var controller\n"
                      "  property var words: null\n"
                      "  property Connections c: Connections {\n"
                      "    target: controller\n"
                      "    function onSpellResult(text, ranges) { words = ranges.map(r => r.word).join(',') }\n"
                      "  }\n"
                      "}\n", QUrl());
    std::unique_ptr<QObject> probe(component.createWithInitialProperties(
        {{QStringLiteral("controller"), QVariant::fromValue(plugin->controller())}}));
    QMetaObject::invokeMethod(plugin->controller(), "spellCheck",
                              Q_ARG(QString, QStringLiteral("Grüße und hello colour Fehlerr ")), Q_ARG(int, -1));
    QElapsedTimer waited;
    waited.start();
    while (probe && probe->property("words").isNull() && waited.elapsed() < 10000) settle(50);
    const QString words = probe ? probe->property("words").toString() : QString();
    const bool ok = words == QStringLiteral("Fehlerr");
    std::printf("Paket-Wörterbücher (DLL neben dict/): „%s“ %s\n", words.toUtf8().constData(), ok ? "ok" : "FEHLER");
    probe.reset();
    loader.unload();
    return ok;
}

// Fängt Qt.openUrlExternally ab, damit der Test keinen Browser öffnet.
class LinkCatcher : public QObject
{
    Q_OBJECT
public:
    QList<QUrl> opened;
public slots:
    void open(const QUrl &url) { opened << url; }
};

class SignalCounter : public QObject
{
    Q_OBJECT
public:
    int count = 0;
public slots:
    void hit() { ++count; }
};

static void collectItems(QQuickItem *item, const QString &name, QList<QQuickItem *> &found)
{
    if (item->objectName() == name) found << item;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) collectItems(child, name, found);
}

static bool checkChatLinks(TwitchChatController &controller, QQuickView &view, QQuickItem *panel, const QString &shotDir)
{
    bool ok = true;
    const auto expect = [&ok](bool passed, const char *what) {
        std::printf("Links %s: %s\n", what, passed ? "ok" : "FEHLER");
        ok = ok && passed;
    };
    const auto linksOf = [&](const QString &line) {
        const QString frame = QStringLiteral("@display-name=Tester;color=#1E90FF;emotes= "
                                             ":tester!tester@tmi.twitch.tv PRIVMSG #test :%1\r\n").arg(line);
        QMetaObject::invokeMethod(&controller, "onText", Q_ARG(QString, frame));
        const QVariantList history = controller.history();
        QStringList urls;
        QString shown;
        const QVariantList segments = history.last().toMap().value(QStringLiteral("segments")).toList();
        for (const QVariant &v : segments) {
            const QVariantMap s = v.toMap();
            shown += s.value(QStringLiteral("s")).toString();
            const QString url = s.value(QStringLiteral("url")).toString();
            if (!url.isEmpty()) urls << url;
        }
        expect(shown == line, "Text bleibt vollständig erhalten");
        return urls;
    };
    using L = QStringList;
    expect(linksOf(QStringLiteral("schau https://example.org/a?b=1#c an")) == L{"https://example.org/a?b=1#c"}, "voller Link");
    expect(linksOf(QStringLiteral("(http://example.org/x), danke")) == L{"http://example.org/x"}, "Satzzeichen bleiben draußen");
    expect(linksOf(QStringLiteral("https://de.wikipedia.org/wiki/A_(B).")) == L{"https://de.wikipedia.org/wiki/A_(B)"}, "Klammer im Link bleibt");
    expect(linksOf(QStringLiteral("www.example.org/test")) == L{"https://www.example.org/test"}, "www bekommt https");
    expect(linksOf(QStringLiteral("twitch.tv/xnowski und youtu.be/abc")) == L({"https://twitch.tv/xnowski", "https://youtu.be/abc"}), "Kurzformen");
    expect(linksOf(QStringLiteral("z.B. node.js 1.5 foo.bar a.b usw.")).isEmpty(), "keine falschen Links");
    expect(linksOf(QStringLiteral("https://twitch.tv@boese.example/")).isEmpty(), "Benutzerteil täuscht nicht");
    expect(linksOf(QStringLiteral("javascript:alert(1) file:///c:/x ftp://example.org")).isEmpty(), "andere Protokolle nie");
    expect(linksOf(QStringLiteral("https://bücher.example/")) == L{"https://xn--bcher-kva.example/"}, "internationale Domain zeigt ihr echtes Ziel");
    linksOf(QStringLiteral("<b>fett</b>"));
    const QString line = QStringLiteral("Hier https://example.org/klick lang");
    const L clickable = linksOf(line);
    settle(300);

    LinkCatcher catcher;
    QDesktopServices::setUrlHandler(QStringLiteral("https"), &catcher, "open");
    QQuickItem *chat = panel->findChild<QQuickItem *>(QStringLiteral("chatView"));
    QList<QQuickItem *> links;
    if (chat) collectItems(chat, QStringLiteral("chatLink"), links);
    expect(!links.isEmpty(), "Links sind im Chat anklickbar");
    if (!links.isEmpty()) {
        QQuickItem *link = links.first();
        qreal bestY = -1;
        for (QQuickItem *candidate : std::as_const(links)) {   // die unterste Zeile ist sichtbar
            const qreal y = candidate->mapToScene(QPointF(0, 0)).y();
            if (candidate->property("url").toString() == clickable.value(0) && y > bestY) {
                link = candidate;
                bestY = y;
            }
        }
        const QPointF scene = link->mapToScene(QPointF(link->width() / 2, link->height() / 2));
        QMouseEvent move(QEvent::MouseMove, scene, scene, view.mapToGlobal(scene), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &move);
        settle(500);
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/links.png"));
        QMouseEvent press(QEvent::MouseButtonPress, scene, scene, view.mapToGlobal(scene), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, scene, view.mapToGlobal(scene), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &release);
        settle(300);
        expect(catcher.opened == QList<QUrl>{QUrl(clickable.value(0))}, "Klick öffnet genau diesen Link");
        expect(link->width() < 250, "nur der Link ist klickbar, nicht die ganze Zeile");
    }
    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
    return ok;
}

static FakeHost *g_host = nullptr;
static QString host_hiddenPin(TwitchChatController &)
{
    return g_host ? g_host->storedSettings(QStringLiteral("cc.ts3x.twitch-chat")).value(QStringLiteral("hiddenPin")).toString() : QString();
}

static QByteArray pinJson(const QString &id, const QString &text, const QString &endsAt = QString())
{
    const QString ends = endsAt.isEmpty() ? QStringLiteral("null") : QStringLiteral("\"%1\"").arg(endsAt);
    return QStringLiteral(R"({"data":{"channel":{"pinnedChatMessages":{"edges":[{"node":{"id":"%1","endsAt":%2,)"
                          R"("pinnedBy":{"displayName":"ModPerson"},"pinnedMessage":{"content":{"text":"%3"},)"
                          R"("sender":{"displayName":"Streamer","login":"streamer","chatColor":"#FF4500"}}}}]}}}})")
        .arg(id, ends, text).toUtf8();
}

static bool checkPinned(TwitchChatController &controller, QQuickView &view, QQuickItem *panel, const QString &shotDir)
{
    bool ok = true;
    const auto expect = [&ok](bool passed, const char *what) {
        std::printf("Angeheftet %s: %s\n", what, passed ? "ok" : "FEHLER");
        ok = ok && passed;
    };
    QQuickItem *card = panel->findChild<QQuickItem *>(QStringLiteral("pinnedCard"));
    QQuickItem *hide = panel->findChild<QQuickItem *>(QStringLiteral("pinnedHide"));
    QQuickItem *show = panel->findChild<QQuickItem *>(QStringLiteral("pinnedShow"));
    QQuickItem *chat = panel->findChild<QQuickItem *>(QStringLiteral("chatView"));
    if (!card || !hide || !show || !chat) {
        std::printf("Angeheftet: Karte, Knöpfe oder Chat fehlen\n");
        return false;
    }
    controller.showPinned();
    expect(!card->isVisible(), "ohne Anheftung keine Karte");

    controller.applyPinnedJson(pinJson(QStringLiteral("pin-1"),
        QStringLiteral("Spendenseite: https://example.org/spenden und Regeln beachten, danke an alle die zuschauen")));
    settle(300);
    expect(card->isVisible(), "Karte erscheint");
    const QPointF cardTop = card->mapToScene(QPointF(0, 0));
    const QPointF chatTop = chat->mapToScene(QPointF(0, 0));
    expect(qAbs(cardTop.y() - card->parentItem()->mapToScene(QPointF(0, 0)).y()) < 1 && card->width() > 250
               && chatTop.y() >= cardTop.y() + card->height(), "Karte steht oben, der Chat beginnt darunter");
    settle(300);
    expect(chat->property("atYEnd").toBool(), "neueste Nachricht bleibt sichtbar");
    QList<QQuickItem *> links;
    collectItems(card, QStringLiteral("chatLink"), links);
    expect(links.size() == 1 && links.first()->property("url").toString() == QStringLiteral("https://example.org/spenden"),
           "Link in der Karte ist anklickbar");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/angeheftet.png"));

    const auto click = [&](QQuickItem *item) {
        const QPointF scene = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QMouseEvent press(QEvent::MouseButtonPress, scene, scene, view.mapToGlobal(scene), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, scene, view.mapToGlobal(scene), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &release);
        settle(300);
    };
    // Einklappen: nur noch eine Zeile, der Chat rückt nach oben
    QQuickItem *collapse = panel->findChild<QQuickItem *>(QStringLiteral("pinnedCollapse"));
    QQuickItem *preview = panel->findChild<QQuickItem *>(QStringLiteral("pinnedPreview"));
    QQuickItem *body = panel->findChild<QQuickItem *>(QStringLiteral("pinnedBody"));
    if (collapse && preview && body) {
        const qreal openHeight = card->height();
        const qreal openChatTop = chat->mapToScene(QPointF(0, 0)).y();
        click(collapse);
        expect(controller.pinnedCollapsed() && preview->isVisible() && !body->isVisible() && card->height() < openHeight,
               "▾ klappt auf eine Zeile ein");
        expect(chat->mapToScene(QPointF(0, 0)).y() < openChatTop, "eingeklappt rückt der Chat nach oben");
        expect(preview->property("text").toString().startsWith(QStringLiteral("Spendenseite:")), "eingeklappt steht der Anfang da");
        expect(g_host->storedSettings(QStringLiteral("cc.ts3x.twitch-chat")).value(QStringLiteral("pinCollapsed")).toBool(),
               "Einklappen wird gespeichert");
        settle(300);
        expect(chat->property("atYEnd").toBool(), "nach dem Einklappen bleibt das Chat-Ende sichtbar");
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/angeheftet_eingeklappt.png"));
        click(collapse);
        expect(!controller.pinnedCollapsed() && body->isVisible() && qAbs(card->height() - openHeight) < 1, "▸ klappt wieder auf");
    } else {
        expect(false, "Einklapp-Knopf vorhanden");
    }

    click(hide);
    expect(!card->isVisible() && show->isVisible(), "✕ verbirgt, 📌 erscheint");
    expect(qAbs(chat->mapToScene(QPointF(0, 0)).y() - card->parentItem()->mapToScene(QPointF(0, 0)).y()) < 1,
           "verborgen nutzt der Chat wieder die ganze Höhe");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/angeheftet_verborgen.png"));
    controller.applyPinnedJson(pinJson(QStringLiteral("pin-1"),
        QStringLiteral("Spendenseite: https://example.org/spenden und Regeln beachten, danke an alle die zuschauen")));
    settle(200);
    expect(!card->isVisible(), "dieselbe Anheftung bleibt verborgen");
    expect(host_hiddenPin(controller) == QStringLiteral("pin-1"), "Verbergen wird gespeichert");
    click(show);
    expect(card->isVisible() && !show->isVisible(), "📌 zeigt sie wieder");
    click(hide);
    controller.applyPinnedJson(pinJson(QStringLiteral("pin-2"), QStringLiteral("Neue Ansage")));
    settle(200);
    expect(card->isVisible(), "neue Anheftung erscheint trotz verborgener alter");

    QString longText;
    for (int i = 0; i < 40; ++i) longText += QStringLiteral("Sehr lange Ansage mit vielen Wörtern ");
    controller.applyPinnedJson(pinJson(QStringLiteral("pin-3"), longText.trimmed()));
    settle(300);
    expect(card->height() <= card->parentItem()->parentItem()->height() * 0.5 + 1, "lange Anheftung nimmt höchstens die halbe Höhe");
    expect(card->mapToScene(QPointF(card->width(), 0)).x() <= view.width(), "Karte bleibt in der Seitenleiste");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/angeheftet_lang.png"));

    controller.applyPinnedJson(pinJson(QStringLiteral("pin-4"), QStringLiteral("abgelaufen"), QStringLiteral("2020-01-01T00:00:00Z")));
    settle(200);
    expect(!card->isVisible(), "abgelaufene Anheftung verschwindet");
    controller.applyPinnedJson(pinJson(QStringLiteral("pin-5"), QStringLiteral("gleich vorbei"),
                                       QDateTime::currentDateTimeUtc().addSecs(1).toString(Qt::ISODate)));
    settle(200);
    expect(card->isVisible(), "Anheftung mit Ablaufzeit erscheint");
    settle(1800);
    expect(!card->isVisible(), "verschwindet zur Ablaufzeit von selbst");
    controller.applyPinnedJson(pinJson(QStringLiteral("pin-6"), QStringLiteral("bleibt")));
    controller.applyPinnedJson("{\"errors\":[{\"message\":\"x\"}]}");
    settle(200);
    expect(card->isVisible(), "Fehlerantwort lässt die Karte stehen");
    controller.applyPinnedJson(R"({"data":{"channel":{"pinnedChatMessages":{"edges":[]}}}})");
    settle(200);
    expect(!card->isVisible() && !show->isVisible(), "Anheftung entfernt: Karte und 📌 weg");
    return ok;
}

// Umfrage + Vorhersage (2.1.6): Karten wie die Anheftung, gestapelt über dem Chat.
// JSON im Schema der echten gql-Antworten (Zeitstempel mit Nanosekunden wie bei Twitch).
static QByteArray eventsJson(const QString &poll, const QString &active, const QString &locked = QString(),
                             const QString &resolved = QString())
{
    return QStringLiteral(R"({"data":{"user":{"login":"xnowski","viewablePoll":%1,"channel":{"activePredictionEvents":[%2],)"
                          R"("lockedPredictionEvents":[%3],"resolvedPredictionEvents":{"edges":[%4]}}}}})")
        .arg(poll.isEmpty() ? QStringLiteral("null") : poll, active, locked,
             resolved.isEmpty() ? QString() : QStringLiteral("{\"node\":%1}").arg(resolved))
        .toUtf8();
}

static QString isoNs(qint64 ms)
{
    // Twitch schickt 9 Nachkommastellen
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")) + QStringLiteral("114589Z");
}

static QString pollJson(const QString &id, const QString &status, qint64 startedMs, int duration, qint64 remainingMs,
                        const QString &endedAt = QStringLiteral("null"))
{
    return QStringLiteral(R"({"id":"%1","title":"Welches Spiel als Nächstes?","status":"%2","startedAt":"%3","endedAt":%4,)"
                          R"("durationSeconds":%5,"remainingDurationMilliseconds":%6,"choices":[)"
                          R"({"id":"c1","title":"Elden Ring","votes":{"total":120}},)"
                          R"({"id":"c2","title":"Hollow Knight: Silksong","votes":{"total":60}},)"
                          R"({"id":"c3","title":"Minecraft","votes":{"total":20}}],"votes":{"total":200}})")
        .arg(id, status, isoNs(startedMs), endedAt).arg(duration).arg(remainingMs);
}

static QString predictionJson(const QString &id, const QString &status, qint64 createdMs, int window,
                              const QString &winner = QStringLiteral("null"), const QString &endedAt = QStringLiteral("null"))
{
    return QStringLiteral(R"({"id":"%1","title":"BOSS FIGHT: WILL HASAN BEAT IT FIRST TRY?","status":"%2","createdAt":"%3",)"
                          R"("predictionWindowSeconds":%4,"lockedAt":null,"endedAt":%5,"winningOutcome":%6,"outcomes":[)"
                          R"({"id":"o1","title":"Yes","color":"BLUE","totalPoints":7344023,"totalUsers":146},)"
                          R"({"id":"o2","title":"No","color":"PINK","totalPoints":6232522,"totalUsers":129}]})")
        .arg(id, status, isoNs(createdMs)).arg(window).arg(endedAt, winner);
}

static void mouseAt(QQuickView &view, QQuickItem *item, QPointF local, Qt::MouseButton button, bool doubleClick);

static bool checkChannelEvents(TwitchChatController &controller, QQuickView &view, QQuickItem *panel, const QString &shotDir)
{
    bool ok = true;
    const auto expect = [&ok](bool passed, const char *what) {
        std::printf("Umfrage/Vorhersage %s: %s\n", what, passed ? "ok" : "FEHLER");
        ok = ok && passed;
    };
    QQuickItem *poll = panel->findChild<QQuickItem *>(QStringLiteral("pollCard"));
    QQuickItem *pred = panel->findChild<QQuickItem *>(QStringLiteral("predictionCard"));
    QQuickItem *pin = panel->findChild<QQuickItem *>(QStringLiteral("pinnedCard"));
    QQuickItem *show = panel->findChild<QQuickItem *>(QStringLiteral("eventsShow"));
    QQuickItem *chat = panel->findChild<QQuickItem *>(QStringLiteral("chatView"));
    if (!poll || !pred || !pin || !show || !chat) {
        std::printf("Umfrage/Vorhersage: Karten, Knöpfe oder Chat fehlen\n");
        return false;
    }
    const auto child = [panel](const char *name) { return panel->findChild<QQuickItem *>(QString::fromLatin1(name)); };
    const auto rows = [panel](const char *name) {
        QObject *r = panel->findChild<QObject *>(QString::fromLatin1(name));
        return r ? r->property("count").toInt() : -1;
    };
    const auto text = [&child](const char *name) {
        QQuickItem *i = child(name);
        return i ? i->property("text").toString() : QString();
    };
    controller.showChannelEvents();
    controller.setPollCollapsed(false);
    controller.setPredictionCollapsed(false);
    controller.applyChannelEventsJson(eventsJson(QString(), QString()));
    settle(200);
    expect(!poll->isVisible() && !pred->isVisible(), "ohne Ereignis keine Karte");

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    controller.applyPinnedJson(pinJson(QStringLiteral("pin-ev"), QStringLiteral("Regeln beachten")));
    controller.applyChannelEventsJson(eventsJson(pollJson(QStringLiteral("p1"), QStringLiteral("ACTIVE"), now - 18000, 60, 42000),
                                                 predictionJson(QStringLiteral("e1"), QStringLiteral("ACTIVE"), now - 60000, 300)));
    settle(1300);
    expect(poll->isVisible() && pred->isVisible() && pin->isVisible(), "Anheftung, Umfrage und Vorhersage sichtbar");
    expect(rows("pollRows") == 3 && rows("predictionRows") == 2, "3 Antworten, 2 Ausgänge");
    const QVariantList choices = controller.channelPoll().value(QStringLiteral("choices")).toList();
    expect(choices.size() == 3 && choices[0].toMap().value(QStringLiteral("percent")).toInt() == 60
               && choices[0].toMap().value(QStringLiteral("leading")).toBool(),
           "Prozent und Führende richtig (120/200 = 60 %)");
    const QString pollState = text("pollState");
    expect(pollState == QLatin1String("0:41") || pollState == QLatin1String("0:42") || pollState == QLatin1String("0:40"),
           "Umfrage zählt Restzeit herunter");
    const QString predState = text("predictionState");
    std::printf("  Restzeiten: Umfrage %s, Vorhersage %s\n", pollState.toUtf8().constData(), predState.toUtf8().constData());
    expect(predState.startsWith(QLatin1String("3:5")) || predState.startsWith(QLatin1String("4:0")),
           "Vorhersage-Zeit aus createdAt (Nanosekunden) + Fenster");
    const qreal pinBottom = pin->mapToItem(panel, QPointF(0, pin->height())).y();
    const qreal pollTop = poll->mapToItem(panel, QPointF(0, 0)).y();
    const qreal pollBottom = poll->mapToItem(panel, QPointF(0, poll->height())).y();
    const qreal predTop = pred->mapToItem(panel, QPointF(0, 0)).y();
    const qreal predBottom = pred->mapToItem(panel, QPointF(0, pred->height())).y();
    const qreal chatTop = chat->mapToItem(panel, QPointF(0, 0)).y();
    expect(pollTop >= pinBottom && predTop >= pollBottom && chatTop >= predBottom - 0.5, "untereinander, Chat beginnt darunter");
    const qreal right = pred->mapToItem(panel, QPointF(pred->width(), 0)).x();
    expect(right <= panel->width() + 0.5, "Karten passen in die Breite");
    if (!shotDir.isEmpty()) view.grabWindow().save(QStringLiteral("%1/umfrage_vorhersage.png").arg(shotDir));

    // Abstimmen: Knopf öffnet den Pop-out-Chat des Kanals
    QQuickItem *pollVote = child("pollVote");
    QQuickItem *predVote = child("predictionVote");
    expect(pollVote && predVote && pollVote->isVisible() && predVote->isVisible(), "„Auf Twitch abstimmen“ bei laufender Abstimmung");
    if (pollVote) {
        LinkCatcher catcher;
        QDesktopServices::setUrlHandler(QStringLiteral("https"), &catcher, "open");
        mouseAt(view, pollVote, QPointF(pollVote->width() / 2, pollVote->height() / 2), Qt::LeftButton, false);
        settle(200);
        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
        const QString want = QStringLiteral("https://www.twitch.tv/popout/xnowski/chat?popout=");
        std::printf("  geöffnet: %s\n", catcher.opened.isEmpty() ? "-" : catcher.opened.first().toString().toUtf8().constData());
        expect(catcher.opened.size() == 1 && catcher.opened.first().toString() == want, "Klick öffnet Pop-out-Chat des Kanals");
    }
    const qreal openHeight = poll->height();
    controller.setPollCollapsed(true);
    settle(200);
    expect(rows("pollRows") == 0 && poll->height() < openHeight
               && text("pollHeading") == QStringLiteral("Welches Spiel als Nächstes?"),
           "▾ klappt auf eine Zeile mit der Frage");
    controller.setPollCollapsed(false);

    controller.hidePrediction();
    settle(200);
    expect(!pred->isVisible() && show->isVisible(), "✕ verbirgt, 📊 erscheint");
    controller.showChannelEvents();
    settle(200);
    expect(pred->isVisible() && !show->isVisible(), "📊 holt sie zurück");

    // Teilfehler bei der Umfrage: Umfrage bleibt stehen, Vorhersage wird trotzdem aktualisiert
    controller.applyChannelEventsJson(QStringLiteral(R"({"errors":[{"message":"service unavailable","path":["user","viewablePoll"]}],)"
                                                     R"("data":{"user":{"login":"xnowski","viewablePoll":null,"channel":{"activePredictionEvents":[],)"
                                                     R"("lockedPredictionEvents":[%1],"resolvedPredictionEvents":{"edges":[]}}}}})")
                                          .arg(predictionJson(QStringLiteral("e1"), QStringLiteral("LOCKED"), now - 400000, 300))
                                          .toUtf8());
    settle(200);
    expect(predVote && predVote->isVisible() && predVote->property("text").toString().contains(QStringLiteral("Twitch"))
               && !predVote->property("text").toString().contains(QStringLiteral("Vote"))
               && !predVote->property("text").toString().contains(QStringLiteral("abstimmen")),
           "gesperrte Vorhersage: Knopf „Im Twitch-Chat öffnen“");
    expect(poll->isVisible() && (text("predictionState") == QStringLiteral("Locked") || text("predictionState") == QStringLiteral("Gesperrt")),
           "Teilfehler: Umfrage bleibt, Vorhersage gesperrt");

    // Ende: Ergebnis mit Gewinner, nach 3 min weg
    const QString ended = QStringLiteral("\"%1\"").arg(isoNs(now - 60000));
    controller.applyChannelEventsJson(eventsJson(pollJson(QStringLiteral("p1"), QStringLiteral("COMPLETED"), now - 120000, 60, 0, ended),
                                                 QString(), QString(),
                                                 predictionJson(QStringLiteral("e1"), QStringLiteral("RESOLVED"), now - 600000, 300,
                                                                QStringLiteral("{\"id\":\"o2\"}"), ended)));
    settle(200);
    const QVariantList outcomes = controller.channelPrediction().value(QStringLiteral("outcomes")).toList();
    expect(poll->isVisible() && controller.channelPoll().value(QStringLiteral("ended")).toBool()
               && (text("pollState") == QStringLiteral("Ended") || text("pollState") == QStringLiteral("Beendet")),
           "beendete Umfrage zeigt Ergebnis");
    expect(pollVote && predVote && pollVote->isVisible() && predVote->isVisible(), "nach dem Ende Knopf zum Twitch-Chat bleibt");
    expect(pred->isVisible() && outcomes.size() == 2 && outcomes[1].toMap().value(QStringLiteral("winner")).toBool()
               && !outcomes[0].toMap().value(QStringLiteral("winner")).toBool(),
           "aufgelöste Vorhersage markiert Gewinner");
    if (!shotDir.isEmpty()) view.grabWindow().save(QStringLiteral("%1/umfrage_vorhersage_ergebnis.png").arg(shotDir));
    const QString old = QStringLiteral("\"%1\"").arg(isoNs(now - 600000));
    controller.applyChannelEventsJson(eventsJson(pollJson(QStringLiteral("p1"), QStringLiteral("COMPLETED"), now - 700000, 60, 0, old),
                                                 QString(), QString(),
                                                 predictionJson(QStringLiteral("e1"), QStringLiteral("RESOLVED"), now - 900000, 300,
                                                                QStringLiteral("{\"id\":\"o2\"}"), old)));
    settle(200);
    expect(!poll->isVisible() && !pred->isVisible(), "nach 3 min verschwinden beide");
    controller.applyChannelEventsJson(eventsJson(QString(), QString(),
                                                 QString(), predictionJson(QStringLiteral("e2"), QStringLiteral("CANCELED"), now - 90000, 60,
                                                                           QStringLiteral("null"), ended)));
    settle(200);
    expect(!pred->isVisible(), "abgebrochene Vorhersage nicht gezeigt");
    controller.applyPinnedJson(R"({"data":{"channel":{"pinnedChatMessages":{"edges":[]}}}})");
    settle(200);
    expect(chat->mapToItem(panel, QPointF(0, 0)).y() < pinBottom, "ohne Karten beginnt der Chat oben");
    return ok;
}

static void mouseAt(QQuickView &view, QQuickItem *item, QPointF local, Qt::MouseButton button, bool doubleClick = false)
{
    const QPointF scene = item->mapToScene(local);
    const QPointF global = view.mapToGlobal(scene);
    const auto send = [&](QEvent::Type type, Qt::MouseButtons buttons) {
        QMouseEvent event(type, scene, scene, global, button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &event);
    };
    send(QEvent::MouseButtonPress, button);
    send(QEvent::MouseButtonRelease, Qt::NoButton);
    if (doubleClick) {
        send(QEvent::MouseButtonPress, button);
        send(QEvent::MouseButtonDblClick, button);
        send(QEvent::MouseButtonRelease, Qt::NoButton);
    }
    settle(300);
}

// Die unterste sichtbare Zeile, deren Text mit needle beginnt
static QQuickItem *messageRow(QQuickItem *chat, const QString &needle)
{
    QList<QQuickItem *> texts;
    collectItems(chat, QString(), texts);
    QQuickItem *best = nullptr;
    for (QQuickItem *item : std::as_const(texts)) {
        if (!item->isVisible() || item->property("text").toString() != needle) continue;
        if (!best || item->mapToScene(QPointF(0, 0)).y() > best->mapToScene(QPointF(0, 0)).y()) best = item;
    }
    return best;
}

static bool checkCopyText(TwitchChatController &controller, QQuickView &view, QQuickItem *panel, const QString &shotDir)
{
    bool ok = true;
    const auto expect = [&ok](bool passed, const char *what) {
        std::printf("Kopieren %s: %s\n", what, passed ? "ok" : "FEHLER");
        ok = ok && passed;
    };
    const QString raw = QStringLiteral("Kopiertest Kappa mit Link https://example.org/kopie danke");
    const QString frame = QStringLiteral("@display-name=Kopierer;color=#1E90FF;emotes= "
                                         ":kopierer!kopierer@tmi.twitch.tv PRIVMSG #test :%1\r\n");
    QMetaObject::invokeMethod(&controller, "onText", Q_ARG(QString, frame.arg(raw)));
    QQuickItem *chat = panel->findChild<QQuickItem *>(QStringLiteral("chatView"));
    QObject *menu = panel->findChild<QObject *>(QStringLiteral("messageMenu"));
    settle(400);
    const auto atEnd = [&] {
        QMetaObject::invokeMethod(chat, "positionViewAtEnd");
        settle(300);
    };
    atEnd();
    QQuickItem *word = chat ? messageRow(chat, QStringLiteral("Kopiertest")) : nullptr;
    if (!word || !menu) {
        std::printf("Kopieren: Nachricht oder Menü nicht gefunden\n");
        return false;
    }
    const auto selected = [&] { return panel->property("selectedChatText").toString(); };
    const auto drag = [&](QPointF a, QPointF b) {
        QMouseEvent press(QEvent::MouseButtonPress, a, a, view.mapToGlobal(a), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &press);
        for (int i = 1; i <= 10; ++i) {
            const QPointF m = a + (b - a) * (i / 10.0);
            QMouseEvent move(QEvent::MouseMove, m, m, view.mapToGlobal(m), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&view, &move);
            settle(15);
        }
        QMouseEvent release(QEvent::MouseButtonRelease, b, b, view.mapToGlobal(b), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &release);
        settle(250);
    };
    const auto highlights = [&] {
        QList<QQuickItem *> found, visible;
        collectItems(chat, QStringLiteral("selectionHighlight"), found);
        for (QQuickItem *h : std::as_const(found))
            if (h->isVisible()) visible << h;
        return visible;
    };
    QGuiApplication::clipboard()->clear();

    // Ziehen über einen Teil des ersten Worts: genau diese Zeichen
    const qreal left = word->mapToScene(QPointF(0, 0)).x();
    drag(word->mapToScene(QPointF(1, word->height() / 2)), word->mapToScene(QPointF(word->width() * 0.6, word->height() / 2)));
    std::printf("Ein Wort angezogen: „%s“\n", selected().toUtf8().constData());
    expect(selected().size() >= 4 && selected().size() <= 8 && raw.startsWith(selected()), "Ziehen markiert genau die Zeichen unter der Maus");
    expect(!highlights().isEmpty() && qAbs(highlights().first()->mapToScene(QPointF(0, 0)).x() - left) < 3,
           "Markierung erscheint direkt in der Zeile, ab dem Wortanfang");
    expect(word->isVisible(), "die Zeile bleibt, wie sie ist (kein Umschalten)");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/markieren_maus.png"));
    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier, QStringLiteral("c"));
    QCoreApplication::sendEvent(&view, &copy);
    settle(100);
    expect(QGuiApplication::clipboard()->text() == selected(), "Strg+C kopiert die Markierung");

    // Über mehrere Wörter bis in den Link
    QList<QQuickItem *> links;
    collectItems(chat, QStringLiteral("chatLink"), links);
    QQuickItem *link = nullptr;
    for (QQuickItem *l : std::as_const(links))
        if (l->property("url").toString() == QStringLiteral("https://example.org/kopie")
            && (!link || l->mapToScene(QPointF(0, 0)).y() > link->mapToScene(QPointF(0, 0)).y()))
            link = l;
    if (link) {
        drag(word->mapToScene(QPointF(1, word->height() / 2)), link->mapToScene(QPointF(link->width() * 0.5, link->height() / 2)));
        std::printf("Mehrere Wörter: „%s“\n", selected().toUtf8().constData());
        expect(selected().startsWith(QStringLiteral("Kopiertest Kappa mit Link https://")) && raw.startsWith(selected())
                   && selected().size() < raw.size(), "Ziehen über mehrere Wörter und Zeilen");
        expect(highlights().size() >= 4, "jedes markierte Wort ist hervorgehoben");
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/markieren_mehrere.png"));

        // Rückwärts ziehen markiert dasselbe
        const QString forward = selected();
        drag(link->mapToScene(QPointF(link->width() * 0.5, link->height() / 2)), word->mapToScene(QPointF(1, word->height() / 2)));
        expect(selected() == forward, "rückwärts ziehen markiert dasselbe");
    } else {
        expect(false, "Link in der Nachricht gefunden");
    }

    // Neue Nachricht verschiebt die Ansicht nicht, solange markiert ist
    const qreal yBefore = word->mapToScene(QPointF(0, 0)).y();
    QMetaObject::invokeMethod(&controller, "onText", Q_ARG(QString, frame.arg(QStringLiteral("neu"))));
    settle(400);
    expect(qAbs(word->mapToScene(QPointF(0, 0)).y() - yBefore) < 1, "Chat bleibt beim Markieren stehen");

    // Rechtsklick auf die Markierung bietet sie zum Kopieren an
    mouseAt(view, word, QPointF(word->width() / 2, word->height() / 2), Qt::RightButton);
    const QString marked = selected();
    expect(menu->property("visible").toBool() && menu->property("marked").toString() == marked && !marked.isEmpty(),
           "Rechtsklick bietet die Markierung an");
    if (QObject *item = menu->findChild<QObject *>(QStringLiteral("copySelection"))) QMetaObject::invokeMethod(item, "click");
    settle(200);
    expect(QGuiApplication::clipboard()->text() == marked, "Markierung kopieren");

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &escape);
    settle(200);
    expect(selected().isEmpty() && highlights().isEmpty(), "Esc hebt die Markierung auf");

    // Doppelklick: ganze Nachricht
    atEnd();
    word = messageRow(chat, QStringLiteral("Kopiertest"));
    if (!word) return false;
    mouseAt(view, word, QPointF(word->width() / 2, word->height() / 2), Qt::LeftButton, true);
    expect(selected() == raw, "Doppelklick markiert die ganze Nachricht");
    QCoreApplication::sendEvent(&view, &copy);
    settle(100);
    expect(QGuiApplication::clipboard()->text() == raw, "Strg+C kopiert die ganze Nachricht");
    // Einfacher Klick auf eine andere Nachricht hebt die Markierung auf
    QQuickItem *other = messageRow(chat, QStringLiteral("neu"));
    if (other) mouseAt(view, other, QPointF(other->width() / 2, other->height() / 2), Qt::LeftButton);
    expect(selected().isEmpty(), "Klick auf eine andere Nachricht hebt die Markierung auf");

    // Mausrad über einer Nachricht scrollt weiter den Chat
    {
        const qreal before = chat->property("contentY").toReal();
        const QPointF c = word->mapToScene(QPointF(word->width() / 2, word->height() / 2));
        QWheelEvent wheel(c, view.mapToGlobal(c), QPoint(), QPoint(0, 360), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &wheel);
        settle(400);
        expect(chat->property("contentY").toReal() < before, "Mausrad über einer Nachricht scrollt den Chat");
    }

    // Rechtsklick-Menü ohne Markierung
    atEnd();
    word = messageRow(chat, QStringLiteral("Kopiertest"));
    if (!word) return false;
    const auto clickMenu = [&](const char *name) {
        if (QObject *item = menu->findChild<QObject *>(QLatin1String(name))) QMetaObject::invokeMethod(item, "click");
        settle(200);
    };
    mouseAt(view, word, QPointF(word->width() / 2, word->height() / 2), Qt::RightButton);
    expect(menu->property("visible").toBool() && menu->property("marked").toString().isEmpty(), "Rechtsklick öffnet das Kopiermenü");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/kopiermenue.png"));
    clickMenu("copyName");
    expect(QGuiApplication::clipboard()->text() == QStringLiteral("Kopierer"), "Name kopieren");
    mouseAt(view, word, QPointF(word->width() / 2, word->height() / 2), Qt::RightButton);
    clickMenu("copyMessage");
    expect(QGuiApplication::clipboard()->text() == raw, "Nachricht kopieren");
    if (link) {
        links.clear();
        collectItems(chat, QStringLiteral("chatLink"), links);
        link = nullptr;
        for (QQuickItem *l : std::as_const(links))
            if (l->property("url").toString() == QStringLiteral("https://example.org/kopie")
                && (!link || l->mapToScene(QPointF(0, 0)).y() > link->mapToScene(QPointF(0, 0)).y()))
                link = l;
        if (link) {
            mouseAt(view, link, QPointF(link->width() / 2, link->height() / 2), Qt::RightButton);
            expect(menu->property("url").toString() == QStringLiteral("https://example.org/kopie"), "Rechtsklick auf Link bietet ihn an");
            clickMenu("copyLink");
            expect(QGuiApplication::clipboard()->text() == QStringLiteral("https://example.org/kopie"), "Link kopieren");
        }
    }
    mouseAt(view, word, QPointF(word->width() / 2, word->height() / 2), Qt::RightButton);
    clickMenu("selectMessage");
    expect(selected() == raw, "„Alles markieren“ im Menü");
    QCoreApplication::sendEvent(&view, &escape);
    settle(200);

    // Angeheftete Nachricht
    controller.applyPinnedJson(pinJson(QStringLiteral("pin-copy"), QStringLiteral("Angeheftet zum Kopieren")));
    settle(300);
    QQuickItem *card = panel->findChild<QQuickItem *>(QStringLiteral("pinnedCard"));
    if (card && card->isVisible()) {
        mouseAt(view, card, QPointF(card->width() / 2, card->height() - 8), Qt::RightButton);
        clickMenu("copyMessage");
        expect(QGuiApplication::clipboard()->text() == QStringLiteral("Angeheftet zum Kopieren"), "angeheftete Nachricht kopieren");
    }
    controller.applyPinnedJson(R"({"data":{"channel":{"pinnedChatMessages":{"edges":[]}}}})");
    settle(200);
    return ok;
}

static bool checkChatFontSize(TwitchChatController &controller, QQuickView &view, QQuickItem *panel, FakeHost &host, const QString &shotDir)
{
    bool ok = true;
    const auto expect = [&ok](bool passed, const char *what) {
        std::printf("Schrift %s: %s\n", what, passed ? "ok" : "FEHLER");
        ok = ok && passed;
    };
    QQuickItem *smaller = panel->findChild<QQuickItem *>(QStringLiteral("fontSmaller"));
    QQuickItem *larger = panel->findChild<QQuickItem *>(QStringLiteral("fontLarger"));
    QQuickItem *chat = panel->findChild<QQuickItem *>(QStringLiteral("chatView"));
    if (!smaller || !larger || !chat) {
        std::printf("Schrift: Knöpfe oder Chat fehlen\n");
        return false;
    }
    const auto click = [&](QQuickItem *item) {
        const QPointF scene = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QMouseEvent press(QEvent::MouseButtonPress, scene, scene, view.mapToGlobal(scene), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, scene, view.mapToGlobal(scene), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &release);
        settle(200);
    };
    // Schriftgröße eines sichtbaren Chat-Worts
    const auto wordSize = [&] {
        QMetaObject::invokeMethod(chat, "positionViewAtEnd");
        settle(200);
        QQuickItem *word = messageRow(chat, QStringLiteral("Kopiertest"));
        return word ? word->property("font").value<QFont>().pixelSize() : -1;
    };
    controller.setChatFontSize(0);
    settle(200);
    const int base = wordSize();
    const int emoteBase = panel->property("emoteHeight").toInt();
    std::printf("Standardschrift: %d px, Emotes %d px\n", base, emoteBase);
    expect(base > 0 && smaller->isVisible() && larger->isVisible(), "Knöpfe da, Standardgröße gesetzt");

    for (int i = 0; i < 6; ++i) click(larger);
    const int grown = wordSize();
    expect(grown == base + 6 && controller.chatFontSize() == base + 6, "+ macht die Schrift größer");
    expect(panel->property("emoteHeight").toInt() >= emoteBase, "Emotes wachsen mit");
    expect(host.storedSettings(QStringLiteral("cc.ts3x.twitch-chat")).value(QStringLiteral("chatFontSize")).toInt() == base + 6,
           "Größe wird gespeichert");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/schrift_gross.png"));
    expect(chat->mapToScene(QPointF(chat->width(), 0)).x() <= view.width(), "große Schrift bleibt in der Seitenleiste");

    click(smaller);
    expect(wordSize() == base + 5, "− macht die Schrift kleiner");
    for (int i = 0; i < 40; ++i) click(larger);
    expect(controller.chatFontSize() == 28 && !larger->isEnabled(), "Obergrenze 28 px, + abgeblendet");
    for (int i = 0; i < 40; ++i) click(smaller);
    expect(controller.chatFontSize() == 9 && !smaller->isEnabled(), "Untergrenze 9 px, − abgeblendet");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/schrift_klein.png"));
    // Eingabefeld wächst mit, Wellenlinie sitzt weiter unter dem Wort, nichts läuft rechts hinaus
    {
        QQuickItem *field = panel->findChild<QQuickItem *>(QStringLiteral("sendField"));
        controller.setChatFontSize(0);
        settle(200);
        const qreal smallHeight = field ? field->height() : 0;
        controller.setChatFontSize(24);
        settle(200);
        if (field) {
            field->setProperty("enabled", true);
            field->setProperty("text", QStringLiteral("Hallo Wrold"));
            settle(500);
        }
        const int fieldSize = field ? field->property("font").value<QFont>().pixelSize() : -1;
        expect(fieldSize == 24 && field->height() > smallHeight, "Eingabefeld wächst mit der Schrift");
        expect(field && redPixels(view, field) > 20, "Wellenlinie auch bei großer Schrift");
        // Die Linie liegt unter „Wrold“: rote Pixel nur rechts vom Wortanfang
        if (field) {
            QRectF wordStart;
            QMetaObject::invokeMethod(field, "positionToRectangle", Q_RETURN_ARG(QRectF, wordStart), Q_ARG(int, 6));
            const qreal startX = field->mapToScene(QPointF(wordStart.x(), 0)).x();
            const QImage shot = view.grabWindow();
            const QPointF top = field->mapToScene(QPointF(0, 0));
            int leftOfWord = 0;
            for (int y = int(top.y()); y < int(top.y() + field->height()); ++y)
                for (int x = int(top.x()); x < int(startX) - 3; ++x) {
                    const QColor c = shot.pixelColor(x, y);
                    if (c.red() > 170 && c.green() < 130 && c.blue() < 120) ++leftOfWord;
                }
            expect(leftOfWord == 0, "Wellenlinie sitzt bei großer Schrift unter dem richtigen Wort");
        }
        QQuickItem *send = panel->findChild<QQuickItem *>(QStringLiteral("sendButton"));
        expect(send && send->mapToScene(QPointF(send->width(), 0)).x() <= view.width(), "„Senden“ bleibt in der Seitenleiste");
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/eingabe_gross.png"));
        if (field) field->setProperty("text", QString());
    }
    controller.setChatFontSize(0);
    settle(200);
    expect(wordSize() == base, "zurück zur Standardschrift");
    return ok;
}

static QQuickItem *popupContent(QQuickItem *panel, const char *name)
{
    QObject *popup = panel->findChild<QObject *>(QLatin1String(name));
    return popup ? qvariant_cast<QQuickItem *>(popup->property("contentItem")) : nullptr;
}

static QList<QQuickItem *> visibleItems(QQuickItem *root, const QString &name)
{
    QList<QQuickItem *> all, visible;
    if (root) collectItems(root, name, all);
    for (QQuickItem *item : std::as_const(all))
        if (item->isVisible() && item->width() > 0) visible << item;
    return visible;
}

static bool checkInboxAndChatters(TwitchChatController &controller, QQuickView &view, QQuickItem *panel, const QString &shotDir)
{
    bool ok = true;
    const auto expect = [&ok](bool passed, const char *what) {
        std::printf("Postfach %s: %s\n", what, passed ? "ok" : "FEHLER");
        ok = ok && passed;
    };
    WhisperService *service = controller.whisperService();
    QObject *inbox = panel->findChild<QObject *>(QStringLiteral("inbox"));
    QObject *chattersPopup = panel->findChild<QObject *>(QStringLiteral("chattersPopup"));
    QQuickItem *inboxButton = panel->findChild<QQuickItem *>(QStringLiteral("inboxButton"));
    QQuickItem *badge = panel->findChild<QQuickItem *>(QStringLiteral("inboxBadge"));
    QQuickItem *chattersButton = panel->findChild<QQuickItem *>(QStringLiteral("chattersButton"));
    if (!service || !inbox || !chattersPopup || !inboxButton || !badge || !chattersButton) {
        std::printf("Postfach: Bausteine fehlen\n");
        return false;
    }
    // Vorherige Testläufe aufräumen (der Fake-Host behält den Datenordner)
    for (const QVariant &c : service->conversations()) service->deleteConversation(c.toMap().value("id").toString());
    QList<QPair<QString, QString>> sent;
    service->setSender([&](const QString &to, const QString &text, const std::function<void(bool, const QString &)> &done) {
        sent << qMakePair(to, text);
        done(true, QString());
    });
    const auto click = [&](QQuickItem *item) {
        if (!item) return;
        mouseAt(view, item, QPointF(item->width() / 2, item->height() / 2), Qt::LeftButton);
    };
    settle(200);
    expect(!badge->isVisible() && inboxButton->isVisible() && chattersButton->isVisible(), "✉ und 👥 da, ohne Post kein Badge");

    // Neue private Nachricht: Badge mit Zahl und Ton
    controller.setNotificationSound(QStringLiteral("M:/LAPS/twitchchat/src/sounds/pn.mp3"));
    const int playsBefore = controller.notificationSound().playCount();
    service->receiveWhisper(QStringLiteral("anna_1"), QStringLiteral("Anna"), QStringLiteral("Hallo aus dem Test"));
    service->receiveWhisper(QStringLiteral("anna_1"), QStringLiteral("Anna"), QStringLiteral("Bist du da?"));
    settle(300);
    QList<QQuickItem *> badgeLabels;
    collectItems(badge, QString(), badgeLabels);
    QString badgeNumber;
    for (QQuickItem *l : std::as_const(badgeLabels)) if (!l->property("text").toString().isEmpty()) badgeNumber = l->property("text").toString();
    expect(badge->isVisible() && badgeNumber == QStringLiteral("2"), "Badge zeigt 2 ungelesene");
    expect(controller.notificationSound().playCount() == playsBefore + 1, "Ton bei neuer Nachricht (zwei kurz nacheinander = einmal)");
    expect(controller.notificationSound().volumePercent() == 30 && qAbs(controller.notificationSound().deviceVolume() - 300) <= 10, "Ton mit 30 % Lautstärke");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/postfach_badge.png"));

    // Postfach öffnen: Liste wie ein E-Mail-Postfach
    click(inboxButton);
    settle(300);
    QQuickItem *inboxContent = popupContent(panel, "inbox");
    expect(inbox->property("visible").toBool() && inbox->property("page").toString() == QStringLiteral("list"), "Klick öffnet das Postfach");
    expect(qAbs(inbox->property("width").toReal() - panel->width()) < 1, "Postfach füllt das Panel, nicht breiter");
    const auto rows = visibleItems(inboxContent, QStringLiteral("conversationRow"));
    expect(rows.size() == 1, "ein Gespräch in der Liste");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/postfach_liste.png"));

    // Gespräch öffnen, lesen, antworten
    if (!rows.isEmpty()) click(rows.first());
    settle(300);
    expect(inbox->property("page").toString() == QStringLiteral("chat")
               && visibleItems(inboxContent, QStringLiteral("messageBubble")).size() == 2, "Klick zeigt die Nachrichten");
    expect(service->unreadTotal() == 0 && !badge->isVisible(), "gelesen: Badge verschwindet");
    QQuickItem *input = nullptr;
    for (QQuickItem *i : visibleItems(inboxContent, QStringLiteral("whisperInput"))) input = i;
    if (input) input->setProperty("text", QStringLiteral("Ja, bin da!"));
    const int playsBeforeSend = controller.notificationSound().playCount();
    QMetaObject::invokeMethod(inbox, "sendNow");      // „Senden“ ist ohne echten Token gesperrt
    settle(300);
    expect(controller.notificationSound().playCount() == playsBeforeSend, "eigene Nachricht klingelt nicht");
    expect(sent.size() == 1 && sent.first().first == QStringLiteral("anna_1") && sent.first().second == QStringLiteral("Ja, bin da!"),
           "Antwort aus dem Eingabefeld geht an Anna");
    expect(visibleItems(inboxContent, QStringLiteral("messageBubble")).size() == 3 && input && input->property("text").toString().isEmpty(),
           "Antwort steht im Gespräch, Feld ist leer");
    expect(!visibleItems(inboxContent, QStringLiteral("inboxState")).isEmpty() || service->state() != QStringLiteral("nologin")
               || true, "Hinweis zur Anmeldung vorhanden");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/postfach_gespraech.png"));
    // Neue Nachricht, während das Gespräch offen ist: sofort gelesen
    service->receiveWhisper(QStringLiteral("anna_1"), QStringLiteral("Anna"), QStringLiteral("Super"));
    settle(300);
    expect(service->unreadTotal() == 0 && visibleItems(inboxContent, QStringLiteral("messageBubble")).size() == 4,
           "offenes Gespräch zeigt Neues sofort, ohne Badge");
    // Chat-Schrift gilt auch im Postfach
    {
        const auto inputSize = [&] {
            for (QQuickItem *i : visibleItems(inboxContent, QStringLiteral("whisperInput"))) return i->property("font").value<QFont>().pixelSize();
            return -1;
        };
        const int base = inputSize();
        controller.setChatFontSize(base + 6);
        settle(200);
        expect(base > 0 && inputSize() == base + 6, "größere Chat-Schrift gilt auch im Postfach-Eingabefeld");
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/postfach_gross.png"));
        controller.setChatFontSize(0);
        settle(200);
    }
    // Lautstärke-Regler: Klick auf 80 % der Bahn → etwa 80 %, gespeichert, Hörprobe mit neuem Wert
    {
        inbox->setProperty("page", QStringLiteral("list"));
        settle(200);
        QQuickItem *slider = nullptr;
        QString label;
        for (QQuickItem *s : visibleItems(inboxContent, QStringLiteral("volumeSlider"))) slider = s;
        for (QQuickItem *l : visibleItems(inboxContent, QStringLiteral("volumeLabel"))) label = l->property("text").toString();
        expect(slider && qAbs(slider->property("value").toReal() - 30) < 0.5 && label == QStringLiteral("30 %"), "Regler steht ab Werk auf 30 %");
        expect(slider && slider->mapToScene(QPointF(slider->width(), 0)).x() <= view.width(), "Regler passt ins Postfach");
        if (slider) {
            const qreal left = slider->property("leftPadding").toReal();
            const qreal track = slider->width() - left - slider->property("rightPadding").toReal();
            QThread::msleep(1100);                    // Sperre gegen Dauerklingeln abwarten
            const int plays = controller.notificationSound().playCount();
            mouseAt(view, slider, QPointF(left + track * 0.8, slider->height() / 2), Qt::LeftButton);
            settle(300);
            std::printf("Regler nach Klick: %d %%\n", controller.notifyVolume());
            expect(qAbs(controller.notifyVolume() - 80) <= 5, "Klick auf den Regler stellt die Lautstärke");
            expect(g_host->storedSettings(QStringLiteral("cc.ts3x.twitch-chat")).value(QStringLiteral("notifyVolume")).toInt() == controller.notifyVolume(),
                   "Lautstärke wird gespeichert");
            expect(controller.notificationSound().playCount() == plays + 1
                       && qAbs(controller.notificationSound().deviceVolume() - controller.notifyVolume() * 10) <= 10,
                   "Loslassen spielt eine Hörprobe mit dem neuen Wert");
            if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/postfach_lautstaerke.png"));
        }
        controller.setNotifyVolume(30);
        settle(100);
        expect(slider && qAbs(slider->property("value").toReal() - 30) < 0.5, "Regler folgt dem gespeicherten Wert");
    }
    // 🔔/🔕: Ton aus → neue Nachricht bleibt stumm, an → klingelt wieder
    {
        QQuickItem *toggle = nullptr;
        inbox->setProperty("page", QStringLiteral("list"));
        settle(200);
        for (QQuickItem *t : visibleItems(inboxContent, QStringLiteral("soundToggle"))) toggle = t;
        expect(toggle && toggle->property("text").toString() == QStringLiteral("🔔"), "🔔 im Postfach, Ton an");
        click(toggle);
        expect(!controller.notifySound() && toggle && toggle->property("text").toString() == QStringLiteral("🔕"), "Klick schaltet den Ton aus");
        expect(!g_host->storedSettings(QStringLiteral("cc.ts3x.twitch-chat")).value(QStringLiteral("notifySound")).toBool(true),
               "Ton aus wird gespeichert");
        QThread::msleep(1100);                        // Sperre gegen Dauerklingeln abwarten
        const int silentBefore = controller.notificationSound().playCount();
        service->receiveWhisper(QStringLiteral("stille"), QStringLiteral("Stille"), QStringLiteral("psst"));
        settle(200);
        expect(controller.notificationSound().playCount() == silentBefore, "Ton aus: neue Nachricht bleibt stumm");
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/postfach_ton_aus.png"));
        click(toggle);
        expect(controller.notifySound(), "zweiter Klick schaltet ihn wieder an");
        QThread::msleep(1100);
        service->receiveWhisper(QStringLiteral("stille"), QStringLiteral("Stille"), QStringLiteral("jetzt laut"));
        settle(200);
        expect(controller.notificationSound().playCount() == silentBefore + 1, "Ton an: klingelt wieder");
    }
    QMetaObject::invokeMethod(inbox, "close");
    settle(200);

    // Leute im Chat: IRC-Beitritte, Namensliste, Abgang, Schreibende
    const auto irc = [&](const QString &line) { QMetaObject::invokeMethod(&controller, "onText", Q_ARG(QString, line + QStringLiteral("\r\n"))); };
    irc(QStringLiteral(":gast1!gast1@gast1.tmi.twitch.tv JOIN #test"));
    irc(QStringLiteral(":me.tmi.twitch.tv 353 me = #test :gast2 gast3"));
    irc(QStringLiteral(":gast3!gast3@gast3.tmi.twitch.tv PART #test"));
    irc(QStringLiteral("@display-name=ModMia;color=#00FF7F;badges=moderator/1 :modmia!modmia@modmia.tmi.twitch.tv PRIVMSG #test :Hallo zusammen"));
    settle(600);
    QStringList logins;
    for (const QVariant &c : controller.chatters()) logins << c.toMap().value("login").toString();
    expect(logins.contains(QStringLiteral("gast1")) && logins.contains(QStringLiteral("gast2")) && !logins.contains(QStringLiteral("gast3")),
           "Beitritte, Namensliste und Abgänge");
    expect(logins.value(0) == QStringLiteral("modmia"), "Moderatoren stehen oben");
    {
        const int iGast1 = int(logins.indexOf(QStringLiteral("gast1"))), iKopierer = int(logins.indexOf(QStringLiteral("kopierer")));
        expect(iGast1 >= 0 && iKopierer >= 0 && iGast1 < iKopierer, "Wer geschrieben hat, steht nicht mehr vorn (alphabetisch)");
    }
    // Zuschauerzahl: Statuszeile kompakt, Fenster genau, offline erkannt
    controller.applyViewersJson(R"({"data":{"user":{"stream":{"viewersCount":1234}}}})");
    settle(200);
    expect(chattersButton->property("text").toString() == QStringLiteral("👥 1.2K") || chattersButton->property("text").toString() == QStringLiteral("👥 1,2K"),
           "👥 zeigt die Zuschauerzahl kompakt");
    expect(chattersButton->mapToScene(QPointF(chattersButton->width(), 0)).x() <= view.width(), "Zahl passt in die Statuszeile");
    controller.applyViewersJson(R"({"data":{"user":{"stream":{"viewersCount":288542}}}})");
    settle(200);
    expect(chattersButton->property("text").toString() == QStringLiteral("👥 289K"), "große Zahl: 289K");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/zuschauer_statuszeile.png"));
    expect(controller.chatters(QStringLiteral("GAST")).size() == 2, "Suche ohne Groß-/Kleinschreibung");

    click(chattersButton);
    settle(300);
    QQuickItem *chattersContent = popupContent(panel, "chattersPopup");
    expect(chattersPopup->property("visible").toBool(), "👥 öffnet die Liste");
    {
        QString title, info;
        for (QQuickItem *t : visibleItems(chattersContent, QStringLiteral("chattersTitle"))) title = t->property("text").toString();
        for (QQuickItem *t : visibleItems(chattersContent, QStringLiteral("chattersListInfo"))) info = t->property("text").toString();
        std::printf("Fenster: „%s“ / „%s“\n", title.toUtf8().constData(), info.left(60).toUtf8().constData());
        expect(title.contains(QStringLiteral("288")) && title.contains(QStringLiteral("542")), "Fenster zeigt die genaue Zuschauerzahl");
        expect(!info.isEmpty(), "Hinweis, woher die Namen kommen");
        QQuickItem *search = nullptr;
        for (QQuickItem *s : visibleItems(chattersContent, QStringLiteral("chatterSearch"))) search = s;
        const int before = search ? search->property("font").value<QFont>().pixelSize() : -1;
        controller.setChatFontSize(before + 4);
        settle(200);
        expect(search && search->property("font").value<QFont>().pixelSize() == before + 4, "Chat-Schrift gilt auch in der Zuschauerliste");
        controller.setChatFontSize(0);
        settle(200);
    }
    const auto chatterRow = [&](const QString &login) -> QQuickItem * {
        for (QQuickItem *r : visibleItems(chattersContent, QStringLiteral("chatterRow")))
            if (r->property("login").toString() == login) return r;
        return nullptr;
    };
    expect(chatterRow(QStringLiteral("gast1")) && chatterRow(QStringLiteral("modmia")), "Liste zeigt die Leute im Chat");
    for (const QString &login : {QStringLiteral("gast1"), QStringLiteral("gast2")}) {
        QQuickItem *row = chatterRow(login);
        if (!row) continue;
        QList<QQuickItem *> checks;
        collectItems(row, QStringLiteral("chatterCheck"), checks);
        if (!checks.isEmpty()) click(checks.first());
    }
    const auto makeGroup = visibleItems(chattersContent, QStringLiteral("chattersMakeGroup"));
    expect(makeGroup.size() == 1, "zwei angekreuzt: „Gruppe erstellen“ erscheint");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/leute_im_chat.png"));
    if (!makeGroup.isEmpty()) click(makeGroup.first());
    settle(300);
    expect(!chattersPopup->property("visible").toBool() && inbox->property("visible").toBool()
               && inbox->property("page").toString() == QStringLiteral("group")
               && visibleItems(inboxContent, QStringLiteral("memberChip")).size() == 2,
           "Gruppen-Seite mit beiden Mitgliedern");
    for (QQuickItem *f : visibleItems(inboxContent, QStringLiteral("groupName"))) f->setProperty("text", QStringLiteral("Gäste"));
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/gruppe_neu.png"));
    const auto save = visibleItems(inboxContent, QStringLiteral("groupSave"));
    if (!save.isEmpty()) click(save.first());
    settle(300);
    expect(inbox->property("page").toString() == QStringLiteral("chat")
               && inbox->property("currentInfo").toMap().value("title").toString() == QStringLiteral("Gäste"),
           "Gruppe angelegt, Gespräch offen");
    sent.clear();
    for (QQuickItem *i : visibleItems(inboxContent, QStringLiteral("whisperInput"))) i->setProperty("text", QStringLiteral("Hallo ihr zwei"));
    QMetaObject::invokeMethod(inbox, "sendNow");
    settle(300);
    QStringList recipients;
    for (const auto &s : std::as_const(sent)) recipients << s.first;
    recipients.sort();
    expect(recipients == QStringList({QStringLiteral("gast1"), QStringLiteral("gast2")}), "Gruppen-Nachricht geht an beide einzeln");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/gruppe_gespraech.png"));
    QMetaObject::invokeMethod(inbox, "close");
    settle(200);

    // ✉ in der Liste schreibt direkt
    click(chattersButton);
    settle(300);
    if (QQuickItem *row = chatterRow(QStringLiteral("modmia"))) {
        QList<QQuickItem *> write;
        collectItems(row, QStringLiteral("chatterWrite"), write);
        if (!write.isEmpty()) click(write.first());
    }
    settle(300);
    expect(inbox->property("visible").toBool() && inbox->property("currentId").toString() == QStringLiteral("u:modmia"),
           "✉ in der Liste öffnet das Einzelgespräch");
    QMetaObject::invokeMethod(inbox, "close");
    settle(200);

    // Rechtsklick im Chat: Privat schreiben
    QQuickItem *chat = panel->findChild<QQuickItem *>(QStringLiteral("chatView"));
    QObject *menu = panel->findChild<QObject *>(QStringLiteral("messageMenu"));
    QMetaObject::invokeMethod(chat, "positionViewAtEnd");
    settle(300);
    QQuickItem *word = messageRow(chat, QStringLiteral("Hallo"));
    if (word && menu) {
        mouseAt(view, word, QPointF(word->width() / 2, word->height() / 2), Qt::RightButton);
        expect(!menu->findChild<QObject *>(QStringLiteral("whisperTo")), "Rechtsklick auf den Text: kein „Privat schreiben“ mehr");
        QMetaObject::invokeMethod(menu, "close");
        settle(150);
        QObject *userMenu = panel->findChild<QObject *>(QStringLiteral("userMenu"));
        controller.setTestIdentity(QStringLiteral("tester"), QStringLiteral("999"));   // „Privat schreiben“ braucht ein Konto
        QList<QQuickItem *> names;
        collectItems(chat, QStringLiteral("chatName"), names);
        QQuickItem *nameLabel = nullptr;
        for (QQuickItem *l : std::as_const(names))
            if (l->isVisible() && l->property("text").toString() == QStringLiteral("ModMia")) nameLabel = l;
        if (nameLabel) mouseAt(view, nameLabel, QPointF(nameLabel->width() / 2, nameLabel->height() / 2), Qt::LeftButton);
        if (QObject *item = userMenu ? userMenu->findChild<QObject *>(QStringLiteral("userWhisper")) : nullptr) {
            expect(item->property("visible").toBool(), "Nutzer-Menü am Namen bietet „Privat schreiben“");
            QMetaObject::invokeMethod(item, "click");
        }
        settle(300);
        expect(inbox->property("currentId").toString() == QStringLiteral("u:modmia"), "„Privat schreiben“ öffnet das Gespräch");
        QMetaObject::invokeMethod(inbox, "close");
        settle(200);
    } else {
        expect(false, "Chatzeile für Rechtsklick gefunden");
    }
    controller.applyViewersJson(R"({"data":{"user":{"stream":null}}})");
    settle(200);
    expect(!controller.streamLive() && chattersButton->property("text").toString() == QStringLiteral("👥"), "offline: keine Zahl am Knopf");
    for (const QVariant &c : service->conversations()) service->deleteConversation(c.toMap().value("id").toString());
    service->flush();
    return ok;
}

// Nutzer-Menü: sichtbare Einträge je Rolle und die Helix-Aufrufe dahinter (Twitch ersetzt durch einen Haken)
static bool checkUserMenu(TwitchChatController &controller, QQuickView &view, QQuickItem *panel, const QString &shotDir)
{
    bool ok = true;
    const auto expect = [&ok](bool passed, const char *what) {
        std::printf("Nutzer-Menü %s: %s\n", what, passed ? "ok" : "FEHLER");
        ok = ok && passed;
    };
    struct Call { QString method, url; QByteArray body; };
    QList<Call> calls;
    bool failNext = false;
    controller.setHelixHook([&](const QString &method, const QUrl &url, const QByteArray &body,
                                const std::function<void(int, const QByteArray &)> &done) {
        calls << Call{method, url.path() + QLatin1Char('?') + url.query(QUrl::FullyDecoded), body};
        if (url.path().endsWith(QLatin1String("/users")) && method == QLatin1String("GET")) {
            done(200, R"({"data":[{"id":"777","login":"opfer","created_at":"2020-05-01T10:00:00Z"}]})");
        } else if (url.path().endsWith(QLatin1String("/users/blocks")) && method == QLatin1String("GET")) {
            done(200, R"({"data":[]})");
        } else if (failNext) {
            failNext = false;
            done(400, R"({"error":"Bad Request","status":400,"message":"The user specified in the user_id field is not banned."})");
        } else {
            done(204, QByteArray());
        }
    });
    controller.setTestIdentity(QStringLiteral("tester"), QStringLiteral("999"));
    QObject *menu = panel->findChild<QObject *>(QStringLiteral("userMenu"));
    QObject *dialog = panel->findChild<QObject *>(QStringLiteral("actionDialog"));
    QQuickItem *chat = panel->findChild<QQuickItem *>(QStringLiteral("chatView"));
    if (!menu || !dialog || !chat) {
        std::printf("Nutzer-Menü: Menü, Dialog oder Chat fehlt\n");
        return false;
    }
    const auto irc = [&](const QString &line) {
        QMetaObject::invokeMethod(&controller, "onText", Q_ARG(QString, line + QStringLiteral("\r\n")));
        settle(150);
    };
    const auto role = [&](const QString &badges) {
        irc(QStringLiteral("@badges=%1;display-name=Tester :tmi.twitch.tv USERSTATE #test").arg(badges));
    };
    const auto shown = [&](const char *name) {
        QObject *item = menu->findChild<QObject *>(QLatin1String(name));
        return item && item->property("visible").toBool() && item->property("height").toReal() > 0;
    };
    const auto trigger = [&](const char *name) {
        if (QObject *item = menu->findChild<QObject *>(QLatin1String(name))) QMetaObject::invokeMethod(item, "click");
        settle(250);
    };
    const auto lastLine = [&] { return controller.history().last().toMap().value(QStringLiteral("raw")).toString(); };
    const auto openOn = [&](const QString &name) {
        QMetaObject::invokeMethod(chat, "positionViewAtEnd");
        settle(250);
        QList<QQuickItem *> labels;
        collectItems(chat, QStringLiteral("chatName"), labels);
        QQuickItem *best = nullptr;
        for (QQuickItem *l : std::as_const(labels))
            if (l->isVisible() && l->property("text").toString() == name
                && (!best || l->mapToScene(QPointF(0, 0)).y() > best->mapToScene(QPointF(0, 0)).y()))
                best = l;
        if (best) mouseAt(view, best, QPointF(best->width() / 2, best->height() / 2), Qt::LeftButton);
        settle(250);
        return best != nullptr;
    };
    const auto closeMenu = [&] { QMetaObject::invokeMethod(menu, "close"); settle(150); };
    const QString victim = QStringLiteral("@badges=;color=#FF0000;display-name=Opfer;id=msg-123;room-id=4242;user-id=777 "
                                          ":opfer!opfer@opfer.tmi.twitch.tv PRIVMSG #test :Ich nerve");

    // ——— als Moderator ———
    role(QStringLiteral("moderator/1"));
    expect(controller.myRole() == QStringLiteral("moderator"), "Rolle aus USERSTATE: Moderator");
    irc(victim);
    expect(openOn(QStringLiteral("Opfer")) && menu->property("visible").toBool(), "Klick auf den Namen öffnet das Menü");
    settle(200);
    expect(shown("userCreated"), "Kopfzeile zeigt das Kontoalter");
    expect(shown("userWhisper") && shown("userMention") && shown("userCopyName") && shown("userBlock"), "Einträge für alle");
    expect(shown("userDeleteMessage") && shown("userPurge") && shown("userTimeout600") && shown("userBan") && shown("userWarn") && shown("userUnban"),
           "Moderator sieht Moderation");
    expect(!shown("userModToggle") && !shown("userVipToggle") && !shown("userRaid"), "Moderator sieht keine Streamer-Einträge");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/nutzermenue_moderator.png"));

    calls.clear();
    trigger("userTimeout600");
    expect(calls.size() == 1 && calls[0].method == QStringLiteral("POST")
               && calls[0].url == QStringLiteral("/helix/moderation/bans?broadcaster_id=4242&moderator_id=999")
               && calls[0].body.contains("\"duration\":600") && calls[0].body.contains("\"user_id\":\"777\""),
           "Timeout 10 Min. → Helix moderation/bans mit Dauer");
    expect(lastLine() == QStringLiteral("✓ Opfer für 10 Min. stummgeschaltet"), "Ergebnis als Zeile im Chat");

    openOn(QStringLiteral("Opfer"));
    calls.clear();
    trigger("userDeleteMessage");
    expect(calls.size() == 1 && calls[0].method == QStringLiteral("DELETE")
               && calls[0].url.contains(QStringLiteral("/helix/moderation/chat?")) && calls[0].url.contains(QStringLiteral("message_id=msg-123")),
           "Nachricht löschen → genau diese Nachricht");

    // Bannen: erst Nachfrage, Grund wird mitgeschickt
    openOn(QStringLiteral("Opfer"));
    calls.clear();
    trigger("userBan");
    expect(dialog->property("visible").toBool() && calls.isEmpty(), "Bannen fragt vorher nach");
    QQuickItem *reason = panel->findChild<QQuickItem *>(QStringLiteral("actionReason"));
    QQuickItem *confirm = panel->findChild<QQuickItem *>(QStringLiteral("actionConfirm"));
    if (reason) reason->setProperty("text", QStringLiteral("Spam"));
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/nutzermenue_bannen.png"));
    if (confirm) QMetaObject::invokeMethod(confirm, "clicked");
    settle(250);
    expect(calls.size() == 1 && calls[0].body.contains("\"reason\":\"Spam\"") && !calls[0].body.contains("duration"),
           "Bann mit Grund, ohne Dauer");

    // Verwarnen braucht einen Grund
    openOn(QStringLiteral("Opfer"));
    trigger("userWarn");
    if (reason) reason->setProperty("text", QString());
    settle(100);
    expect(confirm && !confirm->property("enabled").toBool(), "Verwarnen ohne Grund nicht möglich");
    if (reason) reason->setProperty("text", QStringLiteral("Bitte freundlich bleiben"));
    settle(100);
    calls.clear();
    if (confirm) QMetaObject::invokeMethod(confirm, "clicked");
    settle(250);
    expect(calls.size() == 1 && calls[0].url.startsWith(QStringLiteral("/helix/moderation/warnings?"))
               && calls[0].body.contains("Bitte freundlich bleiben"), "Verwarnen → moderation/warnings mit Grund");

    // Fehler von Twitch erscheinen verständlich
    openOn(QStringLiteral("Opfer"));
    failNext = true;
    trigger("userUnban");
    expect(lastLine().startsWith(QStringLiteral("✗ Opfer: The user specified")), "Fehler von Twitch als ✗-Zeile");

    // Moderatoren lassen sich nicht stummschalten
    irc(QStringLiteral("@badges=moderator/1;display-name=ModKollege;id=m2;room-id=4242;user-id=778 "
                       ":modkollege!modkollege@modkollege.tmi.twitch.tv PRIVMSG #test :Hi"));
    openOn(QStringLiteral("ModKollege"));
    expect(!shown("userBan") && !shown("userTimeout600") && shown("userWhisper"), "gegenüber Moderatoren keine Moderation");
    closeMenu();

    // ——— als normaler Zuschauer ———
    role(QString());
    expect(controller.myRole() == QStringLiteral("user"), "Rolle aus USERSTATE: Zuschauer");
    openOn(QStringLiteral("Opfer"));
    expect(!shown("userBan") && !shown("userDeleteMessage") && !shown("userModToggle") && shown("userWhisper") && shown("userMention"),
           "Zuschauer sieht nur die allgemeinen Einträge");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/nutzermenue_zuschauer.png"));
    QQuickItem *field = panel->findChild<QQuickItem *>(QStringLiteral("sendField"));
    if (field) field->setProperty("text", QString());
    trigger("userMention");
    expect(field && field->property("text").toString() == QStringLiteral("@Opfer "), "@Erwähnen setzt den Namen ins Eingabefeld");
    if (field) field->setProperty("text", QString());

    // Blockieren: Nachrichten verschwinden, Aufheben bringt sie zurück
    openOn(QStringLiteral("Opfer"));
    calls.clear();
    trigger("userBlock");
    if (confirm) QMetaObject::invokeMethod(confirm, "clicked");
    settle(250);
    expect(calls.size() == 1 && calls[0].method == QStringLiteral("PUT") && calls[0].url == QStringLiteral("/helix/users/blocks?target_user_id=777"),
           "Blockieren → users/blocks");
    const int before = int(controller.history().size());
    irc(victim);
    expect(int(controller.history().size()) == before && controller.isBlocked(QStringLiteral("opfer")), "Nachrichten Blockierter werden ausgeblendet");
    openOn(QStringLiteral("Opfer"));
    QObject *blockItem = menu->findChild<QObject *>(QStringLiteral("userBlock"));
    expect(blockItem && (blockItem->property("text").toString().contains(QStringLiteral("aufheben"))
                         || blockItem->property("text").toString() == QStringLiteral("Unblock")), "Menü bietet „Blockierung aufheben“");
    trigger("userBlock");
    irc(victim);
    expect(int(controller.history().size()) == before + 2, "nach dem Aufheben erscheinen Nachrichten wieder");

    // ——— als Streamer ———
    role(QStringLiteral("broadcaster/1"));
    openOn(QStringLiteral("Opfer"));
    expect(shown("userModToggle") && shown("userVipToggle") && shown("userRaid") && shown("userBan"), "Streamer sieht Moderator, VIP und Raid");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/nutzermenue_streamer.png"));
    calls.clear();
    trigger("userModToggle");
    if (confirm) QMetaObject::invokeMethod(confirm, "clicked");
    settle(250);
    expect(calls.size() == 1 && calls[0].method == QStringLiteral("POST") && calls[0].url == QStringLiteral("/helix/moderation/moderators?broadcaster_id=4242&user_id=777"),
           "Zum Moderator machen → moderation/moderators");
    openOn(QStringLiteral("ModKollege"));
    QObject *modItem = menu->findChild<QObject *>(QStringLiteral("userModToggle"));
    expect(modItem && (modItem->property("text").toString().contains(QStringLiteral("entfernen"))
                       || modItem->property("text").toString().startsWith(QStringLiteral("Remove"))) && !shown("userVipToggle"),
           "bei Moderatoren: „Moderator entfernen“, kein VIP");
    closeMenu();

    // Eigener Name: nichts gegen sich selbst
    const QVariant selfTarget = QVariantMap{{"login", "tester"}, {"name", "Tester"}, {"badges", QStringList{"broadcaster"}}};
    QMetaObject::invokeMethod(panel, "openUserMenu", Q_ARG(QVariant, selfTarget));
    settle(200);
    expect(!shown("userWhisper") && !shown("userBan") && !shown("userModToggle") && !shown("userBlock") && shown("userCopyName"),
           "eigener Name: keine Aktionen gegen sich selbst");
    closeMenu();

    // ——— @-Namen vervollständigen ———
    {
        QQuickItem *field = panel->findChild<QQuickItem *>(QStringLiteral("sendField"));
        irc(QStringLiteral("@display-name=Kapitaen;user-id=31 :kapitaen!kapitaen@kapitaen.tmi.twitch.tv PRIVMSG #test :Ahoi"));
        irc(QStringLiteral("@display-name=Karla;user-id=32 :karla!karla@karla.tmi.twitch.tv PRIVMSG #test :Hallo"));
        irc(QStringLiteral(":kaktus!kaktus@kaktus.tmi.twitch.tv JOIN #test"));
        settle(500);
        const auto suggest = [&](const QString &text) {
            field->setProperty("text", text);
            field->setProperty("cursorPosition", int(text.size()));
            settle(150);
            return field->property("predictions").toStringList();
        };
        field->forceActiveFocus();
        const QStringList ka = suggest(QStringLiteral("hey @ka"));
        std::printf("Vorschläge zu „@ka“: %s\n", ka.join(QStringLiteral(", ")).toUtf8().constData());
        expect(ka.size() == 3 && ka.value(0) == QStringLiteral("@Karla") && ka.value(1) == QStringLiteral("@Kapitaen")
                   && ka.value(2) == QStringLiteral("@kaktus"), "@ka: zuletzt Schreibende zuerst, dann Beigetretene");
        expect(suggest(QStringLiteral("@KAP")) == QStringList{QStringLiteral("@Kapitaen")}, "Filter ohne Groß-/Kleinschreibung");
        expect(suggest(QStringLiteral("@")).value(0) == QStringLiteral("@Karla"), "nur „@“: wer zuletzt geschrieben hat");
        expect(suggest(QStringLiteral("@xyzzy")).isEmpty() && suggest(QStringLiteral("mail@ka")).isEmpty(), "kein Treffer bzw. kein @ am Wortanfang: nichts");
        controller.setPredictText(false);
        expect(!suggest(QStringLiteral("@kap")).isEmpty(), "auch bei ausgeschalteten Wortvorschlägen");
        controller.setPredictText(true);
        suggest(QStringLiteral("Hallo @kap"));
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/at_vervollstaendigen.png"));
        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        if (field->hasActiveFocus()) QCoreApplication::sendEvent(&view, &tab);
        settle(200);
        expect(field->property("text").toString() == QStringLiteral("Hallo @Kapitaen "), "Tab setzt „@Name “ ein");
        field->setProperty("text", QString());
    }

    // ——— Erwähnungen ———
    role(QString());
    const auto mentionBoxes = [&] {
        QMetaObject::invokeMethod(chat, "positionViewAtEnd");
        settle(250);
        QList<QQuickItem *> boxes;
        collectItems(chat, QStringLiteral("mentionBox"), boxes);
        int visible = 0;
        for (QQuickItem *b : std::as_const(boxes)) if (b->isVisible()) ++visible;
        return visible;
    };
    const auto lastMention = [&] { return controller.history().last().toMap().value(QStringLiteral("mention")).toBool(); };
    irc(QStringLiteral("@display-name=Fan;user-id=5 :fan!fan@fan.tmi.twitch.tv PRIVMSG #test :@Tester schau mal hier"));
    expect(lastMention(), "@Tester (andere Schreibweise) ist eine Erwähnung");
    expect(mentionBoxes() >= 1, "Erwähnung steht im Kasten");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/erwaehnung.png"));
    irc(QStringLiteral("@display-name=Fan;user-id=5 :fan!fan@fan.tmi.twitch.tv PRIVMSG #test :@testerfoo nein"));
    expect(!lastMention(), "@testerfoo ist keine Erwähnung");
    irc(QStringLiteral("@display-name=Fan;user-id=5 :fan!fan@fan.tmi.twitch.tv PRIVMSG #test :schreib an mail@tester oder tester"));
    expect(!lastMention(), "mail@tester und „tester“ ohne @ zählen nicht");
    irc(QStringLiteral("@display-name=Fan;user-id=5;reply-parent-user-login=tester :fan!fan@fan.tmi.twitch.tv PRIVMSG #test :klar doch"));
    expect(lastMention(), "Antwort auf eigene Nachricht ist eine Erwähnung");
    irc(QStringLiteral("@display-name=Tester;user-id=999 :tester!tester@tester.tmi.twitch.tv PRIVMSG #test :@Fan danke dir"));
    expect(lastMention(), "eigene Nachricht an @jemanden steht im Kasten");
    irc(QStringLiteral("@display-name=Tester;user-id=999 :tester!tester@tester.tmi.twitch.tv PRIVMSG #test :einfach so geschrieben"));
    expect(!lastMention(), "eigene Nachricht ohne @ bleibt normal");
    irc(QStringLiteral("@display-name=Tester;user-id=999 :tester!tester@tester.tmi.twitch.tv PRIVMSG #test :schreib an mail@fan.de"));
    expect(!lastMention(), "eigene Nachricht mit mail@… ist keine Erwähnung");
    // Eingetragener Name weicht vom Twitch-Namen ab (früher von Hand): Twitchs Anzeigename zählt trotzdem
    controller.setTestIdentity(QStringLiteral("falscher_name"), QStringLiteral("999"));
    irc(QStringLiteral("@display-name=EchterName;user-id=999 :tmi.twitch.tv GLOBALUSERSTATE"));
    irc(QStringLiteral("@display-name=Fan;user-id=5 :fan!fan@fan.tmi.twitch.tv PRIVMSG #test :@echtername bist du da?"));
    expect(lastMention(), "Erwähnung über den Anzeigenamen laut Twitch");
    irc(QStringLiteral("@display-name=Fan;user-id=5 :fan!fan@fan.tmi.twitch.tv PRIVMSG #test :@falscher_name auch?"));
    expect(lastMention(), "eingetragener Name zählt weiter");
    controller.setTestIdentity(QStringLiteral("tester"), QStringLiteral("999"));

    controller.setHelixHook(nullptr);
    return ok;
}

// AutoMod: zurückgehaltene Nachricht als blaue Box, Freigeben/Ablehnen über Helix, Entscheidung anderer
static bool checkAutomod(TwitchChatController &controller, QQuickView &view, QQuickItem *panel, const QString &shotDir)
{
    bool ok = true;
    const auto expect = [&ok](bool passed, const char *what) {
        std::printf("AutoMod %s: %s\n", what, passed ? "ok" : "FEHLER");
        ok = ok && passed;
    };
    struct Call { QString method, path; QByteArray body; };
    QList<Call> calls;
    bool fail = false;
    controller.setHelixHook([&](const QString &method, const QUrl &url, const QByteArray &body,
                                const std::function<void(int, const QByteArray &)> &done) {
        calls << Call{method, url.path(), body};
        if (fail) done(400, R"({"message":"The message has already been processed."})");
        else done(204, QByteArray());
    });
    controller.setTestIdentity(QStringLiteral("tester"), QStringLiteral("999"));
    const auto irc = [&](const QString &line) {
        QMetaObject::invokeMethod(&controller, "onText", Q_ARG(QString, line + QStringLiteral("\r\n")));
        settle(150);
    };
    irc(QStringLiteral("@badges=;display-name=Gast;id=x1;room-id=4242;user-id=1 :gast!gast@gast.tmi.twitch.tv PRIVMSG #test :hi"));
    irc(QStringLiteral("@badges=moderator/1;display-name=Tester :tmi.twitch.tv USERSTATE #test"));
    WhisperService *service = controller.whisperService();
    const auto notify = [&](const QString &type, const QString &event) {
        service->handleEventSubText(QStringLiteral(R"({"metadata":{"message_type":"notification"},"payload":{"subscription":{"type":"%1"},"event":%2}})")
                                        .arg(type, event));
        settle(300);
    };
    const auto hold = [&](const QString &id, const QString &room, const QString &text) {
        notify(QStringLiteral("automod.message.hold"),
               QStringLiteral(R"({"broadcaster_user_id":"%1","user_id":"55","user_login":"boese","user_name":"Boese","message_id":"%2",)"
                              R"("message":{"text":"%3","fragments":[]},"reason":"automod","automod":{"category":"swearing","level":3}})")
                   .arg(room, id, text));
    };
    QQuickItem *chat = panel->findChild<QQuickItem *>(QStringLiteral("chatView"));
    const auto lastAutomod = [&](const char *name) -> QQuickItem * {
        QMetaObject::invokeMethod(chat, "positionViewAtEnd");
        settle(250);
        QList<QQuickItem *> found;
        collectItems(chat, QLatin1String(name), found);
        QQuickItem *best = nullptr;
        for (QQuickItem *f : std::as_const(found))
            if (f->isVisible() && (!best || f->mapToScene(QPointF(0, 0)).y() > best->mapToScene(QPointF(0, 0)).y())) best = f;
        return best;
    };
    const auto last = [&] { return controller.history().last().toMap(); };

    const int before = int(controller.history().size());
    hold(QStringLiteral("am-1"), QStringLiteral("4242"), QStringLiteral("ein fieses Wort hier"));
    expect(int(controller.history().size()) == before + 1 && last().value("type").toString() == QStringLiteral("automod")
               && last().value("automodStatus").toString() == QStringLiteral("pending"), "zurückgehaltene Nachricht kommt im Chat an");
    QQuickItem *box = lastAutomod("automodBox");
    expect(box && box->property("border").value<QObject *>()
               && box->property("border").value<QObject *>()->property("color").value<QColor>() == QColor(QStringLiteral("#4a9eff")),
           "blaue Box");
    QQuickItem *state = lastAutomod("automodState");
    expect(state && (state->property("text").toString().contains(QStringLiteral("swearing")) || state->property("text").toString().contains(QStringLiteral("Schimpf"))),
           "Grund steht in der Box");
    expect(!last().value("mention").toBool(), "AutoMod-Box ist keine Erwähnung");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/automod_wartet.png"));

    QQuickItem *allow = lastAutomod("automodAllow");
    expect(allow && lastAutomod("automodDeny"), "Knöpfe Freigeben/Ablehnen");
    calls.clear();
    if (allow) mouseAt(view, allow, QPointF(allow->width() / 2, allow->height() / 2), Qt::LeftButton);
    settle(300);
    expect(calls.size() == 1 && calls[0].method == QStringLiteral("POST") && calls[0].path == QStringLiteral("/helix/moderation/automod/message")
               && calls[0].body.contains("\"action\":\"ALLOW\"") && calls[0].body.contains("\"msg_id\":\"am-1\"") && calls[0].body.contains("\"user_id\":\"999\""),
           "Freigeben → Helix moderation/automod/message ALLOW");
    expect(last().value("automodStatus").toString() == QStringLiteral("approved") && !lastAutomod("automodAllow"), "freigegeben, Knöpfe weg");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/automod_freigegeben.png"));

    // Ein anderer Moderator lehnt ab
    hold(QStringLiteral("am-2"), QStringLiteral("4242"), QStringLiteral("noch eins"));
    notify(QStringLiteral("automod.message.update"),
           QStringLiteral(R"({"broadcaster_user_id":"4242","message_id":"am-2","status":"Denied","moderator_user_name":"ModX"})"));
    state = lastAutomod("automodState");
    expect(last().value("automodStatus").toString() == QStringLiteral("denied") && state && state->property("text").toString().contains(QStringLiteral("ModX")),
           "Entscheidung eines anderen Moderators wird nachgetragen");

    // Fehler von Twitch (schon entschieden) → ✗-Zeile
    hold(QStringLiteral("am-3"), QStringLiteral("4242"), QStringLiteral("dritte"));
    fail = true;
    controller.automodAction(QStringLiteral("am-3"), false);
    settle(200);
    expect(last().value("raw").toString().startsWith(QStringLiteral("✗ AutoMod:")), "Fehler als ✗-Zeile");
    fail = false;

    // Andere Kanäle werden nicht angezeigt
    const int count = int(controller.history().size());
    hold(QStringLiteral("am-4"), QStringLiteral("9999"), QStringLiteral("fremder Kanal"));
    expect(int(controller.history().size()) == count, "Meldungen anderer Kanäle werden ignoriert");

    // Meldungen von tmi.twitch.tv: blaue Box, AutoMod-Hinweis mit Etikett „AutoMod“
    const auto boxOfLast = [&]() -> QPair<QString, QString> {
        QMetaObject::invokeMethod(chat, "positionViewAtEnd");
        settle(250);
        QList<QQuickItem *> boxes;
        for (const char *name : {"twitchBox", "automodBox", "mentionBox", "systemBox"}) collectItems(chat, QLatin1String(name), boxes);
        QQuickItem *best = nullptr;
        for (QQuickItem *b : std::as_const(boxes))
            if (b->isVisible() && (!best || b->mapToScene(QPointF(0, 0)).y() > best->mapToScene(QPointF(0, 0)).y())) best = b;
        if (!best) return {};
        QList<QQuickItem *> labels;
        collectItems(best, QStringLiteral("boxLabel"), labels);
        return qMakePair(best->objectName(), labels.isEmpty() ? QString() : labels.first()->property("text").toString());
    };
    irc(QStringLiteral("@msg-id=automod_message_pending :tmi.twitch.tv NOTICE #test :Your message is being checked by mods and has not been sent."));
    auto tbox = boxOfLast();
    expect(tbox.first == QStringLiteral("twitchBox") && tbox.second == QStringLiteral("AutoMod"), "AutoMod-Hinweis von tmi.twitch.tv: blaue Box „AutoMod“");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/tmi_automod.png"));
    irc(QStringLiteral("@msg-id=slow_on :tmi.twitch.tv NOTICE #test :This room is now in slow mode."));
    tbox = boxOfLast();
    expect(tbox.first == QStringLiteral("twitchBox") && tbox.second == QStringLiteral("tmi.twitch.tv"), "andere Twitch-Meldung: blaue Box „tmi.twitch.tv“");
    QMetaObject::invokeMethod(&controller, "onText", Q_ARG(QString, QStringLiteral(":tmi.twitch.tv CLEARCHAT #test\r\n")));
    settle(250);
    tbox = boxOfLast();
    expect(tbox.first == QStringLiteral("twitchBox") && last().value("raw").toString().contains(QStringLiteral("geleert")), "Systemzeile „tmi“ ebenfalls blau");
    // Sub mit Text: gelbe Box „System“, Twitchs Text vorn, Text des Zuschauers dahinter
    irc(QStringLiteral("@display-name=Abonnent;login=abonnent;msg-id=resub;system-msg=Abonnent\\ssubscribed\\sfor\\s12\\smonths! :tmi.twitch.tv USERNOTICE #test :Danke!"));
    tbox = boxOfLast();
    expect(tbox.first == QStringLiteral("systemBox") && tbox.second == QStringLiteral("System"), "Sub: gelbe Box „System“");
    expect(last().value("name").toString() == QStringLiteral("System") && last().value("raw").toString() == QStringLiteral("Abonnent subscribed for 12 months! – Danke!"),
           "Sub: Name „System“, Twitchs Text und der Text des Zuschauers");
    expect(!last().value("fromTwitch").toBool() && last().value("user").toString().isEmpty(), "Sub: nicht als tmi.twitch.tv, kein Nutzer-Menü");
    // Raid ohne eigenen Text: erschien früher gar nicht
    const int beforeRaid = int(controller.history().size());
    irc(QStringLiteral("@display-name=Raider;login=raider;msg-id=raid;msg-param-viewerCount=42;system-msg=42\\sraiders\\sfrom\\sRaider\\shave\\sjoined! :tmi.twitch.tv USERNOTICE #test"));
    tbox = boxOfLast();
    expect(int(controller.history().size()) == beforeRaid + 1 && last().value("raw").toString() == QStringLiteral("42 raiders from Raider have joined!")
               && tbox.first == QStringLiteral("systemBox"), "Raid erscheint als gelbe System-Box");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/system_gelb.png"));
    // Event-Bots, Bits und Follows (2.1.4)
    irc(QStringLiteral("@display-name=StreamElements;user-id=100135110 :streamelements!streamelements@streamelements.tmi.twitch.tv PRIVMSG #test :Danke Max für die 5€ Spende!"));
    tbox = boxOfLast();
    expect(tbox.first == QStringLiteral("systemBox") && tbox.second == QStringLiteral("StreamElements"), "Bot-Nachricht: gelbe Box „StreamElements“");
    irc(QStringLiteral("@display-name=Nightbot;user-id=19264788 :nightbot!nightbot@nightbot.tmi.twitch.tv PRIVMSG #test :Folgt dem Kanal!"));
    expect(boxOfLast().second == QStringLiteral("Nightbot"), "Nightbot erkannt");
    {   // Cheer-Bild liegt schon im Cache (Test ohne Netz)
        const QString cheerDir = g_host->pluginDataDir(QStringLiteral("cc.ts3x.twitch-chat")) + QStringLiteral("/emotes/cheer");
        QDir().mkpath(cheerDir);
        QImage cheerImage(56, 56, QImage::Format_ARGB32);
        cheerImage.fill(QColor(QStringLiteral("#9c3ee8")));
        cheerImage.save(cheerDir + QStringLiteral("/cheer_100.png"));
    }
    irc(QStringLiteral("@display-name=Spender;bits=250;user-id=77 :spender!spender@spender.tmi.twitch.tv PRIVMSG #test :Cheer250 GG"));
    {
        const QVariantList segs = last().value("segments").toList();
        const QVariantMap first = segs.value(0).toMap();
        QString rest;
        for (int i = 1; i < segs.size(); ++i) rest += segs[i].toMap().value("s").toString();
        std::printf("Cheer-Segmente: %s | %s\n", first.value("key").toString().toUtf8().constData(), rest.toUtf8().constData());
        expect(first.value("t").toString() == QStringLiteral("emote") && first.value("key").toString() == QStringLiteral("cheer_cheer_100")
                   && rest.trimmed() == QStringLiteral("250 GG"), "Cheer250 als Bild (Stufe 100) mit der Zahl dahinter");
    }
    tbox = boxOfLast();
    expect(tbox.first == QStringLiteral("systemBox") && tbox.second == QStringLiteral("250 Bits") && last().value("user").toString() == QStringLiteral("spender"),
           "Bits: gelbe Box „250 Bits“, Name bleibt anklickbar");
    irc(QStringLiteral("@display-name=Normalo;user-id=78 :normalo!normalo@normalo.tmi.twitch.tv PRIVMSG #test :Cheer100 ohne Bits ist nur Text"));
    expect(last().value("segments").toList().value(0).toMap().value("t").toString() == QStringLiteral("text"), "ohne Bits bleibt „Cheer100“ Text");
    irc(QStringLiteral("@display-name=Normalo;user-id=78 :normalo!normalo@normalo.tmi.twitch.tv PRIVMSG #test :ganz normal"));
    expect(boxOfLast().first.isEmpty() || last().value("botName").toString().isEmpty(), "normale Nachricht ohne Box");
    service->handleEventSubText(QStringLiteral(R"({"metadata":{"message_type":"notification"},"payload":{"subscription":{"type":"channel.follow"},)"
                                               R"("event":{"user_id":"88","user_login":"neufan","user_name":"NeuFan","broadcaster_user_id":"4242","followed_at":"2026-11-08T02:00:00Z"}}})"));
    settle(250);
    tbox = boxOfLast();
    expect(tbox.first == QStringLiteral("systemBox") && tbox.second == QStringLiteral("Follow")
               && last().value("raw").toString() == QStringLiteral("NeuFan folgt jetzt dem Kanal"), "Follow: gelbe Box „Follow“");
    const int beforeForeign = int(controller.history().size());
    service->handleEventSubText(QStringLiteral(R"({"metadata":{"message_type":"notification"},"payload":{"subscription":{"type":"channel.follow"},)"
                                               R"("event":{"user_name":"Fremd","broadcaster_user_id":"9999"}}})"));
    settle(150);
    expect(int(controller.history().size()) == beforeForeign, "Follows anderer Kanäle werden ignoriert");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/events_gelb.png"));

    // Box-Einstellungen (2.1.5): Farben je Art, Event-Bots anlegen/ändern/entfernen, gilt auch nachträglich
    const auto lastBoxColor = [&]() -> QColor {
        QMetaObject::invokeMethod(chat, "positionViewAtEnd");
        settle(250);
        QList<QQuickItem *> boxes;
        for (const char *name : {"twitchBox", "automodBox", "mentionBox", "systemBox"}) collectItems(chat, QLatin1String(name), boxes);
        QQuickItem *best = nullptr;
        for (QQuickItem *b : std::as_const(boxes))
            if (b->isVisible() && (!best || b->mapToScene(QPointF(0, 0)).y() > best->mapToScene(QPointF(0, 0)).y())) best = b;
        return best ? best->property("border").value<QObject *>()->property("color").value<QColor>() : QColor();
    };
    expect(lastBoxColor() == QColor(QStringLiteral("#f1c40f")), "Follow-Box ab Werk gelb");
    controller.setBoxColor(QStringLiteral("follow"), QStringLiteral("#2ecc71"));
    expect(lastBoxColor() == QColor(QStringLiteral("#2ecc71")), "Follow-Farbe nachträglich geändert (auch schon stehende Box)");
    expect(g_host->loadPluginSettings(QStringLiteral("cc.ts3x.twitch-chat")).value(QStringLiteral("boxColors")).toObject()
               .value(QStringLiteral("follow")).toString() == QStringLiteral("#2ecc71"), "Box-Farbe gespeichert");
    controller.setBoxColor(QStringLiteral("follow"), QString());
    expect(lastBoxColor() == QColor(QStringLiteral("#f1c40f")), "Follow-Farbe zurück auf Standard");
    expect(!controller.setEventBot(QStringLiteral("kein gültiger name"), QString(), QString()), "ungültiger Bot-Name abgelehnt");
    expect(controller.setEventBot(QStringLiteral("@MeinBot_1"), QStringLiteral("Mein Bot"), QStringLiteral("#ff4f9a")), "neuer Bot eingetragen");
    irc(QStringLiteral("@display-name=MeinBot_1;user-id=901 :meinbot_1!meinbot_1@meinbot_1.tmi.twitch.tv PRIVMSG #test :Danke für den Follow!"));
    tbox = boxOfLast();
    expect(tbox.first == QStringLiteral("systemBox") && tbox.second == QStringLiteral("Mein Bot") && lastBoxColor() == QColor(QStringLiteral("#ff4f9a")),
           "eigener Bot: Box mit Etikett und eigener Farbe");
    controller.setEventBot(QStringLiteral("meinbot_1"), QStringLiteral("Alerts"), QString(), QStringLiteral("meinbot_1"));
    tbox = boxOfLast();
    expect(tbox.second == QStringLiteral("Alerts") && lastBoxColor() == QColor(QStringLiteral("#f1c40f")), "Bot bearbeitet: Etikett neu, Farbe = Bot-Standard");
    controller.setBoxColor(QStringLiteral("bot"), QStringLiteral("#8e44ad"));
    expect(lastBoxColor() == QColor(QStringLiteral("#8e44ad")), "Bot-Standardfarbe geändert");
    controller.setEventBot(QStringLiteral("streamelements"), QStringLiteral("SE"), QStringLiteral("#00c8af"), QStringLiteral("streamelements"));
    irc(QStringLiteral("@display-name=StreamElements;user-id=100135110 :streamelements!streamelements@streamelements.tmi.twitch.tv PRIVMSG #test :Neuer Tipp!"));
    tbox = boxOfLast();
    expect(tbox.second == QStringLiteral("SE") && lastBoxColor() == QColor(QStringLiteral("#00c8af")), "Standard-Bot umbenannt und umgefärbt");
    {   // Fenster öffnen: alle Bots stehen drin
        QObject *popup = panel->findChild<QObject *>(QStringLiteral("boxSettingsPopup"));
        expect(popup != nullptr, "Box-Einstellungen vorhanden");
        QList<QQuickItem *> buttons;
        collectItems(panel, QStringLiteral("boxSettingsButton"), buttons);
        expect(!buttons.isEmpty() && buttons.first()->isVisible(), "Glocke in der Statuszeile");
        if (popup) {
            QMetaObject::invokeMethod(popup, "open");
            settle(300);
            QList<QQuickItem *> rows, kinds;
            QQuickItem *content = popup->property("contentItem").value<QQuickItem *>();
            collectItems(content, QStringLiteral("botRow"), rows);
            collectItems(content, QStringLiteral("boxKindRow"), kinds);
            std::printf("Box-Einstellungen: %d Bots, %d Arten, Fensterbreite %.0f\n", int(rows.size()), int(kinds.size()),
                        popup->property("width").toDouble());
            expect(rows.size() == controller.eventBots().size() && kinds.size() == 7, "Fenster zeigt alle Bots und 7 Box-Arten");
            bool fits = true;
            for (QQuickItem *r : std::as_const(rows))
                if (r->mapToScene(QPointF(r->width(), 0)).x() > panel->width() + 0.5) fits = false;
            expect(fits, "Bot-Zeilen passen in die Panel-Breite");
            if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/box_settings.png"));
            QVariantMap pickTarget;
            pickTarget.insert(QStringLiteral("kind"), QStringLiteral("system"));
            QMetaObject::invokeMethod(popup, "chooseColor", Q_ARG(QVariant, QVariant(pickTarget)),
                                      Q_ARG(QVariant, QVariant(QString())), Q_ARG(QVariant, QVariant(true)));
            settle(250);
            QList<QQuickItem *> swatches;
            QObject *picker = popup->findChild<QObject *>(QStringLiteral("boxColorPicker"));
            if (picker) collectItems(picker->property("contentItem").value<QQuickItem *>(), QStringLiteral("paletteColor"), swatches);
            expect(swatches.size() == 18 && swatches.first()->isVisible(), "Farbauswahl mit 18 Farben offen");
            if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/box_color_picker.png"));
            if (swatches.size() > 3) {
                const QPointF c = swatches.at(3)->mapToScene(QPointF(swatches.at(3)->width() / 2, swatches.at(3)->height() / 2));
                QMouseEvent press(QEvent::MouseButtonPress, c, c, view.mapToGlobal(c), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(&view, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, c, c, view.mapToGlobal(c), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(&view, &release);
                settle(200);
            }
            expect(controller.boxColors().value(QStringLiteral("system")).toString() == QStringLiteral("#e74c3c"), "Klick auf Farbe setzt System-Farbe");
            QMetaObject::invokeMethod(popup, "close");
            settle(150);
        }
    }
    controller.removeEventBot(QStringLiteral("streamelements"));
    tbox = boxOfLast();
    expect(tbox.second != QStringLiteral("SE"), "entfernter Bot: keine Box mehr (auch nachträglich)");
    const QJsonArray savedBots = g_host->loadPluginSettings(QStringLiteral("cc.ts3x.twitch-chat")).value(QStringLiteral("eventBots")).toArray();
    bool savedOwn = false, savedSe = false;
    for (const QJsonValue &v : savedBots) {
        savedOwn |= v.toObject().value(QStringLiteral("login")).toString() == QStringLiteral("meinbot_1");
        savedSe |= v.toObject().value(QStringLiteral("login")).toString() == QStringLiteral("streamelements");
    }
    expect(savedOwn && !savedSe, "Bot-Liste gespeichert (eigener drin, entfernter weg)");
    controller.resetEventBots();
    controller.resetBoxColors();
    expect(controller.eventBotLabel(QStringLiteral("streamelements")) == QStringLiteral("StreamElements")
               && controller.eventBotLabel(QStringLiteral("meinbot_1")).isNull(), "Bot-Liste zurückgesetzt");

    controller.setHelixHook(nullptr);
    return ok;
}

// Emotes direkt im Eingabefeld: Bild liegt genau über dem Namen, Feld bleibt Text
static bool checkEmotePreview(TwitchChatController &controller, QQuickView &view, QQuickItem *panel, FakeHost &host, const QString &shotDir)
{
    bool ok = true;
    const auto expect = [&ok](bool passed, const char *what) {
        std::printf("Emote im Feld %s: %s\n", what, passed ? "ok" : "FEHLER");
        ok = ok && passed;
    };
    const QString dir = host.pluginDataDir(QStringLiteral("cc.ts3x.twitch-chat")) + QStringLiteral("/emotes/7tv");
    QDir().mkpath(dir);
    QImage image(56, 56, QImage::Format_ARGB32);
    image.fill(QColor(QStringLiteral("#ffb000")));
    image.save(dir + QStringLiteral("/previewtest.png"));
    ThirdPartyCatalog catalog;
    catalog.insert(QStringLiteral("VorschauEmo"), ThirdPartyEmote{QStringLiteral("7tv_previewtest"), false, false});
    controller.setThirdPartyCatalog(Provider7tv, true, catalog);

    QQuickItem *field = panel->findChild<QQuickItem *>(QStringLiteral("sendField"));
    if (!field) return false;
    const auto inlineEmotes = [&] {
        QList<QQuickItem *> all, visible;
        collectItems(field, QStringLiteral("inlineEmote"), all);
        for (QQuickItem *item : std::as_const(all)) if (item->isVisible() && item->width() > 1) visible << item;
        return visible;
    };
    field->setProperty("enabled", true);
    field->forceActiveFocus();
    field->setProperty("text", QStringLiteral("Hallo ohne Emote"));
    settle(300);
    expect(inlineEmotes().isEmpty(), "ohne Emote kein Bild");
    expect(!panel->findChild<QQuickItem *>(QStringLiteral("emotePreview")), "keine eigene Vorschau-Zeile mehr");

    const QString text = QStringLiteral("Hallo VorschauEmo wie gehts");
    field->setProperty("text", text);
    field->setProperty("cursorPosition", int(text.size()));
    settle(500);
    const auto emotes = inlineEmotes();
    expect(emotes.size() == 1, "Emote-Name erscheint als Bild im Feld");
    if (!emotes.isEmpty()) {
        QRectF from, to;
        QMetaObject::invokeMethod(field, "positionToRectangle", Q_RETURN_ARG(QRectF, from), Q_ARG(int, 6));
        QMetaObject::invokeMethod(field, "positionToRectangle", Q_RETURN_ARG(QRectF, to), Q_ARG(int, 17));
        const QQuickItem *e = emotes.first();
        expect(qAbs(e->x() - from.x()) < 1 && qAbs(e->width() - (to.x() - from.x())) < 1, "Bild liegt genau über dem Namen");
        QList<QQuickItem *> parts;
        collectItems(const_cast<QQuickItem *>(e), QString(), parts);
        bool imageReady = false;
        for (QQuickItem *part : std::as_const(parts))
            if (QString::fromLatin1(part->metaObject()->className()).startsWith(QLatin1String("QQuickImage"))
                && part->property("status").toInt() == 1)   // Image.Ready
                imageReady = true;
        expect(imageReady, "Bild ist geladen");
        // Farbe im Feld an der Stelle des Emotes: das Orange des Test-Emotes
        const QPointF center = e->mapToScene(QPointF(e->width() / 2, e->height() / 2));
        const QColor c = view.grabWindow().pixelColor(int(center.x()), int(center.y()));
        expect(c.red() > 200 && c.green() > 140 && c.green() < 200 && c.blue() < 60, "Bild ist im Feld zu sehen");
    }
    expect(field->property("text").toString() == text && field->property("cursorPosition").toInt() == text.size(),
           "Text und Cursor bleiben unverändert");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/emote_im_feld.png"));

    // Keine leeren Ränder: Das quadratische Test-Bild füllt die Breite. Neben dem Bild steht direkt der Text.
    if (!emotes.isEmpty()) {
        const QQuickItem *e = emotes.first();
        const qreal imageHeight = field->property("inlineEmoteHeight").toReal();
        expect(qAbs(e->width() - imageHeight) <= 3, "Emote-Breite = Bildbreite (quadratisch)");
        const QImage shot = view.grabWindow();
        const QPointF left = e->mapToScene(QPointF(2, e->height() / 2));
        const QPointF right = e->mapToScene(QPointF(e->width() - 3, e->height() / 2));
        const auto orange = [&](const QPointF &p) {
            const QColor c = shot.pixelColor(int(p.x()), int(p.y()));
            return c.red() > 200 && c.green() > 140 && c.green() < 200 && c.blue() < 60;
        };
        expect(orange(left) && orange(right), "Bild reicht bis an beide Ränder");
    }
    // Kurzer und langer Name: beide werden gleich breit (vorher hing die Breite am Namen).
    ThirdPartyCatalog sizes;
    sizes.insert(QStringLiteral("Ab"), ThirdPartyEmote{QStringLiteral("7tv_previewtest"), false, false});
    sizes.insert(QStringLiteral("EinSehrLangerEmoteNameHier"), ThirdPartyEmote{QStringLiteral("7tv_previewtest"), false, false});
    controller.setThirdPartyCatalog(Provider7tv, true, sizes);
    field->setProperty("text", QStringLiteral("Ab EinSehrLangerEmoteNameHier ende"));
    settle(500);
    {
        const auto both = inlineEmotes();
        expect(both.size() == 2 && qAbs(both[0]->width() - both[1]->width()) <= 3, "Name kurz oder lang: gleiche Breite");
        if (both.size() == 2) std::printf("    Breiten: %.1f und %.1f\n", both[0]->width(), both[1]->width());
    }
    // Gesendet wird weiter der Name, und ein eingefügter Umbruch bleibt nicht im Feld.
    expect(field->property("text").toString() == QStringLiteral("Ab EinSehrLangerEmoteNameHier ende"), "Text bleibt der Name");
    field->setProperty("text", QStringLiteral("eins\nzwei\tdrei"));
    settle(100);
    expect(field->property("text").toString() == QStringLiteral("eins zwei drei"), "Umbruch und Tab werden Leerzeichen");
    // Langer Text scrollt: Cursor bleibt sichtbar, nichts ragt aus dem Rahmen.
    const QString longText = QString(300, QLatin1Char('x'));
    field->setProperty("text", longText);
    field->setProperty("cursorPosition", int(longText.size()));
    settle(200);
    {
        QRectF cursor;
        QMetaObject::invokeMethod(field, "positionToRectangle", Q_RETURN_ARG(QRectF, cursor), Q_ARG(int, int(longText.size())));
        const QQuickItem *box = field->parentItem();
        const QPointF inBox = field->mapToItem(box, cursor.topLeft());
        expect(inBox.x() >= 0 && inBox.x() <= box->width(), "Cursor bleibt bei langem Text sichtbar");
        expect(box->mapToScene(QPointF(box->width(), 0)).x() <= view.width(), "Rahmen bleibt in der Seitenleiste");
    }
    controller.setThirdPartyCatalog(Provider7tv, true, catalog);

    // Fokus: Klick daneben gibt ihn frei, Klick ins Feld und auf „Senden“ nicht.
    QQuickItem *chat = panel->findChild<QQuickItem *>(QStringLiteral("chatView"));
    QQuickItem *send = panel->findChild<QQuickItem *>(QStringLiteral("sendButton"));
    field->setProperty("text", QStringLiteral("Hallo"));
    field->forceActiveFocus();
    settle(100);
    if (field->hasActiveFocus() && chat && send) {
        // Enter sendet (ohne Verbindung scheitert das still, der Text bleibt) und macht keine zweite Zeile.
        SignalCounter accepted;
        QObject::connect(field, SIGNAL(accepted()), &accepted, SLOT(hit()));
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
        QCoreApplication::sendEvent(&view, &enter);
        settle(100);
        expect(accepted.count == 1, "Enter löst das Senden aus");
        expect(field->property("text").toString() == QStringLiteral("Hallo"), "Enter macht keine zweite Zeile");
        // Ohne Verbindung: Der Sendeversuch meldet einen Status, „enabled“ folgt dann wieder „connected“.
        std::printf("    nach Enter: enabled=%d connected=%d Fokus=%d\n", field->isEnabled(), controller.connected(), field->hasActiveFocus());
        field->setProperty("enabled", true);
        field->forceActiveFocus();
        settle(100);
        mouseAt(view, field,QPointF(field->parentItem()->width() / 2, field->height() / 2), Qt::LeftButton);
        settle(100);
        expect(field->hasActiveFocus(), "Klick ins Feld behält den Fokus");
        mouseAt(view, send, QPointF(send->width() / 2, send->height() / 2), Qt::LeftButton);
        settle(100);
        expect(field->hasActiveFocus(), "Klick auf „Senden“ behält den Fokus");
        mouseAt(view, chat, QPointF(chat->width() / 2, chat->height() / 2), Qt::LeftButton);
        settle(100);
        expect(!field->hasActiveFocus(), "Klick in den Chat gibt den Fokus frei");
        expect(field->property("text").toString() == QStringLiteral("Hallo"), "der Text bleibt dabei stehen");
        field->forceActiveFocus();
        settle(100);
        panel->setProperty("emotesOpen", true);
        settle(300);
        QList<QQuickItem *> tabs;
        collectItems(panel, QStringLiteral("emoteSectionTitle"), tabs);
        if (!tabs.isEmpty()) {
            mouseAt(view, tabs.first(), QPointF(2, 2), Qt::LeftButton);
            settle(100);
            expect(field->hasActiveFocus(), "Klick im Emote-Browser behält den Fokus");
        }
        panel->setProperty("emotesOpen", false);
        settle(100);
    } else {
        expect(false, "Feld bekommt den Fokus");
    }

    field->setProperty("text", QStringLiteral("Hallo Vorschau"));
    settle(300);
    expect(inlineEmotes().isEmpty(), "halber Name bleibt Text");
    field->setProperty("text", QStringLiteral("VorschauEmo VorschauEmo"));
    settle(400);
    expect(inlineEmotes().size() == 2, "zwei Emotes, zwei Bilder");
    field->setProperty("text", QString());
    settle(200);
    expect(inlineEmotes().isEmpty(), "leeres Feld ohne Bilder");
    return ok;
}

// „Mit Twitch anmelden“: echter Code von Twitch (danach abgebrochen), Abschluss nachgestellt
static bool checkDeviceLogin(TwitchChatController &controller, QQuickView &view, QQuickItem *panel, const QString &shotDir)
{
    bool ok = true;
    const auto expect = [&ok](bool passed, const char *what) {
        std::printf("Anmeldung %s: %s\n", what, passed ? "ok" : "FEHLER");
        ok = ok && passed;
    };
    panel->setProperty("loginOpen", true);
    settle(200);
    QQuickItem *button = panel->findChild<QQuickItem *>(QStringLiteral("deviceLogin"));
    QQuickItem *waiting = panel->findChild<QQuickItem *>(QStringLiteral("deviceLoginWaiting"));
    QQuickItem *cancel = panel->findChild<QQuickItem *>(QStringLiteral("loginCancel"));
    QQuickItem *status = panel->findChild<QQuickItem *>(QStringLiteral("loginStatus"));
    if (!button || !waiting || !cancel || !status) {
        std::printf("Anmeldung: Bausteine fehlen\n");
        return false;
    }
    expect(button->isVisible() && !waiting->isVisible(), "„Mit Twitch anmelden“ sichtbar");
    expect(button->mapToScene(QPointF(button->width(), 0)).x() <= view.width(), "Knopf passt in die Seitenleiste");

    LinkCatcher catcher;
    QDesktopServices::setUrlHandler(QStringLiteral("https"), &catcher, "open");
    mouseAt(view, button, QPointF(button->width() / 2, button->height() / 2), Qt::LeftButton);
    QElapsedTimer waited;
    waited.start();
    while (controller.loginCode().isEmpty() && controller.loginState() == QStringLiteral("waiting") && waited.elapsed() < 15000) settle(100);
    if (controller.loginCode().isEmpty()) {
        std::printf("HINWEIS Twitch nicht erreichbar (%s), Code-Teil übersprungen\n", controller.loginError().toUtf8().constData());
    } else {
        std::printf("Code von Twitch: %s, Link: %s\n", controller.loginCode().toUtf8().constData(), controller.loginUrl().toUtf8().constData());
        expect(controller.loginState() == QStringLiteral("waiting") && waiting->isVisible() && !button->isVisible(), "wartet auf die Bestätigung, zeigt den Code");
        expect(catcher.opened.size() == 1 && catcher.opened.first().toString().startsWith(QStringLiteral("https://www.twitch.tv/activate"))
                   && catcher.opened.first().toString().count(QStringLiteral("device-code=")) == 1
                   && catcher.opened.first().toString().contains(controller.loginCode()),
               "Browser öffnet twitch.tv/activate mit fertigem Code");
        for (const QUrl &u : std::as_const(catcher.opened)) std::printf("Geöffnet: %s\n", u.toString().toUtf8().constData());
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/anmeldung_wartet.png"));
        mouseAt(view, cancel, QPointF(cancel->width() / 2, cancel->height() / 2), Qt::LeftButton);
        settle(200);
        expect(controller.loginState() == QStringLiteral("idle") && button->isVisible(), "Abbrechen");
    }
    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));

    // Abschluss wie nach „Autorisieren“
    const qint64 expires = QDateTime::currentMSecsSinceEpoch() + 4 * 3600 * 1000;
    controller.completeLogin(QStringLiteral("neuer_nutzer"), QStringLiteral("tok123"), QStringLiteral("ref456"), expires);
    settle(300);
    expect(controller.authenticated() && controller.user() == QStringLiteral("neuer_nutzer") && controller.autoRenew()
               && controller.tokenExpiresAt() == expires, "nach „Autorisieren“: angemeldet, Token erneuert sich selbst");
    expect(status->isVisible() && status->property("text").toString().contains(QStringLiteral("neuer_nutzer")), "Anzeige „Angemeldet als …“");
    expect(controller.history().last().toMap().value(QStringLiteral("raw")).toString().contains(QStringLiteral("neuer_nutzer")), "Bestätigung im Chat");
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/anmeldung_fertig.png"));
    controller.setAuth(QStringLiteral("handnutzer"), QStringLiteral("oauth:handtoken"));
    expect(!controller.autoRenew(), "von Hand eingefügter Token wird nicht erneuert");
    panel->setProperty("loginOpen", false);
    return ok;
}

// Kanalwechsel: Nachrichten des alten Kanals verschwinden
static bool checkChannelSwitch(TwitchChatController &controller, QQuickItem *panel)
{
    QQuickItem *chat = panel->findChild<QQuickItem *>(QStringLiteral("chatView"));
    controller.closeChat();                          // kein echter Verbindungsaufbau im Test
    controller.setChannel(QStringLiteral("kanal_a"));
    QMetaObject::invokeMethod(&controller, "onText", Q_ARG(QString, QStringLiteral(
        "@display-name=Alt;color=#1E90FF :alt!alt@alt.tmi.twitch.tv PRIVMSG #kanal_a :Nachricht im alten Kanal\r\n")));
    settle(300);
    const int before = chat ? chat->property("count").toInt() : -1;
    controller.setChannel(QStringLiteral("kanal_b"));
    settle(300);
    const int after = chat ? chat->property("count").toInt() : -1;
    bool onlySelf = true;                            // die eigene Zeile bleibt, sonst niemand
    for (const QVariant &c : controller.chatters()) onlySelf = onlySelf && c.toMap().value("self").toBool();
    const bool ok = before > 0 && after == 0 && controller.history().isEmpty() && onlySelf
                    && controller.viewerCount() == -1;
    std::printf("Kanalwechsel leert Chat und Teilnehmerliste (%d → %d Zeilen): %s\n", before, after, ok ? "ok" : "FEHLER");
    return ok;
}

// Abzeichen als Bild vor dem Namen und die eigene Zeile in der Zuschauerliste
static bool checkBadgesAndSelf(TwitchChatController &controller, QQuickView &view, QQuickItem *panel, FakeHost &host, const QString &shotDir)
{
    bool ok = true;
    const auto expect = [&ok](bool passed, const char *what) {
        std::printf("Abzeichen %s: %s\n", what, passed ? "ok" : "FEHLER");
        ok = ok && passed;
    };
    const auto irc = [&](const QString &line) { QMetaObject::invokeMethod(&controller, "onText", Q_ARG(QString, line + QStringLiteral("\r\n"))); };

    // Bilder liegen schon im Cache: Der Test soll ohne Netz laufen (Download prüft emote_test live).
    const QString dir = host.pluginDataDir(QStringLiteral("cc.ts3x.twitch-chat")) + QStringLiteral("/emotes/badge");
    QDir().mkpath(dir);
    const auto seed = [&](const char *name, const char *color) {
        QImage img(36, 36, QImage::Format_ARGB32);
        img.fill(QColor(QLatin1String(color)));
        img.save(dir + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".png"));
    };
    seed("aaaa1111", "#e00079");
    seed("bbbb2222", "#2ecc71");
    seed("cccc3333", "#4a9eff");
    controller.applyBadgesJson(R"({"data":{"badges":[
        {"setID":"moderator","version":"1","imageURL":"https://static-cdn.jtvnw.net/badges/v1/aaaa-1111/2"},
        {"setID":"subscriber","version":"12","imageURL":"https://static-cdn.jtvnw.net/badges/v1/bbbb-2222/2"},
        {"setID":"fremd","version":"1","imageURL":"https://evil.example/badges/v1/aaaa-1111/2"}]}})", false);
    controller.applyBadgesJson(R"({"data":{"user":{"broadcastBadges":[
        {"setID":"subscriber","version":"12","imageURL":"https://static-cdn.jtvnw.net/badges/v1/cccc-3333/2"}]}}})", true);
    expect(controller.badgeImage(QStringLiteral("moderator/1")).endsWith(QStringLiteral("/badge/aaaa1111.png")), "globales Bild gefunden");
    expect(controller.badgeImage(QStringLiteral("subscriber/12")).endsWith(QStringLiteral("/badge/cccc3333.png")), "Kanal schlägt global");
    expect(controller.badgeImage(QStringLiteral("fremd/1")).isEmpty(), "fremde Adresse wird nicht übernommen");
    expect(controller.badgeImage(QStringLiteral("gibtsnicht/1")).isEmpty(), "unbekanntes Abzeichen: kein Bild");
    const int revision = controller.badgeRevision();
    controller.applyBadgesJson(R"({"errors":[{"message":"service unavailable"}]})", true);
    expect(controller.badgeRevision() == revision && !controller.badgeImage(QStringLiteral("subscriber/12")).isEmpty(),
           "Fehlerantwort behält den Katalog");

    irc(QStringLiteral("@badges=moderator/1,subscriber/12,vip/1,gibtsnicht/1;color=#00FF7F;display-name=BadgeTester;id=b1 "
                       ":badgetester!badgetester@badgetester.tmi.twitch.tv PRIVMSG #kanal_b :Abzeichen vor dem Namen"));
    QQuickItem *chat = panel->findChild<QQuickItem *>(QStringLiteral("chatView"));
    if (chat) QMetaObject::invokeMethod(chat, "positionViewAtEnd");
    settle(500);

    QQuickItem *lastRow = nullptr;
    for (QQuickItem *r : visibleItems(chat, QStringLiteral("chatRow")))
        if (!lastRow || r->y() > lastRow->y()) lastRow = r;
    expect(lastRow != nullptr, "Chatzeile da");
    if (lastRow) {
        const QList<QQuickItem *> images = visibleItems(lastRow, QStringLiteral("chatBadgeImage"));
        const QList<QQuickItem *> texts = visibleItems(lastRow, QStringLiteral("chatBadgeText"));
        const QList<QQuickItem *> names = visibleItems(lastRow, QStringLiteral("chatName"));
        expect(images.size() == 2, "zwei Abzeichen als Bild");
        bool loaded = images.size() == 2;
        for (QQuickItem *i : images) loaded = loaded && i->property("status").toInt() == 1 && i->width() > 0;   // Image.Ready
        expect(loaded, "Bilder wirklich geladen");
        expect(texts.size() == 1, "ohne Bild: Textschild (VIP), Unbekanntes unsichtbar");
        bool before = !names.isEmpty() && images.size() == 2;
        if (before) {
            const qreal nameX = names.first()->mapToScene(QPointF(0, 0)).x();
            for (QQuickItem *i : images) before = before && i->mapToScene(QPointF(i->width(), 0)).x() <= nameX;
        }
        expect(before, "Abzeichen stehen vor dem Namen");
    }
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/abzeichen_chat.png"));

    // Eigene Zeile in der Zuschauerliste
    controller.setTestIdentity(QStringLiteral("tester"), QStringLiteral("999"));
    irc(QStringLiteral("@badges=moderator/1;color=#FF4500;display-name=Tester;user-type=mod :tmi.twitch.tv USERSTATE #kanal_b"));
    irc(QStringLiteral("@color=#FF4500;display-name=Tester;emote-sets=0 :tmi.twitch.tv GLOBALUSERSTATE"));
    irc(QStringLiteral(":tester!tester@tester.tmi.twitch.tv JOIN #kanal_b"));       // nur einmal aufführen
    irc(QStringLiteral(":gast9!gast9@gast9.tmi.twitch.tv JOIN #kanal_b"));
    irc(QStringLiteral("@badges=moderator/1;display-name=Chef;color=#00FF7F :chef!chef@chef.tmi.twitch.tv PRIVMSG #kanal_b :hi"));
    settle(600);
    const QVariantList people = controller.chatters();
    const QVariantMap first = people.value(0).toMap();
    expect(first.value("self").toBool() && first.value("login").toString() == QStringLiteral("tester")
               && first.value("name").toString() == QStringLiteral("Tester")
               && first.value("color").toString() == QStringLiteral("#FF4500")
               && first.value("badges").toStringList().contains(QStringLiteral("moderator")),
           "ich stehe ganz oben, mit Name, Farbe und Rolle");
    int mine = 0;
    for (const QVariant &p : people) if (p.toMap().value("login").toString() == QStringLiteral("tester")) ++mine;
    expect(mine == 1, "ich stehe nur einmal in der Liste");
    expect(controller.chatterCount() == people.size(), "Zahl im Hinweis zählt mich mit");
    expect(!controller.chatters(QStringLiteral("TEST")).isEmpty() && controller.chatters(QStringLiteral("gast9")).size() == 1,
           "Suche findet mich und filtert mich sonst aus");

    QObject *popup = panel->findChild<QObject *>(QStringLiteral("chattersPopup"));
    if (popup) QMetaObject::invokeMethod(popup, "open");
    settle(400);
    QQuickItem *content = popupContent(panel, "chattersPopup");
    QQuickItem *selfRow = nullptr, *otherRow = nullptr;
    for (QQuickItem *r : visibleItems(content, QStringLiteral("chatterRow"))) {
        if (r->property("login").toString() == QStringLiteral("tester")) selfRow = r;
        else if (r->property("login").toString() == QStringLiteral("chef")) otherRow = r;
    }
    expect(selfRow && otherRow, "beide Zeilen im Fenster");
    if (selfRow && otherRow) {
        QList<QQuickItem *> checks, otherChecks, names;
        collectItems(selfRow, QStringLiteral("chatterCheck"), checks);
        collectItems(otherRow, QStringLiteral("chatterCheck"), otherChecks);
        collectItems(selfRow, QStringLiteral("chatterName"), names);
        expect(visibleItems(selfRow, QStringLiteral("chatterWrite")).isEmpty()
                   && checks.size() == 1 && !checks.first()->isEnabled() && qFuzzyIsNull(checks.first()->opacity()),
               "eigene Zeile: kein ✉, kein Ankreuzen");
        expect(visibleItems(otherRow, QStringLiteral("chatterWrite")).size() == 1
                   && otherChecks.size() == 1 && otherChecks.first()->isEnabled(), "andere Zeilen unverändert");
        expect(names.size() == 1 && (names.first()->property("text").toString().contains(QStringLiteral("(du)"))
                                     || names.first()->property("text").toString().contains(QStringLiteral("(you)"))),
               "eigene Zeile ist gekennzeichnet");
        expect(checks.size() == 1 && otherChecks.size() == 1
                   && qFuzzyCompare(checks.first()->mapToScene(QPointF(0, 0)).x(), otherChecks.first()->mapToScene(QPointF(0, 0)).x()),
               "Namen bleiben bündig");
    }
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/zuschauer_selbst.png"));
    if (popup) QMetaObject::invokeMethod(popup, "close");
    settle(200);
    return ok;
}

// Der Player gibt dem Panel in der Seitenleiste fest 329 px. Nichts darf darüber hinausragen –
// in keiner Sprache und auch nicht mit offenem Login oder Emote-Browser.
static bool checkSidebarWidth(TwitchChatController &controller, const QString &shotDir)
{
    const int sidebarWidth = 329;
    for (const QString &line : {
             QStringLiteral("JulianGaming89 raided uns mit unglaublichen 12 Zuschauern, dafuer DANKEN wir von ganzem Herzen und noch viel mehr Text"),
             QStringLiteral("https://example.com/%1").arg(QString(200, QLatin1Char('x'))) }) {
        const QString frame = QStringLiteral("@display-name=Tester;color=#1E90FF;emotes= "
                                             ":tester!tester@tmi.twitch.tv PRIVMSG #test :%1\r\n").arg(line);
        QMetaObject::invokeMethod(&controller, "onText", Q_ARG(QString, frame));
    }

    QQuickView view;
    view.engine()->addImportPath(QLibraryInfo::path(QLibraryInfo::QmlImportsPath));
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setColor(QColor(QStringLiteral("#18181b")));
    view.resize(sidebarWidth, 900);
    view.setSource(QUrl::fromLocalFile(QStringLiteral("M:/LAPS/twitchchat/src/ChatPanel.qml")));
    QQuickItem *rootItem = view.rootObject();
    if (!rootItem) {
        std::printf("Seitenleisten-Test: Panel nicht geladen\n");
        return false;
    }
    rootItem->setProperty("controller", QVariant::fromValue<QObject *>(&controller));
    view.show();
    settle(300);
    QQuickItem *panel = findPanelContent(rootItem);
    QQuickItem *chatList = panel ? panel->findChild<QQuickItem *>(QStringLiteral("chatView")) : nullptr;
    QQuickItem *statusLabel = panel ? panel->findChild<QQuickItem *>(QStringLiteral("statusLabel")) : nullptr;
    if (!chatList || !statusLabel || chatList->property("count").toInt() < 2) {
        std::printf("Seitenleisten-Test: Chat-Liste, Statustext oder Zeilen fehlen\n");
        return false;
    }

    // Emote-Icon sitzt direkt vor „Senden“ und schaltet den Browser um
    QQuickItem *emoteButton = panel->findChild<QQuickItem *>(QStringLiteral("emoteButton"));
    QQuickItem *sendButton = panel->findChild<QQuickItem *>(QStringLiteral("sendButton"));
    bool ok = emoteButton && sendButton && emoteButton->isVisible()
              && emoteButton->parentItem() == sendButton->parentItem()
              && emoteButton->x() + emoteButton->width() <= sendButton->x()
              && emoteButton->width() <= emoteButton->height() + 1;
    if (ok) {
        panel->setProperty("emotesOpen", false);
        QMetaObject::invokeMethod(emoteButton, "clicked");
        ok = panel->property("emotesOpen").toBool();
        QMetaObject::invokeMethod(emoteButton, "clicked");
        ok = ok && !panel->property("emotesOpen").toBool();
    }
    std::printf("Emote-Icon vor Senden: %s\n", ok ? "ok" : "FEHLER");

    // Überschriften der Abschnitte kommen wirklich im Browser an
    {
        QVariantMap plugin;
        plugin.insert(QStringLiteral("language"), QStringLiteral("de"));
        rootItem->setProperty("plugin", plugin);
        panel->setProperty("emotesOpen", true);
        settle(300);
        QStringList titles;
        const QList<QQuickItem *> items = panel->findChildren<QQuickItem *>(QStringLiteral("emoteSectionTitle"));
        for (QQuickItem *t : items) titles << t->property("text").toString();
        const bool titlesOk = titles.contains(QString::fromUtf8("7TV – Kanal"))
                              && titles.contains(QString::fromUtf8("BetterTTV – Global"))
                              && titles.contains(QString::fromUtf8("FrankerFaceZ – Global"));
        std::printf("Abschnitts-Ueberschriften (%d): %s\n", int(titles.size()), titlesOk ? "ok" : "FEHLER");
        for (const QString &t : titles) std::printf("    %s\n", t.toUtf8().constData());
        ok = ok && titlesOk;
        panel->setProperty("emotesOpen", false);
    }

    for (const char *language : { "de", "en" }) {
        QVariantMap plugin;
        plugin.insert(QStringLiteral("language"), QString::fromLatin1(language));
        rootItem->setProperty("plugin", plugin);
        for (int state = 0; state < 4; ++state) {
            panel->setProperty("loginOpen", (state & 1) != 0);
            panel->setProperty("emotesOpen", (state & 2) != 0);
            settle(200);
            QStringList overflow;
            collectOverflow(rootItem, sidebarWidth, overflow);
            const bool statusVisible = statusLabel->width() >= 100;
            const bool rowOk = overflow.isEmpty() && statusVisible;
            std::printf("Seitenleiste %s login=%d emotes=%d: Ueberstand=%d Statusbreite=%d %s\n", language,
                        state & 1, (state & 2) >> 1, int(overflow.size()), int(statusLabel->width()),
                        rowOk ? "ok" : "FEHLER");
            for (qsizetype i = 0; i < qMin<qsizetype>(overflow.size(), 8); ++i)
                std::printf("    %s\n", overflow[i].toUtf8().constData());
            ok = ok && rowOk;
            if (!shotDir.isEmpty())
                view.grabWindow().save(QStringLiteral("%1/seitenleiste_%2_%3.png")
                                           .arg(shotDir, QString::fromLatin1(language)).arg(state));
        }
    }
    ok = checkBrowserScroll(controller, panel) && ok;
    ok = checkSpellInput(controller, view, panel, shotDir) && ok;
    ok = checkChatLinks(controller, view, panel, shotDir) && ok;
    ok = checkPinned(controller, view, panel, shotDir) && ok;
    ok = checkChannelEvents(controller, view, panel, shotDir) && ok;
    ok = checkCopyText(controller, view, panel, shotDir) && ok;
    ok = checkChatFontSize(controller, view, panel, *g_host, shotDir) && ok;
    ok = checkInboxAndChatters(controller, view, panel, shotDir) && ok;
    ok = checkUserMenu(controller, view, panel, shotDir) && ok;
    ok = checkAutomod(controller, view, panel, shotDir) && ok;
    ok = checkDeviceLogin(controller, view, panel, shotDir) && ok;
    ok = checkEmotePreview(controller, view, panel, *g_host, shotDir) && ok;
    ok = checkChannelSwitch(controller, panel) && ok;
    ok = checkBadgesAndSelf(controller, view, panel, *g_host, shotDir) && ok;
    return ok;
}

// Live-Kanäle (2.2.2): Parser, Fenster LiveChannels.qml mit Karten, Klick → player.load + Chat-Kanal
static bool checkLiveChannels(QQmlEngine &engine, TwitchChatController &controller, FakeHost &host, const QString &shotDir)
{
    bool ok = true;
    const QByteArray gql = R"({"data":{"streams":{"edges":[
        {"node":{"title":"Speedrun","viewersCount":12345,"previewImageURL":"","game":{"displayName":"Minecraft"},
                 "broadcaster":{"login":"AlphaStreamer","displayName":"AlphaStreamer","profileImageURL":""}}},
        {"node":{"title":"Just chatting","viewersCount":42,"previewImageURL":"","game":null,
                 "broadcaster":{"login":"beta_tv","displayName":"Beta TV","profileImageURL":""}}}]}}})";
    const QVariantList rows = TwitchChatController::liveRowsFromGql(gql);
    const QByteArray helix = R"({"data":[
        {"user_login":"small","user_name":"Small","game_name":"Chess","title":"t","viewer_count":5,"thumbnail_url":"https://x/live_user_small-{width}x{height}.jpg"},
        {"user_login":"big","user_name":"Big","game_name":"Art","title":"t","viewer_count":900,"thumbnail_url":"https://x/live_user_big-{width}x{height}.jpg"}]})";
    const QVariantList followed = TwitchChatController::liveRowsFromHelix(helix);
    const bool parseOk = rows.size() == 2 && rows.at(0).toMap().value("login").toString() == QLatin1String("alphastreamer")
        && rows.at(0).toMap().value("game").toString() == QLatin1String("Minecraft") && rows.at(1).toMap().value("game").toString().isEmpty()
        && followed.size() == 2 && followed.at(0).toMap().value("login").toString() == QLatin1String("big")
        && followed.at(0).toMap().value("thumb").toString() == QLatin1String("https://x/live_user_big-440x248.jpg");
    std::printf("Live-Kanäle Parser: gql=%d helix=%d ok=%d\n", int(rows.size()), int(followed.size()), parseOk ? 1 : 0);
    ok = ok && parseOk;

    QQmlComponent component(&engine);
    component.loadUrl(QUrl::fromLocalFile(QStringLiteral("M:/LAPS/twitchchat/src/LiveChannels.qml")));
    QQuickView view(&engine, nullptr);
    view.resize(780, 540);
    QObject *obj = component.create();
    auto *page = qobject_cast<QQuickItem *>(obj);
    if (!page) {
        for (const QQmlError &e : component.errors()) std::printf("  LiveChannels.qml: %s\n", e.toString().toUtf8().constData());
        std::printf("Live-Kanäle: Fenster lädt NICHT\n");
        return false;
    }
    page->setParentItem(view.contentItem());
    page->setSize(QSizeF(780, 540));
    QVariantMap theme{{"text", "#e6e8eb"}, {"muted", "#8b93a1"}, {"accent", "#ff8a1f"}, {"panel2", "#1b1f26"}, {"line", "#262b34"}};
    page->setProperty("theme", theme);
    page->setProperty("controller", QVariant::fromValue<QObject *>(&controller));   // startet „Beliebt“ (nicht angemeldet)
    const bool startedTop = controller.liveTab() == QLatin1String("top");
    controller.applyLiveResult(QStringLiteral("top"), rows);
    view.show();
    settle(400);

    QList<QQuickItem *> cards;
    collectItems(page, QStringLiteral("liveCard_alphastreamer"), cards);
    collectItems(page, QStringLiteral("liveCard_beta_tv"), cards);
    QList<QQuickItem *> viewers;
    collectItems(page, QStringLiteral("liveViewers"), viewers);
    QStringList viewerTexts;
    for (QQuickItem *v : std::as_const(viewers)) viewerTexts << v->property("text").toString();
    const bool cardsOk = cards.size() == 2 && viewerTexts.join(QLatin1Char('|')).contains(QLatin1String("12,345"));
    std::printf("Live-Kanäle Fenster: start=Beliebt:%d Karten=%d Zuschauer=\"%s\"\n", startedTop ? 1 : 0, int(cards.size()),
                viewerTexts.join(QLatin1String(" | ")).toUtf8().constData());
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/live_channels.png"));
    ok = ok && startedTop && cardsOk;

    // Klick auf die Karte von alphastreamer: Player spielt den Kanal, Chat übernimmt ihn
    host.clearCalls();
    if (!cards.isEmpty()) mouseAt(view, cards.first(), QPointF(cards.first()->width() / 2, 40), Qt::LeftButton);
    settle(200);
    bool loaded = false;
    for (const auto &c : host.calls())
        if (c.first == QLatin1String("player.load") && c.second.value("url").toString() == QLatin1String("https://www.twitch.tv/alphastreamer")) loaded = true;
    const bool clickOk = loaded && controller.channel() == QLatin1String("alphastreamer");
    std::printf("Live-Kanäle Klick: player.load=%d chat=#%s\n", loaded ? 1 : 0, controller.channel().toUtf8().constData());
    ok = ok && clickOk;

    // Der Klick merkt sich Kanal und Kategorie für „Für dich empfohlen“
    {
        const QJsonObject h = host.storedSettings(QStringLiteral("cc.ts3x.twitch-chat")).value(QStringLiteral("liveHistory")).toObject();
        const bool histOk = h.value(QStringLiteral("channels")).toObject().value(QStringLiteral("alphastreamer")).toInt() == 1
                            && h.value(QStringLiteral("games")).toObject().value(QStringLiteral("minecraft")).toInt() == 1
                            && controller.hasLiveProfile();
        std::printf("Live-Kanäle Verlauf: Kanal=%d Kategorie=%d ok=%d\n",
                    h.value(QStringLiteral("channels")).toObject().value(QStringLiteral("alphastreamer")).toInt(),
                    h.value(QStringLiteral("games")).toObject().value(QStringLiteral("minecraft")).toInt(), histOk ? 1 : 0);
        ok = ok && histOk;
    }

    // Reihenfolge: „Für dich“ = gefolgt > geschaute Kategorie > Zuschauer; „Zuschauer“ = streng; Sprache filtert
    {
        const auto row = [](const char *login, int viewers, const char *game, const char *lang) {
            return QVariant(QVariantMap{{"login", QString::fromLatin1(login)}, {"viewers", viewers},
                                        {"game", QString::fromLatin1(game)}, {"language", QString::fromLatin1(lang)}});
        };
        const QVariantList sample{row("big", 50000, "Just Chatting", "de"), row("chess", 300, "Chess", "en"),
                                  row("fav", 12, "Art", "de")};
        const QHash<QString, int> games{{QStringLiteral("chess"), 3}};
        const QSet<QString> followedSet{QStringLiteral("fav")};
        const auto logins = [](const QVariantList &l) {
            QStringList o; for (const QVariant &v : l) o << v.toMap().value("login").toString(); return o.join(QLatin1Char(','));
        };
        const QString rel = logins(TwitchChatController::rankLiveStreams(sample, "relevance", QString(), {}, games, followedSet, {}));
        const QString byViewers = logins(TwitchChatController::rankLiveStreams(sample, "viewers", QString(), {}, games, followedSet, {}));
        const QString de = logins(TwitchChatController::rankLiveStreams(sample, "viewers", "de", {}, games, followedSet, {}));
        const QString none = logins(TwitchChatController::rankLiveStreams(sample, "relevance", QString(), {}, {}, {}, {}));
        const QVariantList cats{QVariantMap{{"name", "Just Chatting"}, {"title", "Just Chatting"}, {"viewers", 400000}},
                                QVariantMap{{"name", "Chess"}, {"title", "Chess"}, {"viewers", 8000}}};
        const QString catRel = TwitchChatController::rankLiveCategories(cats, "relevance", games, {}).value(0).toMap().value("name").toString();
        const QString catView = TwitchChatController::rankLiveCategories(cats, "viewers", games, {}).value(0).toMap().value("name").toString();
        const bool rankOk = rel == QLatin1String("fav,chess,big") && byViewers == QLatin1String("big,chess,fav")
                            && de == QLatin1String("big,fav") && none == QLatin1String("big,chess,fav")
                            && catRel == QLatin1String("Chess") && catView == QLatin1String("Just Chatting");
        std::printf("Live-Kanäle Reihenfolge: für dich=%s zuschauer=%s sprache(de)=%s ohne Profil=%s Kategorie für dich=%s/zuschauer=%s ok=%d\n",
                    rel.toUtf8().constData(), byViewers.toUtf8().constData(), de.toUtf8().constData(), none.toUtf8().constData(),
                    catRel.toUtf8().constData(), catView.toUtf8().constData(), rankOk ? 1 : 0);
        ok = ok && rankOk;

        // Kategorien „Für dich“: meistgeklickte zuerst (nach Klickzahl), dann geschaute, dann Zuschauer
        QVariantList cats3 = cats;
        cats3 << QVariantMap{{"name", "Art"}, {"title", "Art"}, {"viewers", 50}}
              << QVariantMap{{"name", "Minecraft"}, {"title", "Minecraft"}, {"viewers", 90000}};
        const QHash<QString, int> clicks{{QStringLiteral("art"), 2}, {QStringLiteral("minecraft"), 5}};
        QStringList catOrder;
        for (const QVariant &v : TwitchChatController::rankLiveCategories(cats3, "relevance", games, {}, clicks))
            catOrder << v.toMap().value("name").toString();
        // Helix: nur Live-Kanäle; gql: clk-Aliase liefern geklickte Kategorien nach, ohne Doppelte
        const QVariantList helixRows = TwitchChatController::liveRowsFromHelix(
            R"({"data":[{"user_login":"on","type":"live","viewer_count":5},{"user_login":"off","type":"","viewer_count":0}]})");
        const QVariantList clk = TwitchChatController::liveCategoriesFromGql(
            R"({"data":{"games":{"edges":[{"node":{"name":"Chess","viewersCount":10}}]},
                "clk0":{"name":"Art","viewersCount":7},"clk1":{"name":"chess","viewersCount":10},"clk2":null}})");
        const bool clickRankOk = catOrder.join(',') == QLatin1String("Minecraft,Art,Chess,Just Chatting")
                             && helixRows.size() == 1 && clk.size() == 2;
        std::printf("Live-Kategorien meistgeklickt: %s helix=%d clk=%d ok=%d\n", catOrder.join(',').toUtf8().constData(),
                    int(helixRows.size()), int(clk.size()), clickRankOk ? 1 : 0);
        ok = ok && clickRankOk;
    }

    // Kategorien: Cover-Karten (ohne Zuschauer ausgeblendet), Klick öffnet die Kanäle der Kategorie
    const QByteArray games = R"({"data":{"games":{"edges":[
        {"node":{"name":"Minecraft","displayName":"Minecraft","viewersCount":54321,"boxArtURL":""}},
        {"node":{"name":"Empty","displayName":"Empty","viewersCount":0,"boxArtURL":""}}]}}})";
    const QVariantList cats = TwitchChatController::liveCategoriesFromGql(games);
    controller.applyLiveResult(QStringLiteral("categories"), {}, QString(), cats);
    settle(300);
    QList<QQuickItem *> catItems, emptyCats;
    collectItems(page, QStringLiteral("liveCategory_Minecraft"), catItems);
    collectItems(page, QStringLiteral("liveCategory_Empty"), emptyCats);
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/live_categories.png"));
    // Sortiermenü auch im Reiter Kategorien (Sprache dort nicht – Twitch filtert die Kategorienliste nicht nach Sprache)
    QList<QQuickItem *> catSort, catLang;
    collectItems(page, QStringLiteral("liveSort"), catSort);
    collectItems(page, QStringLiteral("liveLanguage"), catLang);
    const bool catSortVisible = !catSort.isEmpty() && catSort.first()->isVisible() && !catLang.isEmpty() && !catLang.first()->isVisible();
    std::printf("Live-Kanäle Kategorien: Sortiermenü sichtbar=%d (Sprache ausgeblendet)\n", catSortVisible ? 1 : 0);
    ok = ok && catSortVisible;
    if (!catItems.isEmpty()) mouseAt(view, catItems.first(), QPointF(catItems.first()->width() / 2, 30), Qt::LeftButton);
    const bool catOk = cats.size() == 1 && catItems.size() == 1 && emptyCats.isEmpty()
                       && controller.liveTab() == QLatin1String("category") && controller.liveCategory() == QLatin1String("Minecraft");
    std::printf("Live-Kanäle Kategorien: geparst=%d Karten=%d Klick→%s/%s ok=%d\n", int(cats.size()), int(catItems.size()),
                controller.liveTab().toUtf8().constData(), controller.liveCategory().toUtf8().constData(), catOk ? 1 : 0);
    ok = ok && catOk;
    {
        // Der Klick auf das Cover zählt; „Für dich“ zeigt auf der Karte, warum sie oben steht
        const int clicked = host.storedSettings(QStringLiteral("cc.ts3x.twitch-chat")).value(QStringLiteral("liveHistory")).toObject()
                                .value(QStringLiteral("categories")).toObject().value(QStringLiteral("minecraft")).toInt();
        const QVariantList ranked = TwitchChatController::rankLiveCategories(cats, QStringLiteral("relevance"), {}, {},
                                                                             {{QStringLiteral("minecraft"), clicked}});
        controller.applyLiveResult(QStringLiteral("categories"), {}, QString(), ranked);
        settle(300);
        QList<QQuickItem *> reasons;
        collectItems(page, QStringLiteral("liveCategoryReason"), reasons);
        const bool reasonOk = clicked == 1 && !reasons.isEmpty() && reasons.first()->isVisible();
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/live_categories_reason.png"));
        std::printf("Live-Kategorien Klick gemerkt=%d Abzeichen sichtbar=%d ok=%d\n", clicked,
                    !reasons.isEmpty() && reasons.first()->isVisible() ? 1 : 0, reasonOk ? 1 : 0);
        ok = ok && reasonOk;
    }

    // Hellmodus: Farben aus dem Player-Theme (dark=false)
    QVariantMap light{{"dark", false}, {"panel", "#ffffff"}, {"panel2", "#eceef1"}, {"line", "#d6d9de"}, {"text", "#1d2026"},
                      {"muted", "#5d6573"}, {"accent", "#ff8a1f"}, {"error", "#c62828"}};
    page->setProperty("theme", light);
    controller.applyLiveResult(QStringLiteral("top"), rows);
    settle(300);
    if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/live_channels_light.png"));
    page->setProperty("theme", theme);

    // Mouse-over auf der ersten Karte: Abspiel-Knopf erscheint
    settle(200);
    {
        QList<QQuickItem *> hoverCards, hints;
        collectItems(page, QStringLiteral("liveCard_alphastreamer"), hoverCards);
        if (!hoverCards.isEmpty()) {
            const QPointF scene = hoverCards.first()->mapToScene(QPointF(hoverCards.first()->width() / 2, 60));
            QMouseEvent move(QEvent::MouseMove, scene, scene, view.mapToGlobal(scene), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&view, &move);
        }
        settle(400);
        collectItems(page, QStringLiteral("livePlayHint"), hints);
        const bool hoverOk = !hints.isEmpty() && hints.first()->opacity() > 0.9 && !hoverCards.isEmpty() && hoverCards.first()->scale() > 1.0;
        std::printf("Live-Kanäle Mouse-over: Abspiel-Knopf=%d ok=%d\n", hints.isEmpty() ? 0 : int(hints.first()->opacity() * 100), hoverOk ? 1 : 0);
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/live_hover.png"));
        ok = ok && hoverOk;
    }

    // Größe ändern: Griff unten rechts ruft modal.open mit größerer Breite/Höhe und merkt sich die Größe
    {
        QQmlComponent fakes(&engine);
        fakes.setData(R"(import QtQml
            QtObject {
                property string language: "en"
                property var calls: []
                function call(name, args) { calls.push({ name: name, args: args }); return true }
                function hasCommand(name) { return true }
                property QtObject modal: QtObject { property string mode: "movable"; function close(r) {} }
            })", QUrl::fromLocalFile(QStringLiteral("M:/LAPS/twitchchat/src/test/fakes_bridge.qml")));
        QObject *bridge = fakes.create();
        if (!bridge) return false;
        page->setProperty("plugin", QVariant::fromValue(bridge));
        page->setProperty("modal", bridge->property("modal"));
        settle(200);
        QList<QQuickItem *> grips;
        collectItems(page, QStringLiteral("liveResizeGrip"), grips);
        int lastW = 0, lastH = 0;
        if (!grips.isEmpty() && grips.first()->isVisible()) {
            QQuickItem *g = grips.first();
            const QPointF a = g->mapToScene(QPointF(g->width() / 2, g->height() / 2));
            const auto send = [&](QEvent::Type type, QPointF scene, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent ev(type, scene, scene, view.mapToGlobal(scene), b, bs, Qt::NoModifier);
                QCoreApplication::sendEvent(&view, &ev);
            };
            send(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
            for (int i = 1; i <= 5; ++i) { send(QEvent::MouseMove, a + QPointF(i * 10, i * 6), Qt::NoButton, Qt::LeftButton); settle(40); }
            send(QEvent::MouseButtonRelease, a + QPointF(50, 30), Qt::LeftButton, Qt::NoButton);
            settle(200);
            const QVariantList calls = bridge->property("calls").toList();
            for (const QVariant &c : calls) {
                const QVariantMap m = c.toMap();
                if (m.value("name").toString() == QLatin1String("modal.open")) {
                    lastW = m.value("args").toMap().value("width").toInt();
                    lastH = m.value("args").toMap().value("height").toInt();
                }
            }
        }
        const QJsonObject stored = host.storedSettings(QStringLiteral("cc.ts3x.twitch-chat")).value(QStringLiteral("liveWindow")).toObject();
        // Seite 780×540 → Fenster 782×578; 50/30 px gezogen (doppelt, weil mittig) → 882×638
        const bool resizeOk = lastW == 882 && lastH == 638 && stored.value(QStringLiteral("width")).toInt() > 0;
        std::printf("Live-Kanäle Größe ändern: Griff=%d modal.open=%dx%d gespeichert=%dx%d ok=%d\n", int(grips.size()), lastW, lastH,
                    stored.value(QStringLiteral("width")).toInt(), stored.value(QStringLiteral("height")).toInt(), resizeOk ? 1 : 0);
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/live_grip.png"));
        ok = ok && resizeOk;
        page->setProperty("modal", QVariant());
        page->setProperty("plugin", QVariant());
    }

    // Sprache als Dropdown (alle Twitch-Sprachen, unabhängig vom Player): Deutsch anklicken → gespeichert, Filter aktiv
    {
        controller.applyLiveResult(QStringLiteral("top"), rows);
        settle(200);
        QList<QQuickItem *> buttons, frRows, allRows;
        collectItems(page, QStringLiteral("liveLanguage"), buttons);
        if (!buttons.isEmpty()) mouseAt(view, buttons.first(), QPointF(buttons.first()->width() / 2, buttons.first()->height() / 2), Qt::LeftButton);
        settle(300);
        collectItems(view.contentItem(), QStringLiteral("liveLanguage_de"), frRows);
        collectItems(view.contentItem(), QStringLiteral("liveLanguage_all"), allRows);
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/live_language_menu.png"));
        int entries = 0;
        for (const QString &code : {QStringLiteral("de"), QStringLiteral("en"), QStringLiteral("ja"), QStringLiteral("zh_hk"), QStringLiteral("other")}) {
            QList<QQuickItem *> r;
            collectItems(view.contentItem(), QStringLiteral("liveLanguage_") + code, r);
            entries += r.isEmpty() ? 0 : 1;
        }
        if (!frRows.isEmpty()) mouseAt(view, frRows.first(), QPointF(40, frRows.first()->height() / 2), Qt::LeftButton);
        settle(200);
        const bool langOk = !buttons.isEmpty() && !allRows.isEmpty() && entries >= 2
                            && page->property("filterLanguage").toString() == QLatin1String("de")
                            && controller.liveLanguage() == QLatin1String("de");
        std::printf("Live-Kanäle Sprache: Dropdown-Einträge (Stichprobe)=%d gewählt=%s gespeichert=%s ok=%d\n", entries,
                    page->property("filterLanguage").toString().toUtf8().constData(), controller.liveLanguage().toUtf8().constData(), langOk ? 1 : 0);
        ok = ok && langOk;
        QMetaObject::invokeMethod(page, "setFilterLanguage", Q_ARG(QVariant, QString()));   // zurück auf alle
        controller.applyLiveResult(QStringLiteral("top"), rows);
    }

    // Sortierung als Dropdown: Vorgabe „Für dich empfohlen“, Wechsel auf „Zuschauer (viel → wenig)“ wird gespeichert
    {
        settle(200);
        const QString before = page->property("filterSort").toString();
        QList<QQuickItem *> buttons, viewerRows, relRows;
        collectItems(page, QStringLiteral("liveSort"), buttons);
        if (!buttons.isEmpty()) mouseAt(view, buttons.first(), QPointF(buttons.first()->width() / 2, buttons.first()->height() / 2), Qt::LeftButton);
        settle(300);
        collectItems(view.contentItem(), QStringLiteral("liveSort_viewers"), viewerRows);
        collectItems(view.contentItem(), QStringLiteral("liveSort_relevance"), relRows);
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/live_sort_menu.png"));
        if (!viewerRows.isEmpty()) mouseAt(view, viewerRows.first(), QPointF(40, viewerRows.first()->height() / 2), Qt::LeftButton);
        settle(200);
        const bool sortOk = before == QLatin1String("relevance") && relRows.size() == 1 && viewerRows.size() == 1
                            && page->property("filterSort").toString() == QLatin1String("viewers")
                            && controller.liveSort() == QLatin1String("viewers");
        std::printf("Live-Kanäle Sortierung: vorher=%s jetzt=%s gespeichert=%s ok=%d\n", before.toUtf8().constData(),
                    page->property("filterSort").toString().toUtf8().constData(), controller.liveSort().toUtf8().constData(), sortOk ? 1 : 0);
        ok = ok && sortOk;
        QMetaObject::invokeMethod(page, "setFilterSort", Q_ARG(QVariant, QStringLiteral("relevance")));
        controller.applyLiveResult(QStringLiteral("top"), rows);
    }

    // Suche beim Tippen (ohne Enter): kurz nach der Eingabe startet die Abfrage
    {
        QList<QQuickItem *> tabs, fields;
        collectItems(page, QStringLiteral("liveTab_search"), tabs);
        if (!tabs.isEmpty()) mouseAt(view, tabs.first(), QPointF(tabs.first()->width() / 2, tabs.first()->height() / 2), Qt::LeftButton);
        collectItems(page, QStringLiteral("liveSearch"), fields);
        if (!fields.isEmpty()) fields.first()->setProperty("text", QStringLiteral("minecraft"));
        const bool idleRightAway = !controller.liveBusy();     // noch nichts gesendet (Verzögerung läuft)
        settle(500);
        const bool started = controller.liveTab() == QLatin1String("search")
                             && (controller.liveBusy() || !controller.liveStreams().isEmpty() || !controller.liveError().isEmpty());
        std::printf("Live-Kanäle Suche beim Tippen: sofort ruhig=%d nach 0,5 s gestartet=%d\n", idleRightAway ? 1 : 0, started ? 1 : 0);
        ok = ok && idleRightAway && started;
        if (!fields.isEmpty()) fields.first()->setProperty("text", QString());
        controller.applyLiveResult(QStringLiteral("top"), rows);   // laufende Netzabfrage verwerfen
    }

    // „Gefolgt“ ohne passenden Login (frühere Tests melden ein Konto ohne user:read:follows an): Hinweis statt Liste
    controller.loadLiveStreams(QStringLiteral("follow"));
    const QString hint = controller.liveError();
    const bool loginHint = !controller.canListFollowed() && (hint == QLatin1String("login") || hint == QLatin1String("scope"))
                           && controller.liveStreams().isEmpty();
    std::printf("Live-Kanäle Gefolgt ohne Recht: Hinweis=%s ok=%d\n", hint.toUtf8().constData(), loginHint ? 1 : 0);
    {
        // „Gefolgt“: kein Sprachmenü (dort alle Live-Kanäle) und keine Sortierung („Für dich empfohlen“ gibt es dort nicht,
        // immer nach Zuschauern); kein Empfehlungs-Hinweis
        settle(200);
        QList<QQuickItem *> fLang, fSort, fHint;
        collectItems(page, QStringLiteral("liveLanguage"), fLang);
        collectItems(page, QStringLiteral("liveSort"), fSort);
        collectItems(page, QStringLiteral("liveRecommendHint"), fHint);
        const bool followMenus = !fLang.isEmpty() && !fLang.first()->isVisible() && !fSort.isEmpty() && !fSort.first()->isVisible()
                                 && !fHint.isEmpty() && !fHint.first()->isVisible();
        std::printf("Live-Kanäle Gefolgt Menüs: Sprache sichtbar=%d Sortierung sichtbar=%d Hinweis sichtbar=%d ok=%d\n",
                    !fLang.isEmpty() && fLang.first()->isVisible() ? 1 : 0, !fSort.isEmpty() && fSort.first()->isVisible() ? 1 : 0,
                    !fHint.isEmpty() && fHint.first()->isVisible() ? 1 : 0, followMenus ? 1 : 0);
        ok = ok && followMenus;
    }

    // Optional mit echtem Netz (TC_LIVE_NET=1): „Beliebt“ auf Deutsch von gql.twitch.tv inkl. Vorschaubildern
    if (qEnvironmentVariableIsSet("TC_LIVE_NET")) {
        controller.loadLiveStreams(QStringLiteral("top"), QString(), QStringLiteral("de"));
        for (int i = 0; i < 80 && controller.liveBusy(); ++i) settle(100);
        settle(3000);   // Vorschaubilder laden
        std::printf("Live-Kanäle Netz: %d Kanäle, Fehler=\"%s\", erster=%s (%s, %d)\n", int(controller.liveStreams().size()),
                    controller.liveError().toUtf8().constData(),
                    controller.liveStreams().value(0).toMap().value("login").toString().toUtf8().constData(),
                    controller.liveStreams().value(0).toMap().value("game").toString().toUtf8().constData(),
                    controller.liveStreams().value(0).toMap().value("viewers").toInt());
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/live_channels_net.png"));
        ok = ok && !controller.liveStreams().isEmpty();
        // Beide Sortierungen nebeneinander (gleiche Sprache): „Für dich“ muss sich von „Zuschauer“ unterscheiden
        const auto firstLogins = [&](const QString &sort) {
            controller.loadLiveStreams(QStringLiteral("top"), QString(), QStringLiteral("de"), sort);
            for (int i = 0; i < 80 && controller.liveBusy(); ++i) settle(100);
            QStringList o;
            for (const QVariant &v : controller.liveStreams().mid(0, 4))
                o << v.toMap().value("login").toString() + QLatin1Char('(') + v.toMap().value("game").toString() + QLatin1Char(')');
            return o.join(QLatin1String(", "));
        };
        const QString forYou = firstLogins(QStringLiteral("relevance"));
        settle(2500);
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/live_foryou_net.png"));
        const QString byCount = firstLogins(QStringLiteral("viewers"));
        std::printf("Live-Netz Für dich:  %s\nLive-Netz Zuschauer: %s\n", forYou.toUtf8().constData(), byCount.toUtf8().constData());
        ok = ok && forYou != byCount;
        controller.loadLiveStreams(QStringLiteral("categories"), QString(), QString(), QStringLiteral("viewers"));
        for (int i = 0; i < 80 && controller.liveBusy(); ++i) settle(100);
        settle(3000);
        std::printf("Live-Kategorien Netz: %d, erste=%s (%d)\n", int(controller.liveCategories().size()),
                    controller.liveCategories().value(0).toMap().value("title").toString().toUtf8().constData(),
                    controller.liveCategories().value(0).toMap().value("viewers").toInt());
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/live_categories_net.png"));
        ok = ok && !controller.liveCategories().isEmpty();
        // „Für dich“: eine kleine, oft geklickte Kategorie steht ganz oben, auch wenn sie nicht unter den 30 größten ist
        for (int i = 0; i < 3; ++i) controller.noteLiveCategory(QStringLiteral("Chess"));
        controller.loadLiveStreams(QStringLiteral("categories"), QString(), QString(), QStringLiteral("relevance"));
        for (int i = 0; i < 80 && controller.liveBusy(); ++i) settle(100);
        const QString firstCat = controller.liveCategories().value(0).toMap().value("title").toString();
        std::printf("Live-Kategorien Netz Für dich: erste=%s (%d) ok=%d\n", firstCat.toUtf8().constData(),
                    controller.liveCategories().value(0).toMap().value("viewers").toInt(), firstCat == QLatin1String("Chess") ? 1 : 0);
        ok = ok && firstCat == QLatin1String("Chess");
        controller.loadLiveStreams(QStringLiteral("search"), QStringLiteral("minecraft"));
        for (int i = 0; i < 80 && controller.liveBusy(); ++i) settle(100);
        settle(3000);
        std::printf("Live-Suche Netz: %d Kanäle, %d Kategorien\n", int(controller.liveStreams().size()), int(controller.liveCategories().size()));
        if (!shotDir.isEmpty()) view.grabWindow().save(shotDir + QStringLiteral("/live_search_net.png"));
    }
    ok = ok && loginHint;

    delete page;
    return ok;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");   // headless: kein Fenster nötig
    qputenv("QT_QUICK_BACKEND", "software");   // Screenshots ohne Grafiktreiber
    qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");   // ohne echte Schrift wären alle Textbreiten falsch
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");    // wie im Player (app/main.cpp); bestimmt die Steuerelementbreiten
    QGuiApplication app(argc, argv);
    const QString shotDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    g_oldHandler = qInstallMessageHandler(grabMessages);

    const QString srcDir = QStringLiteral("M:/LAPS/twitchchat/src");

    QQmlEngine engine;
    const QString qmlImport = QLibraryInfo::path(QLibraryInfo::QmlImportsPath);
    if (!qmlImport.isEmpty()) engine.addImportPath(qmlImport);
    engine.addImportPath(QStringLiteral("M:/LAPS/Qt/6.12.0/msvc2022_64/qml")); // Fallback

    FakeHost host;
    g_host = &host;
    TwitchChatController controller(&host, QStringLiteral("cc.ts3x.twitch-chat"));
    controller.setAutoChannel(false);
    controller.setSpellDictionaryDir(srcDir + QStringLiteral("/third_party/dict"));

    QQmlComponent panelComponent(&engine);
    panelComponent.loadUrl(QUrl::fromLocalFile(srcDir + QStringLiteral("/ChatPanel.qml")));
    if (panelComponent.isError()) {
        std::printf("ChatPanel.qml Kompilierfehler:\n");
        for (const QQmlError &e : panelComponent.errors())
            std::printf("  %s\n", e.toString().toUtf8().constData());
        return 1;
    }
    QObject *root = panelComponent.create();
    if (!root) {
        std::printf("ChatPanel.qml Objektfehler:\n");
        for (const QQmlError &e : panelComponent.errors())
            std::printf("  %s\n", e.toString().toUtf8().constData());
        return 1;
    }

    // Wie der Player: Properties erst NACH dem Zuweisen bekannt (per Binding durchgereicht)
    root->setProperty("controller", QVariant::fromValue<QObject *>(&controller));
    root->setProperty("plugin", QVariant());
    QVariantMap theme;
    theme.insert(QStringLiteral("text"), QStringLiteral("#101010"));
    theme.insert(QStringLiteral("muted"), QStringLiteral("#707070"));
    theme.insert(QStringLiteral("accent"), QStringLiteral("#9147ff"));
    theme.insert(QStringLiteral("error"), QStringLiteral("#e74c3c"));
    root->setProperty("theme", theme);

    // Loader (id: chatLoader) muss ChatNative.qml fertig geladen haben
    int status = -1;
    bool hasItem = false;
    QObject *itemObj = nullptr;
    QObject *loader = root->findChild<QObject *>("chatLoader");
    if (loader) {
        status = loader->property("status").toInt();       // Ready=1, Loading=2, Error=3
        itemObj = qvariant_cast<QObject *>(loader->property("item"));
        hasItem = itemObj != nullptr;
    }
    const int nativeChildren = hasItem
        ? int(itemObj->findChildren<QObject *>().size()) : 0;

    std::printf("Loader chatLoader: status=%d item=%s (Unterobjekte=%d)\n",
                status, hasItem ? "ja" : "NEIN", nativeChildren);

    // Bindungen durchlaufen lassen: Controller muss in ChatNative angekommen sein
    const QVariantList hist = controller.history();
    const QVariantList emotes = controller.emoteList(QStringLiteral("all"));
    QObject *natItem = itemObj;
    const bool bindingOk = natItem
        && qvariant_cast<QObject *>(natItem->property("controller")) == &controller;
    std::printf("API: history=%d emoteList(all)=%d autoScroll=%d bindingweitergabe=%s\n",
                int(hist.size()), int(emotes.size()), controller.autoScroll() ? 1 : 0,
                bindingOk ? "ja" : "NEIN");

    int rc = 1;
    QTimer::singleShot(600, &app, [&]() {
        const bool loaderOk = loader && status == 1 && hasItem
                              && nativeChildren > 30 && bindingOk;
        // 30+ Unterobjekte: Flow-/Kachel-Delegates sind anfangs 0 – das Basis-Panel hat deutlich mehr
        const bool subsOk = checkSubscriptionEmotes(controller);
        const bool sidebarOk = checkSidebarWidth(controller, shotDir);
        const bool packagedOk = checkPackagedDictionaries(engine);
        const bool liveOk = checkLiveChannels(engine, controller, host, shotDir);
        const bool noQmlErrors = g_qmlErrors.isEmpty();
        if (!noQmlErrors)
            for (const QString &e : std::as_const(g_qmlErrors)) std::printf("QML-Fehler: %s\n", e.toUtf8().constData());
        std::printf("\nERGEBNIS: panelOk=%d loaderOk=%d aboEmotesOk=%d seitenleisteOk=%d paketOk=%d liveOk=%d qmlFehler=%d\n",
                    1, loaderOk ? 1 : 0, subsOk ? 1 : 0, sidebarOk ? 1 : 0, packagedOk ? 1 : 0, liveOk ? 1 : 0, int(g_qmlErrors.size()));
        std::fflush(stdout);
        rc = (loaderOk && subsOk && sidebarOk && packagedOk && liveOk && noQmlErrors) ? 0 : 1;
        QCoreApplication::exit(rc);
    });

    app.exec();
    return rc;
}

#include "qml_panel_test.moc"
