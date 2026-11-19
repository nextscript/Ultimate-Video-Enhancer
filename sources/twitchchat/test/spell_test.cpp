// Rechtschreibprüfung ohne Panel: Wortzerlegung und echte Wörterbücher, auch unter einem Pfad
// mit Umlauten (Benutzernamen wie „Jürgen“ landen über %APPDATA% im Plugin-Pfad).
#include "GrammarRules.h"
#include "LanguageToolClient.h"
#include "SpellChecker.h"
#include "TextPredictor.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>
#include <cstdio>

static int g_failures = 0;
static void expect(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "ok    " : "FEHLER", what.toUtf8().constData());
    if (!ok) ++g_failures;
}

static QStringList tokens(const QString &text, int cursor = -1)
{
    QStringList out;
    const auto isEmote = [](const QString &w) { return w == QStringLiteral("catJAM") || w == QStringLiteral("monkaS") || w == QStringLiteral("widepeepo"); };
    for (const SpellChecker::Word &w : SpellChecker::tokenize(text, cursor, isEmote)) {
        if (text.mid(w.start, w.length) != w.text) out << QStringLiteral("POSITION-FALSCH");
        out << w.text;
    }
    return out;
}

// Prüft Wörter und liefert die als falsch erkannten zurück.
static QStringList misspelled(SpellChecker &spell, const QString &text)
{
    QStringList out;
    QEventLoop loop;
    const auto c = QObject::connect(&spell, &SpellChecker::checked, &loop,
                                    [&](const QString &, const QVariantList &ranges, const QVariantList &) {
        for (const QVariant &r : ranges) out << r.toMap().value(QStringLiteral("word")).toString();
        loop.quit();
    });
    QTimer::singleShot(30000, &loop, [&] { out << QStringLiteral("ZEITÜBERSCHREITUNG"); loop.quit(); });
    spell.check(text, SpellChecker::tokenize(text, -1, nullptr));
    loop.exec();
    QObject::disconnect(c);
    return out;
}

static QStringList suggestions(SpellChecker &spell, const QString &word, qint64 *ms)
{
    QStringList out;
    QEventLoop loop;
    QElapsedTimer t;
    t.start();
    const auto c = QObject::connect(&spell, &SpellChecker::suggestionsReady, &loop,
                                    [&](const QString &, const QStringList &list) { out = list; loop.quit(); });
    QTimer::singleShot(30000, &loop, [&] { loop.quit(); });
    spell.requestSuggestions(word);
    loop.exec();
    QObject::disconnect(c);
    *ms = t.elapsed();
    return out;
}

// Fundstellen der lokalen Regeln als „Stelle→Korrektur“
static QStringList ruleHits(const QString &text, int cursor = -1, bool german = true, bool english = true)
{
    const auto isDe = [](const QString &w) { return QStringList{"an", "Bord", "Bett", "gehen"}.contains(w); };
    const auto isEn = [](const QString &w) { return QStringList{"apple", "car", "hour", "university", "bed", "Bord", "idea"}.contains(w); };
    const auto isEmote = [](const QString &w) { return w == QStringLiteral("catJAM"); };
    QStringList out;
    const QVariantList issues = GrammarRules::check(text, SpellChecker::ruleWords(text, isEmote), cursor, german, english, isDe, isEn);
    for (const QVariant &v : issues) {
        const QVariantMap m = v.toMap();
        out << text.mid(m.value("start").toInt(), m.value("length").toInt()) + QStringLiteral("→")
                   + m.value("replacements").toStringList().join(QLatin1Char('|'));
    }
    return out;
}

static void testRules()
{
    using L = QStringList;
    expect(ruleHits(QStringLiteral("Das ist ist gut.")) == L{"ist ist→ist"}, QStringLiteral("Regel: doppeltes Wort"));
    expect(ruleHits(QStringLiteral("Ich weiß, dass die die Besten sind.")).isEmpty(), QStringLiteral("Regel: „die die“ bleibt erlaubt"));
    expect(ruleHits(QStringLiteral("Ja ja, gg gg")).isEmpty(), QStringLiteral("Regel: Chat-Wiederholungen bleiben erlaubt"));
    expect(ruleHits(QStringLiteral("hallo zusammen")) == L{"hallo→Hallo"}, QStringLiteral("Regel: Satzanfang"));
    expect(ruleHits(QStringLiteral("Gut. schön hier! wirklich")) == L{"schön→Schön", "wirklich→Wirklich"}, QStringLiteral("Regel: Satzanfang nach Satzzeichen"));
    expect(ruleHits(QStringLiteral("Das ist z.b. gut und bzw. schön ... wirklich")).isEmpty(), QStringLiteral("Regel: nach Abkürzungen und … kein Satzanfang"));
    expect(ruleHits(QStringLiteral("@streamer hallo")).isEmpty(), QStringLiteral("Regel: @Name am Anfang"));
    expect(ruleHits(QStringLiteral("catJAM hallo")).isEmpty(), QStringLiteral("Regel: Emote am Anfang"));
    expect(ruleHits(QStringLiteral("Ich weiß dass es geht")) == L{" dass→, dass"}, QStringLiteral("Regel: Komma vor dass"));
    expect(ruleHits(QStringLiteral("Ich weiß, dass es geht")).isEmpty(), QStringLiteral("Regel: Komma vor dass vorhanden"));
    expect(ruleHits(QStringLiteral("So dass es geht")).isEmpty(), QStringLiteral("Regel: „so dass“"));
    expect(ruleHits(QStringLiteral("Ich weiß dass es geht"), -1, false, true).isEmpty(), QStringLiteral("Regel: dass nur mit Deutsch"));
    expect(ruleHits(QStringLiteral("She has a apple")) == L{"a→an"}, QStringLiteral("Regel: a vor Vokal"));
    expect(ruleHits(QStringLiteral("She has an car")) == L{"an→a"}, QStringLiteral("Regel: an vor Konsonant"));
    expect(ruleHits(QStringLiteral("Wait a hour and a university")) == L{"a→an"}, QStringLiteral("Regel: stummes h, university ohne Fehler"));
    expect(ruleHits(QStringLiteral("Wir sind an bord")).isEmpty(), QStringLiteral("Regel: deutsches an bleibt"));
    expect(ruleHits(QStringLiteral("Wir gehen an gehen")).isEmpty(), QStringLiteral("Regel: an vor deutschem Wort bleibt"));
    expect(ruleHits(QStringLiteral("She has a apple"), -1, true, false).isEmpty(), QStringLiteral("Regel: a/an nur mit Englisch"));
    expect(ruleHits(QStringLiteral("hallo"), 5).isEmpty(), QStringLiteral("Regel: Wort am Cursor bleibt offen"));
    expect(ruleHits(QStringLiteral("hallo"), -1, false, false).isEmpty(), QStringLiteral("Regel: alle Sprachen aus"));
}

static void testPredictor()
{
    const auto isEmote = [](const QString &w) { return w == QStringLiteral("catJAM") || w == QStringLiteral("LUL"); };
    const auto findEmotes = [](const QString &p) {
        QStringList out;
        for (const QString &e : {QStringLiteral("catJAM"), QStringLiteral("LUL")})
            if (e.startsWith(p, Qt::CaseInsensitive)) out << e;
        return out;
    };
    const auto ask = [&](const TextPredictor &t, const QString &text) {
        return t.predict(text, int(text.size()), true, true, isEmote, findEmotes);
    };
    TextPredictor t;
    t.setFrequencyWords({"ich", "Haus", "haben", "Hallo"}, {"the", "have", "house"});
    using L = QStringList;
    expect(ask(t, QStringLiteral("ha")).value(0) == QStringLiteral("Haus"), QStringLiteral("Vorhersage: häufiges Wort zuerst"));
    expect(ask(t, QStringLiteral("hau")).contains(QStringLiteral("Haus")), QStringLiteral("Vorhersage: deutsche Großschreibung"));
    expect(ask(t, QStringLiteral("Hav")) == L{"Have"}, QStringLiteral("Vorhersage: großer Anfang bleibt"));
    expect(ask(t, QStringLiteral("cat")).value(0) == QStringLiteral("catJAM"), QStringLiteral("Vorhersage: Emote-Name"));
    expect(ask(t, QStringLiteral("hallo ")).isEmpty(), QStringLiteral("Vorhersage: ohne Wissen kein Folgewort"));
    expect(ask(t, QString()).isEmpty(), QStringLiteral("Vorhersage: leeres Feld"));
    expect(t.predict(QStringLiteral("ha"), 2, false, true, isEmote, findEmotes) == L{"have"}, QStringLiteral("Vorhersage: nur aktive Sprache"));
    expect(ask(t, QStringLiteral("/w ha")).isEmpty(), QStringLiteral("Vorhersage: keine Vorschläge in Befehlen"));
    expect(ask(t, QStringLiteral("@ha")).isEmpty(), QStringLiteral("Vorhersage: keine Vorschläge in @Namen"));
    expect(t.predict(QStringLiteral("halos"), 2, true, true, isEmote, findEmotes).isEmpty(), QStringLiteral("Vorhersage: nicht mitten im Wort"));
    expect(TextPredictor::wordStart(QStringLiteral("hallo wel"), 9) == 6, QStringLiteral("Vorhersage: Wortanfang"));

    t.learn(QStringLiteral("guten Morgen zusammen"), true, isEmote);
    expect(ask(t, QStringLiteral("guten ")).value(0) == QStringLiteral("Morgen"), QStringLiteral("Vorhersage: gelerntes Folgewort, eigene Schreibweise"));
    expect(ask(t, QStringLiteral("zus")).value(0) == QStringLiteral("zusammen"), QStringLiteral("Vorhersage: gelerntes Wort"));
    expect(ask(t, QStringLiteral("guten Morgen. ")).isEmpty(), QStringLiteral("Vorhersage: neuer Satz hat keinen Vorgänger"));

    t.learn(QStringLiteral("/w freund geheimwort"), true, isEmote);
    t.learn(QStringLiteral("!befehl geheimwort"), true, isEmote);
    expect(ask(t, QStringLiteral("gehei")).isEmpty(), QStringLiteral("Vorhersage: Befehle werden nicht gelernt"));

    t.learn(QStringLiteral("the house"), false, isEmote);
    expect(!ask(t, QStringLiteral("the ")).contains(QStringLiteral("house")), QStringLiteral("Vorhersage: Chat zählt erst bei Wiederholung"));
    t.learn(QStringLiteral("the house"), false, isEmote);
    expect(ask(t, QStringLiteral("the ")).contains(QStringLiteral("house")), QStringLiteral("Vorhersage: wiederholtes Chat-Folgewort"));
    t.learn(QStringLiteral("xyzzy xyzzy xyzzy"), false, isEmote);
    t.learn(QStringLiteral("xyzzy xyzzy xyzzy"), false, isEmote);
    expect(ask(t, QStringLiteral("xyz")).isEmpty(), QStringLiteral("Vorhersage: unbekannte Chat-Wörter bleiben draußen"));

    TextPredictor restored;
    restored.setFrequencyWords({}, {});
    restored.loadOwn(t.saveOwn());
    expect(ask(restored, QStringLiteral("guten ")).value(0) == QStringLiteral("Morgen"), QStringLiteral("Vorhersage: eigenes Wissen übersteht Speichern und Laden"));
    expect(!ask(restored, QStringLiteral("the ")).contains(QStringLiteral("house")), QStringLiteral("Vorhersage: Chat-Wissen wird nicht gespeichert"));
}

static void testLanguageToolOffline()
{
    const QByteArray json = R"({"matches":[
        {"message":"Fehler A","offset":3,"length":2,"replacements":[{"value":"eins"},{"value":"zwei"},{"value":"drei"},{"value":"vier"}],"rule":{"category":{"id":"GRAMMAR"}}},
        {"message":"Tippfehler","offset":9,"length":2,"replacements":[{"value":"x"}],"rule":{"category":{"id":"TYPOS"}}},
        {"message":"Ohne Vorschlag","offset":12,"length":4,"replacements":[],"rule":{"category":{"id":"PUNCTUATION"}}}]})";
    const QVariantList issues = LanguageToolClient::parseMatches(json);
    expect(issues.size() == 2, QStringLiteral("LanguageTool: Tippfehler fallen heraus"));
    expect(issues.value(0).toMap().value("replacements").toStringList().size() == 3, QStringLiteral("LanguageTool: höchstens 3 Korrekturen"));
    expect(issues.value(1).toMap().value("start").toInt() == 12, QStringLiteral("LanguageTool: Position übernommen"));
    expect(LanguageToolClient::parseMatches("kein json").isEmpty(), QStringLiteral("LanguageTool: kaputte Antwort"));

    const QString annotated = QString::fromUtf8(LanguageToolClient::buildAnnotation(QStringLiteral("Hi @name du"), {{3, 5}}));
    expect(annotated.contains(QStringLiteral("\"markup\":\"@name\"")) && annotated.contains(QStringLiteral("\"text\":\" du\"")),
           QStringLiteral("LanguageTool: @Name wird ausgeblendet"));
    const auto hidden = SpellChecker::hiddenRanges(QStringLiteral("hi @a #b https://x.de ok"), nullptr);
    expect(hidden.size() == 3 && hidden.at(0).first == 3, QStringLiteral("LanguageTool: Namen, Tags und Links erkannt"));
}

// Echte Anfrage mit einem Testsatz. Ohne Netz gilt das nicht als Fehler, wird aber gemeldet.
static void testLanguageToolLive()
{
    LanguageToolClient client;
    const QString text = QStringLiteral("Hi @streamer \U0001F600 he go to school.");
    QVariantList issues;
    QString failure;
    bool done = false;
    QEventLoop loop;
    QObject::connect(&client, &LanguageToolClient::result, &loop, [&](const QString &, const QVariantList &found) { issues = found; done = true; loop.quit(); });
    QObject::connect(&client, &LanguageToolClient::failed, &loop, [&](const QString &why) { failure = why; loop.quit(); });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    client.schedule(text, SpellChecker::hiddenRanges(text, nullptr), false, true);
    loop.exec();
    if (!done) {
        std::printf("HINWEIS LanguageTool nicht erreichbar: %s\n", failure.toUtf8().constData());
        return;
    }
    bool found = false;
    for (const QVariant &v : issues) {
        const QVariantMap m = v.toMap();
        std::printf("       LanguageTool: „%s“ → %s | %s\n", text.mid(m.value("start").toInt(), m.value("length").toInt()).toUtf8().constData(),
                    m.value("replacements").toStringList().join(QStringLiteral(", ")).toUtf8().constData(), m.value("message").toString().toUtf8().constData());
        if (text.mid(m.value("start").toInt(), m.value("length").toInt()) == QStringLiteral("go")) found = true;
    }
    expect(found, QStringLiteral("LanguageTool live: Stelle stimmt trotz Emoji und ausgeblendetem Namen"));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using L = QStringList;
    testRules();
    testPredictor();
    testLanguageToolOffline();
    testLanguageToolLive();

    // ——— Wortzerlegung ———
    expect(tokens(QStringLiteral("Hallo Welt, wie gehts?")) == L{"Hallo", "Welt", "wie", "gehts"}, QStringLiteral("normale Wörter, Satzzeichen getrennt"));
    expect(tokens(QStringLiteral("@streamer #tag hallo")) == L{"hallo"}, QStringLiteral("@Namen und #Tags draußen"));
    expect(tokens(QStringLiteral("/me winkt")) == L{"winkt"}, QStringLiteral("/Befehl draußen, Rest geprüft"));
    expect(tokens(QStringLiteral("!uptime bitte")) == L{"bitte"}, QStringLiteral("!Befehl am Anfang draußen"));
    expect(tokens(QStringLiteral("schau https://twitch.tv/abc www.example.com example.de jetzt")) == L{"schau", "jetzt"}, QStringLiteral("Links draußen"));
    expect(tokens(QStringLiteral("abc123 foo_bar 42 gut")) == L{"gut"}, QStringLiteral("Ziffern und Unterstriche draußen"));
    expect(tokens(QStringLiteral("LUL PogChamp iPhone gut")) == L{"gut"}, QStringLiteral("GROSS und BinnenMajuskeln draußen"));
    expect(tokens(QStringLiteral("catJAM widepeepo! danke")) == L{"danke"}, QStringLiteral("Emotes draußen, auch mit Satzzeichen"));
    expect(tokens(QStringLiteral("Twitch-Chat E-Mail")) == L{"Twitch", "Chat", "Mail"}, QStringLiteral("Bindestrich trennt, Einzelbuchstaben draußen"));
    expect(tokens(QStringLiteral("don't geht’s")) == L{"don't", "geht’s"}, QStringLiteral("Apostroph bleibt im Wort"));
    expect(tokens(QStringLiteral("привет ok こんにちは")) == L{"ok"}, QStringLiteral("nichtlateinische Schrift draußen"));
    expect(tokens(QStringLiteral("😀 Füße")) == L{"Füße"}, QStringLiteral("Emoji stört Positionen nicht"));
    expect(tokens(QStringLiteral("hallo wel"), 9) == L{"hallo"}, QStringLiteral("Wort am Cursor bleibt offen"));
    expect(tokens(QStringLiteral("hallo wel "), 10) == L{"hallo", "wel"}, QStringLiteral("nach dem Leerzeichen gilt es als fertig"));
    expect(tokens(QStringLiteral("hallo wel"), 3) == L{"wel"}, QStringLiteral("Cursor im ersten Wort lässt nur dieses offen"));

    // ——— Echte Wörterbücher unter einem Umlaut-Pfad ———
    QTemporaryDir temp;
    const QString dictDir = temp.path() + QStringLiteral("/Jürgen Müller/dict");
    QDir().mkpath(dictDir);
    for (const QString &f : {QStringLiteral("de_DE_frami.aff"), QStringLiteral("de_DE_frami.dic"),
                             QStringLiteral("en_US.aff"), QStringLiteral("en_US.dic"),
                             QStringLiteral("en_GB.aff"), QStringLiteral("en_GB.dic")})
        expect(QFile::copy(QStringLiteral(SPELL_DICT_DIR "/") + f, dictDir + QLatin1Char('/') + f), QStringLiteral("kopiert: ") + f);

    {
        SpellChecker spell;
        spell.setDictionaryDir(dictDir);

        QElapsedTimer load;
        load.start();
        expect(misspelled(spell, QStringLiteral("Hallo the ")).isEmpty(), QStringLiteral("Laden: richtige Wörter ohne Fehler"));
        std::printf("       Laden beider Wörterbücher: %lld ms\n", load.elapsed());

        const QString german = QStringLiteral("Die Rechtschreibprüfung funktioniert heute für Füße, Straße und Größe ");
        expect(misspelled(spell, german).isEmpty(), QStringLiteral("Deutsch: Umlaute, ß und zusammengesetzte Wörter"));
        const QString english = QStringLiteral("The quick brown fox doesn't jump over the lazy dog ");
        expect(misspelled(spell, english).isEmpty(), QStringLiteral("Englisch mit Apostroph"));
        expect(misspelled(spell, QStringLiteral("The colour of the theatre centre, we realise and apologise ")).isEmpty(),
               QStringLiteral("britisches Englisch"));
        expect(misspelled(spell, QStringLiteral("The color of the theater center, we realize and apologize ")).isEmpty(),
               QStringLiteral("US-Englisch bleibt richtig"));
        expect(misspelled(spell, QStringLiteral("gg lol xD thx ")).isEmpty(), QStringLiteral("Chat-Wörter gelten als richtig"));

        expect(misspelled(spell, QStringLiteral("Das ist Rechtschreibfehlerr und Strasseee teh ")) == L{"Rechtschreibfehlerr", "Strasseee", "teh"},
               QStringLiteral("Tippfehler in beiden Sprachen gefunden"));

        QElapsedTimer many;
        many.start();
        QString longText;
        for (int i = 0; i < 60; ++i) longText += QStringLiteral("Heute schauen wir gemeinsam diesen wunderbaren Stream ");
        expect(misspelled(spell, longText.left(500)).isEmpty(), QStringLiteral("500 Zeichen"));
        std::printf("       Prüfdauer 500 Zeichen: %lld ms\n", many.elapsed());

        // ——— nur eine Sprache ———
        spell.setLanguages(true, false);
        expect(misspelled(spell, QStringLiteral("Straße jump ")) == L{"jump"}, QStringLiteral("nur Deutsch: englisches Wort fällt auf"));
        expect(misspelled(spell, QStringLiteral("Straße colour ")) == L{"colour"}, QStringLiteral("nur Deutsch: britisches Wort fällt auf"));
        spell.setLanguages(false, true);
        expect(misspelled(spell, QStringLiteral("house Straße ")) == L{"Straße"}, QStringLiteral("nur Englisch: deutsches Wort fällt auf"));
        expect(misspelled(spell, QStringLiteral("colour color ")).isEmpty(), QStringLiteral("nur Englisch: beide Schreibweisen gelten"));
        spell.setLanguages(false, false);
        expect(misspelled(spell, QStringLiteral("xyzzyqq ")).isEmpty(), QStringLiteral("beide aus: keine Markierung"));
        spell.setLanguages(true, true);

        // ——— Vorschläge ———
        qint64 ms = 0;
        const L de = suggestions(spell, QStringLiteral("Strase"), &ms);
        std::printf("       Vorschläge „Strase“ (%lld ms): %s\n", ms, de.join(QStringLiteral(", ")).toUtf8().constData());
        expect(de.contains(QStringLiteral("Straße")), QStringLiteral("Vorschlag Deutsch, Umlaut korrekt dekodiert"));
        expect(!de.contains(QStringLiteral("St rase")), QStringLiteral("ganze Wörter verdrängen getrennte Vorschläge"));
        qint64 splitMs = 0;
        const L split = suggestions(spell, QStringLiteral("alot"), &splitMs);
        std::printf("       Vorschläge „alot“ (%lld ms): %s\n", splitMs, split.join(QStringLiteral(", ")).toUtf8().constData());
        L en = suggestions(spell, QStringLiteral("recieve"), &ms);
        std::printf("       Vorschläge „recieve“ (%lld ms): %s\n", ms, en.join(QStringLiteral(", ")).toUtf8().constData());
        expect(en.indexOf(QStringLiteral("receive")) == 0, QStringLiteral("Vorschlag Englisch steht vorn, obwohl beide Sprachen aktiv sind"));
        expect(en.size() <= 6, QStringLiteral("höchstens 6 Vorschläge"));
        expect(en.removeDuplicates() == 0, QStringLiteral("keine doppelten Vorschläge aus US und GB"));
        const L gb = suggestions(spell, QStringLiteral("colourfull"), &ms);
        std::printf("       Vorschläge „colourfull“ (%lld ms): %s\n", ms, gb.join(QStringLiteral(", ")).toUtf8().constData());
        expect(gb.contains(QStringLiteral("colourful")), QStringLiteral("Vorschlag Britisch"));
        const L compound = suggestions(spell, QStringLiteral("Rechtschreibfehlerr"), &ms);
        std::printf("       Vorschläge langes Wort (%lld ms): %s\n", ms, compound.join(QStringLiteral(", ")).toUtf8().constData());
        expect(compound.value(0) == QStringLiteral("Rechtschreibfehler"), QStringLiteral("ähnlichster Vorschlag zuerst"));
        expect(!compound.join(QLatin1Char(' ')).contains(QLatin1Char('-')), QStringLiteral("keine Wortanfänge mit Bindestrich"));

        // ——— Wortlisten: Wörterbuch korrigiert die Schreibweise ———
        {
            QStringList de, en;
            QEventLoop wait;
            QObject::connect(&spell, &SpellChecker::frequencyWordsReady, &wait, [&](const QStringList &g, const QStringList &e) { de = g; en = e; wait.quit(); });
            QElapsedTimer timer;
            timer.start();
            spell.loadFrequencyWords();
            QTimer::singleShot(30000, &wait, &QEventLoop::quit);
            wait.exec();
            std::printf("       (Listen nicht kopiert: %lld Wörter, %lld ms)\n", qlonglong(de.size() + en.size()), timer.elapsed());
        }
        spell.setDictionaryDir(QStringLiteral(SPELL_DICT_DIR));
        {
            QStringList de, en;
            QEventLoop wait;
            QObject::connect(&spell, &SpellChecker::frequencyWordsReady, &wait, [&](const QStringList &g, const QStringList &e) { de = g; en = e; wait.quit(); });
            QElapsedTimer timer;
            timer.start();
            spell.loadFrequencyWords();
            QTimer::singleShot(30000, &wait, &QEventLoop::quit);
            wait.exec();
            std::printf("       Wortlisten: %lld Deutsch, %lld Englisch, %lld ms; Deutsch: %s\n", qlonglong(de.size()), qlonglong(en.size()), timer.elapsed(),
                        de.mid(0, 12).join(QLatin1Char(' ')).toUtf8().constData());
            expect(de.size() > 5000 && en.size() > 5000, QStringLiteral("Wortlisten geladen"));
            expect(de.contains(QStringLiteral("Haus")) && !de.contains(QStringLiteral("haus")), QStringLiteral("Wortlisten: Substantive groß"));
            expect(de.contains(QStringLiteral("ich")), QStringLiteral("Wortlisten: normale Wörter klein"));
        }
        spell.setDictionaryDir(dictDir);

        // ——— eigenes Wörterbuch ———
        spell.setUserWords({QStringLiteral("xnowski")});
        expect(misspelled(spell, QStringLiteral("Xnowski ")).isEmpty(), QStringLiteral("eigenes Wort gilt, auch mit anderer Großschreibung"));
    }

    // ——— Wörterbuch fehlt: kein Absturz, keine Markierung, Meldung ———
    {
        SpellChecker spell;
        spell.setDictionaryDir(temp.path() + QStringLiteral("/gibt-es-nicht"));
        int missing = 0;
        QObject::connect(&spell, &SpellChecker::dictionaryMissing, &app, [&](const QString &) { ++missing; });
        expect(misspelled(spell, QStringLiteral("xyzzyqq ")).isEmpty(), QStringLiteral("fehlende Wörterbücher: keine Markierung"));
        QCoreApplication::processEvents();
        expect(missing == 3, QStringLiteral("fehlende Wörterbücher werden gemeldet"));
    }

    std::printf("\nERGEBNIS: %d Fehler\n", g_failures);
    return g_failures ? 1 : 0;
}
