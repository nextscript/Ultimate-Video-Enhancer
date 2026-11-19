// Postfach ohne Netz: Gespräche, Gruppen-Verteiler, Antworten in der richtigen Gruppe,
// EventSub-Nachrichten, doppelte Zustellung, verschlüsselte Ablage und Neustart.
#include "WhisperService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QVariantMap>
#include <cstdio>

static int g_failures = 0;
static void expect(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "ok    " : "FEHLER", what);
    if (!ok) ++g_failures;
}

struct Sent { QString to, text; };

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString store = dir.path() + QStringLiteral("/whispers.dat");
    const QByteArray entropy = "test-whispers";

    QList<Sent> sent;
    QStringList failFor;                       // Empfänger, bei denen Twitch ablehnt
    const auto fakeSender = [&](const QString &to, const QString &text, const std::function<void(bool, const QString &)> &done) {
        sent << Sent{to, text};
        if (failFor.contains(to)) done(false, QStringLiteral("Twitch verlangt eine verifizierte Telefonnummer im Konto"));
        else done(true, QString());
    };

    QString directId, groupId;
    {
        WhisperService w(store, entropy);
        w.setSender(fakeSender);
        expect(w.state() == QStringLiteral("nologin") && w.conversations().isEmpty(), "leeres Postfach ohne Login");
        expect(WhisperService::validLogin(QStringLiteral("abc_12")) && !WhisperService::validLogin(QStringLiteral("a"))
                   && !WhisperService::validLogin(QStringLiteral("Böse")) && !WhisperService::validLogin(QStringLiteral("x y")),
               "Login-Namen werden geprüft");

        // Eingehend: neues Gespräch, ungelesen
        w.receiveWhisper(QStringLiteral("Anna_1"), QStringLiteral("Anna"), QStringLiteral("Hallo du!"), QStringLiteral("w1"));
        directId = QStringLiteral("u:anna_1");
        expect(w.unreadTotal() == 1, "Eingang zählt als ungelesen");
        const QVariantList list = w.conversations();
        expect(list.size() == 1 && list.first().toMap().value("title").toString() == QStringLiteral("Anna")
                   && list.first().toMap().value("lastText").toString() == QStringLiteral("Hallo du!"),
               "Gespräch mit Name und letzter Nachricht");
        w.receiveWhisper(QStringLiteral("anna_1"), QStringLiteral("Anna"), QStringLiteral("Hallo du!"), QStringLiteral("w1"));
        expect(w.messages(directId).size() == 1, "doppelte Zustellung wird verworfen");
        w.markRead(directId);
        expect(w.unreadTotal() == 0, "Lesen setzt den Zähler zurück");

        // Antwort senden
        expect(w.send(directId, QStringLiteral("  Hi Anna  ")), "Antwort wird angenommen");
        expect(sent.size() == 1 && sent.first().to == QStringLiteral("anna_1") && sent.first().text == QStringLiteral("Hi Anna"),
               "Antwort geht an Anna, ohne Leerraum");
        const QVariantMap mine = w.messages(directId).last().toMap();
        expect(mine.value("outgoing").toBool() && mine.value("status").toString() == QStringLiteral("sent"), "eigene Nachricht gilt als zugestellt");
        expect(!w.send(directId, QStringLiteral("   ")), "leere Nachricht wird nicht gesendet");
        expect(w.send(directId, QString(600, QLatin1Char('x'))) && sent.last().text.size() == 500, "Nachricht wird auf 500 Zeichen gekürzt");

        // Gruppe als Verteiler
        sent.clear();
        groupId = w.createGroup(QStringLiteral("Raid-Team"),
                                {QVariantMap{{"login", "Bob"}, {"name", "Bob"}}, QVariantMap{{"login", "carl"}, {"name", "Carl"}},
                                 QVariantMap{{"login", "bob"}, {"name", "Bob"}}, QString("@dora"), QString("kein gültiger")});
        const QVariantMap group = w.conversation(groupId);
        expect(group.value("isGroup").toBool() && group.value("members").toList().size() == 3, "Gruppe mit 3 Mitgliedern (doppelt/ungültig raus)");
        failFor = {QStringLiteral("carl")};
        expect(w.send(groupId, QStringLiteral("Gleich geht's los")), "Gruppen-Nachricht wird angenommen");
        QStringList recipients;
        for (const Sent &s : std::as_const(sent)) recipients << s.to;
        recipients.sort();
        expect(recipients == QStringList({"bob", "carl", "dora"}), "jedes Mitglied bekommt sie einzeln");
        const QVariantMap groupMsg = w.messages(groupId).last().toMap();
        expect(groupMsg.value("status").toString() == QStringLiteral("failed")
                   && groupMsg.value("error").toString().contains(QStringLiteral("Carl"))
                   && groupMsg.value("error").toString().contains(QStringLiteral("Telefonnummer")),
               "Teil-Fehler nennt den Empfänger und den Grund");
        failFor.clear();

        // Antwort eines Mitglieds landet in der Gruppe, Fremde direkt
        w.receiveWhisper(QStringLiteral("bob"), QStringLiteral("Bob"), QStringLiteral("Bin dabei"));
        expect(w.messages(groupId).last().toMap().value("text").toString() == QStringLiteral("Bin dabei")
                   && w.conversation(groupId).value("unread").toInt() == 1,
               "Antwort eines Mitglieds erscheint in der Gruppe");
        expect(w.conversation(QStringLiteral("u:bob")).isEmpty(), "dafür kein zusätzliches Einzelgespräch");
        w.receiveWhisper(QStringLiteral("eve"), QStringLiteral("Eve"), QStringLiteral("Wer bist du?"));
        expect(!w.conversation(QStringLiteral("u:eve")).isEmpty(), "Fremde landen im Einzelgespräch");

        // Mitglied entfernt: antwortet wieder direkt
        w.setGroupMembers(groupId, {QVariantMap{{"login", "bob"}, {"name", "Bob"}}, QVariantMap{{"login", "dora"}, {"name", "dora"}}});
        w.receiveWhisper(QStringLiteral("carl"), QStringLiteral("Carl"), QStringLiteral("Und ich?"));
        expect(!w.conversation(QStringLiteral("u:carl")).isEmpty(), "Ausgetretene antworten wieder direkt");
        w.renameGroup(groupId, QStringLiteral("Team"));
        expect(w.conversation(groupId).value("title").toString() == QStringLiteral("Team"), "Gruppe umbenennen");

        // EventSub: echtes Nachrichtenformat
        w.handleEventSubText(QStringLiteral(R"({"metadata":{"message_type":"notification"},"payload":{"subscription":{"type":"user.whisper.message"},)"
                                            R"("event":{"from_user_id":"1","from_user_login":"fritz","from_user_name":"Fritz","to_user_id":"2",)"
                                            R"("whisper_id":"abc","whisper":{"text":"Über EventSub 😀"}}}})"));
        expect(w.messages(QStringLiteral("u:fritz")).value(0).toMap().value("text").toString() == QStringLiteral("Über EventSub 😀"),
               "EventSub-Nachricht kommt im Postfach an");
        w.handleEventSubText(QStringLiteral(R"({"metadata":{"message_type":"notification"},"payload":{"subscription":{"type":"channel.follow"},"event":{}}})"));
        w.handleEventSubText(QStringLiteral("kaputt"));
        expect(w.conversations().size() == 5, "fremde Ereignisse und Müll ändern nichts");

        // Neueste zuerst
        expect(w.conversations().first().toMap().value("id").toString() == QStringLiteral("u:fritz"), "neueste Unterhaltung steht oben");
        w.deleteConversation(QStringLiteral("u:eve"));
        expect(w.conversation(QStringLiteral("u:eve")).isEmpty() && w.conversations().size() == 4, "Gespräch löschen");
        w.flush();
    }

    // Ablage: verschlüsselt und nach Neustart wieder da
    {
        QFile f(store);
        const bool readable = f.open(QIODevice::ReadOnly);
        const QByteArray raw = readable ? f.readAll() : QByteArray();
        expect(readable && !raw.isEmpty() && !raw.contains("Hallo du") && !raw.contains("anna_1"), "Datei enthält keinen Klartext");

        WhisperService again(store, entropy);
        expect(again.conversations().size() == 4, "Gespräche nach Neustart vorhanden");
        expect(again.messages(directId).size() == 3 && again.conversation(groupId).value("title").toString() == QStringLiteral("Team"),
               "Nachrichten und Gruppe nach Neustart vorhanden");
        expect(again.unreadTotal() == 3, "Ungelesen-Zähler übersteht den Neustart");
        again.receiveWhisper(QStringLiteral("bob"), QStringLiteral("Bob"), QStringLiteral("Noch da"));
        expect(again.messages(groupId).last().toMap().value("text").toString() == QStringLiteral("Noch da"),
               "Gruppen-Zuordnung übersteht den Neustart");
    }
    {
        WhisperService wrongKey(store, QByteArray("anderer-schluessel"));
        expect(wrongKey.conversations().isEmpty(), "mit anderem Schlüssel nicht lesbar");
    }

    std::printf("\nERGEBNIS: %d Fehler\n", g_failures);
    return g_failures ? 1 : 0;
}
