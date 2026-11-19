#include "KickAuth.h"
#include "TokenVault.h"
#include "KickAppDefaults.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

static const char kAuthorize[] = "https://id.kick.com/oauth/authorize";
static const char kToken[] = "https://id.kick.com/oauth/token";
static const char kRevoke[] = "https://id.kick.com/oauth/revoke";
static const char kUsers[] = "https://api.kick.com/public/v1/users";
static const char kScopes[] = "user:read chat:write moderation:ban moderation:chat_message:manage";

QString KickAuth::redirectUri()
{
    return QStringLiteral("http://localhost:%1/callback").arg(kCallbackPort);
}

KickAuth::KickAuth(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
    applyDefaultCredentials();
}

// Ohne eigene App: die eingebaute App des Herausgebers (KickAppDefaults.h)
void KickAuth::applyDefaultCredentials()
{
    m_clientId = QStringLiteral(KICK_DEFAULT_CLIENT_ID);
    m_clientSecret = QStringLiteral(KICK_DEFAULT_CLIENT_SECRET);
}

bool KickAuth::usingOwnApp() const
{
    return m_clientId != QLatin1String(KICK_DEFAULT_CLIENT_ID);
}

static QString errorText(QNetworkReply *reply, const QByteArray &body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    QString msg = o.value(QStringLiteral("message")).toString();
    if (msg.isEmpty()) msg = o.value(QStringLiteral("error_description")).toString();
    if (msg.isEmpty()) msg = o.value(QStringLiteral("error")).toString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (msg.isEmpty()) msg = reply->errorString();
    return status > 0 ? QStringLiteral("HTTP %1: %2").arg(status).arg(msg) : msg;
}

void KickAuth::setVault(TokenVault *vault)
{
    m_vault = vault;
    if (!m_vault) return;
    const QJsonObject d = m_vault->data();
    const QJsonObject k = d.value(QStringLiteral("kick")).toObject();
    m_accessToken = k.value(QStringLiteral("accessToken")).toString();
    m_refreshToken = k.value(QStringLiteral("refreshToken")).toString();
    m_expiresAt = qint64(k.value(QStringLiteral("expiresAt")).toDouble());
    m_userName = k.value(QStringLiteral("userName")).toString();
    m_userId = qint64(k.value(QStringLiteral("userId")).toDouble());
    applyDefaultCredentials();
    emit changed();
    if (!m_refreshToken.isEmpty() && m_expiresAt > 0
        && m_expiresAt - QDateTime::currentMSecsSinceEpoch() < 5 * 60 * 1000)
        refresh([](bool) {});
}

void KickAuth::persist()
{
    if (!m_vault) return;
    QJsonObject d = m_vault->data();
    QJsonObject k;
    k.insert(QStringLiteral("accessToken"), m_accessToken);
    k.insert(QStringLiteral("refreshToken"), m_refreshToken);
    k.insert(QStringLiteral("expiresAt"), double(m_expiresAt));
    k.insert(QStringLiteral("userName"), m_userName);
    k.insert(QStringLiteral("userId"), double(m_userId));
    d.insert(QStringLiteral("kick"), k);
    m_vault->save(d);
}

void KickAuth::setCredentials(const QString &clientId, const QString &clientSecret)
{
    // Leere Client-ID = zurück zur eingebauten App
    const QString id = clientId.trimmed().isEmpty() ? QStringLiteral(KICK_DEFAULT_CLIENT_ID) : clientId.trimmed();
    const QString secret = id == QLatin1String(KICK_DEFAULT_CLIENT_ID) ? QStringLiteral(KICK_DEFAULT_CLIENT_SECRET)
                                                                       : clientSecret.trimmed();
    if (id == m_clientId && (secret.isEmpty() || secret == m_clientSecret)) return;
    if (id != m_clientId) {            // andere App → alte Anmeldung gilt nicht mehr
        m_accessToken.clear();
        m_refreshToken.clear();
        m_expiresAt = 0;
        m_userName.clear();
        m_userId = 0;
    }
    m_clientId = id;
    if (!secret.isEmpty()) m_clientSecret = secret;
    persist();
    emit changed();
}

void KickAuth::setState(const QString &state, const QString &error)
{
    m_state = state;
    m_error = error;
    emit stateChanged();
}

QByteArray KickAuth::randomUrlSafe(int bytes)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    for (int i = 0; i < bytes; ++i)
        raw[i] = char(QRandomGenerator::system()->bounded(256));
    return raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

void KickAuth::startLogin()
{
    if (!hasCredentials()) {
        setState(QStringLiteral("error"), QStringLiteral("credentials"));
        return;
    }
    stopServer();
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, kCallbackPort)) {
        const QString why = m_server->errorString();
        stopServer();
        setState(QStringLiteral("error"), QStringLiteral("Port %1: %2").arg(kCallbackPort).arg(why));
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, &KickAuth::onCallbackConnection);

    m_verifier = randomUrlSafe(48);
    m_oauthState = randomUrlSafe(16);
    const QByteArray challenge = QCryptographicHash::hash(m_verifier, QCryptographicHash::Sha256)
                                     .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QUrl url{QString::fromLatin1(kAuthorize)};
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    q.addQueryItem(QStringLiteral("client_id"), m_clientId);
    q.addQueryItem(QStringLiteral("redirect_uri"), redirectUri());
    q.addQueryItem(QStringLiteral("scope"), QString::fromLatin1(kScopes));
    q.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(challenge));
    q.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    q.addQueryItem(QStringLiteral("state"), QString::fromLatin1(m_oauthState));
    url.setQuery(q);
    m_authorizeUrl = url.toString(QUrl::FullyEncoded);
    setState(QStringLiteral("waiting"));
    QDesktopServices::openUrl(url);

    // Nach 10 Minuten ohne Rückmeldung aufgeben.
    QPointer<QTcpServer> server = m_server;
    QTimer::singleShot(10 * 60 * 1000, this, [this, server]() {
        if (server && server == m_server && m_state == QLatin1String("waiting")) {
            stopServer();
            setState(QStringLiteral("error"), QStringLiteral("timeout"));
        }
    });
}

void KickAuth::cancelLogin()
{
    stopServer();
    setState(QStringLiteral("idle"));
}

void KickAuth::stopServer()
{
    if (!m_server) return;
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
}

static void respondHtml(QTcpSocket *sock, const QString &title, const QString &text)
{
    const QByteArray body = QStringLiteral(
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>%1</title></head>"
        "<body style=\"font-family:sans-serif;background:#0b0e0f;color:#e8e8e8;text-align:center;padding-top:15vh\">"
        "<h2 style=\"color:#53fc18\">%1</h2><p>%2</p></body></html>").arg(title, text).toUtf8();
    QByteArray resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
                      + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    sock->write(resp);
    sock->flush();
    sock->disconnectFromHost();
}

void KickAuth::onCallbackConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            const QByteArray head = sock->peek(8192);
            if (!head.contains("\r\n")) return;
            const QByteArray firstLine = head.left(head.indexOf("\r\n"));
            const QList<QByteArray> parts = firstLine.split(' ');
            sock->readAll();
            if (parts.size() < 2 || parts[0] != "GET") { sock->abort(); return; }
            const QUrl url(QStringLiteral("http://localhost") + QString::fromLatin1(parts[1]));
            if (url.path() != QLatin1String("/callback")) {
                sock->write("HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                sock->disconnectFromHost();
                return;
            }
            const QUrlQuery q(url);
            const QString code = q.queryItemValue(QStringLiteral("code"));
            const QString state = q.queryItemValue(QStringLiteral("state"));
            const QString err = q.queryItemValue(QStringLiteral("error"));
            if (!err.isEmpty() || code.isEmpty() || state != QString::fromLatin1(m_oauthState)) {
                respondHtml(sock, QStringLiteral("Kick Chat"),
                            QStringLiteral("Anmeldung abgebrochen / Login cancelled."));
                stopServer();
                setState(QStringLiteral("error"), err.isEmpty() ? QStringLiteral("state") : err);
                return;
            }
            respondHtml(sock, QStringLiteral("Kick Chat"),
                        QStringLiteral("Angemeldet – du kannst dieses Fenster schließen.<br>"
                                       "Logged in – you can close this window."));
            stopServer();
            exchangeCode(code);
        });
    }
}

void KickAuth::exchangeCode(const QString &code)
{
    QNetworkRequest req{QUrl(QString::fromLatin1(kToken))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setTransferTimeout(20000);
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("client_id"), m_clientId);
    form.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);
    form.addQueryItem(QStringLiteral("redirect_uri"), redirectUri());
    form.addQueryItem(QStringLiteral("code_verifier"), QString::fromLatin1(m_verifier));
    form.addQueryItem(QStringLiteral("code"), code);
    QNetworkReply *reply = m_nam->post(req, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            setState(QStringLiteral("error"), errorText(reply, body));
            return;
        }
        applyTokenResponse(body);
        if (m_accessToken.isEmpty()) {
            setState(QStringLiteral("error"), QStringLiteral("no token"));
            return;
        }
        fetchUser();
    });
}

void KickAuth::applyTokenResponse(const QByteArray &body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const QString access = o.value(QStringLiteral("access_token")).toString();
    if (access.isEmpty()) return;
    m_accessToken = access;
    const QString refresh = o.value(QStringLiteral("refresh_token")).toString();
    if (!refresh.isEmpty()) m_refreshToken = refresh;
    double secs = o.value(QStringLiteral("expires_in")).toDouble();
    if (secs <= 0) secs = o.value(QStringLiteral("expiry")).toDouble();
    m_expiresAt = secs > 0 ? QDateTime::currentMSecsSinceEpoch() + qint64(secs * 1000) : 0;
    persist();
}

void KickAuth::fetchUser()
{
    QNetworkRequest req{QUrl(QString::fromLatin1(kUsers))};
    req.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(20000);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonArray data = QJsonDocument::fromJson(body).object().value(QStringLiteral("data")).toArray();
            if (!data.isEmpty()) {
                const QJsonObject u = data.first().toObject();
                m_userName = u.value(QStringLiteral("name")).toString();
                m_userId = qint64(u.value(QStringLiteral("user_id")).toDouble());
            }
        }
        persist();
        setState(QStringLiteral("idle"));
        emit changed();
    });
}

void KickAuth::refresh(const std::function<void(bool)> &done)
{
    if (m_refreshToken.isEmpty() || !hasCredentials()) { done(false); return; }
    QNetworkRequest req{QUrl(QString::fromLatin1(kToken))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setTransferTimeout(20000);
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("client_id"), m_clientId);
    form.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);
    form.addQueryItem(QStringLiteral("refresh_token"), m_refreshToken);
    QNetworkReply *reply = m_nam->post(req, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            if (status == 400 || status == 401) {      // Erneuerung abgelehnt → neu anmelden
                m_accessToken.clear();
                m_refreshToken.clear();
                m_expiresAt = 0;
                persist();
                emit changed();
            }
            done(false);
            return;
        }
        applyTokenResponse(body);
        emit changed();
        done(!m_accessToken.isEmpty());
    });
}

void KickAuth::logout()
{
    stopServer();
    if (!m_accessToken.isEmpty()) {
        QUrl url{QString::fromLatin1(kRevoke)};
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("token"), m_accessToken);
        q.addQueryItem(QStringLiteral("token_hint_type"), QStringLiteral("access_token"));
        url.setQuery(q);
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        QNetworkReply *reply = m_nam->post(req, QByteArray());
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
    m_accessToken.clear();
    m_refreshToken.clear();
    m_expiresAt = 0;
    m_userName.clear();
    m_userId = 0;
    persist();
    setState(QStringLiteral("idle"));
    emit changed();
}

// Allgemeiner Aufruf der offiziellen API (api.kick.com) mit Token; erneuert bei 401 einmal.
void KickAuth::api(const QByteArray &method, const QString &path, const QJsonObject &body,
                   const std::function<void(bool, int, const QByteArray &, const QString &)> &done)
{
    if (m_accessToken.isEmpty()) { done(false, 401, QByteArray(), QStringLiteral("login")); return; }

    auto run = [this, method, path, body, done](bool retried, auto &&self) -> void {
        QNetworkRequest req{QUrl(QStringLiteral("https://api.kick.com") + path)};
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        req.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
        req.setRawHeader("Accept", "application/json");
        req.setTransferTimeout(20000);
        const QByteArray payload = body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);
        QNetworkReply *reply = m_nam->sendCustomRequest(req, method, payload);
        connect(reply, &QNetworkReply::finished, this, [this, reply, retried, done, self]() {
            reply->deleteLater();
            const QByteArray resp = reply->readAll();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 && !retried && !m_refreshToken.isEmpty()) {
                refresh([done, self](bool ok) {
                    if (ok) self(true, self);
                    else done(false, 401, QByteArray(), QStringLiteral("login"));
                });
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                QString err = errorText(reply, resp);
                if (status == 403) err = QStringLiteral("forbidden: ") + err;
                done(false, status, resp, err);
                return;
            }
            done(true, status, resp, QString());
        });
    };
    if (m_expiresAt > 0 && m_expiresAt - QDateTime::currentMSecsSinceEpoch() < 60 * 1000 && !m_refreshToken.isEmpty()) {
        refresh([run, done](bool ok) {
            if (ok) run(false, run);
            else done(false, 401, QByteArray(), QStringLiteral("login"));
        });
        return;
    }
    run(false, run);
}

void KickAuth::sendChat(qint64 broadcasterUserId, const QString &content, const QString &replyToId,
                        const std::function<void(bool, const QString &)> &done)
{
    if (m_accessToken.isEmpty()) { done(false, QStringLiteral("login")); return; }
    if (broadcasterUserId <= 0) { done(false, QStringLiteral("channel")); return; }
    QJsonObject body;
    body.insert(QStringLiteral("broadcaster_user_id"), double(broadcasterUserId));
    body.insert(QStringLiteral("content"), content);
    body.insert(QStringLiteral("type"), QStringLiteral("user"));
    if (!replyToId.isEmpty()) body.insert(QStringLiteral("reply_to_message_id"), replyToId);
    api("POST", QStringLiteral("/public/v1/chat"), body, [done](bool ok, int, const QByteArray &resp, const QString &err) {
        if (!ok) { done(false, err); return; }
        const QJsonObject data = QJsonDocument::fromJson(resp).object().value(QStringLiteral("data")).toObject();
        if (data.contains(QStringLiteral("is_sent")) && !data.value(QStringLiteral("is_sent")).toBool()) {
            done(false, QStringLiteral("not sent"));
            return;
        }
        done(true, QString());
    });
}
