#pragma once
// Kurze Vorprüfung einer Stream-Adresse vor der Übergabe an den Player:
//   • Weiterleitungen nur zu http/https, kein Wechsel von https zu http
//   • TLS-Zertifikatsfehler werden nicht übergangen
//   • HTML-Antworten (Webseite statt Stream) werden erkannt
// Alles andere (z. B. alte SHOUTcast-Server mit "ICY 200", die Qt nicht versteht) entscheidet der Player selbst.
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>

class QNetworkAccessManager;

namespace uve::radio {

struct ProbeResult
{
    enum Kind { Ok, Unknown, WebPage, BadRedirect, Insecure, Certificate, Unreachable };
    Kind kind = Unknown;
    QString detail;       // Fehlertext (Netzwerk) oder Content-Type
    QString contentType;
    bool blocking() const { return kind == WebPage || kind == BadRedirect || kind == Certificate || kind == Unreachable; }
    QString code() const;
};

class StreamProbe : public QObject
{
    Q_OBJECT
public:
    StreamProbe(QNetworkAccessManager *nam, QObject *parent = nullptr);
    ~StreamProbe() override;
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    // Eine Prüfung zur Zeit; eine neue bricht die alte ab (deren Rückruf kommt nicht mehr)
    void probe(const QString &url, std::function<void(const ProbeResult &)> done);
    void cancel();

private:
    void finish(ProbeResult r);
    void inspect();

    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
    std::function<void(const ProbeResult &)> m_done;
    QByteArray m_head;
    int m_timeoutMs = 7000;
    int m_redirects = 0;
};

} // namespace uve::radio
