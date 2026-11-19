// Nutzer-Menü im Chat: Aktionen je nach eigener Rolle im Kanal (Zuschauer, VIP, Moderator, Streamer).
// Alles läuft über die offizielle Helix-API mit dem eigenen Token; das Ergebnis erscheint als
// Systemzeile im Chat. Twitch prüft die Rechte selbst – das Menü zeigt nur, was sinnvoll ist.
#include "TwitchChatController.h"
#include "WhisperService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {
QString durationText(int seconds)
{
    if (seconds % 86400 == 0) return QStringLiteral("%1 Std.").arg(seconds / 3600);
    if (seconds % 3600 == 0) return QStringLiteral("%1 Std.").arg(seconds / 3600);
    if (seconds % 60 == 0) return QStringLiteral("%1 Min.").arg(seconds / 60);
    return QStringLiteral("%1 Sek.").arg(seconds);
}
}

static QString helixError(int status, const QByteArray &body);   // weiter unten

QString TwitchChatController::myUserId() const
{
    return m_whispers && !m_whispers->userId().isEmpty() ? m_whispers->userId() : m_testUserId;
}

QString TwitchChatController::myRole() const
{
    if (m_token.isEmpty()) return QStringLiteral("anonymous");
    if (!m_channel.isEmpty() && m_channel.compare(m_user, Qt::CaseInsensitive) == 0) return QStringLiteral("broadcaster");
    if (m_myBadges.contains(QStringLiteral("broadcaster"))) return QStringLiteral("broadcaster");
    if (m_myBadges.contains(QStringLiteral("moderator"))) return QStringLiteral("moderator");
    if (m_myBadges.contains(QStringLiteral("vip"))) return QStringLiteral("vip");
    return QStringLiteral("user");
}

void TwitchChatController::setMyBadges(const QStringList &badges)
{
    if (badges == m_myBadges) return;
    m_myBadges = badges;
    emit roleChanged();
    ensureAutomodSubscriptions();
}

// ------------------------------------------------------------ AutoMod
// Twitch meldet zurückgehaltene Nachrichten nur Moderatoren und dem Streamer – über EventSub
// (automod.message.hold / .update, Recht moderator:manage:automod) auf der Postfach-Verbindung.
void TwitchChatController::ensureAutomodSubscriptions()
{
    const QString role = myRole();
    const bool wanted = (role == QLatin1String("moderator") || role == QLatin1String("broadcaster"))
                        && !m_roomId.isEmpty() && !myUserId().isEmpty() && !m_whispers->sessionId().isEmpty();
    const QString session = m_whispers->sessionId();
    if (wanted && m_automodRoom == m_roomId && m_automodSession == session) return;   // schon angemeldet
    for (const QString &id : std::as_const(m_automodSubs)) m_whispers->unsubscribeEvent(id);
    m_automodSubs.clear();
    m_automodRoom.clear();
    m_automodSession.clear();
    if (!wanted) return;
    m_automodRoom = m_roomId;
    m_automodSession = session;
    const QJsonObject condition{{QStringLiteral("broadcaster_user_id"), m_roomId},
                                {QStringLiteral("moderator_user_id"), myUserId()}};
    // AutoMod (v2) und neue Follower (v2, Recht moderator:read:followers) – beides nur für Moderatoren
    for (const QString &type : {QStringLiteral("automod.message.hold"), QStringLiteral("automod.message.update"),
                                QStringLiteral("channel.follow")}) {
        m_whispers->subscribeEvent(type, QStringLiteral("2"), condition, [this, type](bool ok, const QString &id, const QString &error) {
            if (ok && !id.isEmpty()) m_automodSubs << id;
            if (!ok && m_host)
                m_host->log(m_id, QStringLiteral("warning"), QStringLiteral("AutoMod (%1): %2").arg(type, error));
        });
    }
}

void TwitchChatController::handleAutomodEvent(const QString &type, const QJsonObject &event)
{
    if (event.value(QStringLiteral("broadcaster_user_id")).toString() != m_roomId) return;   // anderer Kanal
    const QString id = event.value(QStringLiteral("message_id")).toString();
    if (id.isEmpty()) return;
    if (type == QLatin1String("automod.message.hold")) {
        const QString text = event.value(QStringLiteral("message")).toObject().value(QStringLiteral("text")).toString();
        // Grund: AutoMod-Kategorie (v2: automod.category) oder gesperrter Begriff
        QString reason = event.value(QStringLiteral("automod")).toObject().value(QStringLiteral("category")).toString();
        if (reason.isEmpty()) reason = event.value(QStringLiteral("category")).toString();
        if (event.value(QStringLiteral("reason")).toString() == QLatin1String("blocked_term")) reason = QStringLiteral("blocked_term");
        addMessage(QStringLiteral("automod"), event.value(QStringLiteral("user_login")).toString(),
                   event.value(QStringLiteral("user_name")).toString(), QString(), QStringList(), text, QString(),
                   QVariantMap{{QStringLiteral("userId"), event.value(QStringLiteral("user_id")).toString()},
                               {QStringLiteral("automodId"), id},
                               {QStringLiteral("automodStatus"), QStringLiteral("pending")},
                               {QStringLiteral("automodReason"), reason}});
    } else if (type == QLatin1String("automod.message.update")) {
        const QString status = event.value(QStringLiteral("status")).toString().toLower();   // approved | denied | expired
        setAutomodStatus(id, status, event.value(QStringLiteral("moderator_user_name")).toString());
    }
}

void TwitchChatController::handleFollowEvent(const QJsonObject &event)
{
    if (event.value(QStringLiteral("broadcaster_user_id")).toString() != m_roomId) return;
    const QString name = event.value(QStringLiteral("user_name")).toString();
    if (name.isEmpty()) return;
    addMessage(QStringLiteral("system"), QString(), QStringLiteral("System"), QString(), QStringList(),
               QStringLiteral("%1 folgt jetzt dem Kanal").arg(name), QString(),
               QVariantMap{{QStringLiteral("userNotice"), true}, {QStringLiteral("noticeId"), QStringLiteral("follow")}});
}

void TwitchChatController::setAutomodStatus(const QString &automodId, const QString &status, const QString &by)
{
    for (int i = int(m_history.size()) - 1; i >= 0; --i) {
        if (m_history[i].value(QStringLiteral("automodId")).toString() != automodId) continue;
        m_history[i].insert(QStringLiteral("automodStatus"), status);
        if (!by.isEmpty()) m_history[i].insert(QStringLiteral("automodBy"), by);
        emit messageUpdated(i);
        return;
    }
}

void TwitchChatController::automodAction(const QString &automodId, bool allow)
{
    const QString me = myUserId();
    if (automodId.isEmpty() || me.isEmpty()) return;
    const QJsonObject body{{QStringLiteral("user_id"), me}, {QStringLiteral("msg_id"), automodId},
                           {QStringLiteral("action"), allow ? QStringLiteral("ALLOW") : QStringLiteral("DENY")}};
    helix("POST", QStringLiteral("/moderation/automod/message"), QUrlQuery(), QJsonDocument(body).toJson(QJsonDocument::Compact),
          [this, automodId, allow](int status, const QByteArray &response) {
        if (status >= 200 && status < 300) {
            setAutomodStatus(automodId, allow ? QStringLiteral("approved") : QStringLiteral("denied"), m_ownDisplayName.isEmpty() ? m_user : m_ownDisplayName);
        } else {
            systemLine(QStringLiteral("✗ AutoMod: %1").arg(helixError(status, response)));
        }
    });
}

void TwitchChatController::setTestIdentity(const QString &login, const QString &userId)
{
    m_user = login;
    m_token = QStringLiteral("test");
    m_testUserId = userId;
    emit settingsChanged();
    emit roleChanged();
}

bool TwitchChatController::isBlocked(const QString &login) const
{
    return m_blockedLogins.contains(login.toLower());
}

void TwitchChatController::systemLine(const QString &text)
{
    addMessage(QStringLiteral("system"), QString(), QStringLiteral("tmi"), QString(), QStringList(), text);
}

// Ein Helix-Aufruf. In Tests übernimmt m_helixHook die Antwort.
void TwitchChatController::helix(const QByteArray &method, const QString &path, const QUrlQuery &query,
                                 const QByteArray &body, const std::function<void(int, const QByteArray &)> &done)
{
    QUrl url(QStringLiteral("https://api.twitch.tv/helix") + path);
    url.setQuery(query);
    if (m_helixHook) {
        m_helixHook(QString::fromLatin1(method), url, body, done);
        return;
    }
    if (!m_whispers || m_whispers->clientId().isEmpty() || m_token.isEmpty()) {
        done(0, QByteArray("{\"message\":\"Nicht angemeldet oder Token noch nicht geprüft\"}"));
        return;
    }
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QNetworkRequest req{url};
    req.setRawHeader("Client-Id", m_whispers->clientId().toUtf8());
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    if (!body.isEmpty()) req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_net->sendCustomRequest(req, method, body);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        done(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll());
    });
}

static QString helixError(int status, const QByteArray &body)
{
    const QString message = QJsonDocument::fromJson(body).object().value(QStringLiteral("message")).toString();
    if (status == 401) return QStringLiteral("Dem Token fehlt das Recht dafür – unter „Login“ neu „Mit Twitch anmelden“");
    if (status == 403) return message.isEmpty() ? QStringLiteral("Keine Berechtigung in diesem Kanal") : message;
    if (status == 429) return QStringLiteral("Twitch-Limit erreicht, kurz warten");
    if (!message.isEmpty()) return message;
    return status > 0 ? QStringLiteral("Twitch meldet Fehler %1").arg(status) : QStringLiteral("Keine Verbindung zu Twitch");
}

void TwitchChatController::userAction(const QString &action, const QVariantMap &target)
{
    const QString login = target.value(QStringLiteral("login")).toString().toLower();
    const QString name = target.value(QStringLiteral("name")).toString().isEmpty() ? login : target.value(QStringLiteral("name")).toString();
    QString userId = target.value(QStringLiteral("userId")).toString();
    if (login.isEmpty() && userId.isEmpty()) return;

    // Ohne ID erst nachschlagen (z. B. aus der Zuschauerliste)
    if (userId.isEmpty()) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("login"), login);
        helix("GET", QStringLiteral("/users"), q, QByteArray(), [this, action, target, name](int status, const QByteArray &body) {
            const QJsonArray users = QJsonDocument::fromJson(body).object().value(QStringLiteral("data")).toArray();
            if (status != 200 || users.isEmpty()) {
                systemLine(QStringLiteral("✗ %1: Konto nicht gefunden (%2)").arg(name, helixError(status, body)));
                return;
            }
            QVariantMap resolved = target;
            resolved.insert(QStringLiteral("userId"), users.first().toObject().value(QStringLiteral("id")).toString());
            userAction(action, resolved);
        });
        return;
    }

    const QString room = m_roomId;
    const QString me = myUserId();
    const QString reason = target.value(QStringLiteral("reason")).toString().trimmed().left(500);
    const auto finish = [this, action, name, login, userId](const QString &success) {
        return [this, action, name, login, userId, success](int status, const QByteArray &body) {
            const bool ok = status >= 200 && status < 300;
            if (ok && action == QLatin1String("block")) { m_blockedLogins.insert(login); m_blockedIds.insert(userId); }
            if (ok && action == QLatin1String("unblock")) { m_blockedLogins.remove(login); m_blockedIds.remove(userId); }
            if (ok && action.endsWith(QLatin1String("block"))) { ++m_blockedRevision; emit blockedChanged(); }
            systemLine(ok ? QStringLiteral("✓ ") + success : QStringLiteral("✗ %1: %2").arg(name, helixError(status, body)));
            emit userActionDone(action, ok);
        };
    };
    const auto needRoom = [&]() {
        if (room.isEmpty() || me.isEmpty()) {
            systemLine(QStringLiteral("✗ Noch nicht bereit: Kanal oder eigenes Konto unbekannt – kurz nach dem Verbinden erneut versuchen."));
            return false;
        }
        return true;
    };

    QUrlQuery q;
    if (action == QLatin1String("timeout") || action == QLatin1String("purge") || action == QLatin1String("ban")) {
        if (!needRoom()) return;
        q.addQueryItem(QStringLiteral("broadcaster_id"), room);
        q.addQueryItem(QStringLiteral("moderator_id"), me);
        QJsonObject data{{QStringLiteral("user_id"), userId}};
        int seconds = 0;
        if (action != QLatin1String("ban")) {
            seconds = action == QLatin1String("purge") ? 1 : qBound(1, target.value(QStringLiteral("duration")).toInt(), 1209600);
            data.insert(QStringLiteral("duration"), seconds);
        }
        if (!reason.isEmpty()) data.insert(QStringLiteral("reason"), reason);
        const QString done = action == QLatin1String("ban") ? QStringLiteral("%1 gebannt").arg(name)
                           : action == QLatin1String("purge") ? QStringLiteral("Nachrichten von %1 entfernt").arg(name)
                           : QStringLiteral("%1 für %2 stummgeschaltet").arg(name, durationText(seconds));
        helix("POST", QStringLiteral("/moderation/bans"), q,
              QJsonDocument(QJsonObject{{QStringLiteral("data"), data}}).toJson(QJsonDocument::Compact), finish(done));
    } else if (action == QLatin1String("unban")) {
        if (!needRoom()) return;
        q.addQueryItem(QStringLiteral("broadcaster_id"), room);
        q.addQueryItem(QStringLiteral("moderator_id"), me);
        q.addQueryItem(QStringLiteral("user_id"), userId);
        helix("DELETE", QStringLiteral("/moderation/bans"), q, QByteArray(), finish(QStringLiteral("Bann bzw. Timeout von %1 aufgehoben").arg(name)));
    } else if (action == QLatin1String("delete")) {
        if (!needRoom()) return;
        const QString msgId = target.value(QStringLiteral("msgId")).toString();
        if (msgId.isEmpty()) return;
        q.addQueryItem(QStringLiteral("broadcaster_id"), room);
        q.addQueryItem(QStringLiteral("moderator_id"), me);
        q.addQueryItem(QStringLiteral("message_id"), msgId);
        helix("DELETE", QStringLiteral("/moderation/chat"), q, QByteArray(), finish(QStringLiteral("Nachricht von %1 gelöscht").arg(name)));
    } else if (action == QLatin1String("warn")) {
        if (!needRoom() || reason.isEmpty()) return;
        q.addQueryItem(QStringLiteral("broadcaster_id"), room);
        q.addQueryItem(QStringLiteral("moderator_id"), me);
        const QJsonObject data{{QStringLiteral("user_id"), userId}, {QStringLiteral("reason"), reason}};
        helix("POST", QStringLiteral("/moderation/warnings"), q,
              QJsonDocument(QJsonObject{{QStringLiteral("data"), data}}).toJson(QJsonDocument::Compact),
              finish(QStringLiteral("%1 verwarnt").arg(name)));
    } else if (action == QLatin1String("shoutout")) {
        if (!needRoom()) return;
        q.addQueryItem(QStringLiteral("from_broadcaster_id"), room);
        q.addQueryItem(QStringLiteral("to_broadcaster_id"), userId);
        q.addQueryItem(QStringLiteral("moderator_id"), me);
        helix("POST", QStringLiteral("/chat/shoutouts"), q, QByteArray(), finish(QStringLiteral("Shoutout für %1 gesendet").arg(name)));
    } else if (action == QLatin1String("mod") || action == QLatin1String("unmod")) {
        if (!needRoom()) return;
        q.addQueryItem(QStringLiteral("broadcaster_id"), room);
        q.addQueryItem(QStringLiteral("user_id"), userId);
        const bool add = action == QLatin1String("mod");
        helix(add ? "POST" : "DELETE", QStringLiteral("/moderation/moderators"), q, QByteArray(),
              finish(add ? QStringLiteral("%1 ist jetzt Moderator").arg(name) : QStringLiteral("%1 ist kein Moderator mehr").arg(name)));
    } else if (action == QLatin1String("vip") || action == QLatin1String("unvip")) {
        if (!needRoom()) return;
        q.addQueryItem(QStringLiteral("broadcaster_id"), room);
        q.addQueryItem(QStringLiteral("user_id"), userId);
        const bool add = action == QLatin1String("vip");
        helix(add ? "POST" : "DELETE", QStringLiteral("/channels/vips"), q, QByteArray(),
              finish(add ? QStringLiteral("%1 ist jetzt VIP").arg(name) : QStringLiteral("%1 ist kein VIP mehr").arg(name)));
    } else if (action == QLatin1String("raid")) {
        if (!needRoom()) return;
        q.addQueryItem(QStringLiteral("from_broadcaster_id"), room);
        q.addQueryItem(QStringLiteral("to_broadcaster_id"), userId);
        helix("POST", QStringLiteral("/raids"), q, QByteArray(),
              finish(QStringLiteral("Raid zu %1 gestartet – Twitch zählt jetzt herunter").arg(name)));
    } else if (action == QLatin1String("block") || action == QLatin1String("unblock")) {
        if (me.isEmpty()) { systemLine(QStringLiteral("✗ Nicht angemeldet")); return; }
        q.addQueryItem(QStringLiteral("target_user_id"), userId);
        const bool add = action == QLatin1String("block");
        helix(add ? "PUT" : "DELETE", QStringLiteral("/users/blocks"), q, QByteArray(),
              finish(add ? QStringLiteral("%1 blockiert – Nachrichten werden ausgeblendet").arg(name)
                         : QStringLiteral("Blockierung von %1 aufgehoben").arg(name)));
    }
}

// Kontoalter für die Kopfzeile des Menüs
void TwitchChatController::requestUserInfo(const QString &userId, const QString &login)
{
    // Ohne Login: Kontoalter ist öffentlich und kommt über gql.twitch.tv (ohne Token).
    if (m_token.isEmpty() && !m_helixHook) {
        static const QRegularExpression valid(QStringLiteral("^[a-z0-9_]{2,25}$"));
        const QString key = login.toLower();
        if (!valid.match(key).hasMatch()) return;
        if (!m_net) m_net = new QNetworkAccessManager(this);
        QNetworkRequest req{QUrl(QStringLiteral("https://gql.twitch.tv/gql"))};
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        req.setRawHeader("Client-ID", QByteArray("kimne78kx3ncx6brgo4mv6wki5h1ko"));   // öffentliche Web-Client-ID
        QJsonObject body;
        body.insert(QStringLiteral("query"), QStringLiteral("{ user(login: \"%1\") { id createdAt } }").arg(key));
        QNetworkReply *reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply, key] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) return;
            const QJsonObject u = QJsonDocument::fromJson(reply->readAll()).object()
                                      .value(QStringLiteral("data")).toObject().value(QStringLiteral("user")).toObject();
            if (u.isEmpty()) return;
            const QDateTime created = QDateTime::fromString(u.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
            emit userInfo(key, QVariantMap{{QStringLiteral("createdAt"), created.isValid() ? created.toMSecsSinceEpoch() : qint64(0)},
                                           {QStringLiteral("userId"), u.value(QStringLiteral("id")).toString()}});
        });
        return;
    }
    QUrlQuery q;
    if (!userId.isEmpty()) q.addQueryItem(QStringLiteral("id"), userId);
    else q.addQueryItem(QStringLiteral("login"), login.toLower());
    helix("GET", QStringLiteral("/users"), q, QByteArray(), [this, login](int status, const QByteArray &body) {
        const QJsonArray users = QJsonDocument::fromJson(body).object().value(QStringLiteral("data")).toArray();
        if (status != 200 || users.isEmpty()) return;
        const QJsonObject u = users.first().toObject();
        const QDateTime created = QDateTime::fromString(u.value(QStringLiteral("created_at")).toString(), Qt::ISODate);
        emit userInfo(login.toLower(), QVariantMap{{QStringLiteral("createdAt"), created.isValid() ? created.toMSecsSinceEpoch() : qint64(0)},
                                                   {QStringLiteral("userId"), u.value(QStringLiteral("id")).toString()}});
    });
}

// Blockierte Konten des eigenen Accounts: deren Nachrichten blendet der Chat aus.
void TwitchChatController::fetchBlocked(const QString &cursor)
{
    const QString me = myUserId();
    if (me.isEmpty()) return;
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("broadcaster_id"), me);
    q.addQueryItem(QStringLiteral("first"), QStringLiteral("100"));
    if (!cursor.isEmpty()) q.addQueryItem(QStringLiteral("after"), cursor);
    helix("GET", QStringLiteral("/users/blocks"), q, QByteArray(), [this, cursor](int status, const QByteArray &body) {
        if (status != 200) return;
        if (cursor.isEmpty()) { m_blockedIds.clear(); m_blockedLogins.clear(); }
        const QJsonObject root = QJsonDocument::fromJson(body).object();
        for (const QJsonValue &v : root.value(QStringLiteral("data")).toArray()) {
            m_blockedIds.insert(v.toObject().value(QStringLiteral("user_id")).toString());
            m_blockedLogins.insert(v.toObject().value(QStringLiteral("user_login")).toString().toLower());
        }
        const QString next = root.value(QStringLiteral("pagination")).toObject().value(QStringLiteral("cursor")).toString();
        if (!next.isEmpty() && m_blockedIds.size() < 5000) fetchBlocked(next);
        ++m_blockedRevision;
        emit blockedChanged();
    });
}
