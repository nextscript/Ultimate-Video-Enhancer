#include "GrammarRules.h"

#include <QSet>
#include <QVariantMap>

namespace {

QVariantMap issue(int start, int length, const QString &de, const QString &en, const QStringList &replacements)
{
    return QVariantMap{{QStringLiteral("start"), start},
                       {QStringLiteral("length"), length},
                       {QStringLiteral("message"), de},
                       {QStringLiteral("messageEn"), en},
                       {QStringLiteral("replacements"), replacements},
                       {QStringLiteral("source"), QStringLiteral("local")}};
}

bool onlySpaceBetween(const QString &text, int from, int to)
{
    if (to <= from) return false;
    for (int i = from; i < to; ++i)
        if (!text.at(i).isSpace()) return false;
    return true;
}

// Doppelt sinnvoll („die die“, „had had“) oder im Chat üblich („ja ja“, „gg gg“).
const QSet<QString> &allowedDoubles()
{
    static const QSet<QString> words = {
        QStringLiteral("das"), QStringLiteral("der"), QStringLiteral("die"), QStringLiteral("sie"),
        QStringLiteral("gg"), QStringLiteral("ha"), QStringLiteral("haha"), QStringLiteral("had"),
        QStringLiteral("hey"), QStringLiteral("ja"), QStringLiteral("lol"), QStringLiteral("nein"),
        QStringLiteral("no"), QStringLiteral("so"), QStringLiteral("that"), QStringLiteral("very"),
        QStringLiteral("yes"), QStringLiteral("nee"), QStringLiteral("oh"), QStringLiteral("na"),
    };
    return words;
}

// Abkürzungen, nach deren Punkt kein neuer Satz beginnt.
const QSet<QString> &abbreviations()
{
    static const QSet<QString> words = {
        QStringLiteral("z"), QStringLiteral("b"), QStringLiteral("d"), QStringLiteral("h"), QStringLiteral("u"),
        QStringLiteral("a"), QStringLiteral("e"), QStringLiteral("g"), QStringLiteral("i"), QStringLiteral("bzw"),
        QStringLiteral("usw"), QStringLiteral("etc"), QStringLiteral("ca"), QStringLiteral("evtl"),
        QStringLiteral("ggf"), QStringLiteral("vgl"), QStringLiteral("nr"), QStringLiteral("dr"),
        QStringLiteral("mr"), QStringLiteral("mrs"), QStringLiteral("vs"), QStringLiteral("inkl"),
        QStringLiteral("sog"), QStringLiteral("min"), QStringLiteral("max"),
    };
    return words;
}

bool startsSentence(const QString &text, const QList<SpellChecker::Word> &words, int index)
{
    const SpellChecker::Word &w = words.at(index);
    if (w.start == 0) return true;
    int i = w.start - 1;
    if (!text.at(i).isSpace()) return false;
    while (i >= 0 && text.at(i).isSpace()) --i;
    if (i < 0) return true;
    const QChar end = text.at(i);
    if (end != QLatin1Char('.') && end != QLatin1Char('!') && end != QLatin1Char('?')) return false;
    if (end == QLatin1Char('.')) {
        if (i > 0 && text.at(i - 1) == QLatin1Char('.')) return false;      // Auslassungspunkte
        if (index > 0 && words.at(index - 1).start + words.at(index - 1).length == i
            && abbreviations().contains(words.at(index - 1).text.toLower()))
            return false;
    }
    return true;
}

bool hasVowelSound(const QString &lowerWord)
{
    static const QStringList silentH = {QStringLiteral("hour"), QStringLiteral("honest"), QStringLiteral("honor"),
                                        QStringLiteral("honour"), QStringLiteral("heir")};
    static const QStringList consonantSound = {QStringLiteral("uni"), QStringLiteral("use"), QStringLiteral("usu"),
                                               QStringLiteral("uti"), QStringLiteral("eu"), QStringLiteral("one"),
                                               QStringLiteral("once"), QStringLiteral("ubiq")};
    for (const QString &p : silentH)
        if (lowerWord.startsWith(p)) return true;
    for (const QString &p : consonantSound)
        if (lowerWord.startsWith(p)) return false;
    return QStringLiteral("aeiou").contains(lowerWord.at(0));
}

} // namespace

QVariantList GrammarRules::check(const QString &text, const QList<SpellChecker::Word> &words, int cursor,
                                 bool german, bool english,
                                 const std::function<bool(const QString &)> &isGermanWord,
                                 const std::function<bool(const QString &)> &isEnglishWord)
{
    QVariantList issues;
    if (!german && !english) return issues;
    const auto typing = [cursor](const SpellChecker::Word &w) {
        return cursor > w.start && cursor <= w.start + w.length;
    };

    for (int i = 0; i < words.size(); ++i) {
        const SpellChecker::Word &w = words.at(i);
        const QString lower = w.text.toLower();
        if (typing(w)) continue;

        if (w.text.size() >= 2 && w.text.at(0).isLower() && startsSentence(text, words, i)) {
            QString capital = w.text;
            capital[0] = capital.at(0).toUpper();
            issues << issue(w.start, w.length, QStringLiteral("Der Satz beginnt kleingeschrieben."),
                            QStringLiteral("This sentence starts with a lowercase letter."), {capital});
        }
        if (i == 0) continue;

        const SpellChecker::Word &previous = words.at(i - 1);
        const int previousEnd = previous.start + previous.length;
        if (!onlySpaceBetween(text, previousEnd, w.start)) continue;
        const QString previousLower = previous.text.toLower();

        if (lower == previousLower && lower.size() >= 2 && !allowedDoubles().contains(lower)) {
            issues << issue(previous.start, w.start + w.length - previous.start,
                            QStringLiteral("Das Wort steht doppelt."), QStringLiteral("This word is repeated."),
                            {previous.text});
            continue;
        }

        if (german && lower == QStringLiteral("dass")) {
            static const QSet<QString> noComma = {
                QStringLiteral("so"), QStringLiteral("als"), QStringLiteral("ohne"), QStringLiteral("anstatt"),
                QStringLiteral("statt"), QStringLiteral("außer"), QStringLiteral("und"), QStringLiteral("oder"),
                QStringLiteral("aber"), QStringLiteral("auch"), QStringLiteral("nur"), QStringLiteral("sondern"),
            };
            if (!noComma.contains(previousLower)) {
                issues << issue(previousEnd, w.start + w.length - previousEnd,
                                QStringLiteral("Vor „dass“ steht meist ein Komma."),
                                QStringLiteral("German: \"dass\" usually needs a comma before it."),
                                {QStringLiteral(", ") + w.text});
            }
            continue;
        }

        // a/an: Im Deutschen ist „an“ ein normales Wort. Das Folgewort muss deshalb englisch sein,
        // bei „an“ außerdem nicht zugleich deutsch („an Bord“).
        if (english && (previousLower == QStringLiteral("a") || previousLower == QStringLiteral("an"))
            && w.text.at(0).isLower() && isEnglishWord(w.text)) {
            const bool isAn = previousLower == QStringLiteral("an");
            const bool vowel = hasVowelSound(lower);
            if (isAn && !vowel && !(german && isGermanWord(w.text))) {
                const QString fix = previous.text.at(0).isUpper() ? QStringLiteral("A") : QStringLiteral("a");
                issues << issue(previous.start, previous.length, QStringLiteral("Vor diesem Wort heißt es „a“."),
                                QStringLiteral("Use \"a\" before this word."), {fix});
            } else if (!isAn && vowel) {
                const QString fix = previous.text.at(0).isUpper() ? QStringLiteral("An") : QStringLiteral("an");
                issues << issue(previous.start, previous.length, QStringLiteral("Vor diesem Wort heißt es „an“."),
                                QStringLiteral("Use \"an\" before this word."), {fix});
            }
        }
    }
    return issues;
}
