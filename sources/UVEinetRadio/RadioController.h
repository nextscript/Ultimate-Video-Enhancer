#pragma once
// UVEinetRadio – Logik hinter dem Panel: Katalogsuche, Favoriten, Verlauf, eigene Streams und der Wiedergabestatus.
// Die Wiedergabe selbst macht der Player (libmpv) – hier wird nur über Befehle/Ereignisse gesteuert und beobachtet.
// Texte für die Oberfläche kommen als Codes; übersetzt wird im QML (qsTr, lang/*.qm).
#include "PluginApi.h"
#include "RadioBrowser.h"
#include "RadioTypes.h"
#include "StationStore.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <memory>

class QNetworkAccessManager;

namespace uve::radio {

class CatalogCache;
class LogoCache;
class StreamProbe;

class RadioController : public QObject
{
    Q_OBJECT
    // Suche
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool searchBusy READ searchBusy NOTIFY searchStateChanged)
    Q_PROPERTY(bool searchDone READ searchDone NOTIFY searchStateChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY searchStateChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY searchStateChanged)
    Q_PROPERTY(bool searchFromCache READ searchFromCache NOTIFY searchStateChanged)
    Q_PROPERTY(QVariantList countries READ countries NOTIFY facetsChanged)
    Q_PROPERTY(QVariantList languages READ languages NOTIFY facetsChanged)
    Q_PROPERTY(QVariantList tags READ tags NOTIFY facetsChanged)
    Q_PROPERTY(bool facetsBusy READ facetsBusy NOTIFY facetsChanged)
    Q_PROPERTY(QString facetsError READ facetsError NOTIFY facetsChanged)
    Q_PROPERTY(QString mirror READ mirror NOTIFY mirrorChanged)
    // Gespeichertes
    Q_PROPERTY(QVariantList favorites READ favorites NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY libraryChanged)
    Q_PROPERTY(int libraryRevision READ libraryRevision NOTIFY libraryChanged)
    Q_PROPERTY(QString storageError READ storageError NOTIFY libraryChanged)
    // Jetzt läuft
    Q_PROPERTY(QString state READ state NOTIFY playbackChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY playbackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString liveCodec READ liveCodec NOTIFY trackChanged)
    Q_PROPERTY(int liveBitrate READ liveBitrate NOTIFY trackChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY trackChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY playbackChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY playbackChanged)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY playbackChanged)
    Q_PROPERTY(bool canRefresh READ canRefresh NOTIFY playbackChanged)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY playbackChanged)
    // Einstellungen und Caches
    Q_PROPERTY(bool sendClicks READ sendClicks WRITE setSendClicks NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap lastSearch READ lastSearch NOTIFY settingsChanged)
    Q_PROPERTY(qint64 catalogCacheBytes READ catalogCacheBytes NOTIFY cacheChanged)
    Q_PROPERTY(qint64 logoCacheBytes READ logoCacheBytes NOTIFY cacheChanged)
    Q_PROPERTY(int logoRevision READ logoRevision NOTIFY logosChanged)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    RadioController(uve::IPlayerHost *host, const QString &dataDir, QObject *parent = nullptr);
    ~RadioController() override;

    // Für Tests: feste API-Spiegel, kurze Zeiten
    RadioBrowser *browser() const { return m_browser.get(); }
    void setSearchDebounceMs(int ms) { m_debounce.setInterval(ms); }
    void setProbeTimeoutMs(int ms);
    void setConnectTimeoutMs(int ms) { m_connectTimeout.setInterval(ms); }
    void setMetadataIntervalMs(int ms) { m_metaTimer.setInterval(ms); }
    QNetworkAccessManager *network() const { return m_nam; }

    QVariantList results() const { return m_results; }
    bool searchBusy() const { return m_searchBusy; }
    bool searchDone() const { return m_searchDone; }
    bool hasMore() const { return m_hasMore; }
    QString searchError() const { return m_searchError; }
    bool searchFromCache() const { return m_searchFromCache; }
    QVariantList countries() const { return m_countries; }
    QVariantList languages() const { return m_languages; }
    QVariantList tags() const { return m_tags; }
    bool facetsBusy() const { return m_facetsPending > 0; }
    QString facetsError() const { return m_facetsError; }
    QString mirror() const;

    QVariantList favorites() const;
    QVariantList history() const;
    int libraryRevision() const { return m_libraryRevision; }
    QString storageError() const { return m_storageError; }

    QString state() const { return m_state; }
    QVariantMap current() const { return m_current.isValid() ? m_current.toVariant() : QVariantMap{}; }
    QString artist() const { return m_track.artist; }
    QString title() const { return m_track.title; }
    QString liveCodec() const { return m_liveCodec; }
    int liveBitrate() const { return m_liveBitrate; }
    bool buffering() const { return m_buffering; }
    bool paused() const { return m_paused; }
    QString errorCode() const { return m_errorCode; }
    QString errorDetail() const { return m_errorDetail; }
    bool canRefresh() const;
    bool refreshing() const { return m_refreshReq != 0; }

    bool sendClicks() const { return m_sendClicks; }
    void setSendClicks(bool on);
    QVariantMap lastSearch() const { return m_lastSearch; }
    qint64 catalogCacheBytes() const;
    qint64 logoCacheBytes() const;
    int logoRevision() const { return m_logoRevision; }
    QString version() const { return QLatin1String(kPluginVersion); }

    // ---- aus dem QML
    Q_INVOKABLE void search(const QString &name, const QString &countryCode, const QString &language, const QString &tag,
                            const QString &order);
    Q_INVOKABLE void retrySearch();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void loadFacets();
    // origin: "search", "favorite", "history", "custom" – nur "search" kann den Klick melden (falls aktiviert)
    Q_INVOKABLE void play(const QVariantMap &station, const QString &origin);
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool isFavorite(const QString &id) const;
    Q_INVOKABLE bool toggleFavorite(const QVariantMap &station);
    Q_INVOKABLE void removeFavorite(const QString &id);
    Q_INVOKABLE void removeHistory(const QString &id);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE QString customUrlError(const QString &url) const;
    Q_INVOKABLE bool playCustom(const QString &name, const QString &url, bool saveAsFavorite);
    Q_INVOKABLE void refreshCurrent();
    Q_INVOKABLE QString logoUrl(const QString &remote);
    Q_INVOKABLE void clearCatalogCache();
    Q_INVOKABLE void clearLogoCache();

    // ---- vom Plugin
    void handleEvent(const QString &name, const QVariantMap &data);
    void shutdown();

signals:
    void resultsChanged();
    void searchStateChanged();
    void facetsChanged();
    void mirrorChanged();
    void libraryChanged();
    void playbackChanged();
    void trackChanged();
    void settingsChanged();
    void cacheChanged();
    void logosChanged();

private:
    void runSearch(bool reset);
    void applyProxy();
    void setState(const QString &state, const QString &errorCode = {}, const QString &errorDetail = {});
    void sendToPlayer(const QString &url);
    void onLoaded(const QVariantMap &data);
    void pollMetadata();
    void resetTrack();
    void saveLibrary();
    void saveSettings();
    QVariant call(const QString &cmd, const QVariantMap &args = {}) const;

    uve::IPlayerHost *m_host;
    QString m_dataDir;
    QNetworkAccessManager *m_nam;
    std::unique_ptr<CatalogCache> m_catalogCache;
    std::unique_ptr<LogoCache> m_logos;
    std::unique_ptr<RadioBrowser> m_browser;
    std::unique_ptr<StreamProbe> m_probe;
    StationStore m_store;

    // Suche
    SearchQuery m_query;
    quint64 m_searchGen = 0;
    quint64 m_searchReq = 0;
    QTimer m_debounce;
    QVariantList m_results;
    bool m_searchBusy = false;
    bool m_searchDone = false;
    bool m_hasMore = false;
    bool m_searchFromCache = false;
    QString m_searchError;
    QVariantList m_countries, m_languages, m_tags;
    int m_facetsPending = 0;
    bool m_facetsLoaded = false;
    QString m_facetsError;

    int m_libraryRevision = 0;
    QString m_storageError;

    // Wiedergabe
    quint64 m_playGen = 0;        // steigt mit jedem Sender-/Stopp-Wunsch; ältere Ergebnisse verfallen
    Station m_current;
    QString m_origin;
    QString m_pendingUrl;         // an den Player übergebene Adresse
    QString m_state = QStringLiteral("idle");   // idle, checking, connecting, playing, error, stopped, ended
    QString m_errorCode, m_errorDetail;
    TrackInfo m_track;
    QString m_liveCodec;
    int m_liveBitrate = 0;
    bool m_buffering = false;
    bool m_paused = false;
    QTimer m_connectTimeout;
    QTimer m_metaTimer;
    quint64 m_refreshReq = 0;

    bool m_sendClicks = false;
    QVariantMap m_lastSearch;
    int m_logoRevision = 0;
    QTimer m_logoBatch;
    QString m_appliedProxy;
};

} // namespace uve::radio
