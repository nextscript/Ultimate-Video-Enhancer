#include "TwitchHelix.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

TwitchHelix::TwitchHelix(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent), m_nam(nam)
{
}

void TwitchHelix::setCredentials(const QString &clientId, const QString &accessToken,
                                 qint64 expiresAtMs)
{
    m_clientId = clientId;
    m.accessToken = accessToken;
    m.expiresAtMs = expiresAtMs;
}

bool TwitchHelix::tokenExpired() const
{
    return m.expiresAtMs > 0 && m.expiresAtMs <= QDateTime::currentMSecsSinceEpoch() + 30'000;
}

// ------------------------------------------------------------- Requestweg --

int TwitchHelix::requestUsers(const QStringList &values, bool asId)
{
    static int s_id = 0;
    const int id = ++s_id;
    if (values.isEmpty()) {
        emit failed(Kind::Users, id, "empty", {});
        return id;
    }
    QUrlQuery q;
    const QString key = asId ? QStringLiteral("id") : QStringLiteral("login");
    int n = 0;
    for (const QString &v : values) {
        if (n++ >= 100)
            break;                   // Helix-Limit pro Aufruf
        q.addQueryItem(key, v);
    }
    enqueue(Kind::Users, id, QStringLiteral("/users?") + q.toString(QUrl::FullyEncoded));
    return id;
}

int TwitchHelix::requestSelf()
{
    static int s_id = 0;
    const int id = ++s_id;
    enqueue(Kind::Users, id, QStringLiteral("/users"));
    return id;
}

int TwitchHelix::requestFollowedStreams(const QString &userId, const QString &cursor)
{
    static int s_id = 0;
    const int id = ++s_id;
    QUrlQuery q;
    q.addQueryItem("user_id", userId);
    q.addQueryItem("first", "100");  // offizielles Maximum
    if (!cursor.isEmpty())
        q.addQueryItem("cursor", cursor);
    enqueue(Kind::FollowedStreams, id,
            QStringLiteral("/streams/followed?") + q.toString(QUrl::FullyEncoded));
    return id;
}

int TwitchHelix::requestFollowedChannels(const QString &userId, const QString &cursor)
{
    static int s_id = 0;
    const int id = ++s_id;
    QUrlQuery q;
    q.addQueryItem("user_id", userId);
    q.addQueryItem("first", "100");
    if (!cursor.isEmpty())
        q.addQueryItem("cursor", cursor);
    enqueue(Kind::FollowedChannels, id,
            QStringLiteral("/channels/followed?") + q.toString(QUrl::FullyEncoded));
    return id;
}

int TwitchHelix::requestSearchChannels(const QString &query, bool liveOnly, const QString &cursor)
{
    static int s_id = 0;
    const int id = ++s_id;
    if (query.trimmed().isEmpty()) {
        emit failed(Kind::SearchChannels, id, "empty_query", {});
        return id;
    }
    QUrlQuery q;
    q.addQueryItem("query", query.trimmed());
    q.addQueryItem("live_only", liveOnly ? "true" : "false");
    q.addQueryItem("first", "100");
    if (!cursor.isEmpty())
        q.addQueryItem("cursor", cursor);
    enqueue(Kind::SearchChannels, id,
            QStringLiteral("/search/channels?") + q.toString(QUrl::FullyEncoded));
    return id;
}

int TwitchHelix::requestChannelInfo(const QString &userId)
{
    Q_UNUSED(userId)                   // reserviert; Titel kommt via /channels?broadcaster_id=
    static int s_id = 0;
    return ++s_id;
}

void TwitchHelix::enqueue(Kind kind, int id, const QString &path)
{
    m_queue.append({kind, id, path});
    pump();
}

void TwitchHelix::cancelAll()
{
    m_queue.clear();
    if (m_currentReply) {
        m_currentReply->abort();     // finished-Signal räumt auf
        m_currentReply = nullptr;
    }
    m_busy = false;
}

void TwitchHelix::pump()
{
    if (m_busy || m_queue.isEmpty())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_rateResetAfterMs > now) {
        // Rate-Fenster zu – bis zum Reset warten, dann weitermachen
        QTimer::singleShot(int((m_rateResetAfterMs - now) / 1000 + 1), this, [this] { pump(); });
        return;
    }
    sendNext();
}

void TwitchHelix::sendNext()
{
    if (m_queue.isEmpty())
        return;
    if (!ready()) {
        const Pending p = m_queue.takeFirst();
        emit failed(p.kind, p.id, "no_credentials", {});
        m_busy = false;
        pump();
        return;
    }
    if (tokenExpired()) {
        const Pending p = m_queue.takeFirst();
        emit failed(p.kind, p.id, "unauthorized", {});   // Aufrufer refresht
        emit unauthorized();
        m_busy = false;
        pump();
        return;
    }

    const Pending p = m_queue.takeFirst();
    QNetworkRequest req{QUrl(QStringLiteral("https://api.twitch.tv/helix") + p.path)};
    req.setRawHeader("Client-ID", m_clientId.toUtf8());
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m.accessToken.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    m_busy = true;
    m_currentReply = m_nam->get(req);
    QNetworkReply *reply = m_currentReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, p] {
        if (m_currentReply == reply)
            m_currentReply = nullptr;
        handleReply(reply, p.kind, p.id);
        reply->deleteLater();
        m_busy = false;
        pump();
    });
}

void TwitchHelix::handleReply(QNetworkReply *reply, Kind kind, int id)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Rate-Limit-Header auswerten (begrenzt = 0 oder 15-Minuten-Fenster)
    const QByteArray remRaw = reply->rawHeader("X-RateLimit-Remaining");
    const int remaining = remRaw.isEmpty() ? -1 : remRaw.toInt();
    const qint64 resetEpoch = reply->rawHeader("X-RateLimit-Reset").toLongLong();
    if (remaining == 0 && resetEpoch > 0) {
        const int secs = int(resetEpoch - QDateTime::currentSecsSinceEpoch());
        if (secs > 0) {
            m_rateResetAfterMs = (resetEpoch + 1) * 1000LL;
            emit rateLimited(secs);
        }
    }

    if (reply->error() != QNetworkReply::NoError && status == 0) {
        emit failed(kind, id, "network", reply->errorString());
        return;
    }
    if (status == 401) {
        emit failed(kind, id, "unauthorized", {});
        emit unauthorized();
        return;
    }
    if (status == 429) {
        int wait = reply->rawHeader("Retry-After").toInt();
        if (wait <= 0) {
            m_backoffSec = qMin(m_backoffSec > 0 ? m_backoffSec * 2 : 2, 30);
            wait = m_backoffSec;
        }
        m_rateResetAfterMs = QDateTime::currentMSecsSinceEpoch() + wait * 1000LL;
        emit failed(kind, id, "rate_limited", QString());
        return;
    }
    if (status >= 400) {
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        emit failed(kind, id, QStringLiteral("http_%1").arg(status),
                    o.value("message").toString());
        return;
    }

    m_backoffSec = 0;
    const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
    emit result(kind, id, parseRows(kind, body), nextCursor(body));
}

// ------------------------------------------------------- reine Parser (statisch) --

QList<QVariantMap> TwitchHelix::parseRows(Kind kind, const QJsonObject &body)
{
    const QJsonArray data = body.value("data").toArray();
    switch (kind) {
    case Kind::Users:             return usersToRows(data);
    case Kind::FollowedStreams:   return followedStreamsToRows(data);
    case Kind::FollowedChannels:  return followedChannelsToRows(data);
    case Kind::SearchChannels:    return searchChannelsToRows(data);
    case Kind::ChannelInfo:       return {};
    }
    return {};
}

QString TwitchHelix::nextCursor(const QJsonObject &pagination)
{
    return pagination.value("pagination").toObject().value("cursor").toString();
}

QString TwitchHelix::expandThumbnail(const QString &url, int w, int h)
{
    QString u = url;
    u.replace(QStringLiteral("{width}x{height}"), QStringLiteral("%1x%2").arg(w).arg(h));
    u.replace(QStringLiteral("{width}"), QString::number(w));
    u.replace(QStringLiteral("{height}"), QString::number(h));
    return u;
}

QList<QVariantMap> TwitchHelix::followedStreamsToRows(const QJsonArray &data)
{
    QList<QVariantMap> rows;
    for (const auto &v : data) {
        const QJsonObject o = v.toObject();
        QVariantMap r;
        r["userId"]       = o.value("user_id").toString();
        r["login"]        = o.value("user_name").toString().toLower();   // canonical
        r["name"]         = o.value("user_name").toString();  // Anzeige (Fallback, /users überschreibt)
        r["streamId"]     = o.value("id").toString();
        r["title"]        = o.value("title").toString();
        r["game"]         = o.value("game_name").toString();
        r["viewers"]      = o.value("viewer_count").toInt(0);
        r["live"]         = true;
        r["startedAt"]    = o.value("started_at").toString();
        r["thumbRaw"]     = o.value("thumbnail_url").toString();
        r["avatar"]       = QString();
        rows.append(r);
    }
    return rows;
}

QList<QVariantMap> TwitchHelix::followedChannelsToRows(const QJsonArray &data)
{
    QList<QVariantMap> rows;
    for (const auto &v : data) {
        const QJsonObject o = v.toObject();
        QVariantMap r;
        r["userId"]       = o.value("broadcaster_id").toString();
        r["login"]        = o.value("broadcaster_login").toString().toLower();
        r["name"]         = o.value("broadcaster_name").toString();
        r["live"]         = false;
        r["viewers"]      = 0;
        r["title"]        = QString();
        r["game"]         = QString();
        r["thumbRaw"]     = QString();
        r["avatar"]       = QString();
        r["followedAt"]   = o.value("followed_at").toString();
        rows.append(r);
    }
    return rows;
}

QList<QVariantMap> TwitchHelix::searchChannelsToRows(const QJsonArray &data)
{
    QList<QVariantMap> rows;
    for (const auto &v : data) {
        const QJsonObject o = v.toObject();
        QVariantMap r;
        // "id" = Kanal-/User-ID; broadcaster_id wäre ein Scope-geschütztes Feld
        r["userId"]       = o.value("id").toString();
        r["login"]        = o.value("broadcaster_login").toString().toLower();
        r["name"]         = o.value("display_name").toString();
        r["live"]         = o.value("is_live").toBool();
        r["viewers"]      = 0;   // search/channels liefert keine Zuschauerzahl
        r["title"]        = o.value("title").toString();
        r["game"]         = o.value("game_name").toString();
        r["thumbRaw"]     = o.value("thumbnail_url").toString();
        r["avatar"]       = o.value("thumbnail_url").toString();  // Profilbild
        r["startedAt"]    = o.value("started_at").toString();
        rows.append(r);
    }
    return rows;
}

QList<QVariantMap> TwitchHelix::usersToRows(const QJsonArray &data)
{
    QList<QVariantMap> rows;
    for (const auto &v : data) {
        const QJsonObject o = v.toObject();
        QVariantMap r;
        r["userId"]  = o.value("id").toString();
        r["login"]   = o.value("login").toString().toLower();
        r["name"]    = o.value("display_name").toString();
        r["avatar"]  = o.value("profile_image_url").toString();
        r["desc"]    = o.value("description").toString();
        rows.append(r);
    }
    return rows;
}
