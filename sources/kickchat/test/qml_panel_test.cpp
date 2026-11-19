// Lädt ChatPanel.qml + KickChat.qml offscreen in einer echten QML-Engine (wie im Player),
// verbindet mit einem echten Kick-Kanal, zählt QML-Warnungen und speichert Screenshots.
// Aufruf: qml_panel_test.exe [kanal] [sekunden] [ausgabe.png]

#include "KickChatController.h"
#include "fakehost.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstdio>

static int g_warnings = 0;

// Popup-Inhalte hängen nur im Item-Baum, nicht als QObject-Kinder: dort suchen
static void findItems(QQuickItem *item, const QString &name, QList<QQuickItem *> &out)
{
    if (!item) return;
    if (item->objectName() == name) out.append(item);
    for (QQuickItem *c : item->childItems()) findItems(c, name, out);
}

static void handler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        if (msg.contains(QLatin1String("qrc:")) || msg.contains(QLatin1String(".qml")) || type != QtWarningMsg) ++g_warnings;
    }
    std::fprintf(stderr, "%s\n", msg.toUtf8().constData());
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qInstallMessageHandler(handler);
    QGuiApplication app(argc, argv);
    const QString channel = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("jomotic");
    const int seconds = argc > 2 ? atoi(argv[2]) : 25;
    const QString shot = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QStringLiteral("panel.png");

    FakeHost host;
    host.preloadSettings(QStringLiteral("cc.ts3x.kick-chat"), QJsonObject{{QStringLiteral("showTimestamps"), true}});
    KickChatController ctrl(&host, QStringLiteral("cc.ts3x.kick-chat"), QDir::currentPath() + QStringLiteral("/testdata_kick"));

    QQuickWindow window;
    window.resize(380, 720);
    window.setColor(QColor("#0f1113"));
    QQmlEngine engine;
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(QML_DIR "/ChatPanel.qml")));
    QObject *obj = comp.create();
    if (!obj) {
        std::printf("FAIL: ChatPanel.qml lädt nicht: %s\n", comp.errorString().toUtf8().constData());
        return 1;
    }
    auto *item = qobject_cast<QQuickItem *>(obj);
    item->setParentItem(window.contentItem());
    item->setSize(QSizeF(380, 720));
    QVariantMap theme{{QStringLiteral("text"), QStringLiteral("#e8e8e8")}, {QStringLiteral("muted"), QStringLiteral("#9a9a9a")},
                      {QStringLiteral("accent"), QStringLiteral("#53fc18")}, {QStringLiteral("error"), QStringLiteral("#ff5c5c")}};
    QQmlEngine::setObjectOwnership(&ctrl, QQmlEngine::CppOwnership);
    item->setProperty("theme", theme);
    item->setProperty("controller", QVariant::fromValue<QObject *>(&ctrl));
    window.show();

    auto *loader = item->findChild<QObject *>(QStringLiteral("chatLoader"));
    QObject *inner = loader ? loader->property("item").value<QObject *>() : nullptr;
    std::printf("KickChat.qml geladen: %s\n", inner ? "ja" : "NEIN");
    if (!inner) return 1;

    ctrl.setSpellDictionaryDir(QStringLiteral(DICT_DIR));
    ctrl.setChannel(channel);
    const QString base = shot.left(shot.size() - 4);
    auto save = [&](const QString &suffix) {
        const QString f = base + suffix + QStringLiteral(".png");
        window.grabWindow().save(f);
        std::printf("Screenshot: %s\n", f.toUtf8().constData());
    };
    bool focusReleased = false, userCardOk = false, notifyOk = false, botEditorOpen = false, botColorOk = false;
    const int step = seconds * 1000 / 6;
    QTimer::singleShot(step * 2, [&]() {
        // simulierte Ereignisse: Umfrage, Follow, Event-Bot
        ctrl.handlePusherFrame(QStringLiteral(R"({"event":"App\\Events\\PollUpdateEvent","data":"{\"poll\":{\"title\":\"Welche Map?\",\"options\":[{\"id\":0,\"label\":\"Verdansk\",\"votes\":12},{\"id\":1,\"label\":\"Rebirth\",\"votes\":5}],\"duration\":60,\"remaining\":45,\"result_display_duration\":15}}"})"));
        ctrl.handlePusherFrame(QStringLiteral(R"({"event":"App\\Events\\FollowersUpdated","data":"{\"followersCount\":4719,\"channel_id\":1,\"username\":\"NeuerFan\",\"followed\":true}"})"));
        ctrl.handlePusherFrame(QStringLiteral(R"({"event":"App\\Events\\ChatMessageEvent","data":"{\"id\":\"bot-1\",\"content\":\"Danke fuer den Follow!\",\"type\":\"message\",\"created_at\":\"2026-11-09T21:00:00Z\",\"sender\":{\"id\":9,\"username\":\"BotRix\",\"slug\":\"botrix\",\"identity\":{\"color\":\"#AAAAAA\",\"badges\":[{\"type\":\"bot\",\"text\":\"Bot\"}]}}}"})"));
        save(QStringLiteral("_chat"));
        // Emotes im Eingabefeld: Text mit Kick-Emote-Namen setzen
        if (auto *sf = inner->findChild<QObject *>(QStringLiteral("sendField")))
            QTimer::singleShot(0, sf, [sf]() { sf->setProperty("cursorPosition", sf->property("length")); }),
            sf->setProperty("text", QStringLiteral("Hallo jomoticsalut jomoticliebe Fehlre jomoticcat tesst Stre"));
        // Fokus: Kanal-Feld aktivieren, dann in den Chat klicken → Fokus muss weg
        auto *field = qobject_cast<QQuickItem *>(inner->findChild<QObject *>(QStringLiteral("channelField")));
        if (field) {
            field->forceActiveFocus();
            const bool had = field->hasActiveFocus();
            const QPointF p(190, 400);
            QMouseEvent press(QEvent::MouseButtonPress, p, p, window.mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, p, p, window.mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &press);
            QCoreApplication::sendEvent(&window, &release);
            focusReleased = had && !field->hasActiveFocus();
            std::printf("Fokus vorher: %d, nach Klick daneben: %d\n", int(had), int(field->hasActiveFocus()));
        }
    });
    int inlineCount = 0;
    double maxGapError = -1;
    QTimer::singleShot(step * 2 + step / 2, [&]() {
        auto *sf = qobject_cast<QQuickItem *>(inner->findChild<QObject *>(QStringLiteral("sendField")));
        if (!sf) return;
        const QVariantList ranges = sf->property("emoteRanges").toList();
        inlineCount = int(ranges.size());
        maxGapError = 0;
        for (QObject *o : sf->findChildren<QObject *>(QStringLiteral("inlineEmote"))) {
            auto *it = qobject_cast<QQuickItem *>(o);
            const QVariantMap r = o->property("modelData").toMap();
            const double want = r.value(QStringLiteral("width")).toDouble();
            maxGapError = std::max(maxGapError, std::abs(it->width() - want));
        }
        std::printf("Emotes im Eingabefeld: %d, größte Abweichung Platz/Bildbreite: %.1f px\n", inlineCount, maxGapError);
        save(QStringLiteral("_input"));
    });
    int spellCount = -1, predictionCount = -1;
    QString spellWords, predictionWords;
    QTimer::singleShot(step * 3 - step / 3, [&]() {
        auto *sf = inner->findChild<QObject *>(QStringLiteral("sendField"));
        if (!sf) return;
        const QVariantList ranges = sf->property("spellRanges").toList();
        spellCount = int(ranges.size());
        for (const QVariant &v : ranges) spellWords += v.toMap().value(QStringLiteral("word")).toString() + QLatin1Char(' ');
        const QStringList preds = sf->property("predictions").toStringList();
        predictionCount = int(preds.size());
        predictionWords = preds.join(QLatin1Char(' '));
        std::printf("Rechtschreibung markiert: %d (%s) | Vorschläge für \"Stre\": %d (%s)\n", spellCount,
                    spellWords.toUtf8().constData(), predictionCount, predictionWords.toUtf8().constData());
        save(QStringLiteral("_spell"));
    });
    QTimer::singleShot(step * 3, [&]() {
        auto *card = inner->findChild<QObject *>(QStringLiteral("userCard"));
        const QVariantMap m = ctrl.history().isEmpty() ? QVariantMap{{QStringLiteral("name"), QStringLiteral("jomotic")}}
                                                       : ctrl.history().constLast().toMap();
        if (card) QMetaObject::invokeMethod(card, "openFor", Q_ARG(QVariant, QVariant(m)));
    });
    int grammarCount = -1;
    QString grammarMsg, selected;
    bool dragSelected = false;
    QTimer::singleShot(step * 3 + step / 4, [&]() {
        // Satzbau: doppeltes Wort
        if (auto *sf = inner->findChild<QObject *>(QStringLiteral("sendField")))
            sf->setProperty("text", QStringLiteral("Ich habe habe heute Hunger und so "));
    });
    QTimer::singleShot(step * 3 + step * 3 / 4, [&]() {
        auto *sf = inner->findChild<QObject *>(QStringLiteral("sendField"));
        const QVariantList issues = sf ? sf->property("grammarRanges").toList() : QVariantList();
        grammarCount = int(issues.size());
        if (!issues.isEmpty()) grammarMsg = issues.first().toMap().value(QStringLiteral("message")).toString();
        std::printf("Satzbau-Hinweise: %d (%s)\n", grammarCount, grammarMsg.toUtf8().constData());
        save(QStringLiteral("_grammar"));
        // Markieren: echter Maus-Zug über die letzte sichtbare Chatzeile
        auto *card = inner->findChild<QObject *>(QStringLiteral("userCard"));
        QVariant cardMsg = card ? card->property("msg") : QVariant();
        if (card) QMetaObject::invokeMethod(card, "close");          // modale Karte kurz weg
        auto *view = qobject_cast<QQuickItem *>(inner->findChild<QObject *>(QStringLiteral("chatView")));
        if (view) {
            const QPointF a = view->mapToScene(QPointF(view->width() * 0.35, view->height() - 14));
            const QPointF b = view->mapToScene(QPointF(view->width() * 0.85, view->height() - 14));
            auto send = [&](QEvent::Type t, QPointF p, Qt::MouseButtons bs) {
                QMouseEvent e(t, p, p, window.mapToGlobal(p), Qt::LeftButton, bs, Qt::NoModifier);
                QCoreApplication::sendEvent(&window, &e);
            };
            send(QEvent::MouseButtonPress, a, Qt::LeftButton);
            for (int i = 1; i <= 6; ++i) send(QEvent::MouseMove, a + (b - a) * (i / 6.0), Qt::LeftButton);
            selected = inner->property("selectedChatText").toString();
            dragSelected = !selected.isEmpty();
            std::printf("Markiert per Maus: \"%s\"\n", selected.toUtf8().constData());
            save(QStringLiteral("_select"));
            send(QEvent::MouseButtonRelease, b, Qt::NoButton);
            QMetaObject::invokeMethod(inner, "stopSelecting");
        }
        if (card) QMetaObject::invokeMethod(card, "openFor", Q_ARG(QVariant, cardMsg));
    });
    QTimer::singleShot(step * 4, [&]() {
        auto *card = inner->findChild<QObject *>(QStringLiteral("userCard"));
        userCardOk = card && card->property("opened").toBool() && !card->property("loading").toBool();
        std::printf("Nutzer-Karte offen und geladen: %d\n", int(userCardOk));
        save(QStringLiteral("_user"));
        if (card) QMetaObject::invokeMethod(card, "close");
        auto *np = inner->findChild<QObject *>(QStringLiteral("notifyPopup"));
        if (np) QMetaObject::invokeMethod(np, "open");
    });
    QTimer::singleShot(step * 5, [&]() {
        auto *np = inner->findChild<QObject *>(QStringLiteral("notifyPopup"));
        notifyOk = np && np->property("opened").toBool();
        // Bot bearbeiten: ✎ beim ersten Bot öffnet den Editor (Farbe per Palette oder #RRGGBB)
        QList<QQuickItem *> edits;
        if (np) findItems(np->property("contentItem").value<QQuickItem *>(), QStringLiteral("botEdit"), edits);
        std::printf("Bearbeiten-Knöpfe gefunden: %d\n", int(edits.size()));
        if (!edits.isEmpty()) QMetaObject::invokeMethod(edits.first(), "clicked");
        botEditorOpen = np && !np->property("editingBot").toString().isEmpty();
        // eigene Farbe direkt über den Controller setzen und prüfen
        ctrl.setEventBot(QStringLiteral("BotRix"), QStringLiteral("BotRix"), QStringLiteral("#123abc"), QStringLiteral("BotRix"));
        for (const QVariant &v : ctrl.eventBots())
            if (v.toMap().value(QStringLiteral("name")).toString() == QLatin1String("BotRix"))
                botColorOk = v.toMap().value(QStringLiteral("color")).toString() == QLatin1String("#123abc");
        std::printf("Bot-Editor offen: %d, eigene Bot-Farbe gespeichert: %d\n", int(botEditorOpen), int(botColorOk));
        save(QStringLiteral("_notify"));
        if (np) QMetaObject::invokeMethod(np, "close");
        if (auto *btn = inner->findChild<QObject *>(QStringLiteral("viewerButton")))
            QMetaObject::invokeMethod(btn, "clicked");
    });
    QTimer::singleShot(step * 6, [&]() {
        save(QStringLiteral("_viewers"));
        app.quit();
    });
    app.exec();

    auto *view = inner->findChild<QObject *>(QStringLiteral("chatView"));
    const int rows = view ? view->property("count").toInt() : -1;
    std::printf("Zeilen im Chat: %d, Status: %s, QML-Warnungen: %d\n", rows, ctrl.statusKey().toUtf8().constData(), g_warnings);
    auto *popup = inner->findChild<QObject *>(QStringLiteral("chattersPopup"));
    const QVariantList people = popup ? popup->property("list").toList() : QVariantList();
    const bool popupOpen = popup && popup->property("opened").toBool();
    std::printf("Leute-Liste offen: %s, Einträge: %d (Controller: %d)\n", popupOpen ? "ja" : "NEIN",
                int(people.size()), ctrl.chatterCount());
    for (int i = 0; i < std::min<int>(5, int(people.size())); ++i) {
        const QVariantMap p = people[i].toMap();
        std::printf("  %s %s (%d)\n", p.value(QStringLiteral("roleLabel")).toString().toUtf8().constData(),
                    p.value(QStringLiteral("name")).toString().toUtf8().constData(), p.value(QStringLiteral("count")).toInt());
    }
    std::printf("Fokus freigegeben: %d, Nutzer-Karte: %d, Einstellungen: %d, Umfrage: %d\n", int(focusReleased),
                int(userCardOk), int(notifyOk), int(!ctrl.poll().isEmpty()));
    const bool ok = rows > 0 && g_warnings == 0 && popupOpen && !people.isEmpty() && focusReleased && userCardOk && notifyOk
                    && !ctrl.poll().isEmpty()
                    && inlineCount == 3 && spellWords.contains(QStringLiteral("Fehlre")) && spellWords.contains(QStringLiteral("tesst"))
                    && !spellWords.contains(QStringLiteral("jomotic")) && predictionCount > 0 && botEditorOpen && botColorOk && grammarCount > 0 && dragSelected && maxGapError >= 0 && maxGapError <= 2.0;
    std::printf("%s\n", ok ? "BESTANDEN" : "FEHLGESCHLAGEN");
    delete obj;
    return ok ? 0 : 1;
}
