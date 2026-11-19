#include "WhisperService.h"

#include "TokenVault.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <algorithm>

namespace {
constexpr int kMaxMessagesPerConversation = 500;
constexpr int kMaxConversations = 200;
constexpr int kMaxLength = 500;                  // Twitch: 500 Zeichen an neue Empfänger
const QString kEventSubHost = QStringLiteral("eventsub.wss.twitch.tv");

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }
}

WhisperService::WhisperService(const QString &storePath, const QByteArray &entropy, QObject *parent)
    : QObject(parent), m_store(std::make_unique<TokenVault>(storePath, entropy)), m_net(new QNetworkAccessManager(this))
{
    m_sender = [this](const QString &to, const QString &text, const std::function<void(bool, const QString &)> &done) {
        helixSend(to, text, done);
    };
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1000);
    connect(&m_saveTimer, &QTimer::timeout, this, &WhisperService::flush);
    m_keepalive.setSingleShot(true);
    connect(&m_keepalive, &QTimer::timeout, this, [this] {
        m_ws.close();                            // Twitch schweigt zu lange → neu verbinden
        reconnectLater();
    });
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] { connectEventSub(kEventSubHost, QStringLiteral("/ws")); });
    connect(&m_ws, &MiniWebSocket::textMessage, this, &WhisperService::handleEventSubText);
    connect(&m_ws, &MiniWebSocket::closed, this, [this](const QString &) { reconnectLater(); });
    connect(&m_ws, &MiniWebSocket::failed, this, [this](const QString &why) {
        setState(QStringLiteral("error"), why);
        reconnectLater();
    });
    load();
}

WhisperService::~WhisperService()
{
    if (m_saveTimer.isActive()) flush();
}

// ------------------------------------------------------------ Zustand / Anmeldung
void WhisperService::setState(const QString &state, const QString &detail)
{
    if (m_state == state && m_stateDetail == detail) return;
    m_state = state;
    m_stateDetail = detail;
    emit stateChanged();
}

bool WhisperService::canSend() const
{
    return !m_token.isEmpty() && !m_userId.isEmpty() && m_scopes.contains(QStringLiteral("user:manage:whispers"));
}

void WhisperService::setToken(const QString &token)
{
    if (token == m_token) return;
    ++m_generation;
    m_token = token;
    m_clientId.clear();
    m_userId.clear();
    m_login.clear();
    m_scopes.clear();
    m_retry.stop();
    m_keepalive.stop();
    m_ws.close();
    m_sessionId.clear();
    if (m_token.isEmpty()) {
        setState(QStringLiteral("nologin"));
        return;
    }
    validate();
}

// Client-ID, Konto und Rechte: Helix und EventSub verlangen die Client-ID, für die der Token gilt.
void WhisperService::validate()
{
    setState(QStringLiteral("checking"));
    QNetworkRequest req{QUrl(QStringLiteral("https://id.twitch.tv/oauth2/validate"))};
    req.setRawHeader("Authorization", QByteArray("OAuth ") + m_token.toUtf8());
    QNetworkReply *reply = m_net->get(req);
    const quint64 generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation) return;
        const QJsonObject info = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError || info.value(QStringLiteral("client_id")).toString().isEmpty()) {
            setState(QStringLiteral("error"), QStringLiteral("Token nicht bestätigt: %1").arg(reply->errorString()));
            return;
        }
        m_clientId = info.value(QStringLiteral("client_id")).toString();
        m_userId = info.value(QStringLiteral("user_id")).toString();
        m_login = info.value(QStringLiteral("login")).toString();
        m_scopes.clear();
        const QJsonArray scopes = info.value(QStringLiteral("scopes")).toArray();
        for (const QJsonValue &s : scopes) m_scopes << s.toString();
        const bool canRead = hasScope(QStringLiteral("user:read:whispers")) || hasScope(QStringLiteral("user:manage:whispers"));
        if (!canRead) {
            setState(QStringLiteral("scope"));
            return;
        }
        m_backoffMs = 5000;
        connectEventSub(kEventSubHost, QStringLiteral("/ws"));
    });
}

void WhisperService::connectEventSub(const QString &host, const QString &path)
{
    if (m_token.isEmpty() || m_clientId.isEmpty()) return;
    setState(QStringLiteral("connecting"));
    m_ws.open(host, 443, path);
}

void WhisperService::reconnectLater()
{
    m_keepalive.stop();
    if (m_token.isEmpty() || m_clientId.isEmpty() || m_state == QLatin1String("scope")) return;
    if (m_retry.isActive()) return;
    m_retry.start(m_backoffMs);
    m_backoffMs = std::min(m_backoffMs * 2, 120000);
}

void WhisperService::handleEventSubText(const QString &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json.toUtf8()).object();
    const QJsonObject metadata = root.value(QStringLiteral("metadata")).toObject();
    const QJsonObject payload = root.value(QStringLiteral("payload")).toObject();
    const QString type = metadata.value(QStringLiteral("message_type")).toString();

    const int keepalive = payload.value(QStringLiteral("session")).toObject()
                              .value(QStringLiteral("keepalive_timeout_seconds")).toInt(0);
    if (keepalive > 0) m_keepalive.setInterval((keepalive + 10) * 1000);
    if (m_keepalive.interval() > 0 && m_ws.isOpen()) m_keepalive.start();

    if (type == QLatin1String("session_welcome")) {
        m_backoffMs = 5000;
        m_sessionId = payload.value(QStringLiteral("session")).toObject().value(QStringLiteral("id")).toString();
        subscribe(m_sessionId);
        emit sessionReady();
    } else if (type == QLatin1String("session_reconnect")) {
        // Twitch zieht um: Das Abo wandert zur neuen Adresse mit.
        const QUrl url(payload.value(QStringLiteral("session")).toObject().value(QStringLiteral("reconnect_url")).toString());
        if (url.isValid() && url.host().endsWith(QLatin1String(".twitch.tv"))) {
            m_ws.close();
            m_retry.stop();
            connectEventSub(url.host(), url.path(QUrl::FullyEncoded) + (url.hasQuery() ? QStringLiteral("?") + url.query(QUrl::FullyEncoded) : QString()));
        }
    } else if (type == QLatin1String("notification")) {
        const QJsonObject event = payload.value(QStringLiteral("event")).toObject();
        const QString subType = payload.value(QStringLiteral("subscription")).toObject().value(QStringLiteral("type")).toString();
        if (subType == QLatin1String("user.whisper.message")) {
            receiveWhisper(event.value(QStringLiteral("from_user_login")).toString(),
                           event.value(QStringLiteral("from_user_name")).toString(),
                           event.value(QStringLiteral("whisper")).toObject().value(QStringLiteral("text")).toString(),
                           event.value(QStringLiteral("whisper_id")).toString());
        } else if (!subType.isEmpty()) {
            emit eventNotification(subType, event);
        }
    } else if (type == QLatin1String("revocation")) {
        setState(QStringLiteral("scope"), QStringLiteral("Twitch hat das Abo widerrufen"));
    }
}

void WhisperService::subscribe(const QString &sessionId)
{
    if (sessionId.isEmpty()) return;
    QNetworkRequest req{QUrl(QStringLiteral("https://api.twitch.tv/helix/eventsub/subscriptions"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Client-Id", m_clientId.toUtf8());
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    const QJsonObject body{
        {QStringLiteral("type"), QStringLiteral("user.whisper.message")},
        {QStringLiteral("version"), QStringLiteral("1")},
        {QStringLiteral("condition"), QJsonObject{{QStringLiteral("user_id"), m_userId}}},
        {QStringLiteral("transport"), QJsonObject{{QStringLiteral("method"), QStringLiteral("websocket")},
                                                  {QStringLiteral("session_id"), sessionId}}}};
    QNetworkReply *reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    const quint64 generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 202 || status == 409) setState(QStringLiteral("ready"));
        else if (status == 401 || status == 403) setState(QStringLiteral("scope"), explainError(status, reply->readAll()));
        else setState(QStringLiteral("error"), explainError(status, reply->readAll()));
    });
}

void WhisperService::subscribeEvent(const QString &type, const QString &version, const QJsonObject &condition,
                                    const std::function<void(bool, const QString &, const QString &)> &done)
{
    if (m_sessionId.isEmpty() || m_clientId.isEmpty()) { done(false, QString(), QStringLiteral("Keine EventSub-Sitzung")); return; }
    QNetworkRequest req{QUrl(QStringLiteral("https://api.twitch.tv/helix/eventsub/subscriptions"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Client-Id", m_clientId.toUtf8());
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    const QJsonObject body{{QStringLiteral("type"), type}, {QStringLiteral("version"), version},
                           {QStringLiteral("condition"), condition},
                           {QStringLiteral("transport"), QJsonObject{{QStringLiteral("method"), QStringLiteral("websocket")},
                                                                     {QStringLiteral("session_id"), m_sessionId}}}};
    QNetworkReply *reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        const QString id = QJsonDocument::fromJson(data).object().value(QStringLiteral("data")).toArray()
                               .first().toObject().value(QStringLiteral("id")).toString();
        if (status == 202 || status == 409) done(true, id, QString());
        else done(false, QString(), explainError(status, data));
    });
}

void WhisperService::unsubscribeEvent(const QString &subscriptionId)
{
    if (subscriptionId.isEmpty() || m_clientId.isEmpty()) return;
    QUrl url(QStringLiteral("https://api.twitch.tv/helix/eventsub/subscriptions"));
    url.setQuery(QUrlQuery{{QStringLiteral("id"), subscriptionId}});
    QNetworkRequest req{url};
    req.setRawHeader("Client-Id", m_clientId.toUtf8());
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    QNetworkReply *reply = m_net->sendCustomRequest(req, "DELETE");
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

// ------------------------------------------------------------ Senden über Helix
QString WhisperService::explainError(int status, const QByteArray &body)
{
    const QString message = QJsonDocument::fromJson(body).object().value(QStringLiteral("message")).toString();
    if (status == 401) return QStringLiteral("Dem Token fehlt das Recht dafür – unter „Login“ neu „Mit Twitch anmelden“");
    if (status == 429) return QStringLiteral("Twitch-Limit erreicht (höchstens 40 Empfänger pro Tag, 100 Nachrichten pro Minute)");
    if (message.contains(QLatin1String("phone"), Qt::CaseInsensitive))
        return QStringLiteral("Twitch verlangt eine verifizierte Telefonnummer im Konto");
    if (status == 404) return QStringLiteral("Konto nicht gefunden");
    if (!message.isEmpty()) return message;
    return status > 0 ? QStringLiteral("Twitch meldet Fehler %1").arg(status) : QStringLiteral("Keine Verbindung zu Twitch");
}

void WhisperService::helixSend(const QString &toLogin, const QString &text, const std::function<void(bool, const QString &)> &done)
{
    if (!canSend()) {
        done(false, m_token.isEmpty() ? QStringLiteral("Nicht angemeldet")
                                      : QStringLiteral("Dem Token fehlt das Recht user:manage:whispers – unter „Login“ neu „Mit Twitch anmelden“"));
        return;
    }
    const QString known = m_userIds.value(toLogin);
    if (!known.isEmpty()) {
        postWhisper(known, text, done);
        return;
    }
    QUrl url(QStringLiteral("https://api.twitch.tv/helix/users"));
    url.setQuery(QUrlQuery{{QStringLiteral("login"), toLogin}});
    QNetworkRequest req{url};
    req.setRawHeader("Client-Id", m_clientId.toUtf8());
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    QNetworkReply *reply = m_net->get(req);
    const quint64 generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, toLogin, text, done] {
        reply->deleteLater();
        if (generation != m_generation) { done(false, QStringLiteral("Abgemeldet")); return; }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QJsonArray users = QJsonDocument::fromJson(body).object().value(QStringLiteral("data")).toArray();
        if (status != 200 || users.isEmpty()) {
            done(false, status == 200 ? QStringLiteral("Konto „%1“ gibt es nicht").arg(toLogin) : explainError(status, body));
            return;
        }
        const QString id = users.first().toObject().value(QStringLiteral("id")).toString();
        m_userIds.insert(toLogin, id);
        scheduleSave();
        postWhisper(id, text, done);
    });
}

void WhisperService::postWhisper(const QString &toId, const QString &text, const std::function<void(bool, const QString &)> &done)
{
    QUrl url(QStringLiteral("https://api.twitch.tv/helix/whispers"));
    url.setQuery(QUrlQuery{{QStringLiteral("from_user_id"), m_userId}, {QStringLiteral("to_user_id"), toId}});
    QNetworkRequest req{url};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Client-Id", m_clientId.toUtf8());
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    QNetworkReply *reply = m_net->post(req, QJsonDocument(QJsonObject{{QStringLiteral("message"), text}}).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 204) done(true, QString());
        else done(false, explainError(status, reply->readAll()));
    });
}

// ------------------------------------------------------------ Gespräche
bool WhisperService::validLogin(const QString &login)
{
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9_]{2,25}$"));
    return valid.match(login).hasMatch();
}

WhisperService::Conversation *WhisperService::find(const QString &id)
{
    for (Conversation &c : m_conversations)
        if (c.id == id) return &c;
    return nullptr;
}

const WhisperService::Conversation *WhisperService::find(const QString &id) const
{
    for (const Conversation &c : m_conversations)
        if (c.id == id) return &c;
    return nullptr;
}

int WhisperService::unreadTotal() const
{
    int total = 0;
    for (const Conversation &c : m_conversations) total += c.unread;
    return total;
}

QVariantMap WhisperService::toVariant(const Message &m)
{
    return QVariantMap{{QStringLiteral("from"), m.from}, {QStringLiteral("name"), m.name},
                       {QStringLiteral("text"), m.text}, {QStringLiteral("ts"), m.ts},
                       {QStringLiteral("outgoing"), m.outgoing}, {QStringLiteral("status"), m.status},
                       {QStringLiteral("error"), m.error}};
}

QVariantMap WhisperService::conversation(const QString &id) const
{
    const Conversation *c = find(id);
    if (!c) return {};
    QVariantList members;
    for (const Member &m : c->members)
        members << QVariantMap{{QStringLiteral("login"), m.login}, {QStringLiteral("name"), m.name}};
    const Message *last = c->messages.isEmpty() ? nullptr : &c->messages.last();
    return QVariantMap{{QStringLiteral("id"), c->id}, {QStringLiteral("title"), c->title},
                       {QStringLiteral("isGroup"), c->isGroup}, {QStringLiteral("members"), members},
                       {QStringLiteral("lastText"), last ? last->text : QString()},
                       {QStringLiteral("lastFrom"), last ? (last->outgoing ? QString() : last->name) : QString()},
                       {QStringLiteral("lastOutgoing"), last ? last->outgoing : false},
                       {QStringLiteral("lastTs"), last ? last->ts : qint64(0)},
                       {QStringLiteral("unread"), c->unread}};
}

QVariantList WhisperService::conversations() const
{
    // Neueste zuerst; bei gleicher Zeit (selbe Millisekunde) das zuletzt angelegte Gespräch.
    QVector<const Conversation *> sorted;
    for (auto it = m_conversations.crbegin(); it != m_conversations.crend(); ++it) sorted << &*it;
    const auto lastTs = [](const Conversation *c) { return c->messages.isEmpty() ? qint64(0) : c->messages.last().ts; };
    std::stable_sort(sorted.begin(), sorted.end(), [&](const Conversation *a, const Conversation *b) { return lastTs(a) > lastTs(b); });
    QVariantList out;
    for (const Conversation *c : sorted) out << conversation(c->id);
    return out;
}

QVariantList WhisperService::messages(const QString &id) const
{
    QVariantList out;
    if (const Conversation *c = find(id))
        for (const Message &m : c->messages) out << toVariant(m);
    return out;
}

void WhisperService::markRead(const QString &id)
{
    Conversation *c = find(id);
    if (!c || c->unread == 0) return;
    c->unread = 0;
    scheduleSave();
    emit changed();
}

QString WhisperService::openDirect(const QString &rawLogin, const QString &displayName)
{
    const QString login = rawLogin.trimmed().toLower().remove(QLatin1Char('@'));
    if (!validLogin(login)) return QString();
    const QString id = QStringLiteral("u:") + login;
    if (Conversation *c = find(id)) {
        if (!displayName.isEmpty() && c->title != displayName) { c->title = displayName; c->members[0].name = displayName; scheduleSave(); emit changed(); }
        return id;
    }
    Conversation c;
    c.id = id;
    c.title = displayName.isEmpty() ? login : displayName;
    c.members << Member{login, c.title};
    m_conversations << c;
    scheduleSave();
    emit changed();
    return id;
}

static QVector<QPair<QString, QString>> cleanMembers(const QVariantList &members, const QString &self)
{
    QVector<QPair<QString, QString>> out;
    QSet<QString> seen;
    for (const QVariant &v : members) {
        const QVariantMap m = v.toMap();
        const QString login = (m.isEmpty() ? v.toString() : m.value(QStringLiteral("login")).toString()).trimmed().toLower().remove(QLatin1Char('@'));
        if (!WhisperService::validLogin(login) || login == self || seen.contains(login)) continue;
        seen.insert(login);
        const QString name = m.value(QStringLiteral("name")).toString();
        out << qMakePair(login, name.isEmpty() ? login : name);
    }
    return out;
}

QString WhisperService::createGroup(const QString &title, const QVariantList &members)
{
    const auto clean = cleanMembers(members, m_login);
    if (clean.isEmpty()) return QString();
    Conversation c;
    c.id = QStringLiteral("g:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.isGroup = true;
    c.title = title.trimmed().isEmpty() ? QStringLiteral("Gruppe") : title.trimmed();
    for (const auto &m : clean) c.members << Member{m.first, m.second};
    m_conversations << c;
    scheduleSave();
    emit changed();
    return c.id;
}

void WhisperService::setGroupMembers(const QString &id, const QVariantList &members)
{
    Conversation *c = find(id);
    if (!c || !c->isGroup) return;
    const auto clean = cleanMembers(members, m_login);
    if (clean.isEmpty()) return;
    c->members.clear();
    for (const auto &m : clean) c->members << Member{m.first, m.second};
    for (auto it = m_groupRoute.begin(); it != m_groupRoute.end();) {   // Ausgetretene antworten wieder direkt
        const bool stillMember = std::any_of(c->members.begin(), c->members.end(), [&](const Member &m) { return m.login == it.key(); });
        if (it.value() == id && !stillMember) it = m_groupRoute.erase(it);
        else ++it;
    }
    scheduleSave();
    emit changed();
}

void WhisperService::renameGroup(const QString &id, const QString &title)
{
    Conversation *c = find(id);
    if (!c || !c->isGroup || title.trimmed().isEmpty()) return;
    c->title = title.trimmed();
    scheduleSave();
    emit changed();
}

void WhisperService::deleteConversation(const QString &id)
{
    const auto before = m_conversations.size();
    m_conversations.erase(std::remove_if(m_conversations.begin(), m_conversations.end(),
                                         [&](const Conversation &c) { return c.id == id; }), m_conversations.end());
    if (m_conversations.size() == before) return;
    for (auto it = m_groupRoute.begin(); it != m_groupRoute.end();)
        it = it.value() == id ? m_groupRoute.erase(it) : std::next(it);
    scheduleSave();
    emit changed();
}

void WhisperService::appendMessage(Conversation &c, const Message &m)
{
    c.messages << m;
    if (c.messages.size() > kMaxMessagesPerConversation)
        c.messages.remove(0, c.messages.size() - kMaxMessagesPerConversation);
}

bool WhisperService::send(const QString &id, const QString &rawText)
{
    Conversation *c = find(id);
    const QString text = rawText.trimmed();
    if (!c || text.isEmpty() || c->members.isEmpty()) return false;
    Message m;
    m.from = m_login;
    m.name = m_login;
    m.text = text.left(kMaxLength);
    m.ts = now();
    m.outgoing = true;
    m.status = QStringLiteral("sending");
    appendMessage(*c, m);
    const qint64 ts = m.ts;
    if (c->isGroup)
        for (const Member &member : std::as_const(c->members)) m_groupRoute.insert(member.login, id);

    // Eine Nachricht, n Empfänger: erst wenn alle geantwortet haben, steht das Ergebnis fest.
    struct Tally { int pending = 0; QStringList failed; QString reason; };
    auto tally = std::make_shared<Tally>();
    tally->pending = int(c->members.size());
    const QVector<Member> recipients = c->members;
    const bool group = c->isGroup;
    for (const Member &member : recipients) {
        m_sender(member.login, m.text, [this, id, ts, tally, member, group](bool ok, const QString &error) {
            if (!ok) {
                tally->failed << member.name;
                tally->reason = error;
            }
            if (--tally->pending > 0) return;
            Conversation *conv = find(id);
            if (!conv) return;
            for (Message &msg : conv->messages) {
                if (!msg.outgoing || msg.ts != ts) continue;
                msg.status = tally->failed.isEmpty() ? QStringLiteral("sent") : QStringLiteral("failed");
                msg.error = tally->failed.isEmpty() ? QString()
                          : group ? QStringLiteral("Nicht an %1: %2").arg(tally->failed.join(QStringLiteral(", ")), tally->reason)
                                  : tally->reason;
                break;
            }
            scheduleSave();
            emit changed();
        });
    }
    scheduleSave();
    emit changed();
    emit messageAdded(id, true);
    return true;
}

void WhisperService::receiveWhisper(const QString &rawLogin, const QString &fromName, const QString &text, const QString &whisperId)
{
    const QString login = rawLogin.toLower();
    if (!validLogin(login) || text.isEmpty()) return;
    if (!whisperId.isEmpty()) {
        if (m_seenWhispers.contains(whisperId)) return;
        m_seenWhispers << whisperId;
        if (m_seenWhispers.size() > 200) m_seenWhispers.removeFirst();
    }
    const QString name = fromName.isEmpty() ? login : fromName;
    // Antwort eines Gruppenmitglieds: in die Gruppe, an die du ihm zuletzt geschrieben hast.
    QString id = m_groupRoute.value(login);
    Conversation *c = id.isEmpty() ? nullptr : find(id);
    if (!c) {
        id = openDirect(login, name);
        c = find(id);
    }
    if (!c) return;
    for (Member &member : c->members)
        if (member.login == login) member.name = name;
    Message m;
    m.from = login;
    m.name = name;
    m.text = text;
    m.ts = now();
    m.status = QStringLiteral("sent");
    appendMessage(*c, m);
    c->unread += 1;
    scheduleSave();
    emit changed();
    emit messageAdded(id, false);
}

// ------------------------------------------------------------ Ablage (DPAPI)
void WhisperService::scheduleSave() { m_saveTimer.start(); }

void WhisperService::flush()
{
    m_saveTimer.stop();
    QJsonArray conversations;
    QVector<Conversation> kept = m_conversations;
    if (kept.size() > kMaxConversations) kept.remove(0, kept.size() - kMaxConversations);
    for (const Conversation &c : kept) {
        QJsonArray members, messages;
        for (const Member &m : c.members)
            members.append(QJsonObject{{QStringLiteral("login"), m.login}, {QStringLiteral("name"), m.name}});
        for (const Message &m : c.messages) {
            // „wird gesendet“ überlebt keinen Neustart: Was offen blieb, gilt als nicht bestätigt.
            const QString status = m.status == QLatin1String("sending") ? QStringLiteral("failed") : m.status;
            messages.append(QJsonObject{{QStringLiteral("from"), m.from}, {QStringLiteral("name"), m.name},
                                        {QStringLiteral("text"), m.text}, {QStringLiteral("ts"), m.ts},
                                        {QStringLiteral("out"), m.outgoing}, {QStringLiteral("status"), status},
                                        {QStringLiteral("error"), m.status == QLatin1String("sending") ? QStringLiteral("Nicht bestätigt") : m.error}});
        }
        conversations.append(QJsonObject{{QStringLiteral("id"), c.id}, {QStringLiteral("title"), c.title},
                                         {QStringLiteral("group"), c.isGroup}, {QStringLiteral("unread"), c.unread},
                                         {QStringLiteral("members"), members}, {QStringLiteral("messages"), messages}});
    }
    QJsonObject ids, routes;
    for (auto it = m_userIds.constBegin(); it != m_userIds.constEnd(); ++it) ids.insert(it.key(), it.value());
    for (auto it = m_groupRoute.constBegin(); it != m_groupRoute.constEnd(); ++it) routes.insert(it.key(), it.value());
    m_store->save(QJsonObject{{QStringLiteral("conversations"), conversations},
                              {QStringLiteral("userIds"), ids}, {QStringLiteral("routes"), routes}});
}

void WhisperService::load()
{
    const QJsonObject data = m_store->data();
    const QJsonArray conversations = data.value(QStringLiteral("conversations")).toArray();
    for (const QJsonValue &v : conversations) {
        const QJsonObject o = v.toObject();
        Conversation c;
        c.id = o.value(QStringLiteral("id")).toString();
        c.title = o.value(QStringLiteral("title")).toString();
        c.isGroup = o.value(QStringLiteral("group")).toBool();
        c.unread = o.value(QStringLiteral("unread")).toInt();
        for (const QJsonValue &mv : o.value(QStringLiteral("members")).toArray())
            c.members << Member{mv.toObject().value(QStringLiteral("login")).toString(), mv.toObject().value(QStringLiteral("name")).toString()};
        for (const QJsonValue &mv : o.value(QStringLiteral("messages")).toArray()) {
            const QJsonObject mo = mv.toObject();
            Message m;
            m.from = mo.value(QStringLiteral("from")).toString();
            m.name = mo.value(QStringLiteral("name")).toString();
            m.text = mo.value(QStringLiteral("text")).toString();
            m.ts = qint64(mo.value(QStringLiteral("ts")).toDouble());
            m.outgoing = mo.value(QStringLiteral("out")).toBool();
            m.status = mo.value(QStringLiteral("status")).toString();
            m.error = mo.value(QStringLiteral("error")).toString();
            c.messages << m;
        }
        if (!c.id.isEmpty() && !c.members.isEmpty()) m_conversations << c;
    }
    const QJsonObject ids = data.value(QStringLiteral("userIds")).toObject();
    for (auto it = ids.constBegin(); it != ids.constEnd(); ++it) m_userIds.insert(it.key(), it.value().toString());
    const QJsonObject routes = data.value(QStringLiteral("routes")).toObject();
    for (auto it = routes.constBegin(); it != routes.constEnd(); ++it) m_groupRoute.insert(it.key(), it.value().toString());
}
