#include "RadioBrowser.h"
#include "Caches.h"

#include <QDnsLookup>
#include <QHostInfo>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSet>
#include <QTimer>
#include <algorithm>
#include <memory>

namespace uve::radio {

// Rückfall, wenn DNS nichts liefert (dokumentierte Spiegel; die Liste wird beim Start per DNS ersetzt)
static const char *kFallbackMirrors[] = {"de1.api.radio-browser.info", "de2.api.radio-browser.info",
                                         "fi1.api.radio-browser.info", "nl1.api.radio-browser.info",
                                         "at1.api.radio-browser.info"};

QUrlQuery SearchQuery::toQuery() const
{
    QUrlQuery q;
    if (!name.trimmed().isEmpty()) q.addQueryItem(QStringLiteral("name"), name.trimmed());
    if (!countryCode.isEmpty()) q.addQueryItem(QStringLiteral("countrycode"), countryCode);
    if (!language.isEmpty()) {
        q.addQueryItem(QStringLiteral("language"), language);
        q.addQueryItem(QStringLiteral("languageExact"), QStringLiteral("true"));
    }
    if (!tag.isEmpty()) {
        q.addQueryItem(QStringLiteral("tag"), tag);
        q.addQueryItem(QStringLiteral("tagExact"), QStringLiteral("true"));
    }
    if (order == QLatin1String("name")) {
        q.addQueryItem(QStringLiteral("order"), QStringLiteral("name"));
    } else {
        q.addQueryItem(QStringLiteral("order"), QStringLiteral("clickcount"));
        q.addQueryItem(QStringLiteral("reverse"), QStringLiteral("true"));
    }
    q.addQueryItem(QStringLiteral("hidebroken"), QStringLiteral("true"));
    q.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    q.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    return q;
}

RadioBrowser::RadioBrowser(QNetworkAccessManager *nam, CatalogCache *cache, QObject *parent)
    : QObject(parent), m_nam(nam), m_cache(cache)
{
    const QString env = qEnvironmentVariable("UVEINETRADIO_API");
    if (!env.isEmpty()) {
        QList<QUrl> list;
        for (const QString &s : env.split(QLatin1Char(','), Qt::SkipEmptyParts)) list << QUrl(s.trimmed());
        setMirrors(list);
    }
}

void RadioBrowser::setMirrors(const QList<QUrl> &mirrors)
{
    m_mirrors = mirrors;
    m_current = 0;
    m_discovered = true;
}

QString RadioBrowser::currentMirror() const
{
    return m_mirrors.isEmpty() ? QString() : m_mirrors.value(m_current).host();
}

// --------------------------------------------------------------------------- Spiegelermittlung
// Laut Dokumentation: DNS-Abfrage von all.api.radio-browser.info, zu jeder Adresse den Namen per Rückwärtssuche,
// dann zufällig mischen und per HTTPS über den Namen ansprechen. Zusätzlich die SRV-Einträge _api._tcp.
void RadioBrowser::discover()
{
    if (m_discovering) return;
    m_discovering = true;

    auto found = std::make_shared<QSet<QString>>();
    auto pending = std::make_shared<int>(2);   // SRV + A/AAAA
    auto guard = std::make_shared<bool>(false);
    auto finish = [this, found, guard] {
        if (*guard) return;
        *guard = true;
        QList<QUrl> list;
        for (const QString &h : *found) list << QUrl(QStringLiteral("https://") + h);
        discoveryDone(list);
    };
    auto done = [pending, finish] { if (--*pending <= 0) finish(); };
    // Insgesamt höchstens 4 s warten – danach mit dem, was da ist (oder dem Rückfall)
    QTimer::singleShot(4000, this, finish);

    auto *srv = new QDnsLookup(QDnsLookup::SRV, QStringLiteral("_api._tcp.radio-browser.info"), this);
    connect(srv, &QDnsLookup::finished, this, [srv, found, done] {
        if (srv->error() == QDnsLookup::NoError)
            for (const auto &r : srv->serviceRecords()) {
                QString t = r.target();
                if (t.endsWith(QLatin1Char('.'))) t.chop(1);
                if (t.endsWith(QLatin1String(".radio-browser.info"), Qt::CaseInsensitive)) found->insert(t.toLower());
            }
        srv->deleteLater();
        done();
    });
    srv->lookup();

    QHostInfo::lookupHost(QStringLiteral("all.api.radio-browser.info"), this, [this, found, done](const QHostInfo &info) {
        const auto addrs = info.addresses();
        if (addrs.isEmpty()) { done(); return; }
        auto left = std::make_shared<int>(int(addrs.size()));
        for (const auto &a : addrs) {
            QHostInfo::lookupHost(a.toString(), this, [found, left, done](const QHostInfo &rev) {
                const QString h = rev.hostName().toLower();
                if (h.endsWith(QLatin1String(".radio-browser.info")) && h != QLatin1String("all.api.radio-browser.info"))
                    found->insert(h);
                if (--*left == 0) done();
            });
        }
    });
}

void RadioBrowser::discoveryDone(QList<QUrl> found)
{
    m_discovering = false;
    if (found.isEmpty())
        for (const char *h : kFallbackMirrors) found << QUrl(QStringLiteral("https://") + QLatin1String(h));
    std::shuffle(found.begin(), found.end(), *QRandomGenerator::global());
    m_mirrors = found;
    m_current = 0;
    m_discovered = true;
    emit mirrorsDiscovered(int(found.size()));
    emit mirrorChanged(currentMirror());
    const auto waiting = std::exchange(m_waiting, {});
    for (const quint64 id : waiting) start(id);
}

// --------------------------------------------------------------------------- Anfragen
QString RadioBrowser::cacheKey(const QString &path, const QUrlQuery &query) const
{
    return path + QLatin1Char('?') + query.toString(QUrl::FullyEncoded);
}

quint64 RadioBrowser::get(const QString &path, const QUrlQuery &query, int cacheTtlSec, Callback cb)
{
    const quint64 id = ++m_seq;
    // Frischer Cache-Treffer: ohne Netz antworten
    if (cacheTtlSec > 0 && m_cache) {
        const QByteArray cached = m_cache->get(cacheKey(path, query), cacheTtlSec);
        if (!cached.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(cached);
            if (!doc.isNull()) {
                QTimer::singleShot(0, this, [cb, doc] { cb(Result{true, doc, {}, false}); });
                return id;
            }
        }
    }
    m_pending.insert(id, Pending{id, path, query, cacheTtlSec, std::move(cb), 0, {}});
    if (!m_discovered) { m_waiting << id; discover(); return id; }
    start(id);
    return id;
}

void RadioBrowser::abort(quint64 requestId)
{
    m_waiting.removeAll(requestId);
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) return;
    QPointer<QNetworkReply> r = it->reply;
    m_pending.erase(it);   // zuerst entfernen: finished() des abgebrochenen Replys findet nichts mehr
    if (r) { r->disconnect(this); r->abort(); r->deleteLater(); }
}

void RadioBrowser::start(quint64 id)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end()) return;
    if (m_mirrors.isEmpty()) { fail(id, QStringLiteral("no API mirror")); return; }
    ++it->attempts;
    QUrl url = m_mirrors.value(m_current);
    url.setPath(it->path);
    url.setQuery(it->query);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(m_timeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->get(req);
    it->reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, id, reply] { finished(id, reply); });
}

bool RadioBrowser::retryable(QNetworkReply *reply) const
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 500 || status == 429 || status == 408) return true;
    switch (reply->error()) {
    case QNetworkReply::NoError:
        return false;
    case QNetworkReply::OperationCanceledError:   // Zeitlimit (setTransferTimeout); eigene Abbrüche kommen hier nicht an
        return true;
    case QNetworkReply::ContentNotFoundError:
    case QNetworkReply::ContentAccessDenied:
        return false;   // gleicher Fehler auf jedem Spiegel
    default:
        return true;    // Timeout, Verbindung, TLS-Fehler des Spiegels, DNS …
    }
}

void RadioBrowser::finished(quint64 id, QNetworkReply *reply)
{
    reply->deleteLater();
    auto it = m_pending.find(id);
    if (it == m_pending.end() || it->reply != reply) return;   // abgebrochen oder überholt

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
        const QByteArray body = reply->readAll();
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
        if (!doc.isNull()) {
            if (it->cacheTtl > 0 && m_cache) m_cache->put(cacheKey(it->path, it->query), body);
            const Callback cb = it->cb;
            m_pending.erase(it);
            cb(Result{true, doc, {}, false});
            return;
        }
        // Kaputte Antwort eines Spiegels: wie Ausfall behandeln
    } else if (!retryable(reply)) {
        fail(id, reply->errorString());
        return;
    }
    const QString why = reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                                 : QStringLiteral("HTTP %1").arg(status);
    // Nächster Spiegel, begrenzte Wiederholungen
    if (m_mirrors.size() > 1) {
        m_current = (m_current + 1) % int(m_mirrors.size());
        emit mirrorChanged(currentMirror());
    }
    if (it->attempts < qMin(m_maxAttempts, qMax(1, int(m_mirrors.size())))) {
        start(id);
        return;
    }
    fail(id, why);
}

void RadioBrowser::fail(quint64 id, const QString &error)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end()) return;
    Pending p = *it;
    m_pending.erase(it);
    // API nicht erreichbar: zuletzt gespeicherte Antwort (auch veraltet) besser als nichts
    if (p.cacheTtl > 0 && m_cache) {
        const QByteArray cached = m_cache->get(cacheKey(p.path, p.query), -1);
        const QJsonDocument doc = QJsonDocument::fromJson(cached);
        if (!doc.isNull()) { p.cb(Result{true, doc, error, true}); return; }
    }
    p.cb(Result{false, {}, error, false});
}

quint64 RadioBrowser::search(const SearchQuery &q, Callback cb)
{
    return get(QStringLiteral("/json/stations/search"), q.toQuery(), 3600, std::move(cb));
}

static QUrlQuery facetQuery(int limit)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("order"), QStringLiteral("stationcount"));
    q.addQueryItem(QStringLiteral("reverse"), QStringLiteral("true"));
    q.addQueryItem(QStringLiteral("hidebroken"), QStringLiteral("true"));
    if (limit > 0) q.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    return q;
}

quint64 RadioBrowser::countries(Callback cb) { return get(QStringLiteral("/json/countries"), facetQuery(0), 86400, std::move(cb)); }
quint64 RadioBrowser::languages(Callback cb) { return get(QStringLiteral("/json/languages"), facetQuery(300), 86400, std::move(cb)); }
quint64 RadioBrowser::tags(Callback cb) { return get(QStringLiteral("/json/tags"), facetQuery(400), 86400, std::move(cb)); }

quint64 RadioBrowser::stationByUuid(const QString &uuid, Callback cb)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("uuids"), uuid);
    return get(QStringLiteral("/json/stations/byuuid"), q, 0, std::move(cb));
}

void RadioBrowser::click(const QString &uuid)
{
    if (uuid.isEmpty() || uuid.startsWith(QLatin1String("custom:"))) return;
    get(QStringLiteral("/json/url/") + QString::fromLatin1(QUrl::toPercentEncoding(uuid)), {}, 0, [](const Result &) {});
}

} // namespace uve::radio
