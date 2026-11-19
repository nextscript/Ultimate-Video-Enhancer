#pragma once
// Wortvorschläge beim Tippen, rein lokal. Quellen, nach Vorrang: Folgewörter aus dem, was der
// Nutzer und der Chat geschrieben haben, gelernte Wörter, Emote-Namen, Häufigkeitslisten.
// Gespeichert wird nur, was der Nutzer selbst schreibt; Chat-Wissen bleibt im Arbeitsspeicher.

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

class TextPredictor
{
public:
    using EmoteTest = std::function<bool(const QString &)>;
    using EmoteSearch = std::function<QStringList(const QString &prefix)>;

    // words: nach Häufigkeit sortiert. Die erste Liste wird beim Vorschlagen über languages gewählt.
    void setFrequencyWords(const QStringList &german, const QStringList &english);
    bool hasFrequencyWords() const { return !m_german.empty() || !m_english.empty(); }

    void learn(const QString &text, bool own, const EmoteTest &isEmote);
    QStringList predict(const QString &text, int cursor, bool german, bool english,
                        const EmoteTest &isEmote, const EmoteSearch &findEmotes, int count = 3) const;
    // Position, ab der ein Vorschlag das angefangene Wort ersetzt
    static int wordStart(const QString &text, int cursor);

    QJsonObject saveOwn() const;
    void loadOwn(const QJsonObject &saved);

private:
    struct Entry
    {
        QString lower;
        QString surface;
        int rank = 0;
    };
    struct Model
    {
        QHash<QString, int> words;                       // kleingeschrieben → Anzahl
        QHash<QString, QHash<QString, int>> following;   // Vorgänger → Folgewort → Anzahl
        void add(const QString &previous, const QString &word);
        int size() const { return int(words.size()); }
    };
    static std::vector<Entry> buildEntries(const QStringList &words);

    std::vector<Entry> m_german;
    std::vector<Entry> m_english;
    QHash<QString, QString> m_known;      // kleingeschrieben → Schreibweise aus der Häufigkeitsliste
    QHash<QString, QString> m_surfaces;   // kleingeschrieben → zuletzt gesehene Schreibweise
    Model m_own;
    Model m_chat;
};
