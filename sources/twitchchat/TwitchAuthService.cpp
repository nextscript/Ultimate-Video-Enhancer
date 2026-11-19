#include "TwitchAuthService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

static const char *kDeviceUrl = "https://id.twitch.tv/oauth2/device";
static const char *kTokenUrl  = "https://id.twitch.tv/oauth2/token";
static const char *kRevokeUrl = "https://id.twitch.tv/oauth2/revoke";

TwitchAuthService::TwitchAuthService(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent), m_nam(nam)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setSingleShot(true);
    connect(m_pollTimer, &QTimer::timeout, this, &TwitchAuthService::pollNow);
}

void TwitchAuthService::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

// ------------------------------------------------------------ Device Flow --

void TwitchAuthService::begin(const QString &clientId, const QString &scopes)
{
    m_clientId = clientId;
    m_refreshToken.clear();
    m_refreshPending = false;
    m_pollTimer->stop();

    QUrlQuery q;
    q.addQueryItem("client_id", clientId);
    q.addQueryItem("scopes", scopes);   // QUrlQuery kodiert korrekt
    QNetworkRequest req{QUrl(kDeviceUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    auto *reply = m_nam->post(req, q.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleDeviceReply(reply); });
    setState(State::AwaitingUser);       // Übergang; echte Daten kommen im Callback
}

void TwitchAuthService::handleDeviceReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        setState(State::Error);
        emit authFailed("network", reply->errorString());
        return;
    }
    DeviceInfo info;
    QString err;
    if (!parseDeviceResponse(reply->readAll(), &info, &err)) {
        setState(State::Error);
        emit authFailed("device_request", err);
        return;
    }
    m_deviceCode = info.deviceCode;
    m_userCode = info.userCode;
    // verification_uri_complete enthält den Code schon. Twitch schickt inzwischen auch in
    // verification_uri bereits „?device-code=…“ mit – dann nicht ein zweites Mal anhängen.
    if (!info.verificationUriComplete.isEmpty()) {
        m_verificationUri = info.verificationUriComplete;
    } else if (info.verificationUri.contains(QLatin1String("device-code="))) {
        m_verificationUri = info.verificationUri;
    } else {
        m_verificationUri = info.verificationUri
                            + (info.verificationUri.contains(QLatin1Char('?')) ? QStringLiteral("&") : QStringLiteral("?"))
                            + QStringLiteral("device-code=")
                            + QString::fromUtf8(QUrl::toPercentEncoding(info.userCode.toUtf8()));
    }
    m_interval = qBound(1, info.interval, 30);
    m_deviceExpiresAtMs = QDateTime::currentMSecsSinceEpoch() + info.expiresIn * 1000LL;
    emit deviceReady(m_userCode, m_verificationUri);
    schedulePoll(m_interval);
    setState(State::Polling);
}

void TwitchAuthService::schedulePoll(int seconds)
{
    m_pollTimer->start(qMax(1, seconds) * 1000);
}

void TwitchAuthService::cancel()
{
    m_pollTimer->stop();
    m_deviceCode.clear();
    m_userCode.clear();
    m_verificationUri.clear();
    setState(State::Idle);
}

void TwitchAuthService::pollNow()
{
    if (m_deviceCode.isEmpty())
        return;
    if (QDateTime::currentMSecsSinceEpoch() > m_deviceExpiresAtMs) {
        m_deviceCode.clear();
        setState(State::Error);
        emit authFailed("device_expired", tr("Der Geräte-Code ist abgelaufen. Bitte erneut verbinden."));
        return;
    }
    QUrlQuery q;
    q.addQueryItem("device_code", m_deviceCode);
    q.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    q.addQueryItem("client_id", m_clientId);
    postToken(q.toString(QUrl::FullyEncoded).toUtf8());
}

void TwitchAuthService::postToken(const QByteArray &formBody)
{
    QNetworkRequest req{QUrl(kTokenUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    auto *reply = m_nam->post(req, formBody);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleTokenReply(reply); });
}

// Öffentlicher Client: Refresh ohne Client-Secret (offiziell vorgesehen).
void TwitchAuthService::resume(const QString &clientId, const QString &accessToken,
                               const QString &refreshToken, qint64 expiresAtMs)
{
    m_clientId = clientId;
    if (!accessToken.isEmpty() && expiresAtMs > QDateTime::currentMSecsSinceEpoch() + 30'000) {
        m_refreshToken = refreshToken;   // Token lebt noch – nichts zu tun
        setState(State::SignedIn);
        return;
    }
    if (refreshToken.isEmpty()) {
        setState(State::Error);
        emit authFailed("expired", tr("Sitzung abgelaufen – bitte neu anmelden."));
        return;
    }
    m_refreshToken = refreshToken;
    m_refreshPending = true;
    QUrlQuery q;
    q.addQueryItem("grant_type", "refresh_token");
    q.addQueryItem("refresh_token", refreshToken);
    q.addQueryItem("client_id", clientId);
    postToken(q.toString(QUrl::FullyEncoded).toUtf8());
}

void TwitchAuthService::refreshNow(const QString &clientId)
{
    resume(clientId, {}, m_refreshToken.isEmpty() ? QString() : m_refreshToken, 0);
}

void TwitchAuthService::handleTokenReply(QNetworkReply *reply)
{
    reply->deleteLater();
    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError && status != 400) {
        setState(State::Error);
        emit authFailed("network", reply->errorString());
        return;
    }

    if (status == 200) {
        TokenInfo ti;
        QString err;
        if (!parseTokenResponse(body, &ti, &err)) {
            setState(State::Error);
            emit authFailed("token_parse", err);
            return;
        }
        m_refreshToken = ti.refreshToken;   // neuer, einmalig gültiger Refresh-Token
        m_deviceCode.clear();
        m_userCode.clear();
        m_verificationUri.clear();
        m_pollTimer->stop();
        m_refreshPending = false;
        setState(State::SignedIn);
        emit signedIn(ti.accessToken, ti.refreshToken, ti.expiresAtMs, ti.scope);
        return;
    }

    // 400: Warten / Bremsen / endgültiger Fehler
    const QString reason = tokenFlowError(body);
    if (reason == QLatin1String("authorization_pending")) {
        schedulePoll(m_interval);
        return;
    }
    if (reason == QLatin1String("slow_down")) {
        m_interval = qMin(m_interval + 5, 60);
        schedulePoll(m_interval);
        return;
    }
    m_pollTimer->stop();
    m_deviceCode.clear();
    m_refreshPending = false;
    setState(State::Error);
    QString msg = body.isEmpty() ? QStringLiteral("HTTP %1").arg(status)
                                 : QJsonDocument::fromJson(body).object()
                                       .value("message").toString();
    emit authFailed(reason.isEmpty() ? QStringLiteral("http_%1").arg(status) : reason,
                    msg.isEmpty() ? reply->errorString() : msg);
}

// --------------------------------------------------------------- Revocation --

void TwitchAuthService::revoke(const QString &clientId, const QString &accessToken)
{
    m_pollTimer->stop();
    m_deviceCode.clear();
    m_userCode.clear();
    m_verificationUri.clear();
    m_refreshToken.clear();
    if (!accessToken.isEmpty()) {
        QUrlQuery q;
        q.addQueryItem("token", accessToken);
        q.addQueryItem("client_id", clientId);
        QNetworkRequest req{QUrl(kRevokeUrl)};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        auto *reply = m_nam->post(req, q.toString(QUrl::FullyEncoded).toUtf8());
        connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
    }
    setState(State::Idle);
}

// ------------------------------------------------- reines Parsen (testbar) --

bool TwitchAuthService::parseDeviceResponse(const QByteArray &json, DeviceInfo *out, QString *error)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        if (error) *error = tr("Ungültige Antwort (kein JSON-Objekt).");
        return false;
    }
    const QJsonObject o = doc.object();
    out->deviceCode = o.value("device_code").toString();
    out->userCode = o.value("user_code").toString();
    out->verificationUri = o.value("verification_uri").toString();
    out->verificationUriComplete = o.value("verification_uri_complete").toString();
    out->expiresIn = o.value("expires_in").toInt(0);
    out->interval = o.value("interval").toInt(5);
    if (out->deviceCode.isEmpty() || out->userCode.isEmpty() || out->verificationUri.isEmpty()) {
        if (error) *error = o.value("message").toString(tr("Fehlende Felder in der Antwort."));
        return false;
    }
    return true;
}

bool TwitchAuthService::parseTokenResponse(const QByteArray &json, TokenInfo *out, QString *error)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        if (error) *error = tr("Ungültige Token-Antwort.");
        return false;
    }
    const QJsonObject o = doc.object();
    out->accessToken = o.value("access_token").toString();
    out->refreshToken = o.value("refresh_token").toString();
    const int expiresIn = o.value("expires_in").toInt(4 * 3600);
    out->expiresAtMs = QDateTime::currentMSecsSinceEpoch() + expiresIn * 1000LL;
    for (const auto &s : o.value("scope").toArray())
        out->scope << s.toString();
    if (out->accessToken.isEmpty()) {
        if (error) *error = o.value("message").toString(tr("Kein access_token in der Antwort."));
        return false;
    }
    return true;
}

QString TwitchAuthService::tokenFlowError(const QByteArray &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    // Twitch liefert {"status":400,"message":"authorization_pending"}
    QString msg = o.value("message").toString();
    return msg;
}

bool TwitchAuthService::scopeAllows(const QStringList &granted, const QString &needed)
{
    return granted.contains(needed);
}
