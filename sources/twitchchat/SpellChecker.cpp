#include "SpellChecker.h"

#include "GrammarRules.h"

#include <hunspell.hxx>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QVariantMap>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kMaxSuggestions = 6;
constexpr int kMaxCachedWords = 20000;

// Im Twitch-Chat normale Wörter, die in keinem der beiden Wörterbücher stehen.
const QSet<QString> &chatWords()
{
    static const QSet<QString> words = {
        QStringLiteral("afk"), QStringLiteral("bday"), QStringLiteral("brb"), QStringLiteral("btw"),
        QStringLiteral("clip"), QStringLiteral("clips"), QStringLiteral("emote"), QStringLiteral("emotes"),
        QStringLiteral("ez"), QStringLiteral("gg"), QStringLiteral("gj"), QStringLiteral("gl"),
        QStringLiteral("gn"), QStringLiteral("hf"), QStringLiteral("idk"), QStringLiteral("imo"),
        QStringLiteral("irl"), QStringLiteral("kekw"), QStringLiteral("lmao"), QStringLiteral("lol"),
        QStringLiteral("lul"), QStringLiteral("mod"), QStringLiteral("mods"), QStringLiteral("nvm"),
        QStringLiteral("np"), QStringLiteral("omg"), QStringLiteral("ok"), QStringLiteral("pls"),
        QStringLiteral("plz"), QStringLiteral("pog"), QStringLiteral("poggers"), QStringLiteral("raid"),
        QStringLiteral("rofl"), QStringLiteral("stream"), QStringLiteral("streamer"), QStringLiteral("sub"),
        QStringLiteral("subs"), QStringLiteral("thx"), QStringLiteral("twitch"), QStringLiteral("ty"),
        QStringLiteral("vip"), QStringLiteral("vod"), QStringLiteral("wp"), QStringLiteral("wtf"),
        QStringLiteral("xd"),
    };
    return words;
}

struct Dictionary
{
    std::unique_ptr<Hunspell> speller;
    bool utf8 = true;

    std::string encode(const QString &word) const
    {
        const QByteArray bytes = utf8 ? word.toUtf8() : word.toLatin1();
        return std::string(bytes.constData(), size_t(bytes.size()));
    }
    QString decode(const std::string &word) const
    {
        return utf8 ? QString::fromUtf8(word.data(), qsizetype(word.size()))
                    : QString::fromLatin1(word.data(), qsizetype(word.size()));
    }
    // Das deutsche Wörterbuch ist ISO-8859-1: Was sich darin nicht schreiben lässt, kennt es nicht.
    bool canEncode(const QString &word) const
    {
        if (utf8) return true;
        for (const QChar c : word)
            if (c.unicode() > 0xFF) return false;
        return true;
    }
};

bool isLatinWord(const QString &word)
{
    for (const QChar c : word) {
        if (c.isSurrogate()) return false;
        const QChar::Script script = c.script();
        if (script != QChar::Script_Latin && script != QChar::Script_Common && script != QChar::Script_Inherited)
            return false;
    }
    return true;
}

// Eine Großschreibung nach dem ersten Buchstaben: GROSS, PogChamp, iPhone. Das Apostroph-s
// bleibt ein normales Wort.
bool hasInnerUpperCase(const QString &word)
{
    for (int i = 1; i < word.size(); ++i)
        if (word.at(i).isUpper()) return true;
    return false;
}

// Damerau-Levenshtein ohne Groß-/Kleinschreibung: Vertauschte Buchstaben zählen als ein Fehler.
int editDistance(const QString &first, const QString &second)
{
    const QString a = first.toLower();
    const QString b = second.toLower();
    std::vector<std::vector<int>> d(size_t(a.size()) + 1, std::vector<int>(size_t(b.size()) + 1));
    for (int i = 0; i <= a.size(); ++i) d[size_t(i)][0] = i;
    for (int j = 0; j <= b.size(); ++j) d[0][size_t(j)] = j;
    for (int i = 1; i <= a.size(); ++i) {
        for (int j = 1; j <= b.size(); ++j) {
            const size_t x = size_t(i), y = size_t(j);
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            d[x][y] = std::min({d[x - 1][y] + 1, d[x][y - 1] + 1, d[x - 1][y - 1] + cost});
            if (i > 1 && j > 1 && a.at(i - 1) == b.at(j - 2) && a.at(i - 2) == b.at(j - 1))
                d[x][y] = std::min(d[x][y], d[x - 2][y - 2] + 1);
        }
    }
    return d[size_t(a.size())][size_t(b.size())];
}

QString normalized(QString word)
{
    word.replace(QChar(0x2019), QLatin1Char('\''));   // typografisches Apostroph
    return word;
}

} // namespace

struct SpellChecker::Engine
{
    QHash<QString, std::shared_ptr<Dictionary>> dictionaries;   // Dateiname → geladen (auch leer, wenn sie fehlt)
    QHash<QString, bool> cache;
    bool cachedGerman = false;
    bool cachedEnglish = false;

    std::shared_ptr<Dictionary> load(SpellChecker *owner, const QString &dir, const QString &name)
    {
        const auto known = dictionaries.constFind(name);
        if (known != dictionaries.constEnd()) return known.value();

        std::shared_ptr<Dictionary> result;
        const QString base = dir + QLatin1Char('/') + name;
        if (QFileInfo::exists(base + QStringLiteral(".aff")) && QFileInfo::exists(base + QStringLiteral(".dic"))) {
            // Mit \\?\-Präfix liest Hunspell den Pfad als UTF-8 und öffnet ihn über die
            // Breitzeichen-API; sonst scheitert es an Umlauten im Benutzernamen.
            const QByteArray prefix = QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(base).absoluteFilePath())).toUtf8();
            const QByteArray aff = "\\\\?\\" + prefix + ".aff";
            const QByteArray dic = "\\\\?\\" + prefix + ".dic";
            result = std::make_shared<Dictionary>();
            result->speller = std::make_unique<Hunspell>(aff.constData(), dic.constData());
            result->utf8 = result->speller->get_dict_encoding() == "UTF-8";
        } else {
            emit owner->dictionaryMissing(base);
        }
        dictionaries.insert(name, result);
        return result;
    }

    QList<std::shared_ptr<Dictionary>> active(SpellChecker *owner, const QString &dir, bool german, bool english)
    {
        QList<std::shared_ptr<Dictionary>> list;
        if (german)
            if (auto d = load(owner, dir, QStringLiteral("de_DE_frami"))) list << d;
        if (english) {
            if (auto d = load(owner, dir, QStringLiteral("en_US"))) list << d;
            if (auto d = load(owner, dir, QStringLiteral("en_GB"))) list << d;
        }
        return list;
    }

    bool isCorrect(const QString &word, const QList<std::shared_ptr<Dictionary>> &dicts)
    {
        const auto cached = cache.constFind(word);
        if (cached != cache.constEnd()) return cached.value();
        bool ok = false;
        for (const auto &d : dicts) {
            if (d->canEncode(word) && d->speller->spell(d->encode(word))) {
                ok = true;
                break;
            }
        }
        if (cache.size() >= kMaxCachedWords) cache.clear();
        cache.insert(word, ok);
        return ok;
    }
};

SpellChecker::SpellChecker(QObject *parent)
    : QObject(parent), m_context(new QObject), m_engine(new Engine)
{
    m_context->moveToThread(&m_thread);
    m_thread.setObjectName(QStringLiteral("twitchchat-spell"));
    m_thread.start(QThread::LowPriority);
}

SpellChecker::~SpellChecker()
{
    m_thread.quit();
    m_thread.wait();
    delete m_context;
    delete m_engine;
}

void SpellChecker::setDictionaryDir(const QString &dir) { m_dir = dir; }

void SpellChecker::setLanguages(bool german, bool english)
{
    m_german = german;
    m_english = english;
}

void SpellChecker::setUserWords(const QSet<QString> &lowerCaseWords) { m_userWords = lowerCaseWords; }

bool SpellChecker::isHiddenChunk(const QString &chunk, int chunkStart, const std::function<bool(const QString &)> &isEmote)
{
    static const QRegularExpression domain(QStringLiteral("^\\S+\\.[A-Za-z]{2,}(?:[/?#:]\\S*)?$"));
    if (chunk.startsWith(QLatin1Char('@')) || chunk.startsWith(QLatin1Char('#'))) return true;
    if (chunkStart == 0 && (chunk.startsWith(QLatin1Char('/')) || chunk.startsWith(QLatin1Char('!')))) return true;
    if (chunk.contains(QStringLiteral("://")) || chunk.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)
        || domain.match(chunk).hasMatch())
        return true;
    return isEmote && isEmote(chunk);
}

QList<SpellChecker::Word> SpellChecker::ruleWords(const QString &text, const std::function<bool(const QString &)> &isEmote)
{
    static const QRegularExpression chunks(QStringLiteral("\\S+"));
    static const QRegularExpression letters(QStringLiteral("\\p{L}[\\p{L}\\p{M}]*(?:['\\x{2019}]\\p{L}[\\p{L}\\p{M}]*)*"),
                                            QRegularExpression::UseUnicodePropertiesOption);
    QList<Word> words;
    auto chunkIt = chunks.globalMatch(text);
    while (chunkIt.hasNext()) {
        const auto chunkMatch = chunkIt.next();
        const QString chunk = chunkMatch.captured();
        const int chunkStart = int(chunkMatch.capturedStart());
        if (isHiddenChunk(chunk, chunkStart, isEmote)) continue;
        auto wordIt = letters.globalMatch(chunk);
        while (wordIt.hasNext()) {
            const auto wordMatch = wordIt.next();
            if (isEmote && isEmote(wordMatch.captured())) continue;
            words.append(Word{chunkStart + int(wordMatch.capturedStart()), int(wordMatch.capturedLength()), wordMatch.captured()});
        }
    }
    return words;
}

QList<QPair<int, int>> SpellChecker::hiddenRanges(const QString &text, const std::function<bool(const QString &)> &isEmote)
{
    static const QRegularExpression chunks(QStringLiteral("\\S+"));
    QList<QPair<int, int>> ranges;
    auto chunkIt = chunks.globalMatch(text);
    while (chunkIt.hasNext()) {
        const auto match = chunkIt.next();
        if (isHiddenChunk(match.captured(), int(match.capturedStart()), isEmote))
            ranges.append({int(match.capturedStart()), int(match.capturedLength())});
    }
    return ranges;
}

QList<SpellChecker::Word> SpellChecker::tokenize(const QString &text, int cursor,
                                                 const std::function<bool(const QString &)> &isEmote)
{
    static const QRegularExpression chunks(QStringLiteral("\\S+"));
    static const QRegularExpression letters(QStringLiteral("\\p{L}[\\p{L}\\p{M}]*(?:['\\x{2019}]\\p{L}[\\p{L}\\p{M}]*)*"),
                                            QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression digitOrUnderscore(QStringLiteral("[\\p{N}_]"),
                                                      QRegularExpression::UseUnicodePropertiesOption);

    QList<Word> words;
    auto chunkIt = chunks.globalMatch(text);
    while (chunkIt.hasNext()) {
        const auto chunkMatch = chunkIt.next();
        const QString chunk = chunkMatch.captured();
        const int chunkStart = int(chunkMatch.capturedStart());
        if (isHiddenChunk(chunk, chunkStart, isEmote)) continue;
        if (digitOrUnderscore.match(chunk).hasMatch()) continue;

        auto wordIt = letters.globalMatch(chunk);
        while (wordIt.hasNext()) {
            const auto wordMatch = wordIt.next();
            const QString word = wordMatch.captured();
            const int start = chunkStart + int(wordMatch.capturedStart());
            const int end = start + int(word.size());
            if (word.size() < 2) continue;
            if (hasInnerUpperCase(word) || !isLatinWord(word)) continue;
            if (cursor > start && cursor <= end) continue;      // wird gerade getippt
            if (isEmote && isEmote(word)) continue;
            words.append(Word{start, int(word.size()), word});
        }
    }
    return words;
}

void SpellChecker::check(const QString &text, const QList<Word> &words, const QList<Word> &rulesWords,
                         int cursor, bool rules)
{
    const quint64 id = ++m_latestCheck;
    const QString dir = m_dir;
    const bool german = m_german;
    const bool english = m_english;
    const QSet<QString> userWords = m_userWords;
    QMetaObject::invokeMethod(m_context, [=, this] {
        if (id != m_latestCheck.load()) return;     // schon weitergetippt
        if (m_engine->cachedGerman != german || m_engine->cachedEnglish != english) {
            m_engine->cache.clear();
            m_engine->cachedGerman = german;
            m_engine->cachedEnglish = english;
        }
        const auto dicts = m_engine->active(this, dir, german, english);
        QVariantList ranges;
        if (!dicts.isEmpty()) {
            for (const Word &w : words) {
                const QString lower = w.text.toLower();
                if (userWords.contains(lower) || chatWords().contains(lower)) continue;
                if (m_engine->isCorrect(normalized(w.text), dicts)) continue;
                ranges.append(QVariantMap{{QStringLiteral("start"), w.start},
                                          {QStringLiteral("length"), w.length},
                                          {QStringLiteral("word"), w.text}});
            }
        }
        QVariantList issues;
        if (rules) {
            const auto inDictionaries = [&](const QString &word, const QList<std::shared_ptr<Dictionary>> &list) {
                for (const auto &d : list)
                    if (d->canEncode(word) && d->speller->spell(d->encode(word))) return true;
                return false;
            };
            const auto germanDicts = m_engine->active(this, dir, german, false);
            const auto englishDicts = m_engine->active(this, dir, false, english);
            issues = GrammarRules::check(text, rulesWords, cursor, german, english,
                [&](const QString &w) { return inDictionaries(w, germanDicts); },
                [&](const QString &w) { return inDictionaries(w, englishDicts); });
        }
        if (id == m_latestCheck.load())
            emit checked(text, ranges, issues);
    }, Qt::QueuedConnection);
}

void SpellChecker::loadFrequencyWords()
{
    const QString dir = m_dir;
    QMetaObject::invokeMethod(m_context, [=, this] {
        const auto readWords = [&dir](const QString &name) {
            QStringList words;
            QFile file(dir + QLatin1Char('/') + name);
            if (file.open(QIODevice::ReadOnly))
                words = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            return words;
        };
        // Die Listen stammen aus Untertiteln: alles klein und mit Namen und Tippfehlern. Das
        // Wörterbuch entscheidet, was bleibt und wie es geschrieben wird („haus“ → „Haus“).
        const auto corrected = [](const QStringList &words, const QList<std::shared_ptr<Dictionary>> &dicts) {
            QStringList out;
            for (const QString &word : words) {
                QString capital = word;
                capital[0] = capital.at(0).toUpper();
                bool lowerOk = false, capitalOk = false;
                for (const auto &d : dicts) {
                    if (!d->canEncode(word)) continue;
                    lowerOk = lowerOk || d->speller->spell(d->encode(word));
                    capitalOk = capitalOk || d->speller->spell(d->encode(capital));
                }
                if (lowerOk) out << word;
                else if (capitalOk) out << capital;
            }
            return out;
        };
        const QStringList german = corrected(readWords(QStringLiteral("freq_de.txt")),
                                              m_engine->active(this, dir, true, false));
        const QStringList english = corrected(readWords(QStringLiteral("freq_en.txt")),
                                               m_engine->active(this, dir, false, true));
        emit frequencyWordsReady(german, english);
    }, Qt::QueuedConnection);
}

void SpellChecker::requestSuggestions(const QString &word)
{
    const quint64 id = ++m_latestSuggest;
    const QString dir = m_dir;
    const bool german = m_german;
    const bool english = m_english;
    QMetaObject::invokeMethod(m_context, [=, this] {
        if (id != m_latestSuggest.load()) return;
        const auto dicts = m_engine->active(this, dir, german, english);
        const QString query = normalized(word);

        // Immer abwechselnd aus beiden Sprachen, damit keine die andere verdrängt.
        QList<QStringList> perDictionary;
        for (const auto &d : dicts) {
            QStringList list;
            if (d->canEncode(query)) {
                const std::vector<std::string> found = d->speller->suggest(d->encode(query));
                for (const std::string &s : found)
                    list << d->decode(s);
            }
            perDictionary << list;
        }
        QStringList merged;
        for (int i = 0;; ++i) {
            bool any = false;
            for (const QStringList &list : perDictionary) {
                if (i >= list.size()) continue;
                any = true;
                const QString &candidate = list.at(i);
                // Hunspell bietet auch Wortanfänge wie „Rechtschreibfehler-“ an.
                if (!candidate.endsWith(QLatin1Char('-')) && !merged.contains(candidate))
                    merged << candidate;
            }
            if (!any) break;
        }
        // Jedes Wörterbuch sortiert nur für sich: Der ähnlichste Treffer gehört nach oben,
        // egal aus welcher Sprache er kommt. Getrennte Vorschläge wie „St rase“ stehen hinter
        // allen ganzen Wörtern und fallen bei genug Treffern heraus (auch „a lot“ für „alot“).
        const auto isSplit = [](const QString &s) {
            return s.contains(QLatin1Char(' ')) || s.contains(QLatin1Char('-'));
        };
        std::stable_sort(merged.begin(), merged.end(), [&](const QString &a, const QString &b) {
            if (isSplit(a) != isSplit(b)) return isSplit(b);
            return editDistance(query, a) < editDistance(query, b);
        });
        if (merged.size() > kMaxSuggestions) merged.resize(kMaxSuggestions);
        emit suggestionsReady(word, merged);
    }, Qt::QueuedConnection);
}
