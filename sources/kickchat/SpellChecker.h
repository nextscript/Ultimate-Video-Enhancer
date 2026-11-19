#pragma once
// Rechtschreibprüfung fürs Chat-Eingabefeld (Deutsch + US-/britisches Englisch, Hunspell).
// Die Wörterbücher laden erst beim ersten Prüfen. Laden, Prüfen und Vorschläge laufen auf
// einem eigenen Thread, damit das Tippen und der Chat nie stocken. Eine Antwort zu einem
// veralteten Text wird verworfen, weil das Panel nur den jeweils letzten Stand braucht.

#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QVariantList>
#include <atomic>
#include <functional>

class SpellChecker : public QObject
{
    Q_OBJECT
public:
    struct Word
    {
        int start = 0;     // UTF-16-Position in der Nachricht (wie TextField sie zählt)
        int length = 0;
        QString text;
    };

    explicit SpellChecker(QObject *parent = nullptr);
    ~SpellChecker() override;

    // Ordner mit de_DE_frami, en_US und en_GB (je .aff/.dic)
    void setDictionaryDir(const QString &dir);
    void setLanguages(bool german, bool english);
    void setUserWords(const QSet<QString> &lowerCaseWords);

    // Zerlegt die Nachricht in zu prüfende Wörter. Draußen bleiben: Befehle, @Namen, #Tags,
    // Links, Wörter mit Ziffern, GROSS geschriebene, BinnenMajuskeln (fast immer Emotes),
    // Emotes laut isEmote, nichtlateinische Schrift und das Wort, das gerade am Cursor entsteht.
    static QList<Word> tokenize(const QString &text, int cursor,
                                const std::function<bool(const QString &)> &isEmote);

    // Teil einer Nachricht, der nicht als Text gilt: @Name, #Tag, Link, Emote, am Anfang auch ein Befehl.
    static bool isHiddenChunk(const QString &chunk, int chunkStart, const std::function<bool(const QString &)> &isEmote);
    // Alle Wörter für die Satzbauregeln: auch GROSS und Einzelbuchstaben, nur Verstecktes fehlt.
    static QList<Word> ruleWords(const QString &text, const std::function<bool(const QString &)> &isEmote);
    // Ausgeblendete Bereiche als Start/Länge (für LanguageTool)
    static QList<QPair<int, int>> hiddenRanges(const QString &text, const std::function<bool(const QString &)> &isEmote);

    void check(const QString &text, const QList<Word> &words, const QList<Word> &rulesWords = {},
               int cursor = -1, bool rules = false);
    void loadFrequencyWords();
    void requestSuggestions(const QString &word);

signals:
    void checked(const QString &text, const QVariantList &ranges, const QVariantList &issues);   // ranges: [{start,length,word}]
    void frequencyWordsReady(const QStringList &german, const QStringList &english);
    void suggestionsReady(const QString &word, const QStringList &suggestions);
    void dictionaryMissing(const QString &path);

private:
    struct Engine;
    QThread m_thread;
    QObject *m_context = nullptr;     // lebt auf m_thread; dorthin gehen alle Aufträge
    Engine *m_engine = nullptr;       // nur auf m_thread benutzt
    QString m_dir;
    bool m_german = true;
    bool m_english = true;
    QSet<QString> m_userWords;
    std::atomic<quint64> m_latestCheck{0};
    std::atomic<quint64> m_latestSuggest{0};
};
