#pragma once
// Twitch-Kanal-Browser für das Plugin:
//  – OAuth-Login (Device Code Grant) mit sicherer Vault-Sitzung
//  – „Folge ich“ (Live-Streams + optional offline), „Suchen“, „Favoriten“
//  – Kartenmodell (Name, Titel, Kategorie, Zuschauer, Live-Status, Thumbnail)
//  – Wiedergabe im vorhandenen UVE-Videobereich über host "player.load"
//    (der Player löst twitch.tv-URLs selbst auf – Qualitätsmenü/Proxy bleiben erhalten)
//  – Aktualisierung nur, solange der Panel sichtbar ist; Favoriten & Sortierung lokal

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include "PluginApi.h"
#include "TwitchHelix.h"   // Kind-Enum wird in Methodensignaturen gebraucht

class TokenVault;
class TwitchChatController;
class TwitchAuthService;
class ThumbnailCache;
class QNetworkAccessManager;
class QTimer;

class TwitchBrowserController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString authState    READ authState    NOTIFY authStateChanged) // idle|loggingIn|signedIn|error
    Q_PROPERTY(bool signedIn        READ signedIn     NOTIFY authStateChanged)
    Q_PROPERTY(QString userCode     READ userCode     NOTIFY authStateChanged)
    Q_PROPERTY(QString verificationUri READ verificationUri NOTIFY authStateChanged)
    Q_PROPERTY(QString profileName  READ profileName  NOTIFY authStateChanged)
    Q_PROPERTY(QString profileLogin READ profileLogin NOTIFY authStateChanged)
    Q_PROPERTY(QString profileAvatar READ profileAvatar NOTIFY authStateChanged)
    Q_PROPERTY(QString clientId     READ clientId     WRITE setClientId NOTIFY settingsChanged)
    Q_PROPERTY(QString scopes       READ scopes       WRITE setScopes   NOTIFY settingsChanged)
    Q_PROPERTY(int refreshSec       READ refreshSec   WRITE setRefreshSec NOTIFY settingsChanged)

    Q_PROPERTY(QString tab          READ tab          WRITE setTab      NOTIFY channelsChanged)   // follow|search|fav
    Q_PROPERTY(QString query        READ query        WRITE setQuery    NOTIFY channelsChanged)
    Q_PROPERTY(bool showOffline     READ showOffline  WRITE setShowOffline NOTIFY channelsChanged)
    Q_PROPERTY(QString sortMode     READ sortMode     WRITE setSortMode NOTIFY channelsChanged)   // live|viewers|alpha|favorites
    Q_PROPERTY(bool browserOpen     READ browserOpen  WRITE setBrowserOpen NOTIFY settingsChanged)
    Q_PROPERTY(qreal splitRatio     READ splitRatio   WRITE setSplitRatio NOTIFY settingsChanged)

    Q_PROPERTY(QVariantList channels READ channels    NOTIFY channelsChanged)
    Q_PROPERTY(QStringList favorites READ favorites   NOTIFY channelsChanged)
    Q_PROPERTY(QString activeChannel READ activeChannel NOTIFY channelsChanged)
    Q_PROPERTY(bool busy            READ busy         NOTIFY busyChanged)
    Q_PROPERTY(QString statusText   READ statusText   NOTIFY statusChanged)
    Q_PROPERTY(bool hasPlayerLoad   READ hasPlayerLoad CONSTANT)

public:
    TwitchBrowserController(uve::IPlayerHost *host, const QString &pluginId,
                            TokenVault *vault, TwitchChatController *chat,
                            QObject *parent = nullptr);
    ~TwitchBrowserController() override;

    void init();                       // Einstellungen + gespeicherte Sitzung laden, ggf. still refreschen
    Q_INVOKABLE void setViewActive(bool active);  // Panel sichtbar → Refresh-Timer & Downloads steuern

    QString authState() const { return m_authState; }
    bool signedIn() const;
    QString userCode() const;
    QString verificationUri() const;
    QString profileName() const { return m_profileName; }
    QString profileLogin() const { return m_profileLogin; }
    QString clientId() const { return m_clientId; }
    void setClientId(const QString &v);
    // Die Twitch-Token gehören zur der Client-ID, mit der sie geholt wurden –
    // bei Helix-Anfragen muss genau die im Client-ID-Header stehen.
    QString sessionClientId() const { return m_sessionClientId.isEmpty() ? m_clientId : m_sessionClientId; }
    QString scopes() const { return m_scopes; }
    void setScopes(const QString &v);
    int refreshSec() const { return m_refreshSec; }
    void setRefreshSec(int sec);

    QString tab() const { return m_tab; }
    void setTab(const QString &t);
    QString query() const { return m_query; }
    void setQuery(const QString &q);
    bool showOffline() const { return m_showOffline; }
    void setShowOffline(bool on);
    QString sortMode() const { return m_sortMode; }
    void setSortMode(const QString &m);
    bool browserOpen() const { return m_browserOpen; }
    void setBrowserOpen(bool on);
    qreal splitRatio() const { return m_splitRatio; }
    void setSplitRatio(qreal r);

    QVariantList channels() const { return m_view; }
    QStringList favorites() const { return m_favorites; }
    QString activeChannel() const { return m_activeChannel; }
    bool busy() const { return !m_outstanding.isEmpty(); }
    QString statusText() const { return m_statusText; }
    bool hasPlayerLoad() const;

    Q_INVOKABLE void login();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void openVerification();     // offizielle Twitch-Seite im Standardbrowser
    Q_INVOKABLE void copyUserCode();
    Q_INVOKABLE void logout();
    Q_INVOKABLE bool manualSignIn(const QString &accessToken);   // Token von einer externen Seite

    Q_INVOKABLE void refreshNow();           // current tab neu laden
    Q_INVOKABLE void loadMore();             // nächste Cursor-Seite

    Q_INVOKABLE bool playChannel(const QString &login);   // player.load – vorhandener Resolver
    Q_INVOKABLE void openChannelExternal(const QString &login);
    Q_INVOKABLE void toggleFavorite(const QString &login);
    Q_INVOKABLE bool isFavorite(const QString &login) const;
    Q_INVOKABLE QString thumbnailFor(const QString &login) const;  // löst bei Bedarf Download aus

signals:
    void authStateChanged();
    void channelsChanged();
    void settingsChanged();
    void statusChanged();
    void busyChanged();
    void thumbnailReady(const QString &login);

private slots:
    void onSearchDebounced();
    void onRefreshTick();

private:
    // ---- Sitzungsverwaltung -------------------------------------------------
    void loadSessionFromVault();
    void storeSession();               // schreibt m_accessToken/… in den Vault
    void clearSession();
    void applyHelixCredentials();
    void setStatus(const QString &text);

    // ---- Datenbeschaffung ---------------------------------------------------
    void startTabFetch();                    // Tab-Inhalt neu (Seite 1)
    void fetchFollowPage(const QString &cursor, int page);
    void fetchFollowOfflinePage(const QString &cursor, int page);
    void fetchAvatarsFor(const QStringList &userIds);
    void enrichFavoriteLogins();
    void handleResult(TwitchHelix::Kind kind, int requestId,
                      const QList<QVariantMap> &rows, const QString &cursor);
    void handleFailed(TwitchHelix::Kind kind, int requestId,
                      const QString &reason, const QString &detail);
    void mergeRows(const QList<QVariantMap> &rows, const QString &origin);
    void finishFollowPhase();          // Live+offline fertig → Avatare anreichern
    static QSet<QString> liveLoginsOf(const QList<QVariantMap> &rows);
    int track(int requestId, TwitchHelix::Kind kind);   // als "beschäftigt" registrieren
    void persistChatLoginIfAllowed();
    void rebuildView();
    void scheduleRefreshTimer();
    void tryRefreshAfterUnauthorized();

    // ---- Persistenz -----------------------------------------------------------
    void persistSettings();
    QString pluginDataDir() const;   // Host-Abfrage mit Fallback (Temp-Ordner)

    uve::IPlayerHost *m_host = nullptr;
    QString m_id;
    TokenVault *m_vault = nullptr;
    TwitchChatController *m_chat = nullptr;

    QNetworkAccessManager *m_nam = nullptr;
    TwitchAuthService *m_auth = nullptr;
    TwitchHelix *m_helix = nullptr;
    ThumbnailCache *m_thumbs = nullptr;
    QTimer *m_refreshTimer = nullptr;      // singleShot-Kette
    QTimer *m_searchDebounce = nullptr;

    // Konto – Tokens nur im Speicher bzw. verschlüsselt im TokenVault
    QString m_clientId, m_scopes;
    QString m_sessionClientId;         // Client-ID, zu der die Tokens gehören (Vault-Sitzung)
    int m_refreshSec = 90;
    QString m_accessToken, m_refreshTokenStr;
    qint64 m_expiresAtMs = 0;
    QString m_profileName, m_profileLogin, m_profileUserId, m_profileAvatar;
    QStringList m_grantedScope;
    bool m_manualAttempt = false;      // manuell eingegebenes Token wird gerade geprüft

public:
    QString profileAvatar() const { return m_profileAvatar; }

    // Ansicht
    QString m_tab = QStringLiteral("follow"), m_query, m_sortMode = QStringLiteral("live");
    bool m_showOffline = false, m_browserOpen = true;
    qreal m_splitRatio = 0.42;
    QStringList m_favorites;
    QString m_activeChannel;

    QMap<QString, QVariantMap> m_pool;     // login → Zeile (kumuliert)
    QVariantList m_view;                   // gefiltert/sortiert fürs QML

    // Pagination-Zustand pro Tab
    QString m_followCursor, m_followOfflineCursor, m_searchCursor;
    int m_followPage = 0, m_offlinePage = 0, m_searchPage = 0;
    bool m_followOfflineDone = false;
    int m_selfReqId = 0;
    bool m_pendingFetch = false;           // Fetch nach erfolgreichem Refresh erlauben
    bool m_unauthRetried = false;          // genau ein Retry nach Token-Erneuerung
    bool m_viewActive = false;

    QHash<int, TwitchHelix::Kind> m_outstanding;
    int m_followReqId = 0, m_offlineReqId = 0, m_searchReqId = 0;
    mutable QMap<QString, QString> m_thumbLoginByUrl;   // expandierte Thumbnail-URL → login

    QString m_authState = QStringLiteral("idle");   // idle|loggingIn|signedIn|error
    QString m_statusText;
};
