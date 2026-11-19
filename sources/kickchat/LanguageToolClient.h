#pragma once
// Grammatikprüfung online über api.languagetool.org. Der Text geht an diesen Dienst; deshalb ist
// sie abschaltbar, wartet auf eine Tipp-Pause und blendet @Namen, Tags und Links aus (Markup).
// Die Gratis-Stufe erlaubt 20 Anfragen pro Minute, das Limit hält dieser Client selbst ein.

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

class LanguageToolClient : public QObject
{
    Q_OBJECT
public:
    using Range = QPair<int, int>;   // Start, Länge in UTF-16-Einheiten

    explicit LanguageToolClient(QObject *parent = nullptr);

    // Prüft text nach der Tipp-Pause; wiederholte Aufrufe mit gleichem Text schaden nicht.
    void schedule(const QString &text, const QList<Range> &hidden, bool german, bool english);
    void cancel();

    // Rechtschreibung bleibt bei Hunspell, Tippfehler-Treffer fallen hier heraus.
    static QVariantList parseMatches(const QByteArray &json);
    static QByteArray buildAnnotation(const QString &text, const QList<Range> &hidden);

signals:
    void result(const QString &text, const QVariantList &issues);
    void failed(const QString &reason);

private:
    void fire();
    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    QTimer m_pause;
    QElapsedTimer m_clock;
    QVector<qint64> m_sentAt;
    qint64 m_blockedUntil = 0;
    QString m_text;
    QList<Range> m_hidden;
    QString m_language;
    QString m_lastKey;          // Sprache + Text der letzten Antwort
    QVariantList m_lastIssues;
};
