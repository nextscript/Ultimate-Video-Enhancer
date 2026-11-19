#include "KickChatController.h"
#include "KickAuth.h"
#include "KickEmoteCache.h"
#include "InputEmoteStyler.h"
#include "SpellChecker.h"
#include "LanguageToolClient.h"
#include "TokenVault.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QImageReader>
#include <QMouseEvent>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrl>

#include <QJsonArray>

#include <algorithm>

namespace {

// Kick hat den Pusher-Schlüssel im Oktober 2026 gewechselt. Der erste funktioniert, der zweite
// ist der alte (falls Kick zurückwechselt). Eigene Werte: Einstellungen „pusherKey“/„pusherCluster“.
const char *const kPusherKeys[] = {"34bf7a0ff419a2a775b9", "32cbd69e4b950bf97679"};
const char kPusherCluster[] = "us2";
const int kMaxHistory = 400;
const int kInfoIntervalMs = 60 * 1000;

QString str(const QJsonObject &o, const char *key) { return o.value(QLatin1String(key)).toString(); }

qint64 num(const QJsonValue &v)
{
    if (v.isDouble()) return qint64(v.toDouble());
    if (v.isString()) return v.toString().toLongLong();
    return 0;
}

// Kick schickt manche Unterobjekte als JSON-Text (Verlauf: metadata), live als Objekt.
QJsonObject objectOrString(const QJsonValue &v)
{
    if (v.isObject()) return v.toObject();
    if (v.isString()) return QJsonDocument::fromJson(v.toString().toUtf8()).object();
    return QJsonObject();
}

qint64 parseTime(const QString &s)
{
    if (s.isEmpty()) return QDateTime::currentMSecsSinceEpoch();
    QDateTime t = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!t.isValid()) t = QDateTime::fromString(s, Qt::ISODate);
    if (!t.isValid()) t = QDateTime::fromString(s, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (!t.isValid()) return QDateTime::currentMSecsSinceEpoch();
    if (t.timeSpec() == Qt::LocalTime && !s.contains(QLatin1Char('+')) && !s.endsWith(QLatin1Char('Z')))
        t.setTimeZone(QTimeZone::UTC);
    return t.toMSecsSinceEpoch();
}

const QRegularExpression &emoteTokenRe()
{
    static const QRegularExpression re{QStringLiteral("\\[emote:(\\d+):([^\\]\\s]*)\\]")};
    return re;
}

const QRegularExpression &linkRe()
{
    static const QRegularExpression re{
        QStringLiteral("^(?:https?://|www\\.)[^\\s]+$|^[a-z0-9-]+(?:\\.[a-z0-9-]+)*\\.(?:com|net|org|de|tv|gg|io|me|be|ly|co|eu|at|ch)(?:/[^\\s]*)?$"),
        QRegularExpression::CaseInsensitiveOption};
    return re;
}

QString normalizeColor(const QString &c)
{
    static const QRegularExpression hex{QStringLiteral("^#[0-9a-fA-F]{6}$")};
    return hex.match(c).hasMatch() ? c : QStringLiteral("#53fc18");
}

// Kick-Rollen als kleine Etiketten (Kick liefert für sie keine Bild-Adressen)
struct BadgeStyle { const char *type; const char *label; const char *color; const char *tip; };
const BadgeStyle kBadgeStyles[] = {
    {"broadcaster", "Streamer", "#e9113c", "Broadcaster"},
    {"moderator", "Mod", "#00c73c", "Moderator"},
    {"vip", "VIP", "#e005b9", "VIP"},
    {"og", "OG", "#f0a500", "OG"},
    {"founder", "Founder", "#d4af37", "Founder"},
    {"verified", "✓", "#1fa2f5", "Verified"},
    {"staff", "Staff", "#53fc18", "Kick Staff"},
    {"sidekick", "Sidekick", "#00b4d8", "Sidekick"},
    {"bot", "Bot", "#8a8a8a", "Bot"},
    {"sub_gifter", "Gifter", "#a970ff", "Sub Gifter"},
    {"trainwreckstv", "TW", "#ff7a00", "Trainwreckstv"},
};

QString escapeKey(const QString &s) { return s.toLower().trimmed(); }

} // namespace

// =========================================================================== Aufbau
KickChatController::KickChatController(uve::IPlayerHost *host, const QString &pluginId, const QString &dataDir,
                                       QObject *parent)
    : QObject(parent), m_host(host), m_id(pluginId), m_dataDir(dataDir),
      m_cache(new KickEmoteCache(dataDir + QStringLiteral("/emotes"), this)),
      m_auth(new KickAuth(this))
{
    connect(m_cache, &KickEmoteCache::ready, this, &KickChatController::onEmoteReady);
    connect(m_auth, &KickAuth::changed, this, &KickChatController::authChanged);
    connect(m_auth, &KickAuth::changed, this, &KickChatController::chattersChanged);
    connect(m_auth, &KickAuth::changed, this, &KickChatController::fetchMyRole);
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, [this]() {
        // Umfrage abgelaufen: Ergebnis noch kurz zeigen, dann weg
        if (m_poll.isEmpty()) return;
        if (!m_poll.value(QStringLiteral("ended")).toBool()) {
            m_poll.insert(QStringLiteral("ended"), true);
            emit pollChanged();
            m_pollTimer.start(std::max(5, m_poll.value(QStringLiteral("showResultSec")).toInt()) * 1000);
        } else {
            m_poll.clear();
            emit pollChanged();
        }
    });
    connect(m_auth, &KickAuth::stateChanged, this, &KickChatController::loginStateChanged);

    connect(&m_ws, &MiniWebSocket::opened, this, [this]() {
        setStatus(QStringLiteral("connecting"), QStringLiteral("pusher"));
    });
    connect(&m_ws, &MiniWebSocket::textMessage, this, &KickChatController::handlePusherFrame);
    connect(&m_ws, &MiniWebSocket::closed, this, [this](const QString &why) {
        m_subscribed = false;
        m_pusherPing.stop();
        if (m_wantOpen) { setStatus(QStringLiteral("closed"), why); scheduleReconnect(); }
        else setStatus(QStringLiteral("idle"));
    });
    connect(&m_ws, &MiniWebSocket::failed, this, [this](const QString &why) {
        m_subscribed = false;
        m_pusherPing.stop();
        setStatus(QStringLiteral("error"), why);
        if (m_wantOpen) scheduleReconnect();
    });

    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() { if (m_wantOpen) openChat(); });

    m_infoTimer.setInterval(kInfoIntervalMs);
    connect(&m_infoTimer, &QTimer::timeout, this, &KickChatController::fetchChannel);

    // Pusher erwartet spätestens alle 120 s Aktivität
    m_pusherPing.setInterval(60 * 1000);
    connect(&m_pusherPing, &QTimer::timeout, this, [this]() {
        if (m_ws.isOpen()) m_ws.sendText(QStringLiteral("{\"event\":\"pusher:ping\",\"data\":{}}"));
    });

    m_spell = new SpellChecker(this);
    connect(m_spell, &SpellChecker::checked, this, [this](const QString &text, const QVariantList &ranges, const QVariantList &issues) {
        emit spellResult(text, ranges);
        if (text != m_grammarText) return;
        m_localIssues = issues;
        emitGrammarResult();
    });
    m_languageTool = new LanguageToolClient(this);
    connect(m_languageTool, &LanguageToolClient::result, this, [this](const QString &text, const QVariantList &issues) {
        m_languageToolFailureLogged = false;
        if (text != m_grammarText) return;
        m_remoteIssues = issues;
        emitGrammarResult();
    });
    connect(m_languageTool, &LanguageToolClient::failed, this, [this](const QString &reason) {
        if (m_host && !m_languageToolFailureLogged)
            m_host->log(m_id, QStringLiteral("warning"), QStringLiteral("LanguageTool: %1").arg(reason));
        m_languageToolFailureLogged = true;
    });
    connect(m_spell, &SpellChecker::frequencyWordsReady, this, [this](const QStringList &german, const QStringList &english) {
        m_predictor.setFrequencyWords(german, english);
        emit predictionsChanged();
    });
    connect(m_spell, &SpellChecker::suggestionsReady, this, &KickChatController::spellSuggestions);
    connect(m_spell, &SpellChecker::dictionaryMissing, this, [this](const QString &path) {
        if (m_host) m_host->log(m_id, QStringLiteral("warning"), QStringLiteral("Wörterbuch fehlt: %1").arg(path));
    });
    m_predictFile = m_dataDir + QStringLiteral("/predict.json");
    {
        QFile saved(m_predictFile);
        if (saved.open(QIODevice::ReadOnly))
            m_predictor.loadOwn(QJsonDocument::fromJson(saved.readAll()).object());
    }

    m_chattersTimer.setSingleShot(true);
    m_chattersTimer.setInterval(1000);
    connect(&m_chattersTimer, &QTimer::timeout, this, &KickChatController::chattersChanged);

    loadSettings();
}

KickChatController::~KickChatController()
{
    m_wantOpen = false;
    m_ws.close();
}

QNetworkAccessManager *KickChatController::nam()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    }
    return m_nam;
}

static QNetworkRequest jsonRequest(const QString &url)
{
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("Accept", "application/json");
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("UVE-Player-KickChat/1.0"));
    req.setTransferTimeout(20000);
    return req;
}

void KickChatController::applyVault(TokenVault *vault)
{
    m_auth->setVault(vault);
}

// =========================================================================== Einstellungen
void KickChatController::loadSettings()
{
    if (!m_host) return;
    const QJsonObject s = m_host->loadPluginSettings(m_id);
    m_channel = escapeKey(s.value(QStringLiteral("channel")).toString());
    m_autoChannel = s.value(QStringLiteral("autoChannel")).toBool(true);
    m_autoScroll = s.value(QStringLiteral("autoscroll")).toBool(true);
    m_showTimestamps = s.value(QStringLiteral("showTimestamps")).toBool(false);
    m_chatFontSize = std::clamp(s.value(QStringLiteral("chatFontSize")).toInt(13), 9, 28);
    m_hiddenPin = s.value(QStringLiteral("hiddenPin")).toString();
    m_favorites.clear();
    for (const QJsonValue &v : s.value(QStringLiteral("favoriteEmotes")).toArray())
        if (v.isString()) m_favorites.append(v.toString());
    m_notifySound = s.value(QStringLiteral("notifySound")).toBool(true);
    m_sound.setVolumePercent(s.value(QStringLiteral("notifyVolume")).toInt(30));
    m_showFollows = s.value(QStringLiteral("showFollows")).toBool(true);
    m_blocked.clear();
    for (const QJsonValue &v : s.value(QStringLiteral("blocked")).toArray())
        if (v.isString() && !v.toString().isEmpty()) m_blocked.append(v.toString().toLower());
    loadBoxSettings(s);
    m_spellGerman = s.value(QStringLiteral("spellGerman")).toBool(true);
    m_spellEnglish = s.value(QStringLiteral("spellEnglish")).toBool(true);
    m_predictText = s.value(QStringLiteral("predictText")).toBool(true);
    m_grammarCheck = s.value(QStringLiteral("grammarCheck")).toBool(true);
    // Online-Prüfung (api.languagetool.org) ab Werk an. Ein gespeichertes „aus“ gilt nur, wenn der
    // Nutzer es selbst gewählt hat (1.3.0 hat „aus“ als Vorgabe mitgespeichert).
    m_grammarOnlineChosen = s.value(QStringLiteral("grammarOnlineChosen")).toBool(false);
    m_grammarOnline = m_grammarOnlineChosen ? s.value(QStringLiteral("grammarOnline")).toBool(true) : true;
    m_spellWords.clear();
    for (const QJsonValue &v : s.value(QStringLiteral("spellWords")).toArray())
        if (v.isString() && !v.toString().isEmpty()) m_spellWords.insert(v.toString().toLower());
    if (m_spell) {
        m_spell->setLanguages(m_spellGerman, m_spellEnglish);
        m_spell->setUserWords(m_spellWords);
    }
}

void KickChatController::persist()
{
    if (!m_host) return;
    QJsonObject s = m_host->loadPluginSettings(m_id);
    s.insert(QStringLiteral("channel"), m_channel);
    s.insert(QStringLiteral("autoChannel"), m_autoChannel);
    s.insert(QStringLiteral("autoscroll"), m_autoScroll);
    s.insert(QStringLiteral("showTimestamps"), m_showTimestamps);
    s.insert(QStringLiteral("chatFontSize"), m_chatFontSize);
    s.insert(QStringLiteral("hiddenPin"), m_hiddenPin);
    s.insert(QStringLiteral("favoriteEmotes"), QJsonArray::fromStringList(m_favorites));
    s.insert(QStringLiteral("notifySound"), m_notifySound);
    s.insert(QStringLiteral("notifyVolume"), m_sound.volumePercent());
    s.insert(QStringLiteral("showFollows"), m_showFollows);
    s.insert(QStringLiteral("blocked"), QJsonArray::fromStringList(m_blocked));
    s.insert(QStringLiteral("spellGerman"), m_spellGerman);
    s.insert(QStringLiteral("spellEnglish"), m_spellEnglish);
    s.insert(QStringLiteral("predictText"), m_predictText);
    s.insert(QStringLiteral("grammarCheck"), m_grammarCheck);
    s.insert(QStringLiteral("grammarOnline"), m_grammarOnline);
    s.insert(QStringLiteral("grammarOnlineChosen"), m_grammarOnlineChosen);
    QStringList words(m_spellWords.cbegin(), m_spellWords.cend());
    words.sort();
    s.insert(QStringLiteral("spellWords"), QJsonArray::fromStringList(words));
    m_host->savePluginSettings(m_id, s);
}

void KickChatController::setAutoChannel(bool on)
{
    if (on == m_autoChannel) return;
    m_autoChannel = on;
    persist();
    emit settingsChanged();
    if (on && m_host) tryFollow(m_host->currentMediaPath(), false);
}

void KickChatController::setAutoScroll(bool on)
{
    if (on == m_autoScroll) return;
    m_autoScroll = on;
    persist();
    emit settingsChanged();
}

void KickChatController::setShowTimestamps(bool on)
{
    if (on == m_showTimestamps) return;
    m_showTimestamps = on;
    persist();
    emit settingsChanged();
}

void KickChatController::setChatFontSize(int px)
{
    px = std::clamp(px, 9, 28);
    if (px == m_chatFontSize) return;
    m_chatFontSize = px;
    persist();
    emit settingsChanged();
}

void KickChatController::setStatus(const QString &key, const QString &detail)
{
    if (key == m_statusKey && detail == m_statusDetail) return;
    m_statusKey = key;
    m_statusDetail = detail;
    emit statusChanged();
}

// =========================================================================== Anmeldung
bool KickChatController::authenticated() const { return m_auth->loggedIn(); }
QString KickChatController::user() const { return m_auth->userName(); }
bool KickChatController::hasAppCredentials() const { return m_auth->hasCredentials(); }
QString KickChatController::appClientId() const { return m_auth->usingOwnApp() ? m_auth->clientId() : QString(); }
bool KickChatController::usingOwnApp() const { return m_auth->usingOwnApp(); }
QString KickChatController::redirectUri() const { return KickAuth::redirectUri(); }
QString KickChatController::loginState() const { return m_auth->state(); }
QString KickChatController::loginError() const { return m_auth->error(); }
QString KickChatController::loginUrl() const { return m_auth->authorizeUrl(); }
void KickChatController::setAppCredentials(const QString &id, const QString &secret) { m_auth->setCredentials(id, secret); }
void KickChatController::startLogin() { m_auth->startLogin(); }
void KickChatController::cancelLogin() { m_auth->cancelLogin(); }
void KickChatController::logout() { m_auth->logout(); }

void KickChatController::watchWindowPresses(QObject *window)
{
    if (!window) return;
    window->removeEventFilter(this);                  // mehrfaches Anmelden zählt nur einmal
    window->installEventFilter(this);
}

bool KickChatController::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        const QPointF p = static_cast<QMouseEvent *>(event)->scenePosition();
        emit windowPressed(watched, p.x(), p.y());
    }
    return QObject::eventFilter(watched, event);      // nur mitlesen: der Klick geht normal weiter
}

void KickChatController::openInBrowser(const QString &url)
{
    QUrl u = QUrl::fromUserInput(url);
    if (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https"))
        QDesktopServices::openUrl(u);
}

// =========================================================================== Kanal
void KickChatController::setChannel(const QString &channelIn)
{
    QString c = guessChannel(channelIn);
    if (c.isEmpty()) c = escapeKey(channelIn);
    static const QRegularExpression valid{QStringLiteral("^[a-z0-9_-]{1,64}$")};
    if (!c.isEmpty() && !valid.match(c).hasMatch()) return;
    if (c == m_channel) {
        if (!c.isEmpty() && !m_ws.isOpen()) openChat();
        return;
    }
    const bool wasOpen = m_wantOpen;
    closeChat();
    m_channel = c;
    m_channelDisplay.clear();
    m_chatroomId = m_channelId = m_channelUserId = 0;
    m_viewerCount = -1;
    m_live = false;
    m_streamTitle.clear();
    m_chatModes.clear();
    m_subBadges.clear();
    m_pinned.clear();
    // Kanal-Emotes des alten Kanals entfernen (global/Emoji bleiben)
    for (auto it = m_emotes.begin(); it != m_emotes.end();) {
        if (it->group == QLatin1String("channel") || it->group == QLatin1String("7tv-channel")) it = m_emotes.erase(it);
        else ++it;
    }
    clearHistory();
    m_chatters.clear();
    emit chattersChanged();
    m_poll.clear();
    m_pollTimer.stop();
    emit pollChanged();
    setMyRole(m_auth->loggedIn() ? QStringLiteral("user") : QStringLiteral("anonymous"));
    persist();
    emit channelChanged();
    emit channelInfoChanged();
    emit pinnedChanged();
    emit emotesChanged();
    Q_UNUSED(wasOpen)
    if (!m_channel.isEmpty()) openChat();
}

void KickChatController::openChat()
{
    if (m_channel.isEmpty()) { setStatus(QStringLiteral("idle")); return; }
    m_wantOpen = true;
    m_reconnectTimer.stop();
    if (m_chatroomId == 0) {          // erst Kanal-Infos holen, dann verbinden
        setStatus(QStringLiteral("connecting"), QStringLiteral("channel"));
        fetchChannel();
        return;
    }
    connectSocket();
    if (!m_infoTimer.isActive()) m_infoTimer.start();
}

void KickChatController::closeChat()
{
    m_wantOpen = false;
    m_reconnectTimer.stop();
    m_infoTimer.stop();
    m_pusherPing.stop();
    m_subscribed = false;
    ++m_generation;
    m_ws.close();
    setStatus(QStringLiteral("idle"));
}

void KickChatController::reconnect()
{
    closeChat();
    m_chatroomId = 0;
    openChat();
}

void KickChatController::scheduleReconnect()
{
    if (m_reconnectTimer.isActive()) return;
    m_reconnectTimer.start(m_backoffMs);
    m_backoffMs = std::min(m_backoffMs * 2, 60000);
}

void KickChatController::connectSocket()
{
    if (m_ws.isOpen() || m_chatroomId == 0) {
        if (m_ws.isOpen() && !m_subscribed) subscribe();
        return;
    }
    setStatus(QStringLiteral("connecting"), QStringLiteral("socket"));
    QStringList keys;
    QString cluster = QString::fromLatin1(kPusherCluster);
    if (m_host) {
        const QJsonObject s = m_host->loadPluginSettings(m_id);
        static const QRegularExpression safe{QStringLiteral("^[a-z0-9-]{2,40}$")};
        const QString k = s.value(QStringLiteral("pusherKey")).toString().trimmed();
        const QString c = s.value(QStringLiteral("pusherCluster")).toString().trimmed();
        if (safe.match(k).hasMatch()) keys << k;
        if (safe.match(c).hasMatch()) cluster = c;
    }
    for (const char *k : kPusherKeys) keys << QString::fromLatin1(k);
    const QString key = keys.at(m_keyIndex % keys.size());
    m_ws.open(QStringLiteral("ws-%1.pusher.com").arg(cluster), 443,
              QStringLiteral("/app/%1?protocol=7&client=js&version=8.4.0&flash=false").arg(key));
}

void KickChatController::subscribe()
{
    const QStringList channels{
        QStringLiteral("chatrooms.%1.v2").arg(m_chatroomId),
        QStringLiteral("chatroom_%1").arg(m_chatroomId),
        QStringLiteral("channel.%1").arg(m_channelId),
        QStringLiteral("channel_%1").arg(m_channelId),
    };
    for (const QString &c : channels) {
        QJsonObject data{{QStringLiteral("auth"), QString()}, {QStringLiteral("channel"), c}};
        QJsonObject msg{{QStringLiteral("event"), QStringLiteral("pusher:subscribe")}, {QStringLiteral("data"), data}};
        m_ws.sendText(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    }
}

void KickChatController::fetchChannel()
{
    if (m_channel.isEmpty()) return;
    const quint64 gen = m_generation;
    const QString slug = m_channel;
    QNetworkReply *reply = nam()->get(jsonRequest(QStringLiteral("https://kick.com/api/v2/channels/") + slug));
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, slug]() {
        reply->deleteLater();
        if (gen != m_generation || slug != m_channel) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (status == 404) {
            setStatus(QStringLiteral("notfound"), slug);
            m_wantOpen = false;
            return;
        }
        if (reply->error() != QNetworkReply::NoError || body.isEmpty()) {
            if (m_chatroomId == 0) {
                setStatus(QStringLiteral("error"), QStringLiteral("HTTP %1 %2").arg(status).arg(reply->errorString()));
                if (m_wantOpen) scheduleReconnect();
            }
            return;
        }
        const bool first = m_chatroomId == 0;
        applyChannelJson(body);
        if (m_chatroomId == 0) {
            setStatus(QStringLiteral("error"), QStringLiteral("chatroom"));
            return;
        }
        if (first) {
            fetchMyRole();
            fetchEmotes();
            fetch7tv();
            fetchHistory();
            if (m_wantOpen) {
                connectSocket();
                if (!m_infoTimer.isActive()) m_infoTimer.start();
            }
        }
    });
}

void KickChatController::applyChannelJson(const QByteArray &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (o.isEmpty()) return;
    m_channelId = num(o.value(QStringLiteral("id")));
    m_channelUserId = num(o.value(QStringLiteral("user_id")));
    const QJsonObject user = o.value(QStringLiteral("user")).toObject();
    if (!user.isEmpty()) m_channelDisplay = str(user, "username");

    const QJsonObject room = o.value(QStringLiteral("chatroom")).toObject();
    if (!room.isEmpty()) {
        m_chatroomId = num(room.value(QStringLiteral("id")));
        QStringList modes;
        if (room.value(QStringLiteral("slow_mode")).toBool())
            modes << QStringLiteral("slow:%1").arg(room.value(QStringLiteral("message_interval")).toInt());
        if (room.value(QStringLiteral("followers_mode")).toBool())
            modes << QStringLiteral("followers:%1").arg(room.value(QStringLiteral("following_min_duration")).toInt());
        if (room.value(QStringLiteral("subscribers_mode")).toBool()) modes << QStringLiteral("subscribers");
        if (room.value(QStringLiteral("emotes_mode")).toBool()) modes << QStringLiteral("emotes");
        m_chatModes = modes.join(QLatin1Char(','));
    }

    m_subBadges.clear();
    for (const QJsonValue &v : o.value(QStringLiteral("subscriber_badges")).toArray()) {
        const QJsonObject b = v.toObject();
        const QString src = b.value(QStringLiteral("badge_image")).toObject().value(QStringLiteral("src")).toString();
        if (!src.isEmpty()) m_subBadges.append({b.value(QStringLiteral("months")).toInt(), src});
    }
    std::sort(m_subBadges.begin(), m_subBadges.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

    const QJsonValue ls = o.value(QStringLiteral("livestream"));
    if (ls.isObject()) {
        const QJsonObject l = ls.toObject();
        m_live = l.value(QStringLiteral("is_live")).toBool(true);
        m_viewerCount = l.value(QStringLiteral("viewer_count")).toInt(-1);
        m_streamTitle = str(l, "session_title");
    } else {
        m_live = false;
        m_viewerCount = -1;
        m_streamTitle.clear();
    }
    emit channelInfoChanged();
}

// =========================================================================== Emotes
void KickChatController::addEmote(const Emote &e)
{
    if (e.name.isEmpty()) return;
    const auto it = m_emotes.constFind(e.name);
    if (it != m_emotes.constEnd()) {
        // Vorrang: Kanal (Kick) > global/Emoji (Kick) > 7TV-Kanal > 7TV-global
        auto rank = [](const QString &g) {
            if (g == QLatin1String("channel")) return 5;
            if (g == QLatin1String("global") || g == QLatin1String("emoji")) return 4;
            if (g == QLatin1String("7tv-channel")) return 3;
            return 2;
        };
        if (rank(it->group) > rank(e.group)) return;
    }
    m_emotes.insert(e.name, e);
    if (e.key.startsWith(QLatin1String("kick_"))) m_emotesById.insert(e.key, e);
}

void KickChatController::fetchEmotes()
{
    const quint64 gen = m_generation;
    QNetworkReply *reply = nam()->get(jsonRequest(QStringLiteral("https://kick.com/emotes/") + m_channel));
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        if (gen != m_generation || reply->error() != QNetworkReply::NoError) return;
        applyEmotesJson(reply->readAll());
    });
}

void KickChatController::applyEmotesJson(const QByteArray &json)
{
    const QJsonArray sets = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &sv : sets) {
        const QJsonObject set = sv.toObject();
        const QString setId = set.value(QStringLiteral("id")).toVariant().toString();
        QString group;
        if (setId == QLatin1String("Global")) group = QStringLiteral("global");
        else if (setId == QLatin1String("Emoji")) group = QStringLiteral("emoji");
        else if (num(set.value(QStringLiteral("id"))) == m_channelId && m_channelId != 0) group = QStringLiteral("channel");
        else group = QStringLiteral("global");
        for (const QJsonValue &ev : set.value(QStringLiteral("emotes")).toArray()) {
            const QJsonObject e = ev.toObject();
            Emote em;
            em.name = str(e, "name");
            em.key = QStringLiteral("kick_%1").arg(num(e.value(QStringLiteral("id"))));
            em.group = group;
            em.subOnly = e.value(QStringLiteral("subscribers_only")).toBool();
            addEmote(em);
        }
    }
    emit emotesChanged();
}

void KickChatController::fetch7tv()
{
    const quint64 gen = m_generation;
    auto parse = [this](const QByteArray &body, const QString &group, bool userObject) {
        QJsonObject root = QJsonDocument::fromJson(body).object();
        const QJsonObject set = userObject ? root.value(QStringLiteral("emote_set")).toObject() : root;
        int n = 0;
        for (const QJsonValue &v : set.value(QStringLiteral("emotes")).toArray()) {
            const QJsonObject e = v.toObject();
            const QString id = str(e, "id");
            if (id.isEmpty()) continue;
            Emote em;
            em.name = str(e, "name");
            em.key = QStringLiteral("7tv_") + id;
            em.group = group;
            em.zeroWidth = (e.value(QStringLiteral("flags")).toInt() & 1) != 0;
            addEmote(em);
            ++n;
        }
        if (n) emit emotesChanged();
    };
    QNetworkReply *g = nam()->get(jsonRequest(QStringLiteral("https://7tv.io/v3/emote-sets/global")));
    connect(g, &QNetworkReply::finished, this, [g, parse]() {
        g->deleteLater();
        if (g->error() == QNetworkReply::NoError) parse(g->readAll(), QStringLiteral("7tv"), false);
    });
    if (m_channelUserId > 0) {
        QNetworkReply *c = nam()->get(jsonRequest(QStringLiteral("https://7tv.io/v3/users/kick/%1").arg(m_channelUserId)));
        connect(c, &QNetworkReply::finished, this, [this, c, parse, gen]() {
            c->deleteLater();
            if (gen == m_generation && c->error() == QNetworkReply::NoError)
                parse(c->readAll(), QStringLiteral("7tv-channel"), true);
        });
    }
}

QString KickChatController::emoteSrc(const QString &key)
{
    return m_cache->fileUrl(key);
}

QVariantList KickChatController::emoteList(const QString &kind, const QString &filter)
{
    QVariantList out;
    QList<Emote> list;
    const QString f = filter.trimmed().toLower();
    for (const Emote &e : std::as_const(m_emotes)) {
        const bool fav = m_favorites.contains(e.name);
        bool ok = false;
        if (kind == QLatin1String("all")) ok = true;
        else if (kind == QLatin1String("favorites")) ok = fav;
        else if (kind == QLatin1String("7tv")) ok = e.group.startsWith(QLatin1String("7tv"));
        else ok = e.group == kind;
        if (!ok) continue;
        if (!f.isEmpty() && !e.name.toLower().contains(f)) continue;
        list.append(e);
    }
    auto order = [](const QString &g) {
        if (g == QLatin1String("channel")) return 0;
        if (g == QLatin1String("global")) return 1;
        if (g == QLatin1String("emoji")) return 2;
        if (g == QLatin1String("7tv-channel")) return 3;
        return 4;
    };
    std::sort(list.begin(), list.end(), [&](const Emote &a, const Emote &b) {
        const int oa = order(a.group), ob = order(b.group);
        if (oa != ob) return oa < ob;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    for (const Emote &e : list) {
        const QString src = emoteSrc(e.key);
        out.append(QVariantMap{
            {QStringLiteral("name"), e.name},
            {QStringLiteral("key"), e.key},
            {QStringLiteral("src"), src},
            {QStringLiteral("anim"), m_cache->isAnimated(e.key)},
            {QStringLiteral("provider"), e.key.startsWith(QLatin1String("7tv_")) ? QStringLiteral("7TV") : QStringLiteral("Kick")},
            {QStringLiteral("group"), e.group},
            {QStringLiteral("favorite"), m_favorites.contains(e.name)},
            {QStringLiteral("subOnly"), e.subOnly},
        });
    }
    return out;
}

void KickChatController::toggleEmoteFavorite(const QString &name)
{
    if (name.isEmpty()) return;
    if (m_favorites.contains(name)) m_favorites.removeAll(name);
    else m_favorites.append(name);
    persist();
    emit emotesChanged();
}

void KickChatController::onEmoteReady(const QString &key)
{
    emit emoteImageReady();
    if (!m_pinned.isEmpty()) {
        m_pinned.insert(QStringLiteral("segments"), buildSegments(m_pinned.value(QStringLiteral("content")).toString()));
        emit pinnedChanged();
    }
    const QSet<QString> ids = m_waitingKeys.take(key);
    if (ids.isEmpty()) return;
    for (int i = int(m_history.size()) - 1; i >= 0; --i) {
        const QString id = m_history[i].value(QStringLiteral("id")).toString();
        if (!ids.contains(id)) continue;
        QVariantMap &m = m_history[i];
        const QString content = m.value(QStringLiteral("content")).toString();
        m.insert(QStringLiteral("segments"), buildSegments(content));
        // Abzeichen mit Bild neu auflösen
        QVariantList badges = m.value(QStringLiteral("badges")).toList();
        for (QVariant &bv : badges) {
            QVariantMap b = bv.toMap();
            const QString bkey = b.value(QStringLiteral("key")).toString();
            if (!bkey.isEmpty() && b.value(QStringLiteral("src")).toString().isEmpty())
                b.insert(QStringLiteral("src"), m_cache->cachedFileUrl(bkey));
            bv = b;
        }
        m.insert(QStringLiteral("badges"), badges);
        emit messageUpdated(id, m);
    }
}

// =========================================================================== Nachrichten
QVariantList KickChatController::buildSegments(const QString &content)
{
    QVariantList out;
    // o/n: Lage des sichtbaren Teils in der Rohnachricht (raw = Wörter mit genau einem Leerzeichen,
    // Emotes als Name) – fürs Markieren und Kopieren in der Chatzeile
    int rawPos = 0;
    auto addText = [&out, &rawPos](const QString &word, bool lead) {
        const QString shown = lead ? QStringLiteral(" ") + word : word;
        QString url;
        if (linkRe().match(word).hasMatch()) {
            url = word;
            if (!url.startsWith(QLatin1String("http"), Qt::CaseInsensitive)) url.prepend(QStringLiteral("https://"));
            // Satzzeichen am Ende gehören nicht zum Link
            while (!url.isEmpty() && QStringLiteral(".,!?)]").contains(url.back())) url.chop(1);
        }
        out.append(QVariantMap{{QStringLiteral("t"), QStringLiteral("text")}, {QStringLiteral("s"), shown},
                               {QStringLiteral("url"), url}, {QStringLiteral("src"), QString()},
                               {QStringLiteral("anim"), false}, {QStringLiteral("name"), QString()},
                               {QStringLiteral("key"), QString()},
                               {QStringLiteral("o"), rawPos}, {QStringLiteral("n"), int(word.size())}});
        rawPos += int(word.size());
    };
    auto addEmoteSeg = [this, &out, &rawPos](const QString &key, const QString &name) {
        const QString src = emoteSrc(key);
        out.append(QVariantMap{{QStringLiteral("t"), QStringLiteral("emote")}, {QStringLiteral("s"), QString()},
                               {QStringLiteral("url"), QString()}, {QStringLiteral("src"), src},
                               {QStringLiteral("anim"), m_cache->isAnimated(key)}, {QStringLiteral("name"), name},
                               {QStringLiteral("key"), key},
                               {QStringLiteral("o"), rawPos}, {QStringLiteral("n"), int(name.size())}});
        rawPos += int(name.size());
    };

    // Emotes als eigene Wörter: Aneinanderhängende Emotes bleiben beim Kopieren getrennt
    QString spaced = content;
    spaced.replace(emoteTokenRe(), QStringLiteral(" [emote:\\1:\\2] "));
    const QStringList words = spaced.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    bool first = true;
    for (const QString &word : words) {
        // [emote:id:name] kann mitten im Wort stehen
        int pos = 0;
        auto it = emoteTokenRe().globalMatch(word);
        bool lead = !first;
        if (!first) rawPos += 1;                       // das trennende Leerzeichen
        while (it.hasNext()) {
            const auto m = it.next();
            if (m.capturedStart() > pos) { addText(word.mid(pos, m.capturedStart() - pos), lead); lead = false; }
            addEmoteSeg(QStringLiteral("kick_") + m.captured(1), m.captured(2));
            lead = false;
            pos = m.capturedEnd();
        }
        if (pos == 0) {
            const auto e = m_emotes.constFind(word);
            if (e != m_emotes.constEnd()) addEmoteSeg(e->key, word);
            else addText(word, lead);
        } else if (pos < word.size()) {
            addText(word.mid(pos), false);
        }
        first = false;
    }
    return out;
}

// Rohtext wie buildSegments ihn zählt: Emotes als Name, Wörter mit genau einem Leerzeichen
static QString plainText(const QString &content)
{
    QString s = content;
    s.replace(emoteTokenRe(), QStringLiteral(" \\2 "));
    return s.split(QLatin1Char(' '), Qt::SkipEmptyParts).join(QLatin1Char(' '));
}

QVariantList KickChatController::buildBadges(const QJsonObject &identity, const QJsonArray &badgesV2)
{
    QVariantList out;
    auto add = [&out](const QString &label, const QString &color, const QString &tip, const QString &src,
                      const QString &key) {
        out.append(QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("color"), color},
                               {QStringLiteral("tip"), tip}, {QStringLiteral("src"), src},
                               {QStringLiteral("key"), key}});
    };
    // Bild-Abzeichen (z. B. Level) aus badges_v2, nur das ausgewählte
    for (const QJsonValue &v : badgesV2) {
        const QJsonObject b = v.toObject();
        if (!b.value(QStringLiteral("selected")).toBool()) continue;
        const QString url = str(b, "image_url");
        const QString key = m_cache->keyForUrl(url);
        if (key.isEmpty()) continue;
        QString tip = str(b, "name");
        const int level = b.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("level")).toInt();
        if (level > 0) tip = QStringLiteral("Level %1").arg(level);
        add(QString(), QString(), tip, m_cache->fileUrl(key), key);
    }
    QList<QJsonObject> badges;
    for (const QJsonValue &v : identity.value(QStringLiteral("badges")).toArray()) badges.append(v.toObject());
    std::stable_sort(badges.begin(), badges.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("sort_order")).toInt(99) < b.value(QStringLiteral("sort_order")).toInt(99);
    });
    for (const QJsonObject &b : std::as_const(badges)) {
        const QString type = str(b, "type");
        const int count = b.value(QStringLiteral("count")).toInt();
        if (type == QLatin1String("subscriber")) {
            QString url;
            for (const auto &sb : m_subBadges)
                if (sb.first <= std::max(count, 1)) url = sb.second;
            const QString tip = QStringLiteral("Subscriber (%1)").arg(std::max(count, 1));
            if (!url.isEmpty()) {
                const QString key = m_cache->keyForUrl(url);
                add(QStringLiteral("Sub"), QStringLiteral("#53fc18"), tip, m_cache->fileUrl(key), key);
            } else {
                add(QStringLiteral("Sub"), QStringLiteral("#53fc18"), tip, QString(), QString());
            }
            continue;
        }
        bool known = false;
        for (const BadgeStyle &s : kBadgeStyles) {
            if (type == QLatin1String(s.type)) {
                QString tip = QString::fromUtf8(s.tip);
                if (count > 0) tip += QStringLiteral(" (%1)").arg(count);
                add(QString::fromUtf8(s.label), QString::fromLatin1(s.color), tip, QString(), QString());
                known = true;
                break;
            }
        }
        if (!known) {
            const QString text = str(b, "text");
            if (!text.isEmpty()) add(text, QStringLiteral("#8a8a8a"), text, QString(), QString());
        }
    }
    return out;
}

static QVariantMap baseMessage()
{
    return QVariantMap{
        {QStringLiteral("id"), QString()},
        {QStringLiteral("type"), QStringLiteral("chat")},       // chat | reply | system
        {QStringLiteral("kind"), QString()},                    // System: sub|gift|host|ban|clear|info|kicks|reward|pin|live
        {QStringLiteral("userId"), QString()},
        {QStringLiteral("user"), QString()},
        {QStringLiteral("name"), QString()},
        {QStringLiteral("color"), QStringLiteral("#53fc18")},
        {QStringLiteral("badges"), QVariantList()},
        {QStringLiteral("segments"), QVariantList()},
        {QStringLiteral("content"), QString()},
        {QStringLiteral("raw"), QString()},
        {QStringLiteral("ts"), double(0)},
        {QStringLiteral("time"), QString()},
        {QStringLiteral("replyName"), QString()},
        {QStringLiteral("replyText"), QString()},
        {QStringLiteral("deleted"), false},
        {QStringLiteral("mention"), false},
        {QStringLiteral("self"), false},
        {QStringLiteral("textEn"), QString()},
        {QStringLiteral("role"), QString()},                    // broadcaster|moderator|vip|og|subscriber|""
        {QStringLiteral("botName"), QString()},                 // Event-Bot: Etikett der Box
    };
}

QVariantMap KickChatController::buildChatMessage(const QJsonObject &msg)
{
    QVariantMap m = baseMessage();
    const QJsonObject sender = msg.value(QStringLiteral("sender")).toObject();
    const QJsonObject identity = sender.value(QStringLiteral("identity")).toObject();
    const QString content = str(msg, "content");
    m.insert(QStringLiteral("id"), str(msg, "id"));
    m.insert(QStringLiteral("type"), str(msg, "type") == QLatin1String("reply") ? QStringLiteral("reply") : QStringLiteral("chat"));
    m.insert(QStringLiteral("userId"), QString::number(num(sender.value(QStringLiteral("id")))));
    m.insert(QStringLiteral("user"), str(sender, "slug"));
    m.insert(QStringLiteral("name"), str(sender, "username"));
    m.insert(QStringLiteral("color"), normalizeColor(str(identity, "color")));
    m.insert(QStringLiteral("badges"), buildBadges(identity, identity.value(QStringLiteral("badges_v2")).toArray()));
    {
        static const char *const order[] = {"broadcaster", "moderator", "vip", "og", "founder", "subscriber"};
        QSet<QString> types;
        for (const QJsonValue &v : identity.value(QStringLiteral("badges")).toArray())
            types.insert(str(v.toObject(), "type"));
        for (const char *r : order)
            if (types.contains(QLatin1String(r))) { m.insert(QStringLiteral("role"), QString::fromLatin1(r)); break; }
    }
    m.insert(QStringLiteral("content"), content);
    m.insert(QStringLiteral("raw"), plainText(content));
    m.insert(QStringLiteral("segments"), buildSegments(content));
    const qint64 ts = parseTime(str(msg, "created_at"));
    m.insert(QStringLiteral("ts"), double(ts));
    m.insert(QStringLiteral("time"), QDateTime::fromMSecsSinceEpoch(ts).toLocalTime().toString(QStringLiteral("HH:mm")));

    const QJsonObject meta = objectOrString(msg.value(QStringLiteral("metadata")));
    if (!meta.isEmpty()) {
        const QJsonObject os = meta.value(QStringLiteral("original_sender")).toObject();
        const QJsonObject om = meta.value(QStringLiteral("original_message")).toObject();
        if (!os.isEmpty() || !om.isEmpty()) {
            m.insert(QStringLiteral("type"), QStringLiteral("reply"));
            m.insert(QStringLiteral("replyName"), str(os, "username"));
            m.insert(QStringLiteral("replyText"), plainText(str(om, "content")));
        }
    }
    const QString me = m_auth->userName();
    if (!me.isEmpty()) {
        const bool self = str(sender, "username").compare(me, Qt::CaseInsensitive) == 0;
        m.insert(QStringLiteral("self"), self);
        const bool mention = !self && (content.contains(QStringLiteral("@") + me, Qt::CaseInsensitive)
                                       || m.value(QStringLiteral("replyName")).toString().compare(me, Qt::CaseInsensitive) == 0);
        m.insert(QStringLiteral("mention"), mention);
    }
    return m;
}

void KickChatController::appendMessage(const QVariantMap &messageIn)
{
    QVariantMap message = messageIn;
    const bool isUserMsg = message.value(QStringLiteral("type")).toString() != QLatin1String("system");
    const QString sender = message.value(QStringLiteral("name")).toString();
    if (isUserMsg && !sender.isEmpty()) {
        if (m_blocked.contains(sender.toLower())) return;          // blockierte Nutzer ausblenden
        const QString bot = eventBotLabel(sender);
        if (!bot.isNull()) {
            message.insert(QStringLiteral("kind"), QStringLiteral("bot"));
            message.insert(QStringLiteral("botName"), bot);
        }
        // eigene Rolle aus eigenen Nachrichten nachführen
        if (message.value(QStringLiteral("self")).toBool()) {
            const QString r = message.value(QStringLiteral("role")).toString();
            if (r == QLatin1String("broadcaster") || r == QLatin1String("moderator") || r == QLatin1String("vip")) setMyRole(r);
        }
    }
    const QString id = message.value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) {
        if (m_seenIds.contains(id)) return;
        m_seenIds.insert(id);
    }
    // Wartende Bilder dieser Nachricht vormerken
    auto noteWaiting = [this, &id](const QVariantList &items) {
        for (const QVariant &v : items) {
            const QVariantMap s = v.toMap();
            const QString key = s.value(QStringLiteral("key")).toString();
            if (!key.isEmpty() && s.value(QStringLiteral("src")).toString().isEmpty()) m_waitingKeys[key].insert(id);
        }
    };
    if (!id.isEmpty()) {
        noteWaiting(message.value(QStringLiteral("segments")).toList());
        noteWaiting(message.value(QStringLiteral("badges")).toList());
    }
    const QString name = message.value(QStringLiteral("name")).toString();
    if (!name.isEmpty() && message.value(QStringLiteral("type")).toString() != QLatin1String("system")) {
        m_recentChatters.removeAll(name);
        m_recentChatters.prepend(name);
        if (m_recentChatters.size() > 300) m_recentChatters.removeLast();
    }
    if (!name.isEmpty() && message.value(QStringLiteral("type")).toString() != QLatin1String("system")) {
        Chatter &c = m_chatters[name.toLower()];
        c.name = name;
        c.user = message.value(QStringLiteral("user")).toString();
        c.color = message.value(QStringLiteral("color")).toString();
        c.role = message.value(QStringLiteral("role")).toString();
        ++c.count;
        c.lastTs = std::max(c.lastTs, qint64(message.value(QStringLiteral("ts")).toDouble()));
        if (!m_chattersTimer.isActive()) m_chattersTimer.start();
    }
    if (m_predictText && isUserMsg && !message.value(QStringLiteral("self")).toBool()) {
        ensureFrequencyWords();
        m_predictor.learn(message.value(QStringLiteral("raw")).toString(), false,
                          [this](const QString &w) { return isKnownEmote(w); });
    }
    if (!m_quiet && message.value(QStringLiteral("mention")).toBool() && m_notifySound) {
        if (m_sound.play()) emit notified();
    }
    m_history.append(message);
    while (m_history.size() > kMaxHistory) {
        m_seenIds.remove(m_history.first().value(QStringLiteral("id")).toString());
        m_history.removeFirst();
    }
    emit newMessage(message);
}

void KickChatController::appendSystem(const QString &kind, const QString &text, const QString &textEn)
{
    static quint64 counter = 0;
    QVariantMap m = baseMessage();
    m.insert(QStringLiteral("id"), QStringLiteral("sys-%1").arg(++counter));
    m.insert(QStringLiteral("type"), QStringLiteral("system"));
    m.insert(QStringLiteral("kind"), kind);
    m.insert(QStringLiteral("content"), text);
    m.insert(QStringLiteral("raw"), plainText(text));
    m.insert(QStringLiteral("textEn"), textEn.isEmpty() ? text : textEn);
    m.insert(QStringLiteral("segments"), buildSegments(text));
    const qint64 ts = QDateTime::currentMSecsSinceEpoch();
    m.insert(QStringLiteral("ts"), double(ts));
    m.insert(QStringLiteral("time"), QDateTime::fromMSecsSinceEpoch(ts).toString(QStringLiteral("HH:mm")));
    appendMessage(m);
}

QVariantList KickChatController::history() const
{
    QVariantList out;
    out.reserve(m_history.size());
    for (const QVariantMap &m : m_history) out.append(m);
    return out;
}

QVariantMap KickChatController::messageById(const QString &id) const
{
    for (int i = int(m_history.size()) - 1; i >= 0; --i)
        if (m_history[i].value(QStringLiteral("id")).toString() == id) return m_history[i];
    return QVariantMap();
}

void KickChatController::clearHistory()
{
    m_history.clear();
    m_seenIds.clear();
    m_waitingKeys.clear();
    m_recentChatters.clear();
    emit historyCleared();
}

QStringList KickChatController::mentionCandidates(const QString &prefix) const
{
    QStringList out;
    const QString p = prefix.startsWith(QLatin1Char('@')) ? prefix.mid(1) : prefix;
    for (const QString &n : m_recentChatters) {
        if (p.isEmpty() || n.startsWith(p, Qt::CaseInsensitive)) out.append(n);
        if (out.size() >= 8) break;
    }
    return out;
}

int KickChatController::chatterCount() const
{
    const QString me = m_auth->userName().toLower();
    return int(m_chatters.size()) + (!me.isEmpty() && !m_chatters.contains(me) ? 1 : 0);
}

QVariantList KickChatController::chatters(const QString &filter) const
{
    auto rank = [](const QString &role) {
        if (role == QLatin1String("broadcaster")) return 0;
        if (role == QLatin1String("moderator")) return 1;
        if (role == QLatin1String("vip")) return 2;
        return 3;
    };
    auto label = [](const QString &role) -> QString {
        if (role == QLatin1String("broadcaster")) return QStringLiteral("Streamer");
        if (role == QLatin1String("moderator")) return QStringLiteral("MOD");
        if (role == QLatin1String("vip")) return QStringLiteral("VIP");
        if (role == QLatin1String("og")) return QStringLiteral("OG");
        if (role == QLatin1String("founder")) return QStringLiteral("Founder");
        if (role == QLatin1String("subscriber")) return QStringLiteral("SUB");
        return QString();
    };
    const QString f = filter.trimmed();
    const QString me = m_auth->userName();
    QList<Chatter> list;
    for (const Chatter &c : m_chatters) {
        if (!me.isEmpty() && c.name.compare(me, Qt::CaseInsensitive) == 0) continue;
        if (!f.isEmpty() && !c.name.contains(f, Qt::CaseInsensitive)) continue;
        list.append(c);
    }
    std::sort(list.begin(), list.end(), [&](const Chatter &a, const Chatter &b) {
        const int ra = rank(a.role), rb = rank(b.role);
        if (ra != rb) return ra < rb;
        if (a.lastTs != b.lastTs) return a.lastTs > b.lastTs;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    QVariantList out;
    auto add = [&](const Chatter &c, bool self) {
        out.append(QVariantMap{{QStringLiteral("name"), c.name}, {QStringLiteral("user"), c.user},
                               {QStringLiteral("color"), c.color}, {QStringLiteral("role"), c.role},
                               {QStringLiteral("roleLabel"), label(c.role)}, {QStringLiteral("count"), c.count},
                               {QStringLiteral("lastTs"), double(c.lastTs)}, {QStringLiteral("self"), self}});
    };
    if (!me.isEmpty() && (f.isEmpty() || me.contains(f, Qt::CaseInsensitive))) {
        Chatter self = m_chatters.value(me.toLower());
        if (self.name.isEmpty()) { self.name = me; self.color = QStringLiteral("#53fc18"); }
        add(self, true);
    }
    for (const Chatter &c : list) add(c, false);
    return out;
}

void KickChatController::fetchHistory()
{
    if (m_channelId == 0) return;
    const quint64 gen = m_generation;
    QNetworkReply *reply = nam()->get(jsonRequest(QStringLiteral("https://kick.com/api/v2/channels/%1/messages").arg(m_channelId)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        if (gen != m_generation || reply->error() != QNetworkReply::NoError) return;
        applyHistoryJson(reply->readAll());
    });
}

void KickChatController::applyHistoryJson(const QByteArray &json)
{
    const QJsonObject data = QJsonDocument::fromJson(json).object().value(QStringLiteral("data")).toObject();
    QJsonArray msgs = data.value(QStringLiteral("messages")).toArray();
    QList<QVariantMap> built;
    for (const QJsonValue &v : msgs) built.append(buildChatMessage(v.toObject()));
    std::sort(built.begin(), built.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("ts")).toDouble() < b.value(QStringLiteral("ts")).toDouble();
    });
    // Verlauf vor bereits live angekommene Nachrichten setzen
    const QList<QVariantMap> live = m_history;
    const bool hadLive = !live.isEmpty();
    if (hadLive) {
        m_history.clear();
        m_seenIds.clear();
        emit historyCleared();
    }
    m_quiet = true;
    for (const QVariantMap &m : built) appendMessage(m);
    for (const QVariantMap &m : live) appendMessage(m);
    m_quiet = false;

    const QJsonValue pin = data.value(QStringLiteral("pinned_message"));
    if (pin.isObject()) setPinned(pin.toObject());
}

// =========================================================================== Pusher
void KickChatController::handlePusherFrame(const QString &text)
{
    const QJsonObject frame = QJsonDocument::fromJson(text.toUtf8()).object();
    const QString event = str(frame, "event");
    const QJsonObject data = objectOrString(frame.value(QStringLiteral("data")));

    if (event == QLatin1String("pusher:connection_established")) {
        m_backoffMs = 1000;
        subscribe();
        m_pusherPing.start();
        return;
    }
    if (event == QLatin1String("pusher:ping")) {
        m_ws.sendText(QStringLiteral("{\"event\":\"pusher:pong\",\"data\":{}}"));
        return;
    }
    if (event == QLatin1String("pusher:pong")) return;
    if (event == QLatin1String("pusher_internal:subscription_succeeded")) {
        if (!m_subscribed && str(frame, "channel").startsWith(QLatin1String("chatrooms."))) {
            m_subscribed = true;
            setStatus(QStringLiteral("connected"));
        }
        return;
    }
    if (event == QLatin1String("pusher:error")) {
        // 4001 = Schlüssel unbekannt → beim nächsten Versuch den nächsten Schlüssel nehmen
        if (data.value(QStringLiteral("code")).toInt() == 4001) ++m_keyIndex;
        setStatus(QStringLiteral("error"), str(data, "message"));
        return;
    }
    handleEvent(event, data);
}

void KickChatController::setPinned(const QJsonObject &data)
{
    const QJsonObject msg = data.value(QStringLiteral("message")).toObject();
    if (msg.isEmpty()) return;
    const QVariantMap built = buildChatMessage(msg);
    QVariantMap pin;
    pin.insert(QStringLiteral("id"), built.value(QStringLiteral("id")));
    pin.insert(QStringLiteral("name"), built.value(QStringLiteral("name")));
    pin.insert(QStringLiteral("color"), built.value(QStringLiteral("color")));
    pin.insert(QStringLiteral("content"), built.value(QStringLiteral("content")));
    pin.insert(QStringLiteral("raw"), built.value(QStringLiteral("raw")));
    pin.insert(QStringLiteral("segments"), built.value(QStringLiteral("segments")));
    const QJsonObject by = data.value(QStringLiteral("pinnedBy")).toObject();
    pin.insert(QStringLiteral("pinnedBy"), by.isEmpty() ? str(data.value(QStringLiteral("pinned_by")).toObject(), "username")
                                                       : str(by, "username"));
    m_pinned = pin;
    emit pinnedChanged();
}

bool KickChatController::pinnedHidden() const
{
    return !m_pinned.isEmpty() && m_pinned.value(QStringLiteral("id")).toString() == m_hiddenPin;
}

void KickChatController::hidePinned()
{
    m_hiddenPin = m_pinned.value(QStringLiteral("id")).toString();
    persist();
    emit pinnedChanged();
}

void KickChatController::showPinned()
{
    m_hiddenPin.clear();
    persist();
    emit pinnedChanged();
}

void KickChatController::handleEvent(const QString &eventIn, const QJsonObject &data)
{
    QString event = eventIn;
    const int slash = event.lastIndexOf(QLatin1Char('\\'));
    if (slash >= 0) event = event.mid(slash + 1);

    if (event == QLatin1String("ChatMessageEvent") || event == QLatin1String("ChatMessageSentEvent")) {
        const QJsonObject msg = data.contains(QStringLiteral("message")) && data.value(QStringLiteral("message")).isObject()
                                    ? data.value(QStringLiteral("message")).toObject() : data;
        if (str(msg, "content").isEmpty() && msg.value(QStringLiteral("sender")).isUndefined()) return;
        appendMessage(buildChatMessage(msg));
        return;
    }
    if (event == QLatin1String("MessageDeletedEvent")) {
        const QString id = str(data.value(QStringLiteral("message")).toObject(), "id");
        for (int i = int(m_history.size()) - 1; i >= 0; --i) {
            if (m_history[i].value(QStringLiteral("id")).toString() != id) continue;
            m_history[i].insert(QStringLiteral("deleted"), true);
            emit messageUpdated(id, m_history[i]);
            break;
        }
        return;
    }
    if (event == QLatin1String("UserBannedEvent")) {
        const QString who = str(data.value(QStringLiteral("user")).toObject(), "username");
        const QString by = str(data.value(QStringLiteral("banned_by")).toObject(), "username");
        const bool permanent = data.value(QStringLiteral("permanent")).toBool();
        const int minutes = data.value(QStringLiteral("duration")).toInt();
        // Nachrichten der Person ausgrauen
        for (QVariantMap &m : m_history) {
            if (m.value(QStringLiteral("name")).toString().compare(who, Qt::CaseInsensitive) == 0
                && !m.value(QStringLiteral("deleted")).toBool()) {
                m.insert(QStringLiteral("deleted"), true);
                emit messageUpdated(m.value(QStringLiteral("id")).toString(), m);
            }
        }
        if (permanent)
            appendSystem(QStringLiteral("mod"), QStringLiteral("%1 wurde von %2 gebannt.").arg(who, by),
                         QStringLiteral("%1 was banned by %2.").arg(who, by));
        else
            appendSystem(QStringLiteral("mod"), QStringLiteral("%1 hat einen Timeout von %2 Min. (%3).").arg(who).arg(minutes).arg(by),
                         QStringLiteral("%1 was timed out for %2 min (%3).").arg(who).arg(minutes).arg(by));
        return;
    }
    if (event == QLatin1String("UserUnbannedEvent")) {
        const QString who = str(data.value(QStringLiteral("user")).toObject(), "username");
        appendSystem(QStringLiteral("mod"), QStringLiteral("%1 ist entbannt.").arg(who), QStringLiteral("%1 was unbanned.").arg(who));
        return;
    }
    if (event == QLatin1String("ChatroomClearEvent")) {
        for (QVariantMap &m : m_history) {
            if (m.value(QStringLiteral("type")).toString() == QLatin1String("system")) continue;
            m.insert(QStringLiteral("deleted"), true);
            emit messageUpdated(m.value(QStringLiteral("id")).toString(), m);
        }
        appendSystem(QStringLiteral("mod"), QStringLiteral("Der Chat wurde von einem Moderator geleert."),
                     QStringLiteral("Chat was cleared by a moderator."));
        return;
    }
    if (event == QLatin1String("ChatroomUpdatedEvent")) {
        QStringList modes;
        const QJsonObject slow = data.value(QStringLiteral("slow_mode")).toObject();
        if (slow.value(QStringLiteral("enabled")).toBool())
            modes << QStringLiteral("slow:%1").arg(slow.value(QStringLiteral("message_interval")).toInt());
        const QJsonObject fol = data.value(QStringLiteral("followers_mode")).toObject();
        if (fol.value(QStringLiteral("enabled")).toBool())
            modes << QStringLiteral("followers:%1").arg(fol.value(QStringLiteral("min_duration")).toInt());
        if (data.value(QStringLiteral("subscribers_mode")).toObject().value(QStringLiteral("enabled")).toBool())
            modes << QStringLiteral("subscribers");
        if (data.value(QStringLiteral("emotes_mode")).toObject().value(QStringLiteral("enabled")).toBool())
            modes << QStringLiteral("emotes");
        m_chatModes = modes.join(QLatin1Char(','));
        emit channelInfoChanged();
        return;
    }
    if (event == QLatin1String("PinnedMessageCreatedEvent")) {
        setPinned(data);
        return;
    }
    if (event == QLatin1String("PinnedMessageDeletedEvent")) {
        m_pinned.clear();
        emit pinnedChanged();
        return;
    }
    if (event == QLatin1String("SubscriptionEvent") || event == QLatin1String("ChannelSubscriptionEvent")) {
        const QString who = str(data, "username");
        const int months = data.value(QStringLiteral("months")).toInt(1);
        if (who.isEmpty()) return;
        appendSystem(QStringLiteral("sub"),
                     months > 1 ? QStringLiteral("%1 hat seit %2 Monaten abonniert!").arg(who).arg(months)
                                : QStringLiteral("%1 hat abonniert!").arg(who),
                     months > 1 ? QStringLiteral("%1 subscribed for %2 months!").arg(who).arg(months)
                                : QStringLiteral("%1 subscribed!").arg(who));
        return;
    }
    if (event == QLatin1String("GiftedSubscriptionsEvent") || event == QLatin1String("LuckyUsersWhoGotGiftSubscriptionsEvent")) {
        if (event == QLatin1String("LuckyUsersWhoGotGiftSubscriptionsEvent")) return;   // doppelt zu GiftedSubscriptionsEvent
        const QString gifter = str(data, "gifter_username");
        const int n = data.value(QStringLiteral("gifted_usernames")).toArray().size();
        if (n <= 0) return;
        const QString g = gifter.isEmpty() ? QStringLiteral("Anonym") : gifter;
        appendSystem(QStringLiteral("gift"),
                     n == 1 ? QStringLiteral("%1 hat %2 ein Abo geschenkt!").arg(g, data.value(QStringLiteral("gifted_usernames")).toArray().first().toString())
                            : QStringLiteral("%1 hat %2 Abos verschenkt!").arg(g).arg(n),
                     n == 1 ? QStringLiteral("%1 gifted a sub to %2!").arg(gifter.isEmpty() ? QStringLiteral("Anonymous") : gifter,
                                                                         data.value(QStringLiteral("gifted_usernames")).toArray().first().toString())
                            : QStringLiteral("%1 gifted %2 subs!").arg(gifter.isEmpty() ? QStringLiteral("Anonymous") : gifter).arg(n));
        return;
    }
    if (event == QLatin1String("StreamHostEvent")) {
        const QString host = str(data, "host_username");
        const int viewers = data.value(QStringLiteral("number_viewers")).toInt();
        const QString extra = str(data, "optional_message");
        appendSystem(QStringLiteral("host"),
                     QStringLiteral("%1 hostet mit %2 Zuschauern!%3").arg(host).arg(viewers).arg(extra.isEmpty() ? QString() : QStringLiteral(" ") + extra),
                     QStringLiteral("%1 is hosting with %2 viewers!%3").arg(host).arg(viewers).arg(extra.isEmpty() ? QString() : QStringLiteral(" ") + extra));
        return;
    }
    if (event == QLatin1String("KicksGifted")) {
        const QString who = str(data.value(QStringLiteral("sender")).toObject(), "username");
        const QJsonObject gift = data.value(QStringLiteral("gift")).toObject();
        const int amount = gift.value(QStringLiteral("amount")).toInt();
        const QString giftName = str(gift, "name");
        const QString msg = str(data, "message");
        appendSystem(QStringLiteral("kicks"),
                     QStringLiteral("%1 hat %2 Kicks geschickt%3%4").arg(who).arg(amount)
                         .arg(giftName.isEmpty() ? QString() : QStringLiteral(" (") + giftName + QLatin1Char(')'))
                         .arg(msg.isEmpty() ? QString() : QStringLiteral(": ") + msg),
                     QStringLiteral("%1 sent %2 Kicks%3%4").arg(who).arg(amount)
                         .arg(giftName.isEmpty() ? QString() : QStringLiteral(" (") + giftName + QLatin1Char(')'))
                         .arg(msg.isEmpty() ? QString() : QStringLiteral(": ") + msg));
        return;
    }
    if (event == QLatin1String("RewardRedeemedEvent")) {
        const QString who = str(data, "username").isEmpty() ? str(data, "user_display_name") : str(data, "username");
        const QString title = str(data, "reward_title");
        const QString input = str(data, "user_input");
        if (who.isEmpty() || title.isEmpty()) return;
        appendSystem(QStringLiteral("reward"),
                     QStringLiteral("%1 hat „%2“ eingelöst%3").arg(who, title, input.isEmpty() ? QString() : QStringLiteral(": ") + input),
                     QStringLiteral("%1 redeemed \"%2\"%3").arg(who, title, input.isEmpty() ? QString() : QStringLiteral(": ") + input));
        return;
    }
    if (event == QLatin1String("FollowersUpdated")) {
        const QString who = str(data, "username");
        const bool followed = data.value(QStringLiteral("followed")).toBool(true);
        if (m_showFollows && followed && !who.isEmpty())
            appendSystem(QStringLiteral("follow"), QStringLiteral("%1 folgt jetzt!").arg(who),
                         QStringLiteral("%1 is now following!").arg(who));
        return;
    }
    if (event == QLatin1String("PollUpdateEvent")) {
        applyPollJson(data);
        return;
    }
    if (event == QLatin1String("PollDeleteEvent")) {
        m_poll.clear();
        m_pollTimer.stop();
        emit pollChanged();
        return;
    }
    if (event == QLatin1String("StreamerIsLive")) {
        m_live = true;
        emit channelInfoChanged();
        appendSystem(QStringLiteral("live"), QStringLiteral("Der Stream ist jetzt live."), QStringLiteral("The stream is now live."));
        return;
    }
    if (event == QLatin1String("StopStreamBroadcast")) {
        m_live = false;
        m_viewerCount = -1;
        emit channelInfoChanged();
        appendSystem(QStringLiteral("live"), QStringLiteral("Der Stream ist beendet."), QStringLiteral("The stream has ended."));
        return;
    }
    if (event == QLatin1String("LivestreamUpdated")) {
        const QString title = str(data.value(QStringLiteral("livestream")).toObject(), "session_title");
        if (!title.isEmpty() && title != m_streamTitle) { m_streamTitle = title; emit channelInfoChanged(); }
        return;
    }
    static QSet<QString> logged;
    if (m_host && !logged.contains(event) && logged.size() < 50) {
        logged.insert(event);
        m_host->log(m_id, QStringLiteral("debug"), QStringLiteral("Unbekanntes Kick-Ereignis: %1 %2")
                    .arg(eventIn, QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)).left(400)));
    }
}

// =========================================================================== Senden
QString KickChatController::resolveEmoteNames(const QString &text) const
{
    // Kick-Emote-Namen als [emote:id:name] schicken, damit Kick sie als Bild zeigt
    QStringList words = text.split(QLatin1Char(' '));
    for (QString &w : words) {
        const auto e = m_emotes.constFind(w);
        if (e != m_emotes.constEnd() && e->key.startsWith(QLatin1String("kick_")))
            w = QStringLiteral("[emote:%1:%2]").arg(e->key.mid(5), w);
    }
    return words.join(QLatin1Char(' '));
}

bool KickChatController::send(const QString &textIn, const QString &replyToId)
{
    const QString text = textIn.trimmed();
    if (text.isEmpty()) return false;
    if (!m_auth->loggedIn()) {
        emit sendResult(false, QStringLiteral("login"));
        return false;
    }
    if (m_channelUserId <= 0) {
        emit sendResult(false, QStringLiteral("channel"));
        return false;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastSendMs < 1000) {
        emit sendResult(false, QStringLiteral("rate"));
        return false;
    }
    m_lastSendMs = now;
    learnOwnMessage(text);
    const QString content = resolveEmoteNames(text).left(500);
    m_auth->sendChat(m_channelUserId, content, replyToId, [this](bool ok, const QString &err) {
        emit sendResult(ok, err);
        if (!ok && m_host) m_host->log(m_id, QStringLiteral("warning"), QStringLiteral("Senden fehlgeschlagen: ") + err);
    });
    return true;
}

QVariantList KickChatController::previewSegments(const QString &text)
{
    return buildSegments(text);
}

QVariantList KickChatController::inputEmotes(const QString &text)
{
    QVariantList out;
    auto add = [&](int start, int length, const QString &key) {
        const QString src = emoteSrc(key);              // startet ggf. den Download (emoteImageReady)
        if (src.isEmpty()) return;
        out.append(QVariantMap{{QStringLiteral("start"), start}, {QStringLiteral("length"), length},
                               {QStringLiteral("src"), src}, {QStringLiteral("anim"), m_cache->isAnimated(key)}});
    };
    int i = 0;
    const int n = int(text.size());
    while (i < n) {
        while (i < n && text.at(i).isSpace()) ++i;
        const int start = i;
        while (i < n && !text.at(i).isSpace()) ++i;
        if (i == start) break;
        const QString word = text.mid(start, i - start);
        const auto m = emoteTokenRe().match(word);
        if (m.hasMatch() && m.capturedStart() == 0 && m.capturedLength() == word.size()) {
            add(start, int(word.size()), QStringLiteral("kick_") + m.captured(1));
            continue;
        }
        const auto e = m_emotes.constFind(word);
        if (e != m_emotes.constEnd()) add(start, int(word.size()), e->key);
    }
    return out;
}

qreal KickChatController::imageAspect(const QString &source)
{
    const auto known = m_imageAspects.constFind(source);
    if (known != m_imageAspects.constEnd()) return known.value();
    const QUrl url(source);
    QImageReader reader(url.isLocalFile() ? url.toLocalFile() : source);
    reader.setDecideFormatFromContent(true);
    const QSize size = reader.size();
    if (size.isEmpty()) return 1.0;                   // nicht merken: Datei kann noch kommen
    const qreal aspect = qreal(size.width()) / size.height();
    m_imageAspects.insert(source, aspect);
    return aspect;
}

void KickChatController::setInputEmotes(QObject *textEdit, const QVariantList &ranges)
{
    InputEmoteStyler::apply(textEdit, ranges);
}

// =========================================================================== Medien → Kanal
QString KickChatController::guessChannel(const QString &text)
{
    static const QRegularExpression re{
        QStringLiteral("(?:^|[^a-z0-9.])(?:www\\.|m\\.|player\\.)?kick\\.com/(?:popout/|embed/)?([a-z0-9_-]{2,64})(?:[/?#\\s]|$)"),
        QRegularExpression::CaseInsensitiveOption};
    static const QStringList reserved{
        QStringLiteral("video"), QStringLiteral("videos"), QStringLiteral("clips"), QStringLiteral("categories"),
        QStringLiteral("category"), QStringLiteral("browse"), QStringLiteral("following"), QStringLiteral("search"),
        QStringLiteral("api"), QStringLiteral("emotes"), QStringLiteral("dashboard"), QStringLiteral("settings"),
        QStringLiteral("subscriptions"), QStringLiteral("terms-of-service"), QStringLiteral("privacy-policy"),
        QStringLiteral("community-guidelines"), QStringLiteral("dmca-policy"), QStringLiteral("help"),
        QStringLiteral("login"), QStringLiteral("signup"), QStringLiteral("stream"), QStringLiteral("auth"),
        QStringLiteral("embed"), QStringLiteral("popout")};
    const auto m = re.match(text);
    if (!m.hasMatch()) return QString();
    const QString c = m.captured(1).toLower();
    return reserved.contains(c) ? QString() : c;
}

void KickChatController::startFollowing()
{
    if (m_autoChannel && m_host) {
        const QString path = m_host->currentMediaPath();
        if (!path.isEmpty() && tryFollow(path, false)) return;
    }
    if (!m_channel.isEmpty() && !m_wantOpen) openChat();
}

bool KickChatController::tryFollow(const QString &text, bool notify)
{
    if (!m_autoChannel || text.isEmpty()) return false;
    const QString ch = guessChannel(text);
    if (ch.isEmpty()) return false;
    if (ch == m_channel) {
        if (!m_wantOpen) openChat();
        return true;
    }
    setChannel(ch);
    if (m_host) {
        m_host->log(m_id, QStringLiteral("info"), QStringLiteral("Auto-Kanal aus Medien erkannt: %1").arg(ch));
        if (notify) m_host->showOsd(QStringLiteral("Kick Chat: %1").arg(ch), 2500);
    }
    return true;
}

void KickChatController::onMediaEvent(const QString &name, const QVariantMap &data)
{
    if (name == QLatin1String("media.loading")) {
        tryFollow(data.value(QStringLiteral("url")).toString(), false);
    } else if (name == QLatin1String("media.loaded")) {
        if (!tryFollow(data.value(QStringLiteral("source")).toString(), true)
            && !tryFollow(data.value(QStringLiteral("path")).toString(), true))
            tryFollow(data.value(QStringLiteral("title")).toString(), true);
    }
}

void KickChatController::onMediaPath(const QString &path)
{
    tryFollow(path, false);
}

// =========================================================================== Benachrichtigungen
namespace {
const char *const kBoxKinds[][2] = {
    {"mention", "#53fc18"}, {"system", "#f0a500"}, {"follow", "#4a9eff"},
    {"kicks", "#ff7ac6"}, {"bot", "#a970ff"}, {"mod", "#ff5c5c"},
};
// Bekannte Bots auf Kick, die Follows, Spenden usw. ankündigen
const char *const kKnownBots[][2] = {
    {"BotRix", "BotRix"}, {"KickBot", "KickBot"}, {"Kicklet", "Kicklet"},
    {"StreamElements", "StreamElements"}, {"Streamlabs", "Streamlabs"}, {"Fossabot", "Fossabot"},
    {"Nightbot", "Nightbot"}, {"Sery_Bot", "Sery_Bot"}, {"WizeBot", "WizeBot"}, {"Moobot", "Moobot"},
    {"TipeeeStream", "TipeeeStream"}, {"SoundAlerts", "SoundAlerts"}, {"Ko-fi", "Ko-fi"},
};
bool validColor(const QString &c)
{
    static const QRegularExpression re{QStringLiteral("^#[0-9a-fA-F]{6}$")};
    return re.match(c).hasMatch();
}
} // namespace

void KickChatController::loadBoxSettings(const QJsonObject &s)
{
    m_boxColors.clear();
    const QJsonObject colors = s.value(QStringLiteral("boxColors")).toObject();
    for (const auto &k : kBoxKinds) {
        const QString c = colors.value(QLatin1String(k[0])).toString();
        if (validColor(c)) m_boxColors.insert(QLatin1String(k[0]), c);
    }
    m_eventBots.clear();
    if (s.contains(QStringLiteral("eventBots"))) {      // eigene Liste: gelöschte Standard-Bots bleiben weg
        for (const QJsonValue &v : s.value(QStringLiteral("eventBots")).toArray()) {
            const QJsonObject b = v.toObject();
            const QString name = b.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty() || !eventBotLabel(name).isNull()) continue;
            const QString label = b.value(QStringLiteral("label")).toString().trimmed();
            const QString color = b.value(QStringLiteral("color")).toString();
            m_eventBots.append({name, label.isEmpty() ? name : label, validColor(color) ? color : QString()});
        }
    } else {
        for (const auto &b : kKnownBots) m_eventBots.append({QLatin1String(b[0]), QLatin1String(b[1]), QString()});
    }
}

void KickChatController::persistBoxSettings()
{
    if (!m_host) return;
    QJsonObject s = m_host->loadPluginSettings(m_id);
    QJsonObject colors;
    for (auto it = m_boxColors.constBegin(); it != m_boxColors.constEnd(); ++it) colors.insert(it.key(), it.value());
    s.insert(QStringLiteral("boxColors"), colors);
    QJsonArray bots;
    for (const EventBot &b : m_eventBots)
        bots.append(QJsonObject{{QStringLiteral("name"), b.name}, {QStringLiteral("label"), b.label},
                                {QStringLiteral("color"), b.color}});
    s.insert(QStringLiteral("eventBots"), bots);
    m_host->savePluginSettings(m_id, s);
}

void KickChatController::setNotifySound(bool on)
{
    if (on == m_notifySound) return;
    m_notifySound = on;
    persist();
    emit notifyChanged();
}

void KickChatController::setNotifyVolume(int percent)
{
    percent = std::clamp(percent, 0, 100);
    if (percent == m_sound.volumePercent()) return;
    m_sound.setVolumePercent(percent);
    persist();
    emit notifyChanged();
}

void KickChatController::previewNotifySound()
{
    m_sound.play();
}

QVariantMap KickChatController::defaultBoxColors() const
{
    QVariantMap m;
    for (const auto &k : kBoxKinds) m.insert(QLatin1String(k[0]), QLatin1String(k[1]));
    return m;
}

QVariantMap KickChatController::boxColors() const
{
    QVariantMap m = defaultBoxColors();
    for (auto it = m_boxColors.constBegin(); it != m_boxColors.constEnd(); ++it) m.insert(it.key(), it.value());
    return m;
}

void KickChatController::setBoxColor(const QString &kind, const QString &color)
{
    if (!defaultBoxColors().contains(kind)) return;
    if (color.isEmpty() || color.compare(defaultBoxColors().value(kind).toString(), Qt::CaseInsensitive) == 0)
        m_boxColors.remove(kind);
    else if (validColor(color)) m_boxColors.insert(kind, color);
    else return;
    persistBoxSettings();
    emit boxSettingsChanged();
}

void KickChatController::resetBoxColors()
{
    m_boxColors.clear();
    persistBoxSettings();
    emit boxSettingsChanged();
}

QVariantList KickChatController::eventBots() const
{
    QVariantList out;
    for (const EventBot &b : m_eventBots)
        out.append(QVariantMap{{QStringLiteral("name"), b.name}, {QStringLiteral("label"), b.label},
                               {QStringLiteral("color"), b.color}});
    return out;
}

QString KickChatController::eventBotLabel(const QString &nameIn) const
{
    // Manche Bot-Konten heißen auf Kick mit „@“ davor (z. B. „@StreamElements“)
    const QString name = nameIn.startsWith(QLatin1Char('@')) ? nameIn.mid(1) : nameIn;
    for (const EventBot &b : m_eventBots)
        if (b.name.compare(name, Qt::CaseInsensitive) == 0) return b.label;
    return QString();
}

bool KickChatController::setEventBot(const QString &nameIn, const QString &labelIn, const QString &color,
                                     const QString &oldName)
{
    static const QRegularExpression valid{QStringLiteral("^[A-Za-z0-9_-]{2,40}$")};
    const QString name = nameIn.trimmed();
    if (!valid.match(name).hasMatch()) return false;
    const QString label = labelIn.trimmed().isEmpty() ? name : labelIn.trimmed().left(30);
    const QString c = validColor(color) ? color : QString();
    const QString key = oldName.isEmpty() ? name : oldName;
    for (EventBot &b : m_eventBots) {
        if (b.name.compare(key, Qt::CaseInsensitive) == 0) {
            b = {name, label, c};
            persistBoxSettings();
            emit boxSettingsChanged();
            return true;
        }
    }
    for (const EventBot &b : m_eventBots)
        if (b.name.compare(name, Qt::CaseInsensitive) == 0) return false;
    m_eventBots.append({name, label, c});
    persistBoxSettings();
    emit boxSettingsChanged();
    return true;
}

void KickChatController::removeEventBot(const QString &name)
{
    for (int i = 0; i < m_eventBots.size(); ++i) {
        if (m_eventBots[i].name.compare(name, Qt::CaseInsensitive) == 0) {
            m_eventBots.removeAt(i);
            persistBoxSettings();
            emit boxSettingsChanged();
            return;
        }
    }
}

void KickChatController::resetEventBots()
{
    m_eventBots.clear();
    for (const auto &b : kKnownBots) m_eventBots.append({QLatin1String(b[0]), QLatin1String(b[1]), QString()});
    persistBoxSettings();
    emit boxSettingsChanged();
}

void KickChatController::setShowFollows(bool on)
{
    if (on == m_showFollows) return;
    m_showFollows = on;
    persist();
    emit boxSettingsChanged();
}

// =========================================================================== Rolle / Nutzer / Moderation
void KickChatController::setMyRole(const QString &role)
{
    if (role == m_myRole) return;
    m_myRole = role;
    emit roleChanged();
}

static QVariantMap userInfoFromJson(const QJsonObject &o)
{
    auto ms = [&o](const char *key) -> double {
        const QString t = str(o, key);
        return t.isEmpty() ? 0.0 : double(parseTime(t));
    };
    QVariantMap info;
    info.insert(QStringLiteral("id"), QString::number(num(o.value(QStringLiteral("id")))));
    info.insert(QStringLiteral("name"), str(o, "username"));
    info.insert(QStringLiteral("slug"), str(o, "slug"));
    info.insert(QStringLiteral("avatar"), str(o, "profile_pic"));
    info.insert(QStringLiteral("createdAt"), ms("created_at"));
    info.insert(QStringLiteral("followingSince"), ms("following_since"));
    info.insert(QStringLiteral("subscribedFor"), o.value(QStringLiteral("subscribed_for")).toInt());
    info.insert(QStringLiteral("isOwner"), o.value(QStringLiteral("is_channel_owner")).toBool());
    info.insert(QStringLiteral("isModerator"), o.value(QStringLiteral("is_moderator")).toBool());
    info.insert(QStringLiteral("isStaff"), o.value(QStringLiteral("is_staff")).toBool());
    QStringList badges;
    bool vip = false;
    for (const QJsonValue &v : o.value(QStringLiteral("badges")).toArray()) {
        const QJsonObject b = v.toObject();
        badges << str(b, "text");
        if (str(b, "type") == QLatin1String("vip")) vip = true;
    }
    info.insert(QStringLiteral("badges"), badges);
    QString role = QStringLiteral("user");
    if (info.value(QStringLiteral("isOwner")).toBool()) role = QStringLiteral("broadcaster");
    else if (info.value(QStringLiteral("isModerator")).toBool()) role = QStringLiteral("moderator");
    else if (vip) role = QStringLiteral("vip");
    info.insert(QStringLiteral("role"), role);
    const QJsonValue banned = o.value(QStringLiteral("banned"));
    info.insert(QStringLiteral("banned"), banned.isObject() || (banned.isBool() && banned.toBool()));
    if (banned.isObject()) {
        const QJsonObject b = banned.toObject();
        info.insert(QStringLiteral("bannedPermanent"), b.value(QStringLiteral("permanent")).toBool());
        info.insert(QStringLiteral("bannedReason"), str(b, "reason"));
        const QString until = str(b, "expires_at");
        info.insert(QStringLiteral("bannedUntil"), until.isEmpty() ? 0.0 : double(parseTime(until)));
    }
    return info;
}

void KickChatController::fetchMyRole()
{
    if (!m_auth->loggedIn() || m_auth->userName().isEmpty()) {
        setMyRole(QStringLiteral("anonymous"));
        return;
    }
    if (m_channel.isEmpty()) { setMyRole(QStringLiteral("user")); return; }
    if (m_auth->userName().compare(m_channel, Qt::CaseInsensitive) == 0
        || m_auth->userName().compare(m_channelDisplay, Qt::CaseInsensitive) == 0) {
        setMyRole(QStringLiteral("broadcaster"));
        return;
    }
    const quint64 gen = m_generation;
    QNetworkReply *reply = nam()->get(jsonRequest(QStringLiteral("https://kick.com/api/v2/channels/%1/users/%2")
                                                      .arg(m_channel, QString::fromUtf8(QUrl::toPercentEncoding(m_auth->userName())))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        if (gen != m_generation) return;
        if (reply->error() != QNetworkReply::NoError) { setMyRole(QStringLiteral("user")); return; }
        const QVariantMap info = userInfoFromJson(QJsonDocument::fromJson(reply->readAll()).object());
        setMyRole(info.value(QStringLiteral("role")).toString());
    });
}

bool KickChatController::isBlocked(const QString &name) const
{
    return m_blocked.contains(name.toLower());
}

void KickChatController::requestUserInfo(const QString &name)
{
    if (m_channel.isEmpty() || name.isEmpty()) return;
    QNetworkReply *reply = nam()->get(jsonRequest(QStringLiteral("https://kick.com/api/v2/channels/%1/users/%2")
                                                      .arg(m_channel, QString::fromUtf8(QUrl::toPercentEncoding(name)))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, name]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit userInfo(name, QVariantMap{{QStringLiteral("error"), reply->errorString()}});
            return;
        }
        emit userInfo(name, userInfoFromJson(QJsonDocument::fromJson(reply->readAll()).object()));
    });
}

void KickChatController::userAction(const QString &action, const QVariantMap &target)
{
    const QString name = target.value(QStringLiteral("name")).toString();
    if (action == QLatin1String("block") || action == QLatin1String("unblock")) {
        if (name.isEmpty()) return;
        const QString key = name.toLower();
        if (action == QLatin1String("block")) {
            if (!m_blocked.contains(key)) m_blocked.append(key);
            for (QVariantMap &m : m_history) {           // bereits sichtbare Nachrichten ausgrauen
                if (m.value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) == 0
                    && !m.value(QStringLiteral("deleted")).toBool()) {
                    m.insert(QStringLiteral("deleted"), true);
                    emit messageUpdated(m.value(QStringLiteral("id")).toString(), m);
                }
            }
        } else {
            m_blocked.removeAll(key);
        }
        persist();
        emit blockedChanged();
        emit userActionDone(action, true, QString());
        return;
    }
    if (!m_auth->loggedIn()) { emit userActionDone(action, false, QStringLiteral("login")); return; }

    auto finish = [this, action, name](bool ok, int status, const QByteArray &, const QString &err) {
        const QString e = status == 403 ? QStringLiteral("forbidden") : err;
        emit userActionDone(action, ok, e);
        if (m_host && !ok)
            m_host->log(m_id, QStringLiteral("warning"),
                        QStringLiteral("Moderation %1 (%2) fehlgeschlagen: %3").arg(action, name, err));
    };

    if (action == QLatin1String("delete")) {
        const QString msgId = target.value(QStringLiteral("messageId")).toString();
        static const QRegularExpression uuid{QStringLiteral("^[0-9a-fA-F-]{8,64}$")};
        if (!uuid.match(msgId).hasMatch()) { emit userActionDone(action, false, QStringLiteral("message")); return; }
        m_auth->api("DELETE", QStringLiteral("/public/v1/chat/") + msgId, QJsonObject(), finish);
        return;
    }
    const qint64 userId = target.value(QStringLiteral("userId")).toString().toLongLong();
    if (userId <= 0 || m_channelUserId <= 0) { emit userActionDone(action, false, QStringLiteral("user")); return; }
    QJsonObject body{{QStringLiteral("broadcaster_user_id"), double(m_channelUserId)},
                     {QStringLiteral("user_id"), double(userId)}};
    if (action == QLatin1String("timeout") || action == QLatin1String("ban")) {
        if (action == QLatin1String("timeout"))
            body.insert(QStringLiteral("duration"), std::clamp(target.value(QStringLiteral("duration")).toInt(), 1, 10080));
        const QString reason = target.value(QStringLiteral("reason")).toString().trimmed().left(100);
        if (!reason.isEmpty()) body.insert(QStringLiteral("reason"), reason);
        m_auth->api("POST", QStringLiteral("/public/v1/moderation/bans"), body, finish);
    } else if (action == QLatin1String("unban")) {
        m_auth->api("DELETE", QStringLiteral("/public/v1/moderation/bans"), body, finish);
    } else {
        emit userActionDone(action, false, QStringLiteral("unknown"));
    }
}

// =========================================================================== Umfrage
void KickChatController::applyPollJson(const QJsonObject &data)
{
    const QJsonObject p = data.value(QStringLiteral("poll")).isObject() ? data.value(QStringLiteral("poll")).toObject() : data;
    const QString title = str(p, "title");
    const QJsonArray opts = p.value(QStringLiteral("options")).toArray();
    if (title.isEmpty() || opts.isEmpty()) return;
    int total = 0, best = -1;
    for (const QJsonValue &v : opts) {
        const int votes = v.toObject().value(QStringLiteral("votes")).toInt();
        total += votes;
        best = std::max(best, votes);
    }
    QVariantList options;
    for (const QJsonValue &v : opts) {
        const QJsonObject o = v.toObject();
        const int votes = o.value(QStringLiteral("votes")).toInt();
        options.append(QVariantMap{{QStringLiteral("id"), o.value(QStringLiteral("id")).toVariant().toString()},
                                   {QStringLiteral("label"), str(o, "label")},
                                   {QStringLiteral("votes"), votes},
                                   {QStringLiteral("percent"), total > 0 ? qRound(votes * 100.0 / total) : 0},
                                   {QStringLiteral("leading"), total > 0 && votes == best}});
    }
    const int remaining = p.value(QStringLiteral("remaining")).toInt(p.value(QStringLiteral("duration")).toInt());
    const int showResult = p.value(QStringLiteral("result_display_duration")).toInt(15);
    m_poll = QVariantMap{{QStringLiteral("title"), title}, {QStringLiteral("options"), options},
                         {QStringLiteral("total"), total},
                         {QStringLiteral("endsAt"), double(QDateTime::currentMSecsSinceEpoch() + qint64(std::max(0, remaining)) * 1000)},
                         {QStringLiteral("ended"), remaining <= 0},
                         {QStringLiteral("showResultSec"), showResult}};
    m_pollTimer.start((remaining > 0 ? remaining : std::max(5, showResult)) * 1000);
    emit pollChanged();
}

void KickChatController::hidePoll()
{
    m_hiddenPoll = m_poll.value(QStringLiteral("title")).toString();
    emit pollChanged();
}

void KickChatController::showPoll()
{
    m_hiddenPoll.clear();
    emit pollChanged();
}

// =========================================================================== Rechtschreibung / Vorschläge
void KickChatController::setSpellDictionaryDir(const QString &dir)
{
    m_spell->setDictionaryDir(dir);
}

void KickChatController::setSpellGerman(bool on)
{
    if (m_spellGerman == on) return;
    m_spellGerman = on;
    m_spell->setLanguages(m_spellGerman, m_spellEnglish);
    persist();
    emit spellSettingsChanged();
}

void KickChatController::setSpellEnglish(bool on)
{
    if (m_spellEnglish == on) return;
    m_spellEnglish = on;
    m_spell->setLanguages(m_spellGerman, m_spellEnglish);
    persist();
    emit spellSettingsChanged();
}

void KickChatController::setPredictText(bool on)
{
    if (m_predictText == on) return;
    m_predictText = on;
    persist();
    emit spellSettingsChanged();
    emit predictionsChanged();
}

void KickChatController::ensureFrequencyWords()
{
    if (m_frequencyRequested) return;
    m_frequencyRequested = true;
    m_spell->loadFrequencyWords();
}

void KickChatController::spellCheck(const QString &text, int cursor)
{
    if (text != m_grammarText) {
        m_grammarText = text;
        m_remoteIssues.clear();     // Positionen gelten nur für den alten Text
    }
    if (m_predictText) ensureFrequencyWords();
    const auto isEmote = [this](const QString &w) { return isKnownEmote(w); };
    const bool anyLanguage = m_spellGerman || m_spellEnglish;
    const bool rules = anyLanguage && m_grammarCheck;
    QList<SpellChecker::Word> words;
    QList<SpellChecker::Word> rulesWords;
    if (anyLanguage) words = SpellChecker::tokenize(text, cursor, isEmote);
    if (rules) rulesWords = SpellChecker::ruleWords(text, isEmote);
    // Auch ein leeres Ergebnis läuft über den Auftrag: Es überholt so ältere Antworten nicht.
    m_spell->check(text, words, rulesWords, cursor, rules);

    // Befehle gehen nie ins Netz
    const bool command = text.startsWith(QLatin1Char('/')) || text.startsWith(QLatin1Char('!'));
    if (rules && m_grammarOnline && !command && rulesWords.size() >= 3)
        m_languageTool->schedule(text, SpellChecker::hiddenRanges(text, isEmote), m_spellGerman, m_spellEnglish);
    else
        m_languageTool->cancel();
}

// LanguageTool kennt den ganzen Satz und hat deshalb Vorrang, wenn beide dieselbe Stelle melden.
void KickChatController::emitGrammarResult()
{
    QVariantList merged = m_grammarOnline ? m_remoteIssues : QVariantList();
    const QVariantList remote = merged;
    for (const QVariant &local : std::as_const(m_localIssues)) {
        const QVariantMap l = local.toMap();
        const int start = l.value(QStringLiteral("start")).toInt();
        const int end = start + l.value(QStringLiteral("length")).toInt();
        bool overlaps = false;
        for (const QVariant &r : remote) {
            const QVariantMap m = r.toMap();
            const int rs = m.value(QStringLiteral("start")).toInt();
            if (start < rs + m.value(QStringLiteral("length")).toInt() && rs < end) { overlaps = true; break; }
        }
        if (!overlaps) merged << local;
    }
    std::stable_sort(merged.begin(), merged.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("start")).toInt() < b.toMap().value(QStringLiteral("start")).toInt();
    });
    emit grammarResult(m_grammarText, merged);
}

void KickChatController::setGrammarCheck(bool on)
{
    if (m_grammarCheck == on) return;
    m_grammarCheck = on;
    if (!on) { m_localIssues.clear(); m_remoteIssues.clear(); m_languageTool->cancel(); emitGrammarResult(); }
    persist();
    emit spellSettingsChanged();
}

void KickChatController::setGrammarOnline(bool on)
{
    if (m_grammarOnline == on) return;
    m_grammarOnline = on;
    m_grammarOnlineChosen = true;                  // bewusst umgeschaltet → bleibt so
    if (!on) { m_remoteIssues.clear(); m_languageTool->cancel(); emitGrammarResult(); }
    persist();
    emit spellSettingsChanged();
}

void KickChatController::requestSpellSuggestions(const QString &word)
{
    m_spell->requestSuggestions(word);
}

void KickChatController::addSpellWord(const QString &word)
{
    const QString lower = word.trimmed().toLower();
    if (lower.isEmpty() || m_spellWords.contains(lower)) return;
    m_spellWords.insert(lower);
    m_spell->setUserWords(m_spellWords);
    persist();
    emit spellSettingsChanged();
}

QStringList KickChatController::emoteNamesWithPrefix(const QString &prefix) const
{
    QStringList out;
    if (prefix.isEmpty()) return out;
    for (auto it = m_emotes.constBegin(); it != m_emotes.constEnd(); ++it) {
        if (it.key().startsWith(prefix, Qt::CaseInsensitive)) out.append(it.key());
        if (out.size() >= 200) break;
    }
    return out;
}

QStringList KickChatController::predictWords(const QString &text, int cursor)
{
    if (!m_predictText) return {};
    ensureFrequencyWords();
    // @-Namen zeigt die eigene Erwähnungsleiste
    const int start = TextPredictor::wordStart(text, cursor);
    if (start < text.size() && start < cursor && text.at(start) == QLatin1Char('@')) return {};
    return m_predictor.predict(text, cursor, m_spellGerman, m_spellEnglish,
                               [this](const QString &w) { return isKnownEmote(w); },
                               [this](const QString &p) { return emoteNamesWithPrefix(p); });
}

int KickChatController::predictionStart(const QString &text, int cursor) const
{
    return TextPredictor::wordStart(text, cursor);
}

void KickChatController::learnOwnMessage(const QString &text)
{
    if (!m_predictText) return;
    m_predictor.learn(text, true, [this](const QString &w) { return isKnownEmote(w); });
    QFile file(m_predictFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(m_predictor.saveOwn()).toJson(QJsonDocument::Compact));
}
