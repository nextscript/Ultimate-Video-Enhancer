#include "TwitchBrowserController.h"
#include "ThumbnailCache.h"
#include "TokenVault.h"
#include "TwitchAuthService.h"
#include "TwitchChatController.h"
#include "TwitchHelix.h"

#include <algorithm>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSet>
#include <QTimer>
#include <QUrl>

static const char *kDefaultScopes = "user:read:follows";   // Minimum für „Folge ich“
static const int kMaxLivePages = 3;      // 3 × 100 Live-Streams automatisch
static const int kMaxOfflinePages = 2;   // 2 × 100 offline_followed
static const int kSearchDebounceMs = 450;

TwitchBrowserController::TwitchBrowserController(uve::IPlayerHost *host, const QString &pluginId,
                                                 TokenVault *vault, TwitchChatController *chat,
                                                 QObject *parent)
    : QObject(parent), m_host(host), m_id(pluginId), m_vault(vault), m_chat(chat)
{
    m_nam = new QNetworkAccessManager(this);
    m_auth = new TwitchAuthService(m_nam, this);
    m_helix = new TwitchHelix(m_nam, this);
    m_thumbs = new ThumbnailCache(m_nam, pluginDataDir() + QStringLiteral("/thumbs"), this);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(m_refreshSec * 1000);
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(kSearchDebounceMs);

    connect(m_refreshTimer, &QTimer::timeout, this, &TwitchBrowserController::onRefreshTick);
    connect(m_searchDebounce, &QTimer::timeout, this, &TwitchBrowserController::onSearchDebounced);

    connect(m_thumbs, &ThumbnailCache::ready, this, [this](const QString &expandedUrl) {
        const QString login = m_thumbLoginByUrl.value(expandedUrl);
        if (!login.isEmpty())
            emit thumbnailReady(login);
    });

    connect(m_auth, &TwitchAuthService::stateChanged, this, [this] {
        using S = TwitchAuthService::State;
        switch (m_auth->state()) {
        case S::AwaitingUser:
        case S::Polling:
            m_authState = QStringLiteral("loggingIn");
            emit authStateChanged();
            break;
        case S::SignedIn:
            m_authState = QStringLiteral("signedIn");
            emit authStateChanged();
            break;
        case S::Error:
            m_authState = QStringLiteral("error");
            emit authStateChanged();
            break;
        case S::Idle:
            if (m_authState != QLatin1String("idle")) {
                m_authState = QStringLiteral("idle");
                emit authStateChanged();
            }
            break;
        }
    });

    connect(m_auth, &TwitchAuthService::signedIn, this,
            [this](const QString &access, const QString &refresh, qint64 expiresMs,
                   const QStringList &scope) {
        m_accessToken = access;
        m_refreshTokenStr = refresh;
        m_expiresAtMs = expiresMs;
        m_grantedScope = scope.isEmpty() ? m_grantedScope : scope;
        if (m_sessionClientId.isEmpty())
            m_sessionClientId = m_clientId;   // Token gelten genau für diese Client-ID
        applyHelixCredentials();
        storeSession();
        if (!m_profileLogin.isEmpty())
            persistChatLoginIfAllowed();
        if (m_pendingFetch) {                      // stiller Refresh nach 401
            m_pendingFetch = false;
            m_unauthRetried = false;
            startTabFetch();
        } else if (!m_profileUserId.isEmpty() || m_tab == QLatin1String("search")) {
            startTabFetch();
        }
        setStatus(QStringLiteral("Bei Twitch angemeldet."));
        if (m_host) m_host->log(m_id, QStringLiteral("info"),
                                QStringLiteral("Twitch-Login erfolgreich (Scope: %1).")
                                    .arg(m_grantedScope.join(QLatin1Char(' '))));
    });

    connect(m_auth, &TwitchAuthService::authFailed, this, [this](const QString &reason,
                                                                 const QString &detail) {
        if (reason == QLatin1String("authorization_pending"))
            return;
        if (m_host)                            // im Log sichtbar – sonst war jeder Auth-Fehler unsichtbar
            m_host->log(m_id, QStringLiteral("warn"),
                        QStringLiteral("Twitch-Auth-Fehler: %1%2")
                            .arg(reason, detail.isEmpty()
                                     ? QString()
                                     : QStringLiteral(" – %1").arg(detail)));
        if (reason == QLatin1String("access_denied"))
            setStatus(QStringLiteral("Anmeldung auf Twitch wurde abgelehnt."));
        else if (reason == QLatin1String("expired_token") || reason == QLatin1String("device_expired"))
            setStatus(QStringLiteral("Der Geräte-Code ist abgelaufen – bitte erneut verbinden."));
        else if (reason == QLatin1String("expired") || reason == QLatin1String("invalid_grant")
                 || reason == QLatin1String("bad_request")) {
            clearSession();
            setStatus(QStringLiteral("Sitzung abgelaufen – bitte neu anmelden."));
        } else
            setStatus(QStringLiteral("Login-Fehler: %1").arg(detail.isEmpty() ? reason : detail));
    });

    connect(m_helix, &TwitchHelix::result, this,
            [this](TwitchHelix::Kind kind, int id, const QList<QVariantMap> &rows,
                   const QString &cursor) { handleResult(kind, id, rows, cursor); });
    connect(m_helix, &TwitchHelix::failed, this,
            [this](TwitchHelix::Kind kind, int id, const QString &reason, const QString &detail) {
                handleFailed(kind, id, reason, detail);
            });
    connect(m_helix, &TwitchHelix::rateLimited, this, [this](int secs) {
        setStatus(QStringLiteral("Zu viele Anfragen an Twitch – %1 s Pause.").arg(secs));
    });
    connect(m_helix, &TwitchHelix::unauthorized, this,
            [this] { tryRefreshAfterUnauthorized(); });

    if (m_chat)
        connect(m_chat, &TwitchChatController::channelChanged, this, [this] {
            const QString c = m_chat->channel();
            if (c != m_activeChannel) {
                m_activeChannel = c;
                emit channelsChanged();       // Akzent der aktiven Karte aktualisieren
            }
        });
}

TwitchBrowserController::~TwitchBrowserController() = default;

QString TwitchBrowserController::pluginDataDir() const
{
    QString d = m_host ? m_host->pluginDataDir(m_id) : QString();
    if (d.isEmpty())
        d = QDir::tempPath() + QStringLiteral("/twitchchat");
    return d;
}

// ------------------------------------------------------------------ Init --

void TwitchBrowserController::init()
{
    const QJsonObject s = m_host ? m_host->loadPluginSettings(m_id) : QJsonObject();
    m_clientId = s.value(QStringLiteral("clientId")).toString().trimmed();
    m_scopes = s.value(QStringLiteral("scopes")).toString().trimmed();
    if (m_scopes.isEmpty())
        m_scopes = QLatin1String(kDefaultScopes);
    m_refreshSec = qBound(30, s.value(QStringLiteral("refreshSec")).toInt(90), 600);
    m_tab = s.value(QStringLiteral("tab")).toString();
    if (m_tab != QLatin1String("search") && m_tab != QLatin1String("fav"))
        m_tab = QStringLiteral("follow");
    m_sortMode = s.value(QStringLiteral("sortMode")).toString();
    if (m_sortMode != QLatin1String("viewers") && m_sortMode != QLatin1String("alpha")
        && m_sortMode != QLatin1String("favorites"))
        m_sortMode = QStringLiteral("live");
    m_showOffline = s.value(QStringLiteral("showOffline")).toBool(false);
    m_browserOpen = s.value(QStringLiteral("browserOpen")).toBool(true);
    m_splitRatio = qBound(0.15, s.value(QStringLiteral("splitRatio")).toDouble(0.42), 0.85);
    m_refreshTimer->setInterval(m_refreshSec * 1000);
    for (const auto &v : s.value(QStringLiteral("favorites")).toArray())
        m_favorites << v.toString().toLower();

    loadSessionFromVault();
    rebuildView();
}

void TwitchBrowserController::loadSessionFromVault()
{
    if (!m_vault)
        return;
    const QJsonObject sess = m_vault->data().value(QStringLiteral("session")).toObject();
    m_accessToken = sess.value(QStringLiteral("accessToken")).toString();
    m_refreshTokenStr = sess.value(QStringLiteral("refreshToken")).toString();
    m_expiresAtMs = qint64(sess.value(QStringLiteral("expiresAt")).toDouble(0));
    m_profileLogin = sess.value(QStringLiteral("login")).toString().toLower();
    m_profileName = sess.value(QStringLiteral("displayName")).toString();
    m_profileUserId = sess.value(QStringLiteral("userId")).toString();
    m_profileAvatar = sess.value(QStringLiteral("avatar")).toString();
    m_sessionClientId = sess.value(QStringLiteral("clientId")).toString();
    for (const auto &v : sess.value(QStringLiteral("scope")).toArray())
        m_grantedScope << v.toString();

    if (m_accessToken.isEmpty()) {
        m_authState = QStringLiteral("idle");
        return;
    }
    applyHelixCredentials();
    m_authState = QStringLiteral("signedIn");
    m_auth->resume(sessionClientId(), m_accessToken, m_refreshTokenStr, m_expiresAtMs);
    persistChatLoginIfAllowed();
    setStatus(m_profileName.isEmpty() ? QStringLiteral("Bei Twitch angemeldet.")
                                      : QStringLiteral("Angemeldet als %1.").arg(m_profileName));
    emit authStateChanged();
}

bool TwitchBrowserController::signedIn() const
{
    return !m_accessToken.isEmpty();
}

QString TwitchBrowserController::userCode() const
{
    return m_auth ? m_auth->userCode() : QString();
}

QString TwitchBrowserController::verificationUri() const
{
    return m_auth ? m_auth->verificationUri() : QString();
}

void TwitchBrowserController::applyHelixCredentials()
{
    // Client-ID-Header muss zur ausgebenden Client-ID des Tokens passen –
    // sonst antwortet Twitch auf jede Anfrage mit 401 („Login läuft schnell ab“).
    m_helix->setCredentials(sessionClientId(), m_accessToken, m_expiresAtMs);
}

void TwitchBrowserController::storeSession()
{
    if (!m_vault)
        return;
    QJsonObject d = m_vault->data();
    QJsonObject sess;
    sess[QStringLiteral("accessToken")] = m_accessToken;
    sess[QStringLiteral("refreshToken")] = m_refreshTokenStr;
    sess[QStringLiteral("expiresAt")] = double(m_expiresAtMs);
    QJsonArray sc;
    for (const QString &s : m_grantedScope)
        sc.append(s);
    sess[QStringLiteral("scope")] = sc;
    sess[QStringLiteral("login")] = m_profileLogin;
    sess[QStringLiteral("displayName")] = m_profileName;
    sess[QStringLiteral("userId")] = m_profileUserId;
    sess[QStringLiteral("avatar")] = m_profileAvatar;
    sess[QStringLiteral("clientId")] = sessionClientId();
    sess[QStringLiteral("obtainedAt")] = double(QDateTime::currentMSecsSinceEpoch());
    d[QStringLiteral("session")] = sess;
    if (!m_vault->save(d) && m_host)
        m_host->log(m_id, QStringLiteral("warn"),
                    QStringLiteral("Sitzung konnte nicht verschlüsselt gespeichert werden "
                                   "(DPAPI nicht verfügbar) – nur für diese Sitzung wirksam."));
}

void TwitchBrowserController::clearSession()
{
    m_accessToken.clear();
    m_refreshTokenStr.clear();
    m_expiresAtMs = 0;
    m_profileUserId.clear();
    m_profileName.clear();
    m_profileLogin.clear();
    m_profileAvatar.clear();
    m_grantedScope.clear();
    m_sessionClientId.clear();
    applyHelixCredentials();
    if (m_vault) {
        QJsonObject d = m_vault->data();
        d.remove(QStringLiteral("session"));
        m_vault->save(d);
    }
    m_helix->cancelAll();
    m_outstanding.clear();
    emit busyChanged();
}

// Nur wenn der Twitch-Token auch Chat-Rechte enthält und der Nutzer noch keinen
// eigenen Chat-Token gesetzt hat: Chat-Login übernehmen (keine Pflichtmigration).
void TwitchBrowserController::persistChatLoginIfAllowed()
{
    if (!m_chat || m_profileLogin.isEmpty())
        return;
    if (!TwitchAuthService::scopeAllows(m_grantedScope, QStringLiteral("chat:read")))
        return;
    if (m_chat->authenticated())
        return;
    m_chat->adoptAuthFromBrowser(m_profileLogin, m_accessToken);
}

// ------------------------------------------------------------- Einstellungen --

void TwitchBrowserController::setClientId(const QString &v)
{
    const QString c = v.trimmed();
    if (c == m_clientId)
        return;
    m_clientId = c;
    persistSettings();
    applyHelixCredentials();
    emit settingsChanged();
}

void TwitchBrowserController::setScopes(const QString &v)
{
    const QString s = v.trimmed();
    if (s == m_scopes || s.isEmpty())
        return;
    m_scopes = s;
    persistSettings();
    emit settingsChanged();
}

void TwitchBrowserController::setRefreshSec(int sec)
{
    sec = qBound(30, sec, 600);
    if (sec == m_refreshSec)
        return;
    m_refreshSec = sec;
    m_refreshTimer->setInterval(sec * 1000);
    persistSettings();
    scheduleRefreshTimer();
    emit settingsChanged();
}

void TwitchBrowserController::setTab(const QString &t)
{
    QString tab = t;
    if (tab != QLatin1String("search") && tab != QLatin1String("fav"))
        tab = QStringLiteral("follow");
    if (tab == m_tab)
        return;
    m_tab = tab;
    m_searchCursor.clear();
    m_searchPage = 0;
    persistSettings();
    startTabFetch();
    scheduleRefreshTimer();
}

void TwitchBrowserController::setQuery(const QString &q)
{
    const QString trimmed = q.trimmed();
    if (trimmed == m_query)
        return;
    m_query = trimmed;
    m_searchPage = 0;
    m_searchDebounce->start();
}

void TwitchBrowserController::setShowOffline(bool on)
{
    if (on == m_showOffline)
        return;
    m_showOffline = on;
    m_followOfflineDone = false;
    persistSettings();
    if (m_tab == QLatin1String("follow") && signedIn() && m_viewActive && !m_showOffline)
        rebuildView();                     // offline Zeilen verschwinden sofort
    else
        startTabFetch();                   // offline Zeilen nachladen
}

void TwitchBrowserController::setSortMode(const QString &m)
{
    QString mode = m;
    if (mode != QLatin1String("viewers") && mode != QLatin1String("alpha")
        && mode != QLatin1String("favorites"))
        mode = QStringLiteral("live");
    if (mode == m_sortMode)
        return;
    m_sortMode = mode;
    persistSettings();
    rebuildView();
}

void TwitchBrowserController::setBrowserOpen(bool on)
{
    if (on == m_browserOpen)
        return;
    m_browserOpen = on;
    if (!on)
        setViewActive(false);        // QML setzt setViewActive(true), wenn es einklappt
    persistSettings();
    emit settingsChanged();
}

void TwitchBrowserController::setSplitRatio(qreal r)
{
    r = qBound(0.15, r, 0.85);
    if (qFuzzyCompare(r, m_splitRatio))
        return;
    m_splitRatio = r;
    persistSettings();
    emit settingsChanged();
}

void TwitchBrowserController::persistSettings()
{
    if (!m_host)
        return;
    QJsonObject s = m_host->loadPluginSettings(m_id);   // lesen-modifizieren-schreiben
    s[QStringLiteral("clientId")] = m_clientId;
    s[QStringLiteral("scopes")] = m_scopes;
    s[QStringLiteral("refreshSec")] = m_refreshSec;
    s[QStringLiteral("tab")] = m_tab;
    s[QStringLiteral("sortMode")] = m_sortMode;
    s[QStringLiteral("showOffline")] = m_showOffline;
    s[QStringLiteral("browserOpen")] = m_browserOpen;
    s[QStringLiteral("splitRatio")] = m_splitRatio;
    QJsonArray fav;
    for (const QString &f : m_favorites)
        fav.append(f);
    s[QStringLiteral("favorites")] = fav;
    m_host->savePluginSettings(m_id, s);
}

// --------------------------------------------------------------- Login --

void TwitchBrowserController::login()
{
    if (m_clientId.isEmpty()) {
        setStatus(QStringLiteral("Bitte zuerst eine Client-ID eintragen – "
                                "siehe README (dev.twitch.tv)."));
        return;
    }
    m_helix->cancelAll();
    m_outstanding.clear();
    emit busyChanged();
    m_sessionClientId = m_clientId;   // neue Anmeldung: Tokens gehören zu dieser Client-ID
    m_auth->begin(m_clientId, m_scopes);
    setStatus(QStringLiteral("Verbinde mit Twitch …"));
}

void TwitchBrowserController::cancelLogin()
{
    m_auth->cancel();
    setStatus(QStringLiteral("Anmeldung abgebrochen."));
}

void TwitchBrowserController::openVerification()
{
    const QString uri = m_auth->verificationUri();
    if (!uri.isEmpty())
        QDesktopServices::openUrl(QUrl(uri));
}

void TwitchBrowserController::copyUserCode()
{
    if (auto *cb = QGuiApplication::clipboard())
        cb->setText(m_auth->userCode());
}

void TwitchBrowserController::logout()
{
    if (signedIn())
        m_auth->revoke(sessionClientId(), m_accessToken);
    clearSession();
    m_authState = QStringLiteral("idle");
    emit authStateChanged();
    rebuildView();
    setStatus(QStringLiteral("Von Twitch abgemeldet."));
}

bool TwitchBrowserController::manualSignIn(const QString &accessToken)
{
    QString t = accessToken.trimmed();
    if (t.startsWith(QLatin1String("oauth:")))
        t.remove(0, 6);
    if (t.isEmpty() || m_clientId.isEmpty()) {
        setStatus(QStringLiteral("Client-ID und Token müssen ausgefüllt sein."));
        return false;
    }
    clearSession();
    m_sessionClientId = m_clientId;   // manuelles Token: gilt für die eingetragene Client-ID
    m_accessToken = t;
    m_expiresAtMs = QDateTime::currentMSecsSinceEpoch() + 4 * 3600LL * 1000LL;
    applyHelixCredentials();
    m_manualAttempt = true;
    m_pendingFetch = true;                 // startTabFetch nach Validierung
    m_selfReqId = track(m_helix->requestSelf(), TwitchHelix::Kind::Users);
    m_authState = QStringLiteral("loggingIn");
    emit authStateChanged();
    setStatus(QStringLiteral("Prüfe Token …"));
    return true;
}

// ---------------------------------------------------------- Daten laden --

void TwitchBrowserController::setViewActive(bool active)
{
    m_viewActive = active;
    if (!active) {
        m_helix->cancelAll();
        m_thumbs->cancelPending();
        m_outstanding.clear();
        emit busyChanged();
        m_refreshTimer->stop();
        return;
    }
    if (signedIn() && m_pool.isEmpty())
        startTabFetch();
    else if (signedIn() && m_tab == QLatin1String("search") && !m_query.isEmpty()
             && m_pool.isEmpty())
        startTabFetch();
    scheduleRefreshTimer();
}

void TwitchBrowserController::scheduleRefreshTimer()
{
    m_refreshTimer->stop();
    if (m_viewActive && signedIn() && m_tab == QLatin1String("follow") && m_browserOpen)
        m_refreshTimer->start();
}

void TwitchBrowserController::onRefreshTick()
{
    if (m_viewActive && signedIn() && m_tab == QLatin1String("follow"))
        fetchFollowPage(QString(), 1);
}

void TwitchBrowserController::onSearchDebounced()
{
    if (m_tab != QLatin1String("search")) {
        m_tab = QStringLiteral("search");
        persistSettings();
    }
    startTabFetch();
}

int TwitchBrowserController::track(int requestId, TwitchHelix::Kind kind)
{
    m_outstanding.insert(requestId, kind);
    emit busyChanged();
    return requestId;
}

void TwitchBrowserController::startTabFetch()
{
    if (!signedIn()) {
        rebuildView();
        return;
    }
    if (m_tab == QLatin1String("follow")) {
        if (m_profileUserId.isEmpty()) {
            m_selfReqId = track(m_helix->requestSelf(), TwitchHelix::Kind::Users);
            return;                       // danach: followed-Abfrage
        }
        fetchFollowPage(QString(), 1);
    } else if (m_tab == QLatin1String("search")) {
        if (m_query.isEmpty()) {
            rebuildView();
            setStatus(QStringLiteral("Suchbegriff eingeben (Kanalname oder Teil davon)."));
            return;
        }
        m_searchReqId = track(m_helix->requestSearchChannels(m_query, !m_showOffline, QString()),
                              TwitchHelix::Kind::SearchChannels);
        setStatus(QStringLiteral("Suche nach „%1“ …").arg(m_query));
    } else { // fav
        rebuildView();
        enrichFavoriteLogins();
    }
}

void TwitchBrowserController::refreshNow()
{
    m_followOfflineDone = false;
    startTabFetch();
}

void TwitchBrowserController::loadMore()
{
    if (!signedIn() || !m_viewActive)
        return;
    if (m_tab == QLatin1String("follow")) {
        if (!m_followCursor.isEmpty())
            fetchFollowPage(m_followCursor, m_followPage + 1);
        else if (m_showOffline && !m_followOfflineCursor.isEmpty())
            fetchFollowOfflinePage(m_followOfflineCursor, m_offlinePage + 1);
    } else if (m_tab == QLatin1String("search") && !m_searchCursor.isEmpty()) {
        m_searchReqId = track(m_helix->requestSearchChannels(m_query, !m_showOffline, m_searchCursor),
                              TwitchHelix::Kind::SearchChannels);
    }
}

void TwitchBrowserController::fetchFollowPage(const QString &cursor, int page)
{
    if (m_profileUserId.isEmpty()) {
        m_selfReqId = track(m_helix->requestSelf(), TwitchHelix::Kind::Users);
        return;
    }
    m_followPage = page;
    m_followReqId = track(m_helix->requestFollowedStreams(m_profileUserId, cursor),
                          TwitchHelix::Kind::FollowedStreams);
    setStatus(page == 1 ? QStringLiteral("Lade gefolgte Live-Kanäle …")
                        : QStringLiteral("Lade weitere Kanäle …"));
}

void TwitchBrowserController::fetchFollowOfflinePage(const QString &cursor, int page)
{
    m_offlinePage = page;
    m_offlineReqId = track(m_helix->requestFollowedChannels(m_profileUserId, cursor),
                           TwitchHelix::Kind::FollowedChannels);
    setStatus(QStringLiteral("Lade offline gefolgte Kanäle …"));
}

void TwitchBrowserController::enrichFavoriteLogins()
{
    QStringList missing;
    for (const QString &fav : m_favorites) {
        if (!m_pool.contains(fav))
            missing << fav;
    }
    if (missing.isEmpty())
        return;
    if (missing.size() > 100)
        missing = missing.mid(0, 100);
    track(m_helix->requestUsers(missing, /*asId=*/false), TwitchHelix::Kind::Users);
}

void TwitchBrowserController::fetchAvatarsFor(const QStringList &userIds)
{
    QStringList ids;
    for (const QString &id : userIds) {
        if (id.isEmpty())
            continue;
        ids << id;
        if (ids.size() >= 200)            // zwei Batches genügen
            break;
    }
    if (ids.isEmpty())
        return;
    // 100er-Helix-Batches
    for (int i = 0; i < ids.size(); i += 100) {
        QStringList batch = ids.mid(i, 100);
        track(m_helix->requestUsers(batch, /*asId=*/true), TwitchHelix::Kind::Users);
    }
}

// ---------------------------------------------------------- Ergebnisse --

void TwitchBrowserController::handleResult(TwitchHelix::Kind kind, int requestId,
                                           const QList<QVariantMap> &rows, const QString &cursor)
{
    if (!m_outstanding.contains(requestId))
        return;
    m_outstanding.remove(requestId);
    if (m_outstanding.isEmpty())
        emit busyChanged();
    m_unauthRetried = false;   // erfolgreiche Antwort: das eine 401-Wiederholungsrecht gilt wieder

    switch (kind) {
    case TwitchHelix::Kind::Users:
        if (requestId == m_selfReqId) {
            if (!rows.isEmpty()) {
                const QVariantMap me = rows.first();
                m_profileUserId = me.value(QStringLiteral("userId")).toString();
                m_profileLogin = me.value(QStringLiteral("login")).toString().toLower();
                m_profileName = me.value(QStringLiteral("name")).toString();
                m_profileAvatar = me.value(QStringLiteral("avatar")).toString();
                storeSession();
                persistChatLoginIfAllowed();
                emit authStateChanged();
                if (m_pendingFetch) {     // manuelles Token validiert
                    m_pendingFetch = false;
                    m_manualAttempt = false;
                    m_unauthRetried = false;
                    m_authState = QStringLiteral("signedIn");
                    emit authStateChanged();
                    setStatus(QStringLiteral("Token übernommen."));
                }
                if (m_viewActive)
                    startTabFetch();
            } else if (m_manualAttempt) {
                setStatus(QStringLiteral("Token ungültig oder abgelaufen."));
                m_manualAttempt = false;
                clearSession();
            }
        } else {
            // Profilbilder / Favoritenanreicherung
            bool touched = false;
            for (const QVariantMap &u : rows) {
                const QString login = u.value(QStringLiteral("login")).toString().toLower();
                auto it = m_pool.find(login);
                if (it != m_pool.end()) {
                    it->insert(QStringLiteral("avatar"), u.value(QStringLiteral("avatar")));
                    if (it->value(QStringLiteral("name")).toString().isEmpty())
                        it->insert(QStringLiteral("name"), u.value(QStringLiteral("name")));
                    touched = true;
                } else {
                    QVariantMap r = u;
                    r[QStringLiteral("live")] = false;
                    r[QStringLiteral("viewers")] = 0;
                    r[QStringLiteral("origin")] = QStringLiteral("fav");
                    m_pool.insert(login, r);
                    touched = true;
                }
            }
            if (touched)
                rebuildView();
        }
        break;

    case TwitchHelix::Kind::FollowedStreams: {
        const QSet<QString> liveLogins = liveLoginsOf(rows);
        // bisherige Follow-Live-Zeilen, die nicht mehr live sind → offline
        for (auto it = m_pool.begin(); it != m_pool.end(); ++it) {
            if (it.value().value(QStringLiteral("origin")).toString() == QLatin1String("follow")
                && it.value().value(QStringLiteral("live")).toBool()
                && !liveLogins.contains(it.key())) {
                it.value().insert(QStringLiteral("live"), false);
                it.value().insert(QStringLiteral("viewers"), 0);
            }
        }
        mergeRows(rows, QStringLiteral("follow"));
        m_followCursor = cursor;
        if (cursor.isEmpty() || m_followPage >= kMaxLivePages || !m_viewActive) {
            if (m_showOffline && !m_followOfflineDone && m_viewActive) {
                fetchFollowOfflinePage(QString(), 1);
            } else {
                m_followOfflineDone = true;
                finishFollowPhase();
            }
        } else {
            fetchFollowPage(cursor, m_followPage + 1);
        }
        break;
    }

    case TwitchHelix::Kind::FollowedChannels: {
        mergeRows(rows, QStringLiteral("follow"));
        m_followOfflineCursor = cursor;
        if (!cursor.isEmpty() && m_offlinePage < kMaxOfflinePages && m_viewActive) {
            fetchFollowOfflinePage(cursor, m_offlinePage + 1);
        } else {
            m_followOfflineDone = true;
            finishFollowPhase();
        }
        break;
    }

    case TwitchHelix::Kind::SearchChannels: {
        if (m_searchPage == 0) {          // neue Suche: alte Suchergebnisse raus
            for (auto it = m_pool.begin(); it != m_pool.end();) {
                if (it.value().value(QStringLiteral("origin")).toString() == QLatin1String("search"))
                    it = m_pool.erase(it);
                else
                    ++it;
            }
        }
        mergeRows(rows, QStringLiteral("search"));
        m_searchCursor = cursor;
        ++m_searchPage;
        rebuildView();
        setStatus(QStringLiteral("%1 Suchergebnisse.").arg(m_pool.size()));
        break;
    }

    case TwitchHelix::Kind::ChannelInfo:
        break;
    }
}

QSet<QString> TwitchBrowserController::liveLoginsOf(const QList<QVariantMap> &rows)
{
    QSet<QString> s;
    for (const QVariantMap &r : rows)
        s.insert(r.value(QStringLiteral("login")).toString().toLower());
    return s;
}

void TwitchBrowserController::mergeRows(const QList<QVariantMap> &rows, const QString &origin)
{
    for (QVariantMap r : rows) {
        const QString login = r.value(QStringLiteral("login")).toString().toLower();
        if (login.isEmpty())
            continue;
        r[QStringLiteral("origin")] = origin;
        auto it = m_pool.find(login);
        if (it != m_pool.end()) {
            QVariantMap &old = it.value();
            for (auto kit = r.constBegin(); kit != r.constEnd(); ++kit) {
                if (kit.key() == QLatin1String("avatar") && kit.value().toString().isEmpty())
                    continue;             // Avatar nicht mit leerem Wert überschreiben
                old.insert(kit.key(), kit.value());
            }
        } else {
            m_pool.insert(login, r);
        }
    }
}

void TwitchBrowserController::finishFollowPhase()
{
    QStringList needAvatars;
    for (auto it = m_pool.constBegin(); it != m_pool.constEnd(); ++it) {
        if (it.value().value(QStringLiteral("origin")).toString() == QLatin1String("follow")
            && it.value().value(QStringLiteral("avatar")).toString().isEmpty())
            needAvatars << it.value().value(QStringLiteral("userId")).toString();
    }
    if (!needAvatars.isEmpty()) {
        fetchAvatarsFor(needAvatars);
        rebuildView();                    // Karten ohne Avatare vorzeigen;
    } else {                              // kommen nach dem /users-Batch nach
        rebuildView();
    }
}

void TwitchBrowserController::handleFailed(TwitchHelix::Kind kind, int requestId,
                                           const QString &reason, const QString &detail)
{
    m_outstanding.remove(requestId);
    if (m_outstanding.isEmpty())
        emit busyChanged();

    if (reason == QLatin1String("unauthorized"))
        return;                            // unauthorized()-Signal kümmert sich
    if (reason == QLatin1String("rate_limited"))
        return;                            // rateLimited()-Signal setzt Status
    if (reason == QLatin1String("no_credentials") || reason == QLatin1String("empty"))
        return;

    if (reason.startsWith(QLatin1String("http_403"))) {
        setStatus(QStringLiteral("Dein Token darf „Folge ich“ nicht lesen. "
                                "Scope user:read:follows nötig (siehe README)."));
    } else if (reason.startsWith(QLatin1String("http_404"))) {
        setStatus(QStringLiteral("Kanal nicht gefunden."));
    } else if (reason.startsWith(QLatin1String("http_"))) {
        setStatus(QStringLiteral("Twitch-Antwort: %1").arg(detail.isEmpty() ? reason : detail));
    } else {
        setStatus(QStringLiteral("Netzwerkfehler: %1").arg(detail));
    }
    if (m_host)
        m_host->log(m_id, QStringLiteral("warn"),
                    QStringLiteral("Twitch-API-Fehler: %1%2")
                        .arg(reason, detail.isEmpty()
                                 ? QString()
                                 : QStringLiteral(" – %1").arg(detail)));
}

void TwitchBrowserController::tryRefreshAfterUnauthorized()
{
    if (m_unauthRetried) {
        if (m_host)
            m_host->log(m_id, QStringLiteral("warn"),
                        QStringLiteral("Twitch meldete erneut 401 nach Refresh – Sitzung beendet (Logout)."));
        clearSession();
        m_authState = QStringLiteral("error");
        emit authStateChanged();
        setStatus(QStringLiteral("Login abgelaufen – bitte neu anmelden."));
        return;
    }
    if (m_refreshTokenStr.isEmpty()) {
        if (m_host)
            m_host->log(m_id, QStringLiteral("warn"),
                        QStringLiteral("Twitch meldete 401, aber kein Refresh-Token vorhanden "
                                       "(manuell übernommenes Token) – Logout."));
        clearSession();
        m_authState = QStringLiteral("error");
        emit authStateChanged();
        setStatus(QStringLiteral("Zugangs-Token abgelaufen – bitte neu verbinden "
                                "(oder gültigen Token eintragen)."));
        return;
    }
    m_unauthRetried = true;
    m_pendingFetch = true;
    m_helix->cancelAll();
    m_outstanding.clear();
    emit busyChanged();
    setStatus(QStringLiteral("Erneuere Twitch-Token …"));
    if (m_host)
        m_host->log(m_id, QStringLiteral("warn"),
                    QStringLiteral("Twitch meldete 401 – versuche Token mit Refresh-Token zu erneuern."));
    // Wichtig: bewusst LEEREN Access-Token übergeben. Sonst sieht resume() den
    // (von Twitch bereits verworfenen) Token als „noch gültig“, kehrt ohne
    // Refresh-POST zurück und die nächste Anfrage liefert wieder 401 –
    // genau das war der „Login läuft schnell ab“-Loop.
    m_auth->resume(sessionClientId(), QString(), m_refreshTokenStr, 0);
}

// --------------------------------------------------------------- Ansicht --

void TwitchBrowserController::rebuildView()
{
    QList<QVariantMap> list;
    for (auto it = m_pool.constBegin(); it != m_pool.constEnd(); ++it) {
        QVariantMap row = it.value();
        const QString login = it.key();
        const QString origin = row.value(QStringLiteral("origin")).toString();
        const bool fav = m_favorites.contains(login);

        bool include = false;
        if (m_tab == QLatin1String("follow"))
            include = origin == QLatin1String("follow");
        else if (m_tab == QLatin1String("search"))
            include = origin == QLatin1String("search");
        else
            include = fav;
        if (include && !row.value(QStringLiteral("live")).toBool() && !m_showOffline)
            include = false;
        if (!include)
            continue;

        row.insert(QStringLiteral("login"), login);
        row.insert(QStringLiteral("isFav"), fav);
        const QString cover = thumbnailFor(login);   // löst Download aus, wenn nötig
        row.insert(QStringLiteral("thumb"), cover);
        list.append(row);
    }

    const QString mode = m_sortMode;
    std::sort(list.begin(), list.end(), [&](const QVariantMap &a, const QVariantMap &b) {
        const bool liveA = a.value(QStringLiteral("live")).toBool();
        const bool liveB = b.value(QStringLiteral("live")).toBool();
        if (liveA != liveB)
            return liveA;                 // Live zuerst (MD-Vorgabe)
        const bool favA = a.value(QStringLiteral("isFav")).toBool();
        const bool favB = b.value(QStringLiteral("isFav")).toBool();
        if (mode == QLatin1String("favorites") && favA != favB)
            return favA;
        if (mode == QLatin1String("viewers")) {
            const int va = a.value(QStringLiteral("viewers")).toInt();
            const int vb = b.value(QStringLiteral("viewers")).toInt();
            if (va != vb)
                return va > vb;
        }
        if (mode == QLatin1String("alpha") || mode == QLatin1String("favorites")) {
            const QString na = a.value(QStringLiteral("name")).toString().toLower();
            const QString nb = b.value(QStringLiteral("name")).toString().toLower();
            if (na != nb)
                return na < nb;
        }
        if (mode == QLatin1String("live")) {       // Twitch-Reihenfolge als Stichwort erhalten
            const int va = a.value(QStringLiteral("viewers")).toInt();
            const int vb = b.value(QStringLiteral("viewers")).toInt();
            if (va != vb)
                return va > vb;
        }
        return a.value(QStringLiteral("login")).toString()
             < b.value(QStringLiteral("login")).toString();
    });

    m_view = QVariantList();
    for (const QVariantMap &row : list)
        m_view.append(row);
    emit channelsChanged();

    if (m_tab == QLatin1String("follow") && !list.isEmpty())
        setStatus(QStringLiteral("%1 Kanäle (%2 live).")
                      .arg(list.size())
                      .arg(std::count_if(list.begin(), list.end(), [](const QVariantMap &r) {
                             return r.value(QStringLiteral("live")).toBool();
                         })));
}

QString TwitchBrowserController::thumbnailFor(const QString &login) const
{
    auto it = m_pool.constFind(login);
    if (it == m_pool.constEnd())
        return {};
    QString raw = it.value().value(QStringLiteral("thumbRaw")).toString();
    if (raw.isEmpty())
        raw = it.value().value(QStringLiteral("avatar")).toString();
    if (raw.isEmpty())
        return {};
    const QString expanded = TwitchHelix::expandThumbnail(raw, 320, 180);
    m_thumbLoginByUrl[expanded] = login;
    return m_thumbs->request(expanded);
}

// --------------------------------------------------------------- Aktionen --

bool TwitchBrowserController::hasPlayerLoad() const
{
    return m_host && m_host->hasCommand(QStringLiteral("player.load"));
}

bool TwitchBrowserController::playChannel(const QString &login)
{
    const QString name = login.trimmed().toLower();
    if (name.isEmpty())
        return false;
    if (!hasPlayerLoad()) {
        setStatus(QStringLiteral("Dein Player unterstützt player.load nicht – "
                                "bitte UVE Player 1.0.6+ verwenden."));
        return false;
    }
    const QVariantMap args{{QStringLiteral("url"),
                            QStringLiteral("https://www.twitch.tv/%1").arg(name)}};
    QString err;
    const QVariant res = m_host->call(m_id, QStringLiteral("player.load"), args, &err);
    if (!res.toBool()) {
        setStatus(QStringLiteral("Konnte %1 nicht abspielen: %2").arg(name, err));
        if (m_host->hasCommand(QStringLiteral("osd.show"))) {
            m_host->call(m_id, QStringLiteral("osd.show"),
                         QVariantMap{{QStringLiteral("text"),
                                      QStringLiteral("Twitch-Stream nicht verfügbar")}},
                         nullptr);
        }
        return false;
    }
    if (m_activeChannel != name) {
        m_activeChannel = name;
        emit channelsChanged();
    }
    setStatus(QStringLiteral("%1 wird im Player abgespielt.").arg(name));
    return true;
}

void TwitchBrowserController::openChannelExternal(const QString &login)
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.twitch.tv/%1").arg(login.trimmed())));
}

void TwitchBrowserController::toggleFavorite(const QString &login)
{
    const QString name = login.trimmed().toLower();
    if (name.isEmpty())
        return;
    if (m_favorites.removeOne(name)) {
        // ok
    } else {
        m_favorites << name;
    }
    persistSettings();
    rebuildView();
}

bool TwitchBrowserController::isFavorite(const QString &login) const
{
    return m_favorites.contains(login.trimmed().toLower());
}

void TwitchBrowserController::setStatus(const QString &text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusChanged();
}
