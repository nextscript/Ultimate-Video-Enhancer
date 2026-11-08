#pragma once
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace uve {

// Download in eine Datei, der Unterbrechungen und Begrenzungen des Servers übersteht:
//  - HTTP 403/408/425/429/5xx und Netzwerkfehler werden wiederholt (Retry-After wird beachtet),
//  - bereits geladene Bytes bleiben erhalten und werden per Range fortgesetzt,
//  - solange ein Versuch Daten liefert, wird der Fehlerzähler zurückgesetzt.
// Dauerhafte Fehler (z. B. 404) melden sich sofort. Löscht sich nicht selbst: Besitzer ist parent.
class ResumableDownload : public QObject
{
    Q_OBJECT
public:
    ResumableDownload(QNetworkAccessManager *net, const QUrl &url, const QString &file, QObject *parent = nullptr);

    void setMaxAttempts(int n) { m_maxAttempts = n; }
    void setBaseDelayMs(int ms) { m_baseDelayMs = ms; }
    // false: vorhandene Datei überschreiben statt fortsetzen
    void setResume(bool resume) { m_resume = resume; }

    void start();
    void abort();

signals:
    void progress(qint64 received, qint64 total);
    void retrying(int attempt, int maxAttempts, int delaySeconds);
    void finished(bool ok, const QString &error);

private:
    void startAttempt();
    void onHeaders();
    void onFinished();
    void retryOrFail(const QString &reason, int retryAfterSeconds);
    void done(bool ok, const QString &error);

    QNetworkAccessManager *m_net;
    QUrl m_url;
    QFile m_file;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
    int m_maxAttempts = 6;
    int m_baseDelayMs = 2000;
    int m_attempt = 0;
    bool m_resume = true;
    bool m_headersSeen = false;
    bool m_aborted = false;
    bool m_finished = false;
    qint64 m_base = 0;        // Bytes aus früheren Versuchen
    qint64 m_atAttemptStart = 0;
};

} // namespace uve
