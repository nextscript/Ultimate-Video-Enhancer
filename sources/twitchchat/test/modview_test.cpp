// Prüft, wie Mod-Aktionen aus dem IRC (CLEARMSG / CLEARCHAT) im Verlauf ankommen – ohne Login.
// Erwartung: Meldungszeile mit Namen/Dauer, betroffene Nachrichten bleiben und tragen deleted + Grund.
#include "fakehost.h"
#include "TwitchChatController.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <cstdio>

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const QString &what)
{
    ok ? ++g_pass : ++g_fail;
    std::printf("%-6s %s\n", ok ? "OK" : "FEHLER", what.toUtf8().constData());
    std::fflush(stdout);
}

static void feed(TwitchChatController &c, const QString &line)
{
    QMetaObject::invokeMethod(&c, "onText", Q_ARG(QString, line + QStringLiteral("\r\n")));
}

static void say(TwitchChatController &c, const QString &user, const QString &id, const QString &text)
{
    feed(c, QStringLiteral("@display-name=%1;id=%2;room-id=1 :%1!%1@%1.tmi.twitch.tv PRIVMSG #testkanal :%3")
                .arg(user, id, text));
}

static QVariantMap byRaw(const TwitchChatController &c, const QString &raw)
{
    for (const QVariant &v : c.history())
        if (v.toMap().value("raw").toString() == raw) return v.toMap();
    return {};
}

static bool hasLine(const TwitchChatController &c, const QString &text)
{
    for (const QVariant &v : c.history())
        if (v.toMap().value("raw").toString() == text) return true;
    return false;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    FakeHost host;
    TwitchChatController c(&host, QStringLiteral("cc.ts3x.twitch-chat"));
    c.setAutoChannel(false);
    c.closeChat();
    c.setChannel(QStringLiteral("testkanal"));
    check(!c.authenticated(), "Test läuft ohne Login");

    say(c, "anna", "m1", "hallo zusammen");
    say(c, "anna", "m2", "zweite nachricht");
    say(c, "bert", "m3", "ich bleibe");
    say(c, "carl", "m4", "carl schreibt");
    say(c, "dora", "m5", "dora schreibt");

    check(byRaw(c, "hallo zusammen").value("deleted").toBool() == false, "Neue Nachricht ist nicht gelöscht");

    feed(c, "@login=anna;room-id=1;target-msg-id=m1 :tmi.twitch.tv CLEARMSG #testkanal :hallo zusammen");
    check(byRaw(c, "hallo zusammen").value("deleted").toBool(), "CLEARMSG: Nachricht m1 als gelöscht markiert");
    check(byRaw(c, "hallo zusammen").value("deletedReason").toString() == "deleted", "CLEARMSG: Grund deleted");
    check(!byRaw(c, "zweite nachricht").value("deleted").toBool(), "CLEARMSG: andere Nachricht bleibt");
    check(hasLine(c, QStringLiteral("Nachricht von anna wurde gelöscht: „hallo zusammen“")), "CLEARMSG: Meldungszeile");

    feed(c, "@ban-duration=600;room-id=1;target-user-id=7 :tmi.twitch.tv CLEARCHAT #testkanal :carl");
    check(byRaw(c, "carl schreibt").value("deletedReason").toString() == "timeout", "Timeout: Nachrichten von carl markiert");
    check(!byRaw(c, "ich bleibe").value("deleted").toBool(), "Timeout: bert bleibt unberührt");
    check(hasLine(c, QStringLiteral("carl wurde für 10 Min. stummgeschaltet (Timeout).")), "Timeout: Meldungszeile mit Dauer");

    feed(c, "@room-id=1;target-user-id=8 :tmi.twitch.tv CLEARCHAT #testkanal :dora");
    check(byRaw(c, "dora schreibt").value("deletedReason").toString() == "ban", "Bann: Nachrichten von dora markiert");
    check(hasLine(c, QStringLiteral("dora wurde gebannt.")), "Bann: Meldungszeile");

    feed(c, "@room-id=1 :tmi.twitch.tv CLEARCHAT #testkanal");
    check(byRaw(c, "ich bleibe").value("deletedReason").toString() == "clear", "Chat geleert: alles markiert");
    check(byRaw(c, "zweite nachricht").value("deleted").toBool(), "Chat geleert: auch anna m2");
    check(hasLine(c, QStringLiteral("Der Chat wurde von einem Moderator geleert.")), "Chat geleert: Meldungszeile");
    check(!byRaw(c, "dora wurde gebannt.").value("deleted").toBool(), "Systemzeilen werden nie durchgestrichen");

    // Optik: echtes Chat-Panel rendern und als Bild ablegen (Pfad als Argument)
    if (argc > 1) {
        TwitchChatController v(&host, QStringLiteral("cc.ts3x.twitch-chat"));
        v.setAutoChannel(false);
        v.closeChat();
        v.setChannel(QStringLiteral("testkanal"));
        QQuickView view;
        view.engine()->addImportPath(QLibraryInfo::path(QLibraryInfo::QmlImportsPath));
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(380, 620);
        view.setColor(Qt::white);
        view.setSource(QUrl::fromLocalFile(QStringLiteral("M:/LAPS/twitchchat/src/ChatPanel.qml")));
        QObject *root = view.rootObject();
        check(root != nullptr, "Panel geladen");
        if (root) {
            root->setProperty("controller", QVariant::fromValue<QObject *>(&v));
            QVariantMap theme{{"text", "#101010"}, {"muted", "#707070"}, {"accent", "#9147ff"}, {"error", "#e74c3c"}};
            root->setProperty("theme", theme);
        }
        view.show();
        const auto pump = [](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) QCoreApplication::processEvents(); };
        pump(800);
        say(v, "anna", "m1", "hallo zusammen Kappa");
        say(v, "bert", "m3", "ich bleibe ganz normal");
        say(v, "carl", "m4", "carl schreibt etwas freches");
        say(v, "dora", "m5", "dora spammt hier");
        say(v, "bert", "m6", "noch eine normale Zeile");
        feed(v, "@login=anna;room-id=1;target-msg-id=m1 :tmi.twitch.tv CLEARMSG #testkanal :hallo zusammen Kappa");
        feed(v, "@ban-duration=600;room-id=1;target-user-id=7 :tmi.twitch.tv CLEARCHAT #testkanal :carl");
        feed(v, "@room-id=1;target-user-id=8 :tmi.twitch.tv CLEARCHAT #testkanal :dora");
        pump(1200);
        const QString file = QString::fromLocal8Bit(argv[1]) + QStringLiteral("/geloescht.png");
        check(view.grabWindow().save(file), "Screenshot " + file);
    }

    std::printf("\nERGEBNIS: %d OK, %d FEHLER\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
