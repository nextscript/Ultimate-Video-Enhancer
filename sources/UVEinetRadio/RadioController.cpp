#include "RadioController.h"
#include "Caches.h"
#include "StreamProbe.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QSet>
#include <algorithm>

namespace uve::radio {

static const QString kId = QLatin1String(kPluginId);

RadioController::RadioController(uve::IPlayerHost *host, const QString &dataDir, QObject *parent)
    : QObject(parent),
      m_host(host),
      m_dataDir(dataDir),
      m_nam(new QNetworkAccessManager(this)),
      m_catalogCache(std::make_unique<CatalogCache>(dataDir)),
      m_logos(std::make_unique<LogoCache>(dataDir, m_nam)),
      m_browser(std::make_unique<RadioBrowser>(m_nam, m_catalogCache.get())),
      m_probe(std::make_unique<StreamProbe>(m_nam)),
      m_store(dataDir)
{
    // Kein Zwischenspeichern von Zugangsdaten o. Ä. – nur HSTS respektieren
    m_nam->setStrictTransportSecurityEnabled(true);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(350);
    connect(&m_debounce, &QTimer::timeout, this, [this] { runSearch(true); });

    m_connectTimeout.setSingleShot(true);
    m_connectTimeout.setInterval(60000);
    connect(&m_connectTimeout, &QTimer::timeout, this, [this] {
        if (m_state == QLatin1String("connecting")) setState(QStringLiteral("error"), QStringLiteral("timeout"));
    });

    m_metaTimer.setInterval(2000);
    connect(&m_metaTimer, &QTimer::timeout, this, &RadioController::pollMetadata);

    m_logoBatch.setSingleShot(true);
    m_logoBatch.setInterval(150);
    connect(&m_logoBatch, &QTimer::timeout, this, [this] { ++m_logoRevision; emit logosChanged(); emit cacheChanged(); });
    connect(m_logos.get(), &LogoCache::logoReady, this, [this] { if (!m_logoBatch.isActive()) m_logoBatch.start(); });
    connect(m_browser.get(), &RadioBrowser::mirrorChanged, this, &RadioController::mirrorChanged);

    if (!m_store.load()) m_storageError = m_store.lastError();

    if (m_host) {
        const QJsonObject s = m_host->loadPluginSettings(kId);
        m_sendClicks = s.value(QStringLiteral("sendClicks")).toBool(false);
        m_lastSearch = s.value(QStringLiteral("lastSearch")).toObject().toVariantMap();
        m_query.order = m_lastSearch.value(QStringLiteral("order"), QStringLiteral("popularity")).toString();
    }
}

RadioController::~RadioController() { shutdown(); }

void RadioController::shutdown()
{
    m_debounce.stop();
    m_metaTimer.stop();
    m_connectTimeout.stop();
    if (m_searchReq) m_browser->abort(std::exchange(m_searchReq, 0));
    if (m_probe) m_probe->cancel();
}

QVariant RadioController::call(const QString &cmd, const QVariantMap &args) const
{
    return m_host ? m_host->call(kId, cmd, args) : QVariant();
}

void RadioController::setProbeTimeoutMs(int ms) { m_probe->setTimeoutMs(ms); }

QString RadioController::mirror() const { return m_browser->currentMirror(); }

// --------------------------------------------------------------------------- Netzwerkeinstellungen
// Die Plugin-API hat keinen eigenen Proxy-Befehl. Der Player gibt seinen Proxy an mpv (http-proxy) – den übernehmen
// wir für Katalog und Logos. Ohne Eintrag gilt der Anwendungs-/Systemproxy von Qt.
void RadioController::applyProxy()
{
    if (!m_host || !m_host->hasCommand(QStringLiteral("mpv.get"))) return;
    const QString p = call(QStringLiteral("mpv.get"), {{"name", QStringLiteral("http-proxy")}}).toString().trimmed();
    if (p == m_appliedProxy) return;
    m_appliedProxy = p;
    const QUrl u(p);
    if (p.isEmpty() || !u.isValid() || u.host().isEmpty()) {
        m_nam->setProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
        return;
    }
    QNetworkProxy proxy(u.scheme().startsWith(QLatin1String("socks")) ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy,
                        u.host(), quint16(u.port(8080)), u.userName(), u.password());
    m_nam->setProxy(proxy);
}

// --------------------------------------------------------------------------- Suche
void RadioController::search(const QString &name, const QString &countryCode, const QString &language, const QString &tag,
                             const QString &order)
{
    m_query.name = name.trimmed();
    m_query.countryCode = countryCode;
    m_query.language = language;
    m_query.tag = tag;
    m_query.order = order == QLatin1String("name") ? QStringLiteral("name") : QStringLiteral("popularity");
    m_query.offset = 0;
    ++m_searchGen;                              // laufende Ergebnisse gelten ab jetzt nicht mehr
    if (m_searchReq) m_browser->abort(std::exchange(m_searchReq, 0));
    m_searchBusy = true;
    m_searchError.clear();
    emit searchStateChanged();
    m_debounce.start();

    const QVariantMap ls{{"name", m_query.name}, {"country", countryCode}, {"language", language}, {"tag", tag}, {"order", m_query.order}};
    if (ls != m_lastSearch) { m_lastSearch = ls; saveSettings(); emit settingsChanged(); }
}

void RadioController::retrySearch()
{
    ++m_searchGen;
    m_query.offset = 0;
    runSearch(true);
}

void RadioController::loadMore()
{
    if (m_searchBusy || !m_hasMore) return;
    m_query.offset = int(m_results.size());
    runSearch(false);
}

void RadioController::runSearch(bool reset)
{
    applyProxy();
    if (m_searchReq) m_browser->abort(std::exchange(m_searchReq, 0));
    const quint64 gen = m_searchGen;
    m_searchBusy = true;
    m_searchError.clear();
    emit searchStateChanged();
    m_searchReq = m_browser->search(m_query, [this, gen, reset](const RadioBrowser::Result &r) {
        if (gen != m_searchGen) return;   // überholt
        m_searchReq = 0;
        m_searchBusy = false;
        m_searchDone = true;
        if (!r.ok) {
            m_searchError = r.error.isEmpty() ? QStringLiteral("error") : r.error;
            m_hasMore = false;
            if (reset) { m_results.clear(); emit resultsChanged(); }
            emit searchStateChanged();
            return;
        }
        m_searchFromCache = r.fromCache;
        if (r.fromCache) m_searchError = r.error;   // offline: Hinweis, Ergebnisse aus dem Cache
        if (reset) m_results.clear();
        const QJsonArray arr = r.data.array();
        QSet<QString> seen;
        for (const auto &v : std::as_const(m_results)) seen.insert(v.toMap().value(QStringLiteral("id")).toString());
        for (const auto &v : arr) {
            const Station s = Station::fromApi(v.toObject());
            if (!s.isValid() || seen.contains(s.id)) continue;
            seen.insert(s.id);
            m_results << s.toVariant();
        }
        m_hasMore = arr.size() >= m_query.limit;
        emit resultsChanged();
        emit searchStateChanged();
        emit cacheChanged();
    });
}

void RadioController::loadFacets()
{
    if (m_facetsLoaded || m_facetsPending > 0) return;
    applyProxy();
    m_facetsError.clear();
    m_facetsPending = 3;
    emit facetsChanged();
    auto collect = [this](QVariantList *target, const char *codeKey) {
        return [this, target, codeKey](const RadioBrowser::Result &r) {
            --m_facetsPending;
            if (!r.ok) m_facetsError = r.error;
            else {
                target->clear();
                for (const auto &v : r.data.array()) {
                    const QJsonObject o = v.toObject();
                    const QString name = cleanText(o.value(QStringLiteral("name")).toString(), 80);
                    const int count = o.value(QStringLiteral("stationcount")).toInt();
                    QString code = codeKey ? o.value(QLatin1String(codeKey)).toString() : name;
                    if (name.isEmpty() || code.isEmpty() || count <= 0) continue;
                    *target << QVariantMap{{"name", name}, {"code", code}, {"count", count}};
                }
                if (target == &m_countries)   // Länder alphabetisch, Sprachen/Genres nach Anzahl
                    std::sort(m_countries.begin(), m_countries.end(), [](const QVariant &a, const QVariant &b) {
                        return a.toMap().value("name").toString().localeAwareCompare(b.toMap().value("name").toString()) < 0;
                    });
            }
            if (m_facetsPending == 0) m_facetsLoaded = m_facetsError.isEmpty();
            emit facetsChanged();
        };
    };
    m_browser->countries(collect(&m_countries, "iso_3166_1"));
    m_browser->languages(collect(&m_languages, nullptr));
    m_browser->tags(collect(&m_tags, nullptr));
}

// --------------------------------------------------------------------------- Favoriten und Verlauf
QVariantList RadioController::favorites() const
{
    QVariantList l;
    for (const auto &s : m_store.favorites()) l << s.toVariant();
    return l;
}

QVariantList RadioController::history() const
{
    QVariantList l;
    for (const auto &s : m_store.history()) l << s.toVariant();
    return l;
}

bool RadioController::isFavorite(const QString &id) const { return m_store.isFavorite(id); }

bool RadioController::toggleFavorite(const QVariantMap &station)
{
    // Rein lokal – funktioniert auch, wenn der Katalog offline ist
    const Station s = Station::fromVariant(station);
    if (!s.isValid()) return false;
    const bool now = m_store.toggleFavorite(s);
    saveLibrary();
    return now;
}

void RadioController::removeFavorite(const QString &id) { m_store.removeFavorite(id); saveLibrary(); }
void RadioController::removeHistory(const QString &id) { m_store.removeHistory(id); saveLibrary(); }
void RadioController::clearHistory() { m_store.clearHistory(); saveLibrary(); }

void RadioController::saveLibrary()
{
    m_storageError = m_store.save() ? QString() : m_store.lastError();
    ++m_libraryRevision;
    emit libraryChanged();
}

// --------------------------------------------------------------------------- Eigener Stream
QString RadioController::customUrlError(const QString &url) const
{
    return urlCheckCode(checkStreamUrl(url));
}

bool RadioController::playCustom(const QString &name, const QString &url, bool saveAsFavorite)
{
    if (!customUrlError(url).isEmpty()) return false;
    const Station s = Station::customStation(name, url);
    if (saveAsFavorite) { m_store.addFavorite(s); saveLibrary(); }
    play(s.toVariant(), QStringLiteral("custom"));
    return true;
}

// --------------------------------------------------------------------------- Wiedergabe
void RadioController::setState(const QString &state, const QString &errorCode, const QString &errorDetail)
{
    m_state = state;
    m_errorCode = errorCode;
    m_errorDetail = cleanText(errorDetail, 300);
    const bool live = state == QLatin1String("playing");
    if (live && !m_metaTimer.isActive()) m_metaTimer.start();
    if (!live) m_metaTimer.stop();
    if (state != QLatin1String("connecting")) m_connectTimeout.stop();
    if (state == QLatin1String("error") && !errorCode.isEmpty() && errorCode != QLatin1String("stream")) {
        // Fehler, die der Player nicht selbst sieht (Prüfung vor dem Laden), zusätzlich über die Player-Anzeige melden
        call(QStringLiteral("osd.show"), {{"text", QStringLiteral("%1: %2").arg(m_current.name, m_errorDetail.isEmpty() ? errorCode : m_errorDetail)},
                                          {"ms", 4000}});
    }
    emit playbackChanged();
}

void RadioController::resetTrack()
{
    m_track = {};
    m_liveCodec.clear();
    m_liveBitrate = 0;
    m_buffering = false;
    emit trackChanged();
}

void RadioController::play(const QVariantMap &stationMap, const QString &origin)
{
    Station s = Station::fromVariant(stationMap);
    // Gespeicherte Variante (mit zuletzt funktionierender Adresse) bevorzugen
    for (const auto &f : m_store.favorites()) if (f.id == s.id) { s = f; break; }
    if (!s.isValid()) return;
    const quint64 gen = ++m_playGen;
    m_probe->cancel();
    m_current = s;
    m_origin = origin;
    m_pendingUrl.clear();
    resetTrack();
    const QString url = s.playUrl();
    const UrlCheck check = checkStreamUrl(url);
    if (check != UrlCheck::Ok && !(check == UrlCheck::WebPage && !s.custom)) {
        // Katalogadressen mit Seitenendung (…/listen.aspx) sind selten, aber echt – nur eigene Eingaben streng prüfen
        setState(QStringLiteral("error"), urlCheckCode(check), url);
        return;
    }
    setState(QStringLiteral("checking"));
    applyProxy();
    if (m_sendClicks && origin == QLatin1String("search") && !s.custom) m_browser->click(s.id);
    m_probe->probe(url, [this, gen, url](const ProbeResult &r) {
        if (gen != m_playGen) return;   // inzwischen anderer Sender oder Stopp
        if (r.blocking()) { setState(QStringLiteral("error"), r.code(), r.detail); return; }
        sendToPlayer(url);
    });
}

void RadioController::sendToPlayer(const QString &url)
{
    m_pendingUrl = url;
    setState(QStringLiteral("connecting"));
    m_connectTimeout.start();
    // Steht der Sender schon in der Wiedergabeliste, diesen Eintrag spielen – sonst anhängen und spielen.
    // Andere Einträge bleiben erhalten.
    const QVariantList items = call(QStringLiteral("playlist.items")).toList();
    for (int i = 0; i < items.size(); ++i) {
        if (sameUrl(items.at(i).toMap().value(QStringLiteral("path")).toString(), url)) {
            call(QStringLiteral("playlist.play"), {{"index", i}});
            return;
        }
    }
    if (m_host && m_host->hasCommand(QStringLiteral("playlist.add")))
        call(QStringLiteral("playlist.add"), {{"urls", QStringList{url}}, {"titles", QVariantList{m_current.name}}, {"play", true}});
    else
        call(QStringLiteral("player.load"), {{"url", url}});
}

void RadioController::stop()
{
    ++m_playGen;
    m_probe->cancel();
    const bool ours = m_state == QLatin1String("playing") || m_state == QLatin1String("connecting");
    m_pendingUrl.clear();
    setState(QStringLiteral("stopped"));
    resetTrack();
    if (ours) call(QStringLiteral("player.stop"));
}

void RadioController::handleEvent(const QString &name, const QVariantMap &data)
{
    const bool active = m_state == QLatin1String("connecting") || m_state == QLatin1String("playing");
    if (name == QLatin1String("media.loading")) {
        // Der Nutzer hat etwas anderes gestartet (z. B. aus der Wiedergabeliste)
        if (active && !sameUrl(data.value(QStringLiteral("url")).toString(), m_pendingUrl)) {
            ++m_playGen;
            m_pendingUrl.clear();
            setState(QStringLiteral("idle"));
            resetTrack();
        }
    } else if (name == QLatin1String("media.loaded")) {
        onLoaded(data);
    } else if (name == QLatin1String("media.ended")) {
        const QString source = data.value(QStringLiteral("source")).toString();
        const QString path = data.value(QStringLiteral("path")).toString();
        if (!active || !(sameUrl(source, m_pendingUrl) || sameUrl(path, m_pendingUrl))) return;   // anderer/alter Titel
        const QString reason = data.value(QStringLiteral("reason")).toString();
        if (reason == QLatin1String("error")) {
            setState(QStringLiteral("error"), QStringLiteral("stream"), data.value(QStringLiteral("error")).toString());
            resetTrack();
        } else if (reason == QLatin1String("eof")) {
            setState(QStringLiteral("ended"));
            resetTrack();
        }
    } else if (name == QLatin1String("player.stopped")) {
        if (active || m_state == QLatin1String("checking")) {
            ++m_playGen;
            m_probe->cancel();
            m_pendingUrl.clear();
            setState(QStringLiteral("stopped"));
            resetTrack();
        }
    } else if (name == QLatin1String("playback.paused")) {
        m_paused = data.value(QStringLiteral("paused")).toBool();
        emit playbackChanged();
    } else if (name == QLatin1String("app.quit")) {
        shutdown();
    }
}

void RadioController::onLoaded(const QVariantMap &data)
{
    const QString source = data.value(QStringLiteral("source")).toString();
    const QString path = data.value(QStringLiteral("path")).toString();
    const bool mine = !m_pendingUrl.isEmpty() && (sameUrl(source, m_pendingUrl) || sameUrl(path, m_pendingUrl));
    const bool waiting = m_state == QLatin1String("connecting")
                      || (m_state == QLatin1String("error") && m_errorCode == QLatin1String("timeout"));
    if (!mine) {
        // Etwas anderes läuft jetzt im Player – unser Sender nicht mehr
        if (m_state == QLatin1String("playing") || m_state == QLatin1String("connecting")) {
            ++m_playGen;
            m_pendingUrl.clear();
            setState(QStringLiteral("idle"));
            resetTrack();
        }
        return;
    }
    if (!waiting) return;   // z. B. erneutes Laden desselben Senders
    // Erst jetzt gilt der Sender als laufend: in den Verlauf, Adresse als funktionierend merken
    m_paused = false;
    m_current.lastWorkingUrl = m_current.custom ? QString() : m_pendingUrl;
    m_store.markWorking(m_current, m_pendingUrl);
    m_store.addHistory(m_current);
    saveLibrary();
    setState(QStringLiteral("playing"));
    pollMetadata();
}

void RadioController::pollMetadata()
{
    if (m_state != QLatin1String("playing") || !m_host) return;
    // Nur auswerten, solange der Player wirklich unseren Sender hat
    const QVariantMap ps = call(QStringLiteral("player.state")).toMap();
    if (!sameUrl(ps.value(QStringLiteral("source")).toString(), m_pendingUrl)
        && !sameUrl(ps.value(QStringLiteral("path")).toString(), m_pendingUrl))
        return;
    const QVariantMap meta = call(QStringLiteral("mpv.get"), {{"name", QStringLiteral("metadata")}}).toMap();
    const TrackInfo t = trackFromMetadata(meta, m_current.name);
    const QString codec = cleanText(call(QStringLiteral("mpv.get"), {{"name", QStringLiteral("audio-codec-name")}}).toString(), 30);
    const int br = call(QStringLiteral("mpv.get"), {{"name", QStringLiteral("audio-bitrate")}}).toInt();
    const bool buf = call(QStringLiteral("mpv.get"), {{"name", QStringLiteral("paused-for-cache")}}).toBool();
    if (t == m_track && codec == m_liveCodec && br / 1000 == m_liveBitrate && buf == m_buffering) return;
    m_track = t;
    m_liveCodec = codec;
    m_liveBitrate = br / 1000;
    m_buffering = buf;
    emit trackChanged();
}

bool RadioController::canRefresh() const
{
    return m_state == QLatin1String("error") && m_current.isValid() && !m_current.custom && m_errorCode != QLatin1String("webpage");
}

void RadioController::refreshCurrent()
{
    if (!canRefresh() || m_refreshReq) return;
    applyProxy();
    const QString id = m_current.id;
    const QString origin = m_origin;
    const quint64 gen = m_playGen;
    m_refreshReq = m_browser->stationByUuid(id, [this, id, origin, gen](const RadioBrowser::Result &r) {
        m_refreshReq = 0;
        if (gen != m_playGen) { emit playbackChanged(); return; }
        const QJsonArray a = r.data.array();
        const Station fresh = (r.ok && !a.isEmpty()) ? Station::fromApi(a.first().toObject()) : Station{};
        if (!fresh.isValid() || fresh.id != id) {
            setState(QStringLiteral("error"), r.ok ? QStringLiteral("notInCatalog") : QStringLiteral("api"), r.error);
            return;
        }
        if (m_store.updateFromCatalog(fresh)) saveLibrary();
        Station s = fresh;
        play(s.toVariant(), origin == QLatin1String("search") ? QStringLiteral("refresh") : origin);
    });
    emit playbackChanged();
}

// --------------------------------------------------------------------------- Einstellungen, Caches, Logos
void RadioController::setSendClicks(bool on)
{
    if (on == m_sendClicks) return;
    m_sendClicks = on;
    saveSettings();
    emit settingsChanged();
}

void RadioController::saveSettings()
{
    if (!m_host) return;
    QJsonObject s = m_host->loadPluginSettings(kId);
    s.insert(QStringLiteral("sendClicks"), m_sendClicks);
    s.insert(QStringLiteral("lastSearch"), QJsonObject::fromVariantMap(m_lastSearch));
    m_host->savePluginSettings(kId, s);
}

QString RadioController::logoUrl(const QString &remote) { return m_logos->logoUrl(remote); }
qint64 RadioController::catalogCacheBytes() const { return m_catalogCache->sizeBytes(); }
qint64 RadioController::logoCacheBytes() const { return m_logos->sizeBytes(); }

void RadioController::clearCatalogCache()
{
    m_catalogCache->clear();
    m_facetsLoaded = false;
    emit cacheChanged();
}

void RadioController::clearLogoCache()
{
    m_logos->clear();
    ++m_logoRevision;
    emit logosChanged();
    emit cacheChanged();
}

} // namespace uve::radio
