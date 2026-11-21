#pragma once
// Radio Browser (https://docs.radio-browser.info/): Spiegelermittlung über all.api.radio-browser.info,
// Wechsel zum nächsten Spiegel bei Timeout/Ausfall, Suche, Filterlisten, Senderaktualisierung, optionaler Klick.
// Getrennt von der Wiedergabe: hier werden nur Katalogdaten geholt.
#include "RadioTypes.h"

#include <QHash>
#include <QJsonDocument>
#include <QList>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

class QNetworkAccessManager;

namespace uve::radio {

class CatalogCache;

struct SearchQuery
{
    QString name;
    QString countryCode;
    QString language;
    QString tag;
    QString order = QStringLiteral("popularity");   // "name" oder "popularity"
    int offset = 0;
    int limit = 30;
    QUrlQuery toQuery() const;
};

class RadioBrowser : public QObject
{
    Q_OBJECT
public:
    RadioBrowser(QNetworkAccessManager *nam, CatalogCache *cache, QObject *parent = nullptr);

    // Für Tests und Notfälle: feste Spiegel statt DNS (auch per Umgebungsvariable UVEINETRADIO_API, kommagetrennt)
    void setMirrors(const QList<QUrl> &mirrors);
    QList<QUrl> mirrors() const { return m_mirrors; }
    QString currentMirror() const;
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    void setMaxAttempts(int n) { m_maxAttempts = qMax(1, n); }

    // Ergebnis eines API-Aufrufs: data bei Erfolg, sonst error (fromCache = Antwort aus dem Katalog-Cache)
    struct Result
    {
        bool ok = false;
        QJsonDocument data;
        QString error;
        bool fromCache = false;
    };
    using Callback = std::function<void(const Result &)>;

    // GET <mirror><path>?<query>; cacheTtlSec > 0: Antwort zwischenspeichern und bei Ausfall verwenden
    // Rückgabe: Anfrage-Nummer (für abort)
    quint64 get(const QString &path, const QUrlQuery &query, int cacheTtlSec, Callback cb);
    void abort(quint64 requestId);

    quint64 search(const SearchQuery &q, Callback cb);
    quint64 countries(Callback cb);
    quint64 languages(Callback cb);
    quint64 tags(Callback cb);
    quint64 stationByUuid(const QString &uuid, Callback cb);
    void click(const QString &uuid);   // /json/url/<uuid> – nur bei aktivierter Klick-Weitergabe aufrufen

signals:
    void mirrorChanged(const QString &host);
    void mirrorsDiscovered(int count);

private:
    struct Pending
    {
        quint64 id = 0;
        QString path;
        QUrlQuery query;
        int cacheTtl = 0;
        Callback cb;
        int attempts = 0;
        QPointer<QNetworkReply> reply;
    };
    void discover();
    void discoveryDone(QList<QUrl> found);
    void start(quint64 id);
    void finished(quint64 id, QNetworkReply *reply);
    void fail(quint64 id, const QString &error);
    bool retryable(QNetworkReply *reply) const;
    QString cacheKey(const QString &path, const QUrlQuery &query) const;

    QNetworkAccessManager *m_nam;
    CatalogCache *m_cache;
    QList<QUrl> m_mirrors;
    int m_current = 0;
    bool m_discovering = false;
    bool m_discovered = false;
    int m_timeoutMs = 8000;
    int m_maxAttempts = 3;
    quint64 m_seq = 0;
    QHash<quint64, Pending> m_pending;
    QList<quint64> m_waiting;   // warten auf die Spiegelermittlung
};

} // namespace uve::radio
