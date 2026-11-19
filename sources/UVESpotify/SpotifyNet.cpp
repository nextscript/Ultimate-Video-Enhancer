#include "SpotifyNet.h"

#include <QDesktopServices>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

namespace uve::spotify {

namespace {

constexpr auto kChromeUa = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36";
constexpr int kTimeoutMs = 30000;
constexpr int kMaxTracks = 1000;
constexpr auto kScopes = "playlist-read-private playlist-read-collaborative user-library-read user-read-private";

QByteArray formBody(const QList<std::pair<QString, QString>> &form)
{
    QUrlQuery q;
    for (const auto &[k, v] : form) q.addQueryItem(k, QString::fromLatin1(QUrl::toPercentEncoding(v)));
    return q.query(QUrl::FullyEncoded).toUtf8();
}

QByteArray callbackPage(bool ok, const QString &text)
{
    const QByteArray body = QStringLiteral("<!doctype html><meta charset=utf-8><title>UVESpotify</title>"
                                           "<body style=\"font-family:sans-serif;background:#121212;color:#eee;text-align:center;padding-top:15vh\">"
                                           "<h2 style=\"color:%1\">UVESpotify</h2><p>%2</p></body>")
                                .arg(ok ? QStringLiteral("#1db954") : QStringLiteral("#e22134"), text.toHtmlEscaped())
                                .toUtf8();
    return "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
           + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

} // namespace

// ---------------------------------------------------------------- Konto
SpotifyAccount::SpotifyAccount(QObject *parent) : QObject(parent), m_nam(new QNetworkAccessManager(this)), m_loginTimeout(new QTimer(this))
{
    m_loginTimeout->setSingleShot(true);
    m_loginTimeout->setInterval(5 * 60 * 1000);
    connect(m_loginTimeout, &QTimer::timeout, this, [this] {
        cancelLogin();
        emit message(QStringLiteral("login timed out"), true);
    });
}

void SpotifyAccount::setClientId(const QString &id)
{
    const QString trimmed = id.trimmed();
    if (trimmed == m_clientId) return;
    m_clientId = trimmed;
    emit changed();
}

void SpotifyAccount::setPort(int port)
{
    if (port < 1024 || port > 65535 || port == m_port) return;
    m_port = port;
    emit changed();
}

QString SpotifyAccount::redirectUri() const
{
    return QStringLiteral("http://127.0.0.1:%1/callback").arg(m_port);
}

void SpotifyAccount::setRefreshToken(const QString &token)
{
    if (token == m_refresh) return;
    m_refresh = token;
    if (token.isEmpty()) { m_access.clear(); m_user.clear(); m_product.clear(); }
    emit refreshTokenChanged();
    emit changed();
}

void SpotifyAccount::login(QString *error)
{
    if (m_clientId.isEmpty()) { if (error) *error = QStringLiteral("no client id"); return; }
    cancelLogin();
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, quint16(m_port))) {
        if (error) *error = m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (m_server && m_server->hasPendingConnections()) {
            QTcpSocket *socket = m_server->nextPendingConnection();
            socket->setParent(this);   // überlebt das Schließen des Servers, bis die Antwortseite gesendet ist
            auto *buffer = new QByteArray;
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QObject::destroyed, this, [buffer] { delete buffer; });
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                buffer->append(socket->readAll());
                if (buffer->size() > 16384) { socket->abort(); return; }
                if (buffer->contains("\r\n\r\n")) onCallback(socket, *buffer);
            });
        }
    });
    m_verifier = randomToken(96);
    m_state = randomToken(24);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client_id"), m_clientId);
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    q.addQueryItem(QStringLiteral("redirect_uri"), redirectUri());
    q.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    q.addQueryItem(QStringLiteral("code_challenge"), pkceChallenge(m_verifier));
    q.addQueryItem(QStringLiteral("scope"), QString::fromLatin1(kScopes));
    q.addQueryItem(QStringLiteral("state"), m_state);
    QUrl url(QStringLiteral("https://accounts.spotify.com/authorize"));
    url.setQuery(q);
    m_loginTimeout->start();
    emit changed();
    if (!QDesktopServices::openUrl(url)) {
        cancelLogin();
        if (error) *error = QStringLiteral("could not open the browser");
    }
}

void SpotifyAccount::cancelLogin()
{
    m_loginTimeout->stop();
    if (!m_server) return;
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
    emit changed();
}

void SpotifyAccount::onCallback(QTcpSocket *socket, const QByteArray &request)
{
    const QList<QByteArray> line = request.left(request.indexOf("\r\n")).split(' ');
    const QUrl target(QString::fromUtf8(line.value(1)));
    if (line.value(0) != "GET" || target.path() != QLatin1String("/callback")) {
        socket->write("HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }
    const QUrlQuery q(target);
    const QString code = q.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
    const QString state = q.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded);
    const QString err = q.queryItemValue(QStringLiteral("error"), QUrl::FullyDecoded);
    const bool ok = err.isEmpty() && !code.isEmpty() && state == m_state;
    socket->write(callbackPage(ok, ok ? QStringLiteral("Angemeldet – du kannst dieses Fenster schließen. / Signed in – you can close this window.")
                                      : QStringLiteral("Anmeldung fehlgeschlagen / Sign-in failed: %1").arg(err.isEmpty() ? QStringLiteral("state") : err)));
    socket->disconnectFromHost();
    const QString verifier = m_verifier;
    cancelLogin();
    if (!ok) { emit message(err.isEmpty() ? QStringLiteral("invalid answer") : err, true); return; }
    tokenRequest({{"grant_type", "authorization_code"}, {"code", code}, {"redirect_uri", redirectUri()}, {"client_id", m_clientId},
                  {"code_verifier", verifier}},
                 [this](const QString &error) {
                     if (!error.isEmpty()) { emit message(error, true); return; }
                     loadProfile();
                 });
}

void SpotifyAccount::tokenRequest(const QList<std::pair<QString, QString>> &form, const std::function<void(const QString &)> &cb)
{
    QNetworkRequest req(QUrl(QStringLiteral("https://accounts.spotify.com/api/token")));
    req.setTransferTimeout(kTimeoutMs);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    QNetworkReply *reply = m_nam->post(req, formBody(form));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (status != 200 || o.value("access_token").toString().isEmpty()) {
            const QString e = o.value("error_description").toString(o.value("error").toString(reply->errorString()));
            // Abgelaufene oder widerrufene Anmeldung: abmelden statt immer wieder zu scheitern
            if (o.value("error").toString() == QLatin1String("invalid_grant")) setRefreshToken({});
            return cb(QStringLiteral("token: %1").arg(e));
        }
        m_access = o.value("access_token").toString();
        m_expires = QDateTime::currentDateTimeUtc().addSecs(o.value("expires_in").toInt(3600) - 60);
        const QString refresh = o.value("refresh_token").toString();
        if (!refresh.isEmpty()) setRefreshToken(refresh);
        cb({});
    });
}

void SpotifyAccount::withToken(const std::function<void(const QString &)> &cb, bool force)
{
    if (!force && !m_access.isEmpty() && QDateTime::currentDateTimeUtc() < m_expires) return cb({});
    if (m_refresh.isEmpty()) return cb(QStringLiteral("not signed in"));
    m_waiting << cb;
    if (m_refreshing) return;
    m_refreshing = true;
    tokenRequest({{"grant_type", "refresh_token"}, {"refresh_token", m_refresh}, {"client_id", m_clientId}}, [this](const QString &error) {
        m_refreshing = false;
        const auto waiting = std::exchange(m_waiting, {});
        for (const auto &w : waiting) w(error);
    });
}

void SpotifyAccount::logout()
{
    cancelLogin();
    setRefreshToken({});
}

void SpotifyAccount::loadProfile()
{
    api(QStringLiteral("https://api.spotify.com/v1/me"), [this](int status, const QJsonObject &o) {
        if (status != 200) {
            emit message(QStringLiteral("profile: HTTP %1 %2").arg(status).arg(o.value("error").toObject().value("message").toString()), true);
            return;
        }
        m_user = o.value("display_name").toString(o.value("id").toString());
        m_product = o.value("product").toString();
        emit changed();
        emit message(QStringLiteral("signed in"), false);
    });
}

void SpotifyAccount::api(const QString &url, const JsonCb &cb)
{
    request(url, cb, false);
}

void SpotifyAccount::request(const QString &url, const JsonCb &cb, bool retried)
{
    withToken([this, url, cb, retried](const QString &error) {
        if (!error.isEmpty()) return cb(0, QJsonObject{{"error", QJsonObject{{"message", error}}}});
        QNetworkRequest req(QUrl::fromEncoded(url.toUtf8()));
        req.setTransferTimeout(kTimeoutMs);
        req.setRawHeader("Authorization", "Bearer " + m_access.toUtf8());
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, url, cb, retried] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 && !retried) {   // Token vorzeitig ungültig: einmal erneuern
                m_access.clear();
                return request(url, cb, true);
            }
            cb(status, QJsonDocument::fromJson(reply->readAll()).object());
        });
    }, retried);
}

void SpotifyAccount::apiPaged(const QString &url, int max, const ItemsCb &cb)
{
    pagedStep(url, max, {}, cb);
}

void SpotifyAccount::pagedStep(const QString &url, int max, QJsonArray items, const ItemsCb &cb)
{
    api(url, [this, max, items, cb](int status, const QJsonObject &o) mutable {
        if (status != 200) return cb(items, status);
        for (const auto &v : o.value("items").toArray()) items.append(v);
        const QString next = o.value("next").toString();
        if (next.isEmpty() || items.size() >= max) return cb(items, 200);
        pagedStep(next, max, items, cb);
    });
}

// ---------------------------------------------------------------- Auflösung
ResolveJob::ResolveJob(const QString &url, SpotifyAccount *account, bool preferVideo, QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)), m_account(account), m_url(url.trimmed()), m_preferVideo(preferVideo)
{
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

void ResolveJob::start()
{
    const Link link = parseLink(m_url);
    switch (link.kind) {
    case Kind::Track: return track(link.id);
    case Kind::Album:
    case Kind::Playlist:
    case Kind::Artist:
    case Kind::Liked: return collection(link);
    case Kind::Unknown: break;
    }
    fail(QStringLiteral("unsupported Spotify link"), true);
}

void ResolveJob::get(const QString &url, const std::function<void(int, const QString &)> &cb, const QByteArray &cookie)
{
    QNetworkRequest req(QUrl::fromEncoded(url.toUtf8()));
    req.setTransferTimeout(kTimeoutMs);
    req.setRawHeader("User-Agent", kChromeUa);
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    if (!cookie.isEmpty()) req.setRawHeader("Cookie", cookie);
    QNetworkReply *reply = m_nam->get(req);
    reply->setParent(this);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb] {
        reply->deleteLater();
        if (m_done) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0) return fail(reply->errorString());
        cb(status, QString::fromUtf8(reply->readAll()));
    });
}

void ResolveJob::fail(const QString &error, bool definitive)
{
    if (m_done) return;
    m_done = true;
    emit failed(error, definitive);
}

void ResolveJob::track(const QString &id)
{
    get(QStringLiteral("https://open.spotify.com/embed/track/") + id, [this, id](int status, const QString &html) {
        Track t = status == 200 ? embedTrack(embedEntity(html)) : Track{};
        if (!t.title.isEmpty()) return begin({t});
        // Embed-Seite verweigert: mit Anmeldung über die Web-API
        if (!m_account || !m_account->loggedIn()) return fail(QStringLiteral("track not found (HTTP %1)").arg(status), status == 404);
        QPointer<ResolveJob> self(this);
        m_account->api(QStringLiteral("https://api.spotify.com/v1/tracks/") + id, [self](int apiStatus, const QJsonObject &o) {
            if (!self) return;
            const Track t = apiTrack(o);
            if (apiStatus != 200 || t.title.isEmpty()) return self->fail(QStringLiteral("track not found (API %1)").arg(apiStatus), apiStatus == 404);
            self->begin({t});
        });
    });
}

void ResolveJob::collection(const Link &link)
{
    const bool signedIn = m_account && m_account->loggedIn();
    if (link.kind == Kind::Liked) {
        if (!signedIn) return fail(QStringLiteral("sign in to play your liked songs"), true);
        m_result.collection = QStringLiteral("Liked Songs");
        QPointer<ResolveJob> self(this);
        m_account->apiPaged(QStringLiteral("https://api.spotify.com/v1/me/tracks?limit=50"), kMaxTracks, [self](const QJsonArray &items, int status) {
            if (!self) return;
            const QList<Track> tracks = apiItems(items);
            if (tracks.isEmpty()) return self->fail(QStringLiteral("liked songs: HTTP %1").arg(status), status != 0);
            self->begin(tracks);
        });
        return;
    }
    // Angemeldet: vollständige Liste über die Web-API (die Embed-Seite zeigt höchstens 100 Titel)
    if (signedIn && link.kind == Kind::Playlist) return playlistFromApi(link.id, true);
    if (signedIn && link.kind == Kind::Album) {
        QPointer<ResolveJob> self(this);
        m_account->apiPaged(QStringLiteral("https://api.spotify.com/v1/albums/%1/tracks?limit=50").arg(link.id), kMaxTracks,
                            [self, link](const QJsonArray &items, int) {
                                if (!self) return;
                                const QList<Track> tracks = apiItems(items);
                                if (tracks.isEmpty()) return self->collectionFromEmbed(link);
                                self->begin(tracks);
                            });
        return;
    }
    collectionFromEmbed(link);
}

void ResolveJob::playlistFromApi(const QString &id, bool items)
{
    QPointer<ResolveJob> self(this);
    m_account->api(QStringLiteral("https://api.spotify.com/v1/playlists/%1?fields=name").arg(id), [self](int status, const QJsonObject &o) {
        if (self && status == 200) self->m_result.collection = o.value("name").toString();
    });
    const QString url = QStringLiteral("https://api.spotify.com/v1/playlists/%1/%2?limit=100").arg(id, items ? QStringLiteral("items") : QStringLiteral("tracks"));
    m_account->apiPaged(url, kMaxTracks, [self, id, items](const QJsonArray &list, int status) {
        if (!self) return;
        const QList<Track> tracks = apiItems(list);
        if (!tracks.isEmpty()) return self->begin(tracks);
        if (items && (status == 404 || status == 400)) return self->playlistFromApi(id, false);   // ältere API: /tracks
        self->collectionFromEmbed({Kind::Playlist, id});   // z. B. Spotify-eigene Listen, die die API neuen Apps verweigert
    });
}

void ResolveJob::collectionFromEmbed(const Link &link)
{
    static const QHash<Kind, QString> types{{Kind::Album, "album"}, {Kind::Playlist, "playlist"}, {Kind::Artist, "artist"}};
    get(QStringLiteral("https://open.spotify.com/embed/%1/%2").arg(types.value(link.kind), link.id), [this](int status, const QString &html) {
        const QJsonObject entity = embedEntity(html);
        const QList<Track> tracks = embedTrackList(entity);
        if (tracks.isEmpty()) {
            const bool privateList = status == 404 || (status == 200 && !entity.isEmpty());
            return fail(privateList ? QStringLiteral("list is empty or private (sign in to play your own playlists)")
                                    : QStringLiteral("list not readable (HTTP %1)").arg(status),
                        privateList);
        }
        if (m_result.collection.isEmpty()) m_result.collection = entity.value("name").toString();
        begin(tracks);
    });
}

void ResolveJob::begin(const QList<Track> &tracks)
{
    if (m_listOnly) {
        m_done = true;
        emit listed(tracks, m_result.collection);
        return;
    }
    m_result.count = int(tracks.size());
    for (int i = 1; i < tracks.size(); ++i) {
        m_result.more << trackUrl(tracks.at(i));
        m_result.moreTitles << tracks.at(i).display();
        m_result.moreDurations << double(tracks.at(i).durationMs) / 1000.0;
    }
    search(tracks.first());
}

void ResolveJob::search(const Track &track)
{
    m_result.title = track.display();
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("search_query"), track.query());
    q.addQueryItem(QStringLiteral("sp"), QStringLiteral("EgIQAQ%3D%3D"));   // nur Videos, keine Kanäle/Playlists
    QUrl url(QStringLiteral("https://www.youtube.com/results"));
    url.setQuery(q);
    const QString query = track.query();
    const qint64 duration = track.durationMs;
    get(QString::fromUtf8(url.toEncoded()), [this, query, duration](int status, const QString &html) {
        const QString id = status == 200 ? pickYoutube(youtubeResults(html), duration, m_preferVideo) : QString();
        // Suchseite nicht lesbar (z. B. Zustimmungsseite): yt-dlp sucht selbst
        m_result.url = id.isEmpty() ? QStringLiteral("ytdl://ytsearch1:") + query : QStringLiteral("https://www.youtube.com/watch?v=") + id;
        m_done = true;
        emit finished(m_result);
    }, QByteArrayLiteral("SOCS=CAI; CONSENT=YES+1"));
}

} // namespace uve::spotify
