#include "TextPredictor.h"

#include "SpellChecker.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

namespace {
constexpr int kMaxLearnedWords = 30000;
constexpr int kChatMinimum = 2;      // Chat-Wissen zählt erst, wenn es sich wiederholt
constexpr double kFollowingScore = 1000000;
constexpr double kOwnScore = 20000;
constexpr double kChatScore = 2000;
constexpr double kEmoteScore = 1500;
constexpr double kFrequencyScore = 1000;

QStringList wordsOfChunk(const QString &chunk)
{
    static const QRegularExpression letters(QStringLiteral("\\p{L}[\\p{L}\\p{M}]*(?:['\\x{2019}]\\p{L}[\\p{L}\\p{M}]*)*"),
                                            QRegularExpression::UseUnicodePropertiesOption);
    QStringList out;
    auto it = letters.globalMatch(chunk);
    while (it.hasNext()) out << it.next().captured();
    return out;
}

bool endsSentence(const QString &chunk)
{
    return chunk.endsWith(QLatin1Char('.')) || chunk.endsWith(QLatin1Char('!')) || chunk.endsWith(QLatin1Char('?'));
}

bool hasDigit(const QString &chunk)
{
    return std::any_of(chunk.begin(), chunk.end(), [](QChar c) { return c.isDigit() || c == QLatin1Char('_'); });
}
} // namespace

std::vector<TextPredictor::Entry> TextPredictor::buildEntries(const QStringList &words)
{
    std::vector<Entry> entries;
    entries.reserve(size_t(words.size()));
    int rank = 0;
    for (const QString &w : words)
        entries.push_back(Entry{w.toLower(), w, rank++});
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.lower < b.lower; });
    return entries;
}

void TextPredictor::setFrequencyWords(const QStringList &german, const QStringList &english)
{
    m_german = buildEntries(german);
    m_english = buildEntries(english);
    m_known.clear();
    for (const QStringList *list : {&german, &english})
        for (const QString &w : *list)
            m_known.insert(w.toLower(), w);
}

void TextPredictor::Model::add(const QString &previous, const QString &word)
{
    words[word] += 1;
    if (!previous.isEmpty()) following[previous][word] += 1;
}

void TextPredictor::learn(const QString &text, bool own, const EmoteTest &isEmote)
{
    if (text.startsWith(QLatin1Char('/')) || text.startsWith(QLatin1Char('!'))) return;   // Befehle, Flüstern
    Model &model = own ? m_own : m_chat;
    if (model.size() >= kMaxLearnedWords) {
        if (own) return;      // Der eigene Wortschatz bleibt erhalten, er wächst nur nicht weiter.
        model = Model();
    }

    static const QRegularExpression chunks(QStringLiteral("\\S+"));
    QString previous;
    auto it = chunks.globalMatch(text);
    while (it.hasNext()) {
        const QString chunk = it.next().captured();
        if (SpellChecker::isHiddenChunk(chunk, 1, nullptr) || hasDigit(chunk)) {   // Befehle sind oben schon raus
            previous.clear();
            continue;
        }
        const QStringList words = isEmote && isEmote(chunk) ? QStringList{chunk} : wordsOfChunk(chunk);
        for (const QString &word : words) {
            const QString lower = word.toLower();
            const bool emote = isEmote && isEmote(word);
            // Aus dem Chat nur bekannte Wörter: Tippfehler anderer gehören nicht in die Vorschläge.
            if (lower.size() < 2 || (!own && !emote && !m_known.contains(lower))) {
                previous.clear();
                continue;
            }
            model.add(previous, lower);
            if (own || emote || !m_surfaces.contains(lower))
                m_surfaces.insert(lower, emote ? word : (own ? word : m_known.value(lower, word)));
            previous = lower;
        }
        if (endsSentence(chunk)) previous.clear();
    }
}

int TextPredictor::wordStart(const QString &text, int cursor)
{
    int start = qBound(0, cursor, int(text.size()));
    while (start > 0) {
        const QChar c = text.at(start - 1);
        if (c.isSpace()) break;
        --start;
    }
    return start;
}

QStringList TextPredictor::predict(const QString &text, int cursor, bool german, bool english,
                                   const EmoteTest &isEmote, const EmoteSearch &findEmotes, int count) const
{
    if (cursor < 0 || cursor > text.size()) return {};
    if (text.startsWith(QLatin1Char('/'))) return {};
    if (cursor < text.size() && !text.at(cursor).isSpace()) return {};   // mitten im Wort

    const int begin = wordStart(text, cursor);
    const QString prefix = text.mid(begin, cursor - begin);
    if (prefix.startsWith(QLatin1Char('@')) || prefix.startsWith(QLatin1Char('!'))
        || prefix.contains(QStringLiteral("://")))
        return {};
    const QString lowerPrefix = prefix.toLower();

    // Vorgängerwort, sofern derselbe Satz weiterläuft
    QString previous;
    {
        int end = begin;
        while (end > 0 && text.at(end - 1).isSpace()) --end;
        const QString lastChunk = text.mid(wordStart(text, end), end - wordStart(text, end));
        if (!lastChunk.isEmpty() && !endsSentence(lastChunk)) {
            if (isEmote && isEmote(lastChunk)) {
                previous = lastChunk.toLower();
            } else {
                const QStringList words = wordsOfChunk(lastChunk);
                if (!words.isEmpty() && lastChunk.endsWith(words.last())) previous = words.last().toLower();
            }
        }
    }
    if (prefix.isEmpty() && previous.isEmpty()) return {};

    QHash<QString, double> scores;
    const auto raise = [&](const QString &lower, double score) {
        double &current = scores[lower];
        current = std::max(current, score);
    };
    const auto addFollowing = [&](const Model &model, int minimum, double factor) {
        const auto found = model.following.constFind(previous);
        if (found == model.following.constEnd()) return;
        for (auto it = found->constBegin(); it != found->constEnd(); ++it)
            if (it.value() >= minimum && it.key().startsWith(lowerPrefix))
                raise(it.key(), kFollowingScore * factor + it.value());
    };
    if (!previous.isEmpty()) {
        addFollowing(m_own, 1, 2);
        addFollowing(m_chat, kChatMinimum, 1);
    }

    QHash<QString, QString> emoteNames;
    if (!prefix.isEmpty()) {
        const auto addWords = [&](const Model &model, int minimum, double base) {
            for (auto it = model.words.constBegin(); it != model.words.constEnd(); ++it)
                if (it.value() >= minimum && it.key().startsWith(lowerPrefix))
                    raise(it.key(), base + it.value());
        };
        addWords(m_own, 1, kOwnScore);
        addWords(m_chat, kChatMinimum, kChatScore);
        if (prefix.size() >= 2 && findEmotes) {
            const QStringList emotes = findEmotes(prefix);
            for (const QString &name : emotes) {
                emoteNames.insert(name.toLower(), name);
                // Exakte Schreibweise: Der Nutzer tippt das Emote gezielt.
                raise(name.toLower(), kEmoteScore + (name.startsWith(prefix) ? 2000 : 0));
            }
        }
        const auto addFrequency = [&](const std::vector<Entry> &entries) {
            auto it = std::lower_bound(entries.begin(), entries.end(), lowerPrefix,
                                       [](const Entry &e, const QString &value) { return e.lower < value; });
            for (; it != entries.end() && it->lower.startsWith(lowerPrefix); ++it)
                raise(it->lower, kFrequencyScore * (1.0 - double(it->rank) / double(entries.size())));
        };
        if (german) addFrequency(m_german);
        if (english) addFrequency(m_english);
    }
    scores.remove(lowerPrefix);

    QStringList ordered = scores.keys();
    std::sort(ordered.begin(), ordered.end(), [&scores](const QString &a, const QString &b) {
        const double x = scores.value(a), y = scores.value(b);
        return x != y ? x > y : a < b;
    });

    QStringList out;
    for (const QString &lower : std::as_const(ordered)) {
        if (out.size() == count) break;
        QString surface = emoteNames.value(lower);
        const bool emote = !surface.isEmpty();
        if (!emote) surface = m_surfaces.value(lower, m_known.value(lower, lower));
        // Großer Anfang bleibt groß, etwa am Satzanfang.
        if (!emote && !prefix.isEmpty() && prefix.at(0).isUpper()) surface[0] = surface.at(0).toUpper();
        if (!out.contains(surface)) out << surface;
    }
    return out;
}

QJsonObject TextPredictor::saveOwn() const
{
    QJsonObject words;
    for (auto it = m_own.words.constBegin(); it != m_own.words.constEnd(); ++it)
        words.insert(m_surfaces.value(it.key(), it.key()), it.value());
    QJsonObject following;
    for (auto it = m_own.following.constBegin(); it != m_own.following.constEnd(); ++it) {
        QJsonObject next;
        for (auto n = it->constBegin(); n != it->constEnd(); ++n)
            next.insert(n.key(), n.value());
        following.insert(it.key(), next);
    }
    return QJsonObject{{QStringLiteral("words"), words}, {QStringLiteral("following"), following}};
}

void TextPredictor::loadOwn(const QJsonObject &saved)
{
    const QJsonObject words = saved.value(QStringLiteral("words")).toObject();
    for (auto it = words.constBegin(); it != words.constEnd(); ++it) {
        const QString surface = it.key();
        m_own.words[surface.toLower()] = it.value().toInt();
        m_surfaces.insert(surface.toLower(), surface);
    }
    const QJsonObject following = saved.value(QStringLiteral("following")).toObject();
    for (auto it = following.constBegin(); it != following.constEnd(); ++it) {
        const QJsonObject next = it.value().toObject();
        for (auto n = next.constBegin(); n != next.constEnd(); ++n)
            m_own.following[it.key()][n.key()] = n.value().toInt();
    }
}
