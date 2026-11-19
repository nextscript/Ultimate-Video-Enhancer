#include "TwitchChatController.h"
#include "LanguageToolClient.h"
#include "TwitchAuthService.h"
#include "WhisperService.h"
#include "SpellChecker.h"
#include "EmoteCache.h"
#include "InputEmoteStyler.h"
#include "TokenVault.h"
#include <QImageReader>
#include <QMouseEvent>

#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <iterator>
#include <QUrlQuery>
#include <algorithm>

static const char *kIrcHost = "irc-ws.chat.twitch.tv";
static const quint16 kIrcPort = 443;
static const int kHistoryMax = 400;
static const QList<QPair<QString, QString>> &knownEventBots();   // weiter unten: Bots, die Events ankündigen
// Eigene Twitch-App des Nutzers für „Mit Twitch anmelden“ (öffentlicher Client, kein Secret)
static const char kAppClientId[] = "iaecrrj7jo49svbbbpg1o33gzcajt0";
static const char kAppScopes[] = "analytics:read:extensions analytics:read:games bits:read channel:bot channel:edit:commercial channel:manage:ads channel:manage:broadcast channel:manage:clips channel:manage:extensions channel:manage:guest_star channel:manage:moderators channel:manage:polls channel:manage:predictions channel:manage:raids channel:manage:redemptions channel:manage:schedule channel:manage:videos channel:manage:vips channel:moderate channel:read:ads channel:read:charity channel:read:editors channel:read:goals channel:read:guest_star channel:read:hype_train channel:read:polls channel:read:predictions channel:read:redemptions channel:read:stream_key channel:read:subscriptions channel:read:vips chat:edit chat:read clips:edit editor:manage:clips moderation:read moderator:manage:announcements moderator:manage:automod moderator:manage:automod_settings moderator:manage:banned_users moderator:manage:blocked_terms moderator:manage:chat_messages moderator:manage:chat_settings moderator:manage:guest_star moderator:manage:shield_mode moderator:manage:shoutouts moderator:manage:suspicious_users moderator:manage:unban_requests moderator:manage:warnings moderator:read:automod_settings moderator:read:banned_users moderator:read:blocked_terms moderator:read:chat_messages moderator:read:chat_settings moderator:read:chatters moderator:read:followers moderator:read:guest_star moderator:read:moderators moderator:read:shield_mode moderator:read:shoutouts moderator:read:suspicious_users moderator:read:unban_requests moderator:read:vips moderator:read:warnings user:bot user:edit user:edit:broadcast user:manage:blocked_users user:manage:chat_color user:manage:whispers user:read:blocked_users user:read:broadcast user:read:chat user:read:email user:read:emotes user:read:follows user:read:moderated_channels user:read:subscriptions user:read:whispers user:write:chat whispers:read";
static constexpr qint64 kRenewBeforeMs = 10 * 60 * 1000;   // 10 Minuten vor Ablauf erneuern

TwitchChatController::TwitchChatController(uve::IPlayerHost *host, const QString &pluginId, QObject *parent)
    : QObject(parent), m_host(host), m_id(pluginId)
{
    m_sendClock.start();

    const QJsonObject s = host ? host->loadPluginSettings(pluginId) : QJsonObject();
    m_channel = s.value(QStringLiteral("channel")).toString().toLower();
    m_autoChannel = s.value(QStringLiteral("autoChannel")).toBool(true);
    m_autoScroll = s.value(QStringLiteral("autoscroll")).toBool(true);
    m_chatFontSize = qBound(0, s.value(QStringLiteral("chatFontSize")).toInt(0), 40);
    m_notifySound = s.value(QStringLiteral("notifySound")).toBool(true);
    loadBoxSettings(s);
    // Login (user/token) steht nicht mehr in den Einstellungen – es liegt
    // verschlüsselt im TokenVault; applyVault() liest es beim Start ein.

    // Bekannte Twitch-Raum-IDs (aus vorherigen Sitzungen): damit die Drittanbieter-Kataloge
    // eines Kanals schon beim Beitritt geladen werden können, bevor die erste
    // Nachricht die room-id-Markierung mitbringt.
    const QJsonObject rooms = s.value(QStringLiteral("roomIds")).toObject();
    for (auto it = rooms.constBegin(); it != rooms.constEnd(); ++it)
        m_roomIds.insert(it.key(), it.value().toString());
    m_roomId = m_roomIds.value(m_channel);

    // Favoriten-Emotes aus früheren Sitzungen (Name → Emote-ID); keine Tokens –
    // die liegen weiterhin verschlüsselt im TokenVault.
    const QJsonArray favs = s.value(QStringLiteral("favoriteEmotes")).toArray();
    for (const QJsonValue &v : favs) {
        const QJsonObject o = v.toObject();
        const QString n = o.value(QStringLiteral("name")).toString();
        if (!n.isEmpty())
            m_favoriteEmotes.insert(n, o.value(QStringLiteral("id")).toString());
    }

    m_spellGerman = s.value(QStringLiteral("spellGerman")).toBool(true);
    m_spellEnglish = s.value(QStringLiteral("spellEnglish")).toBool(true);
    const QJsonArray spellWords = s.value(QStringLiteral("spellWords")).toArray();
    for (const QJsonValue &v : spellWords)
        m_spellWords.insert(v.toString());
    m_spell = new SpellChecker(this);
    m_spell->setLanguages(m_spellGerman, m_spellEnglish);
    m_spell->setUserWords(m_spellWords);
    m_grammarCheck = s.value(QStringLiteral("grammarCheck")).toBool(true);
    m_grammarOnline = s.value(QStringLiteral("grammarOnline")).toBool(true);
    m_predictText = s.value(QStringLiteral("predictText")).toBool(true);
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
    connect(m_spell, &SpellChecker::checked, this, [this](const QString &text, const QVariantList &ranges, const QVariantList &issues) {
        emit spellResult(text, ranges);
        if (text != m_grammarText) return;
        m_localIssues = issues;
        emitGrammarResult();
    });
    connect(m_spell, &SpellChecker::frequencyWordsReady, this, [this](const QStringList &german, const QStringList &english) {
        m_predictor.setFrequencyWords(german, english);
        emit predictionsChanged();
    });
    connect(m_spell, &SpellChecker::suggestionsReady, this, &TwitchChatController::spellSuggestions);
    connect(m_spell, &SpellChecker::dictionaryMissing, this, [this](const QString &path) {
        if (m_host)
            m_host->log(m_id, QStringLiteral("warning"), QStringLiteral("Wörterbuch fehlt: %1").arg(path));
    });

    QString dataDir = host ? host->pluginDataDir(pluginId) : QString();
    if (dataDir.isEmpty()) dataDir = QDir::tempPath() + QStringLiteral("/twitchchat");
    m_emotes = new EmoteCache(dataDir + QStringLiteral("/emotes"), this);
    m_predictFile = dataDir + QStringLiteral("/predict.json");
    m_hiddenPin = s.value(QStringLiteral("hiddenPin")).toString();
    for (const char *prefix : {"cheer", "doodlecheer", "biblethump", "cheerwhal", "corgo", "uni", "showlove", "party",
                               "seemsgood", "pride", "kappa", "frankerz", "heyguys", "dansgame", "elegiggle", "trihard",
                               "kreygasm", "4head", "swiftrage", "notlikethis", "failfish", "vohiyo", "pjsalt",
                               "mrdestructoid", "bday", "ripcheer", "shamrock"}) {
        QVector<CheerTier> tiers;
        for (int tier : {1, 100, 1000, 5000, 10000})
            tiers << CheerTier{tier, QStringLiteral("https://d3aqoihi2n8ty8.cloudfront.net/actions/%1/dark/animated/%2/2.gif")
                                         .arg(QLatin1String(prefix)).arg(tier)};
        m_cheermotes.insert(QLatin1String(prefix), tiers);
    }
    m_whispers = new WhisperService(dataDir + QStringLiteral("/whispers.dat"), (pluginId + QStringLiteral("-whispers")).toUtf8(), this);
    if (!m_net) m_net = new QNetworkAccessManager(this);
    m_auth = new TwitchAuthService(m_net, this);
    m_renewTimer = new QTimer(this);
    m_renewTimer->setSingleShot(true);
    connect(m_renewTimer, &QTimer::timeout, this, [this] {
        if (!m_refreshToken.isEmpty()) m_auth->resume(QString::fromLatin1(kAppClientId), QString(), m_refreshToken, 0);
    });
    connect(m_auth, &TwitchAuthService::deviceReady, this, [this](const QString &code, const QString &url) {
        m_loginCode = code;
        m_loginUrl = url;
        emit loginStateChanged();
        // Hier statt im Panel öffnen: Hängen mehrere Panels am Controller, geht der Browser nur einmal auf.
        QDesktopServices::openUrl(QUrl(url));
        emit loginUrlReady(url);
    });
    connect(m_auth, &TwitchAuthService::signedIn, this,
            [this](const QString &access, const QString &refresh, qint64 expiresAt, const QStringList &) {
        if (!m_loginInteractive) { renewedToken(access, refresh, expiresAt); return; }
        // Neue Anmeldung: Kontoname vom Token erfragen, dann wie „Speichern“ übernehmen
        QNetworkRequest req{QUrl(QStringLiteral("https://id.twitch.tv/oauth2/validate"))};
        req.setRawHeader("Authorization", QByteArray("OAuth ") + access.toUtf8());
        QNetworkReply *reply = m_net->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, access, refresh, expiresAt] {
            reply->deleteLater();
            const QString login = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("login")).toString();
            if (login.isEmpty()) { setLoginState(QStringLiteral("error"), QStringLiteral("Twitch hat den neuen Token nicht bestätigt")); return; }
            completeLogin(login, access, refresh, expiresAt);
        });
    });
    connect(m_auth, &TwitchAuthService::authFailed, this, [this](const QString &reason, const QString &detail) {
        if (m_loginInteractive) {
            m_loginInteractive = false;
            setLoginState(QStringLiteral("error"), reason == QLatin1String("device_expired")
                              ? QStringLiteral("Der Code ist abgelaufen – bitte erneut „Mit Twitch anmelden“.")
                              : reason == QLatin1String("access_denied") ? QStringLiteral("Bei Twitch abgelehnt.")
                              : QStringLiteral("Anmeldung fehlgeschlagen: %1").arg(detail));
            return;
        }
        // Erneuerung gescheitert: Netzwerk → später erneut; Schlüssel ungültig → neu anmelden
        if (reason == QLatin1String("network")) { m_renewTimer->start(60000); return; }
        m_refreshToken.clear();
        storeRenewal();
        setLoginState(QStringLiteral("error"), QStringLiteral("Anmeldung abgelaufen – bitte erneut „Mit Twitch anmelden“."));
    });
    // Wunsch des Nutzers: Ton für private Nachrichten ab Werk auf 30 %, im Postfach einstellbar
    m_sound.setVolumePercent(qBound(0, s.value(QStringLiteral("notifyVolume")).toInt(30), 100));
    // AutoMod-Meldungen kommen über die EventSub-Verbindung des Postfachs
    connect(m_whispers, &WhisperService::sessionReady, this, &TwitchChatController::ensureAutomodSubscriptions);
    connect(m_whispers, &WhisperService::eventNotification, this, [this](const QString &type, const QJsonObject &event) {
        if (type.startsWith(QLatin1String("automod.message."))) handleAutomodEvent(type, event);
        else if (type == QLatin1String("channel.follow")) handleFollowEvent(event);
    });
    // Sobald das Konto bekannt ist: blockierte Nutzer laden (deren Nachrichten werden ausgeblendet)
    connect(m_whispers, &WhisperService::stateChanged, this, [this] {
        if (!m_blockedFetched && !m_whispers->userId().isEmpty()) {
            m_blockedFetched = true;
            fetchBlocked();
        }
    });
    connect(m_whispers, &WhisperService::messageAdded, this, [this](const QString &, bool outgoing) {
        if (!outgoing && m_notifySound) m_sound.play();
    });
    m_chatterNotify = new QTimer(this);
    m_chatterNotify->setSingleShot(true);
    m_chatterNotify->setInterval(400);
    connect(m_chatterNotify, &QTimer::timeout, this, &TwitchChatController::chattersChanged);
    m_chatterPoll = new QTimer(this);
    m_chatterPoll->setInterval(60000);
    connect(m_chatterPoll, &QTimer::timeout, this, [this] { fetchHelixChatters(QString(), {}); });
    m_pinCollapsed = s.value(QStringLiteral("pinCollapsed")).toBool(false);
    m_pinPoll = new QTimer(this);
    m_pinPoll->setInterval(45000);
    connect(m_pinPoll, &QTimer::timeout, this, &TwitchChatController::fetchPinned);
    connect(m_pinPoll, &QTimer::timeout, this, &TwitchChatController::fetchViewers);   // gleicher Takt: 45 s
    m_pinExpiry = new QTimer(this);
    m_pinExpiry->setSingleShot(true);
    connect(m_pinExpiry, &QTimer::timeout, this, &TwitchChatController::clearPinned);
    m_pollCollapsed = s.value(QStringLiteral("pollCollapsed")).toBool(false);
    m_predictionCollapsed = s.value(QStringLiteral("predictionCollapsed")).toBool(false);
    m_eventPoll = new QTimer(this);
    m_eventPoll->setInterval(20000);
    connect(m_eventPoll, &QTimer::timeout, this, &TwitchChatController::fetchChannelEvents);
    {
        QFile saved(m_predictFile);
        if (saved.open(QIODevice::ReadOnly))
            m_predictor.loadOwn(QJsonDocument::fromJson(saved.readAll()).object());
    }
    connect(m_emotes, &EmoteCache::ready, this, &TwitchChatController::onEmoteReady);

    connect(&m_ws, &MiniWebSocket::opened,      this, &TwitchChatController::onOpened);
    connect(&m_ws, &MiniWebSocket::textMessage, this, &TwitchChatController::onText);
    connect(&m_ws, &MiniWebSocket::closed,      this, &TwitchChatController::onClosed);
    connect(&m_ws, &MiniWebSocket::failed,      this, &TwitchChatController::onFailed);
}

// ------------------------------------------------------------ Rechtschreibprüfung
void TwitchChatController::setSpellDictionaryDir(const QString &dir)
{
    m_spell->setDictionaryDir(dir);
}

void TwitchChatController::setSpellGerman(bool on)
{
    if (m_spellGerman == on) return;
    m_spellGerman = on;
    m_spell->setLanguages(m_spellGerman, m_spellEnglish);
    persist();
    emit spellSettingsChanged();
}

void TwitchChatController::setSpellEnglish(bool on)
{
    if (m_spellEnglish == on) return;
    m_spellEnglish = on;
    m_spell->setLanguages(m_spellGerman, m_spellEnglish);
    persist();
    emit spellSettingsChanged();
}

bool TwitchChatController::isKnownEmote(const QString &word) const
{
    return m_tpLookup.contains(word) || m_observedEmotes.contains(word) || m_channelEmotes.contains(word)
        || m_globalEmotes.contains(word) || m_subEmotes.contains(word) || m_hypeOwned.contains(word)
        || m_ownEmotes.contains(word) || m_favoriteEmotes.contains(word);
}

void TwitchChatController::spellCheck(const QString &text, int cursor)
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

    // Befehle gehen nie ins Netz: „/w name …“ ist eine private Nachricht.
    const bool command = text.startsWith(QLatin1Char('/')) || text.startsWith(QLatin1Char('!'));
    if (rules && m_grammarOnline && !command && rulesWords.size() >= 3)
        m_languageTool->schedule(text, SpellChecker::hiddenRanges(text, isEmote), m_spellGerman, m_spellEnglish);
    else
        m_languageTool->cancel();
}

// LanguageTool kennt den ganzen Satz und hat deshalb Vorrang, wenn beide dieselbe Stelle melden.
void TwitchChatController::emitGrammarResult()
{
    QVariantList merged = m_remoteIssues;
    for (const QVariant &local : std::as_const(m_localIssues)) {
        const QVariantMap l = local.toMap();
        const int start = l.value(QStringLiteral("start")).toInt();
        const int end = start + l.value(QStringLiteral("length")).toInt();
        bool overlaps = false;
        for (const QVariant &remote : std::as_const(m_remoteIssues)) {
            const QVariantMap r = remote.toMap();
            const int remoteStart = r.value(QStringLiteral("start")).toInt();
            if (start < remoteStart + r.value(QStringLiteral("length")).toInt() && remoteStart < end) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) merged << local;
    }
    std::stable_sort(merged.begin(), merged.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("start")).toInt() < b.toMap().value(QStringLiteral("start")).toInt();
    });
    emit grammarResult(m_grammarText, merged);
}

// ------------------------------------------------------------ Angeheftete Nachricht
void TwitchChatController::fetchPinned()
{
    if (m_channel.isEmpty()) return;
    if (!m_net) m_net = new QNetworkAccessManager(this);
    // m_channel ist auf ^[a-z0-9_]{2,50}$ geprüft – sicher für die Abfrage.
    requestGql(QStringLiteral("{ channel(name: \"%1\") { pinnedChatMessages(first: 1) { edges { node { id endsAt "
                              "pinnedBy { displayName } pinnedMessage { content { text } "
                              "sender { displayName login chatColor } } } } } } }").arg(m_channel),
               QStringLiteral("pinned"));
}

void TwitchChatController::applyPinnedJson(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (!root.contains(QStringLiteral("data"))) return;      // Fehlerantwort: alten Stand behalten
    const QJsonArray edges = root.value(QStringLiteral("data")).toObject().value(QStringLiteral("channel")).toObject()
                                 .value(QStringLiteral("pinnedChatMessages")).toObject()
                                 .value(QStringLiteral("edges")).toArray();
    const QJsonObject node = edges.isEmpty() ? QJsonObject() : edges.first().toObject().value(QStringLiteral("node")).toObject();
    const QJsonObject message = node.value(QStringLiteral("pinnedMessage")).toObject();
    const QString text = message.value(QStringLiteral("content")).toObject().value(QStringLiteral("text")).toString().trimmed();
    const QString id = node.value(QStringLiteral("id")).toString();
    const QDateTime ends = QDateTime::fromString(node.value(QStringLiteral("endsAt")).toString(), Qt::ISODate);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (id.isEmpty() || text.isEmpty() || (ends.isValid() && ends.toMSecsSinceEpoch() <= now)) {
        clearPinned();
        return;
    }
    const QJsonObject sender = message.value(QStringLiteral("sender")).toObject();
    QVariantMap pin;
    pin.insert(QStringLiteral("id"), id);
    pin.insert(QStringLiteral("text"), text);
    pin.insert(QStringLiteral("sender"), sender.value(QStringLiteral("displayName")).toString());
    pin.insert(QStringLiteral("color"), sender.value(QStringLiteral("chatColor")).toString());
    pin.insert(QStringLiteral("pinnedBy"), node.value(QStringLiteral("pinnedBy")).toObject().value(QStringLiteral("displayName")).toString());
    pin.insert(QStringLiteral("endsAt"), ends.isValid() ? ends.toMSecsSinceEpoch() : 0);
    if (ends.isValid()) m_pinExpiry->start(int(qMin<qint64>(ends.toMSecsSinceEpoch() - now + 500, 24 * 3600 * 1000)));
    else m_pinExpiry->stop();
    const bool same = m_pinned.value(QStringLiteral("id")) == pin.value(QStringLiteral("id"))
                      && m_pinned.value(QStringLiteral("text")) == pin.value(QStringLiteral("text"))
                      && m_pinned.value(QStringLiteral("endsAt")) == pin.value(QStringLiteral("endsAt"));
    if (same) return;
    m_pinned = pin;
    rebuildPinned();
}

// Wie eine Chatzeile: Emotes als Bild, Links klickbar. Native Emotes erkennt Twitch hier nur am
// Namen, deshalb nimmt ownEmoteTag die bekannten Namen des Kontos.
void TwitchChatController::rebuildPinned()
{
    if (m_pinned.isEmpty()) return;
    const QString text = m_pinned.value(QStringLiteral("text")).toString();
    QStringList keys;
    m_pinned.insert(QStringLiteral("segments"), buildSegments(text, ownEmoteTag(text), keys));
    m_pinned.insert(QStringLiteral("emoteIds"), keys);
    emit pinnedChanged();
}

void TwitchChatController::clearPinned()
{
    m_pinExpiry->stop();
    if (m_pinned.isEmpty()) return;
    m_pinned.clear();
    emit pinnedChanged();
}

void TwitchChatController::hidePinned()
{
    const QString id = m_pinned.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || id == m_hiddenPin) return;
    m_hiddenPin = id;
    persist();
    emit pinnedChanged();
}

void TwitchChatController::setPinnedCollapsed(bool collapsed)
{
    if (m_pinCollapsed == collapsed) return;
    m_pinCollapsed = collapsed;
    persist();
    emit pinnedChanged();
}

void TwitchChatController::showPinned()
{
    if (m_hiddenPin.isEmpty()) return;
    m_hiddenPin.clear();
    persist();
    emit pinnedChanged();
}

// ------------------------------------------------------------ Umfrage und Vorhersage
// Wie die Anheftung: IRC meldet beides nicht, EventSub nur dem Streamer selbst. gql liefert
// beides ohne Login (user.viewablePoll, channel.*PredictionEvents).
static const qint64 kEventResultMs = 3 * 60 * 1000;   // Ergebnis nach dem Ende noch 3 min zeigen

void TwitchChatController::fetchChannelEvents()
{
    if (m_channel.isEmpty()) return;
    if (!m_net) m_net = new QNetworkAccessManager(this);
    const QString outcome = QStringLiteral("id title status createdAt predictionWindowSeconds lockedAt endedAt "
                                           "winningOutcome { id } outcomes { id title color totalPoints totalUsers }");
    // m_channel ist auf ^[a-z0-9_]{2,50}$ geprüft – sicher für die Abfrage.
    requestGql(QStringLiteral("{ user(login: \"%1\") { login viewablePoll { id title status startedAt endedAt durationSeconds "
                              "remainingDurationMilliseconds choices { id title votes { total } } votes { total } } "
                              "channel { activePredictionEvents { %2 } lockedPredictionEvents { %2 } "
                              "resolvedPredictionEvents(first: 1) { edges { node { %2 } } } } } }")
                   .arg(m_channel, outcome),
               QStringLiteral("events"));
}

static qint64 isoMs(const QJsonValue &v)
{
    const QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    return t.isValid() ? t.toMSecsSinceEpoch() : 0;
}

void TwitchChatController::applyChannelEventsJson(const QByteArray &json, qint64 nowMs)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (!root.contains(QStringLiteral("data")) || root.value(QStringLiteral("data")).isNull()) return;   // Fehler: alten Stand behalten
    const qint64 now = nowMs >= 0 ? nowMs : QDateTime::currentMSecsSinceEpoch();
    // Teilfehler (z. B. „service unavailable“ nur bei der Umfrage) → diesen Teil nicht anfassen
    bool pollFailed = false, predictionFailed = false;
    for (const QJsonValue &ev : root.value(QStringLiteral("errors")).toArray()) {
        const QJsonArray path = ev.toObject().value(QStringLiteral("path")).toArray();
        for (const QJsonValue &p : path) {
            const QString part = p.toString();
            if (part == QLatin1String("viewablePoll")) pollFailed = true;
            if (part.endsWith(QLatin1String("PredictionEvents")) || part == QLatin1String("channel")) predictionFailed = true;
            if (part == QLatin1String("user") && path.size() == 1) pollFailed = predictionFailed = true;
        }
    }
    const QJsonObject user = root.value(QStringLiteral("data")).toObject().value(QStringLiteral("user")).toObject();
    QVariantMap poll = m_poll, prediction = m_prediction;
    // Kanal fürs Abstimmen auf twitch.tv; nur Twitch-Logins zulassen
    static const QRegularExpression loginRe(QStringLiteral("^[a-z0-9_]{2,50}$"));
    QString login = user.value(QStringLiteral("login")).toString().toLower();
    if (!loginRe.match(login).hasMatch()) {
        login = m_prediction.value(QStringLiteral("channel"), m_poll.value(QStringLiteral("channel"))).toString();
        if (login.isEmpty() && loginRe.match(m_channel).hasMatch()) login = m_channel;
    }

    if (!pollFailed) {
        poll.clear();
        const QJsonObject p = user.value(QStringLiteral("viewablePoll")).toObject();
        const QString status = p.value(QStringLiteral("status")).toString();
        const bool active = status == QLatin1String("ACTIVE");
        const bool ended = status == QLatin1String("COMPLETED") || status == QLatin1String("TERMINATED");
        const qint64 started = isoMs(p.value(QStringLiteral("startedAt")));
        qint64 endedAt = isoMs(p.value(QStringLiteral("endedAt")));
        if (ended && endedAt == 0 && started > 0) endedAt = started + qint64(p.value(QStringLiteral("durationSeconds")).toInt()) * 1000;
        const bool recent = ended && (endedAt == 0 || now - endedAt < kEventResultMs);
        if (!p.value(QStringLiteral("id")).toString().isEmpty() && (active || recent)) {
            qint64 endsAt = 0;
            if (active) {
                const QJsonValue remaining = p.value(QStringLiteral("remainingDurationMilliseconds"));
                if (remaining.isDouble()) endsAt = now + qint64(remaining.toDouble());
                else if (started > 0) endsAt = started + qint64(p.value(QStringLiteral("durationSeconds")).toInt()) * 1000;
            }
            const QJsonArray choicesJson = p.value(QStringLiteral("choices")).toArray();
            qint64 total = 0, best = 0;
            for (const QJsonValue &c : choicesJson) {
                const qint64 v = qint64(c.toObject().value(QStringLiteral("votes")).toObject().value(QStringLiteral("total")).toDouble());
                total += v;
                best = qMax(best, v);
            }
            QVariantList choices;
            for (const QJsonValue &cv : choicesJson) {
                const QJsonObject c = cv.toObject();
                const qint64 v = qint64(c.value(QStringLiteral("votes")).toObject().value(QStringLiteral("total")).toDouble());
                QVariantMap choice;
                choice.insert(QStringLiteral("id"), c.value(QStringLiteral("id")).toString());
                choice.insert(QStringLiteral("title"), c.value(QStringLiteral("title")).toString());
                choice.insert(QStringLiteral("votes"), double(v));
                choice.insert(QStringLiteral("percent"), total > 0 ? int((v * 100 + total / 2) / total) : 0);
                choice.insert(QStringLiteral("leading"), v > 0 && v == best);
                choices << choice;
            }
            poll.insert(QStringLiteral("id"), p.value(QStringLiteral("id")).toString());
            poll.insert(QStringLiteral("title"), p.value(QStringLiteral("title")).toString());
            poll.insert(QStringLiteral("status"), status);
            poll.insert(QStringLiteral("ended"), !active);
            poll.insert(QStringLiteral("endsAt"), double(endsAt));
            poll.insert(QStringLiteral("total"), double(total));
            poll.insert(QStringLiteral("choices"), choices);
            poll.insert(QStringLiteral("channel"), login);
        }
    }

    if (!predictionFailed) {
        prediction.clear();
        const QJsonObject channel = user.value(QStringLiteral("channel")).toObject();
        QJsonObject e;
        const QJsonArray activeList = channel.value(QStringLiteral("activePredictionEvents")).toArray();
        const QJsonArray lockedList = channel.value(QStringLiteral("lockedPredictionEvents")).toArray();
        const QJsonArray resolved = channel.value(QStringLiteral("resolvedPredictionEvents")).toObject()
                                        .value(QStringLiteral("edges")).toArray();
        if (!activeList.isEmpty()) e = activeList.first().toObject();
        else if (!lockedList.isEmpty()) e = lockedList.first().toObject();
        else if (!resolved.isEmpty()) {
            const QJsonObject r = resolved.first().toObject().value(QStringLiteral("node")).toObject();
            const qint64 endedAt = isoMs(r.value(QStringLiteral("endedAt")));
            // Abgebrochene Vorhersagen nicht zeigen, aufgelöste kurz mit Gewinner
            if (r.value(QStringLiteral("status")).toString() == QLatin1String("RESOLVED") && endedAt > 0 && now - endedAt < kEventResultMs)
                e = r;
        }
        if (!e.value(QStringLiteral("id")).toString().isEmpty()) {
            const QString status = e.value(QStringLiteral("status")).toString();
            const QString winner = e.value(QStringLiteral("winningOutcome")).toObject().value(QStringLiteral("id")).toString();
            const QJsonArray outJson = e.value(QStringLiteral("outcomes")).toArray();
            qint64 points = 0, users = 0;
            for (const QJsonValue &o : outJson) {
                points += qint64(o.toObject().value(QStringLiteral("totalPoints")).toDouble());
                users += qint64(o.toObject().value(QStringLiteral("totalUsers")).toDouble());
            }
            QVariantList outcomes;
            int index = 0;
            for (const QJsonValue &ov : outJson) {
                const QJsonObject o = ov.toObject();
                const qint64 pts = qint64(o.value(QStringLiteral("totalPoints")).toDouble());
                QVariantMap out;
                out.insert(QStringLiteral("id"), o.value(QStringLiteral("id")).toString());
                out.insert(QStringLiteral("title"), o.value(QStringLiteral("title")).toString());
                // Twitch: BLUE oder PINK; bei zwei Ausgängen ist der zweite immer pink
                QString color = o.value(QStringLiteral("color")).toString();
                if (color.isEmpty()) color = (outJson.size() == 2 && index == 1) ? QStringLiteral("PINK") : QStringLiteral("BLUE");
                out.insert(QStringLiteral("color"), color);
                out.insert(QStringLiteral("points"), double(pts));
                out.insert(QStringLiteral("users"), double(qint64(o.value(QStringLiteral("totalUsers")).toDouble())));
                out.insert(QStringLiteral("percent"), points > 0 ? int((pts * 100 + points / 2) / points) : 0);
                out.insert(QStringLiteral("winner"), !winner.isEmpty() && winner == out.value(QStringLiteral("id")).toString());
                outcomes << out;
                ++index;
            }
            const qint64 created = isoMs(e.value(QStringLiteral("createdAt")));
            const int window = e.value(QStringLiteral("predictionWindowSeconds")).toInt();
            prediction.insert(QStringLiteral("id"), e.value(QStringLiteral("id")).toString());
            prediction.insert(QStringLiteral("title"), e.value(QStringLiteral("title")).toString());
            prediction.insert(QStringLiteral("status"), status);
            prediction.insert(QStringLiteral("endsAt"), double(status == QLatin1String("ACTIVE") && created > 0 ? created + qint64(window) * 1000 : 0));
            prediction.insert(QStringLiteral("totalPoints"), double(points));
            prediction.insert(QStringLiteral("totalUsers"), double(users));
            prediction.insert(QStringLiteral("outcomes"), outcomes);
            prediction.insert(QStringLiteral("channel"), login);
        }
    }

    // Solange abgestimmt wird, öfter nachfragen
    const bool live = (!poll.isEmpty() && !poll.value(QStringLiteral("ended")).toBool())
                      || prediction.value(QStringLiteral("status")).toString() == QLatin1String("ACTIVE")
                      || prediction.value(QStringLiteral("status")).toString() == QLatin1String("LOCKED");
    const int interval = live ? 15000 : 20000;   // kurze Vorhersage-Fenster (ab 30 s) nicht verpassen
    if (m_eventPoll->interval() != interval) m_eventPoll->setInterval(interval);

    if (poll == m_poll && prediction == m_prediction) return;
    m_poll = poll;
    m_prediction = prediction;
    emit channelEventsChanged();
}

void TwitchChatController::clearChannelEvents()
{
    if (m_poll.isEmpty() && m_prediction.isEmpty()) return;
    m_poll.clear();
    m_prediction.clear();
    emit channelEventsChanged();
}

void TwitchChatController::hidePoll()
{
    const QString id = m_poll.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || id == m_hiddenPoll) return;
    m_hiddenPoll = id;
    emit channelEventsChanged();
}

void TwitchChatController::hidePrediction()
{
    const QString id = m_prediction.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || id == m_hiddenPrediction) return;
    m_hiddenPrediction = id;
    emit channelEventsChanged();
}

void TwitchChatController::showChannelEvents()
{
    if (m_hiddenPoll.isEmpty() && m_hiddenPrediction.isEmpty()) return;
    m_hiddenPoll.clear();
    m_hiddenPrediction.clear();
    emit channelEventsChanged();
}

void TwitchChatController::setPollCollapsed(bool collapsed)
{
    if (m_pollCollapsed == collapsed) return;
    m_pollCollapsed = collapsed;
    persist();
    emit channelEventsChanged();
}

void TwitchChatController::setPredictionCollapsed(bool collapsed)
{
    if (m_predictionCollapsed == collapsed) return;
    m_predictionCollapsed = collapsed;
    persist();
    emit channelEventsChanged();
}

void TwitchChatController::setNotifySound(bool on)
{
    if (m_notifySound == on) return;
    m_notifySound = on;
    persist();
    emit notifySoundChanged();
}

void TwitchChatController::setNotifyVolume(int percent)
{
    percent = qBound(0, percent, 100);
    if (m_sound.volumePercent() == percent) return;
    m_sound.setVolumePercent(percent);
    persist();
    emit notifySoundChanged();
}

void TwitchChatController::previewNotifySound()
{
    m_sound.play();
}

void TwitchChatController::setChatFontSize(int pixels)
{
    pixels = qBound(0, pixels, 40);
    if (m_chatFontSize == pixels) return;
    m_chatFontSize = pixels;
    persist();
    emit chatFontSizeChanged();
}

void TwitchChatController::setGrammarCheck(bool on)
{
    if (m_grammarCheck == on) return;
    m_grammarCheck = on;
    persist();
    emit spellSettingsChanged();
}

void TwitchChatController::setGrammarOnline(bool on)
{
    if (m_grammarOnline == on) return;
    m_grammarOnline = on;
    if (!on) m_remoteIssues.clear();
    persist();
    emit spellSettingsChanged();
}

void TwitchChatController::setPredictText(bool on)
{
    if (m_predictText == on) return;
    m_predictText = on;
    persist();
    emit spellSettingsChanged();
    emit predictionsChanged();
}

void TwitchChatController::ensureFrequencyWords()
{
    if (m_frequencyRequested) return;
    m_frequencyRequested = true;
    m_spell->loadFrequencyWords();
}

QStringList TwitchChatController::emoteNamesWithPrefix(const QString &prefix) const
{
    constexpr int kMaxEmotes = 200;
    QStringList names;
    for (auto it = m_tpLookup.constBegin(); it != m_tpLookup.constEnd() && names.size() < kMaxEmotes; ++it)
        if (it.key().startsWith(prefix, Qt::CaseInsensitive)) names << it.key();
    for (const QHash<QString, QString> *emotes : {&m_ownEmotes, &m_subEmotes, &m_hypeOwned, &m_favoriteEmotes,
                                                  &m_channelEmotes, &m_globalEmotes, &m_observedEmotes}) {
        for (auto it = emotes->constBegin(); it != emotes->constEnd() && names.size() < kMaxEmotes; ++it)
            if (it.key().startsWith(prefix, Qt::CaseInsensitive) && !names.contains(it.key())) names << it.key();
    }
    return names;
}

// „@“ + Anfang eines Namens: Leute aus dem Chat, wer zuletzt geschrieben hat zuerst.
QVariantList TwitchChatController::previewSegments(const QString &text) const
{
    QStringList keys;
    return buildSegments(text, ownEmoteTag(text), keys);
}

void TwitchChatController::setInputEmotes(QObject *textEdit, const QVariantList &ranges)
{
    InputEmoteStyler::apply(textEdit, ranges);
}

qreal TwitchChatController::imageAspect(const QString &source)
{
    const auto known = m_imageAspects.constFind(source);
    if (known != m_imageAspects.constEnd()) return known.value();
    const QUrl url(source);
    QImageReader reader(url.isLocalFile() ? url.toLocalFile() : source);
    reader.setDecideFormatFromContent(true);          // animierte Twitch-Emotes sind GIFs in .png-Dateien
    const QSize size = reader.size();
    if (size.isEmpty()) return 1.0;                   // nicht merken: Datei kann noch kommen
    const qreal aspect = qreal(size.width()) / size.height();
    m_imageAspects.insert(source, aspect);
    return aspect;
}

void TwitchChatController::watchWindowPresses(QObject *window)
{
    if (!window) return;
    window->removeEventFilter(this);                  // mehrfaches Anmelden zählt nur einmal
    window->installEventFilter(this);
}

bool TwitchChatController::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        const QPointF p = static_cast<QMouseEvent *>(event)->scenePosition();
        emit windowPressed(watched, p.x(), p.y());
    }
    return QObject::eventFilter(watched, event);      // nur mitlesen: der Klick geht normal weiter
}

QStringList TwitchChatController::mentionSuggestions(const QString &text, int cursor) const
{
    if (cursor < 0 || cursor > text.size()) return {};
    if (cursor < text.size() && !text.at(cursor).isSpace()) return {};      // mitten im Wort
    const int start = TextPredictor::wordStart(text, cursor);
    const QString word = text.mid(start, cursor - start);
    if (!word.startsWith(QLatin1Char('@'))) return {};
    const QString prefix = word.mid(1).toLower();
    QVector<const Chatter *> found;
    for (auto it = m_chatters.constBegin(); it != m_chatters.constEnd(); ++it)
        if (it->login.startsWith(prefix) || it->name.toLower().startsWith(prefix)) found << &it.value();
    std::sort(found.begin(), found.end(), [](const Chatter *a, const Chatter *b) {
        if (a->spoke != b->spoke) return a->spoke > b->spoke;
        return a->name.toLower() < b->name.toLower();
    });
    QStringList out;
    for (const Chatter *c : std::as_const(found)) {
        if (out.size() == 5) break;
        const QString mention = QLatin1Char('@') + c->name;
        if (mention.compare(word, Qt::CaseInsensitive) != 0) out << mention;   // schon vollständig getippt
    }
    return out;
}

QStringList TwitchChatController::predictWords(const QString &text, int cursor) const
{
    const QStringList mentions = mentionSuggestions(text, cursor);   // unabhängig von „Wortvorschläge“
    if (!mentions.isEmpty() || (cursor > 0 && TextPredictor::wordStart(text, cursor) < cursor
                                && text.at(TextPredictor::wordStart(text, cursor)) == QLatin1Char('@')))
        return mentions;
    if (!m_predictText) return {};
    return m_predictor.predict(text, cursor, m_spellGerman, m_spellEnglish,
                               [this](const QString &w) { return isKnownEmote(w); },
                               [this](const QString &p) { return emoteNamesWithPrefix(p); });
}

int TwitchChatController::predictionStart(const QString &text, int cursor) const
{
    return TextPredictor::wordStart(text, cursor);
}

void TwitchChatController::learnOwnMessage(const QString &text)
{
    if (!m_predictText) return;
    m_predictor.learn(text, true, [this](const QString &w) { return isKnownEmote(w); });
    QFile file(m_predictFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(m_predictor.saveOwn()).toJson(QJsonDocument::Compact));
}

void TwitchChatController::requestSpellSuggestions(const QString &word)
{
    m_spell->requestSuggestions(word);
}

void TwitchChatController::addSpellWord(const QString &word)
{
    const QString lower = word.trimmed().toLower();
    if (lower.isEmpty() || m_spellWords.contains(lower)) return;
    m_spellWords.insert(lower);
    m_spell->setUserWords(m_spellWords);
    persist();
    emit spellSettingsChanged();
}

// ------------------------------------------------------------ Eigenschaften
void TwitchChatController::setChannel(const QString &raw)
{
    QString c = raw.trimmed();
    while (c.startsWith(QLatin1Char('#'))) c.remove(0, 1);
    c = c.toLower();
    if (c.isEmpty()) {                                   // Kanal entfernen = Chat aus
        if (!m_channel.isEmpty()) clearHistory();
        m_channel.clear(); m_joined.clear(); m_roomId.clear();
        clearChannelCatalogs();
        persist(); emit channelChanged();
        closeChat();
        return;
    }
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9_]{2,50}$"));
    if (!valid.match(c).hasMatch()) {
        setStatus(QStringLiteral("error"), QStringLiteral("Ungültiger Kanalname"));
        return;
    }
    if (c == m_channel) return;
    if (!m_channel.isEmpty()) clearHistory();    // keine Nachrichten des vorherigen Kanals stehen lassen
    m_channel = c;
    m_roomId = m_roomIds.value(c);         // bekannte Raum-ID → Kanal-Kataloge können sofort starten
    clearChannelCatalogs();                // alte Kanal-Sets gehören nicht zum neuen Kanal
    clearPinned();                         // gehört zum alten Kanal; nach dem Beitritt neu abfragen
    clearChannelEvents();
    clearChatters();
    m_myBadges.clear();                    // Rolle gilt pro Kanal
    m_myBadgeTags.clear();
    emit roleChanged();
    persist(); emit channelChanged();
    if (m_ws.isOpen()) {
        joinNow();
    } else if (!m_manualClose) {
        openChat();
    }
}

void TwitchChatController::setAutoChannel(bool on)
{
    if (m_autoChannel == on) return;
    m_autoChannel = on;
    persist(); emit settingsChanged();
}

void TwitchChatController::setAutoScroll(bool on)
{
    if (m_autoScroll == on) return;
    m_autoScroll = on;
    persist(); emit settingsChanged();
}

// ------------------------------------------------------------ Anmeldung per Device-Code
// Eigene Twitch-App des Nutzers (öffentlicher Client, kein Secret). Rechte: alle 81 aktuellen
// Twitch-Rechte (Stand 2026-11-07, Wunsch des Nutzers), damit spätere Funktionen keinen neuen
// Login brauchen.

void TwitchChatController::setLoginState(const QString &state, const QString &error)
{
    m_loginState = state;
    m_loginError = error;
    if (state != QLatin1String("waiting")) { m_loginCode.clear(); m_loginUrl.clear(); }
    emit loginStateChanged();
}

void TwitchChatController::startLogin()
{
    if (!m_net) m_net = new QNetworkAccessManager(this);
    m_loginInteractive = true;
    m_loginCode.clear();
    m_loginUrl.clear();
    setLoginState(QStringLiteral("waiting"));
    m_auth->begin(QString::fromLatin1(kAppClientId), QString::fromLatin1(kAppScopes));
}

void TwitchChatController::cancelLogin()
{
    m_loginInteractive = false;
    m_auth->cancel();
    setLoginState(QStringLiteral("idle"));
}

void TwitchChatController::completeLogin(const QString &login, const QString &accessToken,
                                         const QString &refreshToken, qint64 expiresAtMs)
{
    m_loginInteractive = false;
    setAuth(login, accessToken);               // setzt den Erneuerungs-Schlüssel zurück …
    m_refreshToken = refreshToken;             // … deshalb erst danach eintragen
    m_tokenExpiresAt = expiresAtMs;
    storeRenewal();
    scheduleRenew();
    setLoginState(QStringLiteral("idle"));
    systemLine(QStringLiteral("✓ Angemeldet als %1 – der Token erneuert sich ab jetzt selbst.").arg(login));
}

// Stille Erneuerung: neuer Token ohne Neuverbindung des Chats (IRC bleibt angemeldet).
void TwitchChatController::renewedToken(const QString &accessToken, const QString &refreshToken, qint64 expiresAtMs)
{
    m_token = accessToken;
    m_refreshToken = refreshToken;
    m_tokenExpiresAt = expiresAtMs;
    if (m_vault) {
        QJsonObject d = m_vault->data();
        d[QStringLiteral("chatToken")] = accessToken;
        m_vault->save(d);
    }
    storeRenewal();
    m_whispers->setToken(accessToken);         // Postfach und Helix mit dem neuen Token
    scheduleRenew();
    emit loginStateChanged();
}

void TwitchChatController::storeRenewal()
{
    if (!m_vault) return;
    QJsonObject d = m_vault->data();
    d[QStringLiteral("chatRefresh")] = m_refreshToken;
    d[QStringLiteral("chatExpiresAt")] = double(m_tokenExpiresAt);
    m_vault->save(d);
}

void TwitchChatController::scheduleRenew()
{
    m_renewTimer->stop();
    if (m_refreshToken.isEmpty()) return;
    const qint64 wait = m_tokenExpiresAt - kRenewBeforeMs - QDateTime::currentMSecsSinceEpoch();
    m_renewTimer->start(int(qBound<qint64>(1000, wait, qint64(24) * 3600 * 1000)));
}

void TwitchChatController::setAuth(const QString &user, const QString &token)
{
    const QString u = user.trimmed();
    QString t = token.trimmed();
    if (t.startsWith(QLatin1String("oauth:"))) t.remove(0, 6);
    if (u == m_user && t == m_token) return;
    // Abmelden einer Device-Anmeldung: Token bei Twitch widerrufen. Ein von Hand eingefügter
    // Token gehört nicht zu dieser App und wird nicht erneuert.
    if (t.isEmpty() && !m_token.isEmpty() && !m_refreshToken.isEmpty())
        m_auth->revoke(QString::fromLatin1(kAppClientId), m_token);
    if (t != m_token) {
        m_refreshToken.clear();
        m_tokenExpiresAt = 0;
        m_renewTimer->stop();
        emit loginStateChanged();
    }
    m_user = u; m_token = t;
    m_ownColor.clear();            // kommt mit dem nächsten USERSTATE des neuen Kontos
    m_chatterNotify->start();      // eigene Zeile in der Zuschauerliste
    clearSubscriptionEmotes();     // Abo-Emotes gehören zum bisherigen Konto
    m_whispers->setToken(t);
    m_blockedFetched = false;
    m_blockedIds.clear();
    m_blockedLogins.clear();
    emit roleChanged();
    if (m_vault) {                 // verschlüsselt statt Klartext-Einstellungen
        QJsonObject d = m_vault->data();
        d[QStringLiteral("chatUser")] = u;
        d[QStringLiteral("chatToken")] = t;
        d.remove(QStringLiteral("chatRefresh"));
        d.remove(QStringLiteral("chatExpiresAt"));
        m_vault->save(d);
    }
    emit settingsChanged();
    if (m_ws.isOpen()) {           // neue Login-Daten → Verbindung neu aufbauen
        m_ws.close();
    }
}

// Chat-Login aus dem TokenVault; altes Klartext-Login wird einmalig übernommen.
void TwitchChatController::applyVault(TokenVault *vault)
{
    m_vault = vault;
    if (!vault) return;
    if (m_host) {
        const QJsonObject s = m_host->loadPluginSettings(m_id);
        vault->migrateLegacyToken(s.value(QStringLiteral("user")).toString().trimmed(),
                                  s.value(QStringLiteral("token")).toString().trimmed());
    }
    const QJsonObject d = vault->data();
    m_user = d.value(QStringLiteral("chatUser")).toString();
    m_token = d.value(QStringLiteral("chatToken")).toString();
    m_refreshToken = d.value(QStringLiteral("chatRefresh")).toString();
    m_tokenExpiresAt = qint64(d.value(QStringLiteral("chatExpiresAt")).toDouble());
    if (!m_refreshToken.isEmpty() && m_tokenExpiresAt - kRenewBeforeMs <= QDateTime::currentMSecsSinceEpoch()) {
        m_auth->resume(QString::fromLatin1(kAppClientId), QString(), m_refreshToken, 0);   // abgelaufen: sofort erneuern
    } else {
        scheduleRenew();
    }
    m_whispers->setToken(m_token);
    if (!m_token.isEmpty() && !m_user.isEmpty())
        emit settingsChanged();
}

// Browser-Login übernehmen, ohne den Nutzer zu zwingen (MD: keine Pflichtmigration).
void TwitchChatController::adoptAuthFromBrowser(const QString &user, const QString &token)
{
    if (!m_token.isEmpty() || user.isEmpty() || token.isEmpty())
        return;
    setAuth(user, token);
}

// ------------------------------------------------------------ Verbindung
void TwitchChatController::openChat()
{
    if (m_channel.isEmpty()) { setStatus(QStringLiteral("error"), QStringLiteral("Kein Kanal gesetzt")); return; }
    m_manualClose = false;
    m_backoffMs = 1000;
    setStatus(QStringLiteral("connecting"));
    m_ws.open(QString::fromLatin1(kIrcHost), kIrcPort);
}

void TwitchChatController::closeChat()
{
    m_manualClose = true;
    m_joined.clear();
    m_pinPoll->stop();
    m_eventPoll->stop();
    clearPinned();
    clearChannelEvents();
    clearChatters();
    m_ws.close();
    if (m_connected) { m_connected = false; setStatus(QStringLiteral("closed")); }
    else setStatus(QStringLiteral("closed"));
}

void TwitchChatController::startFollowing()
{
    if (m_channel.isEmpty()) {
        if (m_autoChannel && m_host) {                    // läuft beim Start vielleicht schon ein Twitch-Video?
            const QString path = m_host->currentMediaPath();
            if (!path.isEmpty()) tryFollow(path, false);
        }
    }
    if (!m_channel.isEmpty() && !m_manualClose && !m_ws.isOpen()) openChat();
}

void TwitchChatController::onOpened()
{
    m_backoffMs = 1000;
    sendAuthLines();
}

void TwitchChatController::sendAuthLines()
{
    // membership: Beitritte/Abgänge (JOIN/PART) und die Namensliste (353) für die Liste der Leute im Chat
    m_ws.sendText(QStringLiteral("CAP REQ :twitch.tv/tags twitch.tv/commands twitch.tv/membership"));
    if (!m_token.isEmpty()) {
        m_ws.sendText(QStringLiteral("PASS oauth:") + m_token);
        m_ws.sendText(QStringLiteral("NICK ") + (m_user.isEmpty() ? QStringLiteral("justinfan") : m_user));
    } else {
        const int rnd = 10000 + int(QRandomGenerator::global()->bounded(90000));
        m_ws.sendText(QStringLiteral("NICK justinfan%1").arg(rnd));
    }
    if (!m_channel.isEmpty()) m_ws.sendText(QStringLiteral("JOIN #") + m_channel);
    m_joined = m_channel;
}

void TwitchChatController::joinNow()
{
    if (!m_ws.isOpen()) return;
    if (!m_joined.isEmpty() && m_joined != m_channel) m_ws.sendText(QStringLiteral("PART #") + m_joined);
    if (!m_channel.isEmpty()) {
        m_ws.sendText(QStringLiteral("JOIN #") + m_channel);
        m_joined = m_channel;
    } else {
        m_joined.clear();
    }
}

void TwitchChatController::onClosed(const QString &why)
{
    m_connected = false;
    m_joined.clear();
    setStatus(QStringLiteral("closed"), why);
    scheduleReconnect();
}

void TwitchChatController::onFailed(const QString &why)
{
    m_connected = false;
    m_joined.clear();
    setStatus(QStringLiteral("error"), why);
    scheduleReconnect();
}

void TwitchChatController::scheduleReconnect()
{
    if (m_manualClose || m_channel.isEmpty()) return;
    const int wait = m_backoffMs;
    m_backoffMs = qMin(m_backoffMs * 2, 30000);
    QTimer::singleShot(wait, this, [this] {
        if (!m_manualClose && !m_channel.isEmpty() && !m_ws.isOpen()) openChat();
    });
}

// ------------------------------------------------------------ Senden
bool TwitchChatController::send(const QString &text)
{
    // Genau sagen, warum nichts ging – statt nur "false".
    if (m_channel.isEmpty()) { setStatus(QStringLiteral("error"), QStringLiteral("Kein Kanal gesetzt")); return false; }
    if (m_token.isEmpty())    { setStatus(QStringLiteral("error"), QStringLiteral("Kein Login-Token – erst einloggen (siehe README)")); return false; }
    if (!m_ws.isOpen())       { setStatus(QStringLiteral("error"), QStringLiteral("Nicht verbunden – etwas warten, der Chat verbindet sich automatisch")); return false; }
    QString t = text.trimmed();
    t.remove(QLatin1Char('\n')); t.remove(QLatin1Char('\r')); t.remove(QLatin1Char('\x01'));
    if (t.isEmpty()) return false;
    const qint64 now = m_sendClock.elapsed();
    if (now - m_lastSendMs < 1200) { setStatus(QStringLiteral("error"), QStringLiteral("Zu schnell gesendet – kurz warten")); return false; }
    m_ws.sendText(QStringLiteral("PRIVMSG #") + m_channel + QStringLiteral(" :") + t);
    m_lastSendMs = now;
    learnOwnMessage(t);

    // Eigene Nachricht sofort anzeigen. Twitch schickt sie dieser Verbindung normalerweise nicht
    // zurück, die Emotes kommen also aus der Liste des Kontos. Falls doch ein Echo eintrifft,
    // reichert es die lokale Zeile an, statt sie doppelt zu zeigen.
    const int idx = addMessage(QStringLiteral("chat"), m_user.toLower(), m_user, QString(), m_myBadges,
                               t, ownEmoteTag(t), QVariantMap{{QStringLiteral("userId"), myUserId()},
                                                              {QStringLiteral("badgeTags"), m_myBadgeTags}});
    if (idx >= 0) {
        m_history[idx].insert(QStringLiteral("self"), true);
        m_selfEchoTs = m_history[idx].value(QStringLiteral("ts")).toLongLong();
        m_selfEchoText = t;
        m_selfEchoPending = true;
    }
    return true;
}

// Twitch zählt Emote-Positionen in Unicode-Zeichen, QString in UTF-16-Einheiten: Jedes Emoji
// außerhalb der Grundebene vor einem Emote würde dessen Bereich sonst verschieben.
static QString emoteTagToUtf16(const QString &tag, const QString &msg)
{
    bool hasSurrogates = false;
    for (const QChar c : msg) { if (c.isSurrogate()) { hasSurrogates = true; break; } }
    if (tag.isEmpty() || !hasSurrogates) return tag;

    QVector<int> utf16Index;                          // Unicode-Zeichen → UTF-16-Index
    utf16Index.reserve(msg.size());
    for (int i = 0; i < msg.size(); ++i) {
        utf16Index.append(i);
        if (msg[i].isHighSurrogate() && i + 1 < msg.size() && msg[i + 1].isLowSurrogate()) ++i;
    }
    QStringList groups;
    const auto gl = tag.split(QLatin1Char('/'));
    for (const QString &g : gl) {
        const int c = g.indexOf(QLatin1Char(':'));
        if (c <= 0) continue;
        QStringList rs;
        const auto rl = g.mid(c + 1).split(QLatin1Char(','));
        for (const QString &r : rl) {
            const int d = r.indexOf(QLatin1Char('-'));
            if (d <= 0) continue;
            bool ok1 = false, ok2 = false;
            const int s = r.left(d).toInt(&ok1), e = r.mid(d + 1).toInt(&ok2);
            if (!ok1 || !ok2 || s < 0 || e < s || e >= utf16Index.size()) continue;
            // Letztes Zeichen kann selbst zwei Einheiten belegen: bis vor das nächste Zeichen gehen.
            const int end = (e + 1 < utf16Index.size() ? utf16Index[e + 1] : msg.size()) - 1;
            rs << QStringLiteral("%1-%2").arg(utf16Index[s]).arg(end);
        }
        if (!rs.isEmpty()) groups << (g.left(c) + QLatin1Char(':') + rs.join(QLatin1Char(',')));
    }
    return groups.join(QLatin1Char('/'));
}

// Emoticon-Positionen ab Anfang der Vollnachricht inkl. \x01ACTION in Kernkoordinaten
// überführen: Prefix abziehen, Bereiche außerhalb des Kerns abschneiden.
static QString shiftEmoteTag(const QString &tag, int prefixLen, int coreSize)
{
    if (tag.isEmpty()) return tag;
    QStringList groups;
    const auto gl = tag.split(QLatin1Char('/'));
    for (const QString &g : gl) {
        const int c = g.indexOf(QLatin1Char(':'));
        if (c <= 0) continue;
        const QString id = g.left(c);
        QStringList rs;
        const auto rl = g.mid(c + 1).split(QLatin1Char(','));
        for (const QString &r : rl) {
            const int d = r.indexOf(QLatin1Char('-'));
            if (d <= 0) continue;
            bool ok1 = false, ok2 = false;
            int s = r.left(d).toInt(&ok1), e = r.mid(d + 1).toInt(&ok2);
            if (!ok1 || !ok2) continue;
            s -= prefixLen; e -= prefixLen;
            if (s < 0 || e < s || s >= coreSize) continue;
            if (e >= coreSize) e = coreSize - 1;
            rs << QStringLiteral("%1-%2").arg(s).arg(e);
        }
        if (!rs.isEmpty()) groups << (id + QLatin1Char(':') + rs.join(QLatin1Char(',')));
    }
    return groups.join(QLatin1Char('/'));
}

// ------------------------------------------------------------ IRC-Parser
void TwitchChatController::onText(const QString &frame)
{
    // Twitch bündelt mehrere IRC-Zeilen in einem WebSocket-Frame – pro Zeile parsen.
    const QStringList lines = frame.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        QString l = raw;
        while (l.endsWith(QLatin1Char('\r'))) l.chop(1);
        if (l.isEmpty()) continue;
        parseLine(l);
    }
}

void TwitchChatController::parseLine(const QString &line)
{
    QString rest = line;
    QHash<QString, QString> tags;

    if (rest.startsWith(QLatin1Char('@'))) {
        const int sp = rest.indexOf(QLatin1Char(' '));
        if (sp < 0) return;
        const QStringList pairs = rest.mid(1, sp - 1).split(QLatin1Char(';'));
        rest = rest.mid(sp + 1);
        for (const QString &p : pairs) {
            const int e = p.indexOf(QLatin1Char('='));
            if (e > 0) tags.insert(p.left(e), p.mid(e + 1));
        }
    }

    QString prefix;
    if (rest.startsWith(QLatin1Char(':'))) {
        const int sp = rest.indexOf(QLatin1Char(' '));
        if (sp < 0) { prefix = rest.mid(1); rest.clear(); }
        else { prefix = rest.mid(1, sp - 1); rest = rest.mid(sp + 1); }
    }

    QString cmd;
    { const int sp = rest.indexOf(QLatin1Char(' ')); cmd = (sp < 0 ? rest : rest.left(sp)).toUpper(); }

    if (cmd == QLatin1String("PING")) { m_ws.sendText(QStringLiteral("PONG")); return; }

    if (cmd == QLatin1String("USERSTATE")) {             // eigene Abzeichen im Kanal → Rolle fürs Nutzer-Menü
        QStringList badges;
        for (const QString &b : tags.value(QStringLiteral("badges")).split(QLatin1Char(','), Qt::SkipEmptyParts))
            badges << b.section(QLatin1Char('/'), 0, 0);
        m_myBadgeTags = tags.value(QStringLiteral("badges"));
        if (tags.contains(QStringLiteral("color"))) m_ownColor = tags.value(QStringLiteral("color"));
        setMyBadges(badges);
        m_chatterNotify->start();                        // Rolle und Farbe der eigenen Zeile in der Liste
        return;
    }

    if (cmd == QLatin1String("GLOBALUSERSTATE")) {       // kommt nach jedem Login mit den Sets des Kontos
        m_ownDisplayName = unescapeTag(tags.value(QStringLiteral("display-name")));   // für @-Erwähnungen
        m_ownColor = tags.value(QStringLiteral("color"));
        m_chatterNotify->start();
        fetchSubscriptionEmotes(tags.value(QStringLiteral("emote-sets")));
        return;
    }

    if (cmd == QLatin1String("PRIVMSG") || cmd == QLatin1String("USERNOTICE") || cmd == QLatin1String("NOTICE")) {
        // rest beginnt noch mit dem Befehlswort ("PRIVMSG #kanal :text") – erst das überspringen.
        rest.remove(0, cmd.size());
        while (rest.startsWith(QLatin1Char(' '))) rest.remove(0, 1);
        const int sp = rest.indexOf(QLatin1Char(' '));
        QString full = (sp < 0) ? QString() : rest.mid(sp + 1);      // Ziel (#kanal) überspringen
        if (full.startsWith(QLatin1Char(':'))) full.remove(0, 1);

        QString type = cmd == QLatin1String("PRIVMSG") ? QStringLiteral("chat") : QStringLiteral("system");

        // Raum-ID aus der Markierung merken: BTTV, FFZ und 7TV kennen Kanal-Sets nur unter
        // dieser Ziffern-ID (ein Login-Name geht dort nicht). Beim ersten Antreffen sofort fragen.
        const QString rid = tags.value(QStringLiteral("room-id"));
        if (!rid.isEmpty() && rid != m_roomId) {
            m_roomId = rid;
            QMetaObject::invokeMethod(this, &TwitchChatController::ensureAutomodSubscriptions, Qt::QueuedConnection);
            if (!m_channel.isEmpty()) { m_roomIds.insert(m_channel, rid); persist(); }
            fetchThirdPartyChannel(m_channel, rid);
        }

        // \x01ACTION …\x01 = /me: Marker entfernen; Emoticon-Positionen vorher umrechnen.
        int prefixLen = 0, trailLen = 0;
        if (full.startsWith(QChar(1)) && full.indexOf(QLatin1String("ACTION ")) == 1) {
            prefixLen = 8;                                          // \x01 + "ACTION "
            type = QStringLiteral("action");
        }
        if (full.endsWith(QChar(1))) trailLen = 1;
        const QString coreBase = full.mid(prefixLen, full.size() - prefixLen - trailLen);

        QString emoteTag = (cmd == QLatin1String("PRIVMSG")) ? tags.value(QStringLiteral("emotes")) : QString();
        QString core = coreBase;
        if (cmd == QLatin1String("USERNOTICE")) {
            // Twitchs Meldungstext (Sub, Raid, Geschenk-Sub …) steht in „system-msg“; ein eigener Text
            // des Zuschauers folgt dahinter. Emote-Positionen passen dann nicht mehr.
            QString sys = tags.value(QStringLiteral("system-msg"));
            if (sys.isEmpty()) sys = tags.value(QStringLiteral("system-message"));
            sys = unescapeTag(sys).trimmed();
            if (!sys.isEmpty()) {
                core = core.trimmed().isEmpty() ? sys : sys + QStringLiteral(" – ") + core.trimmed();
                emoteTag.clear();
            }
        }
        if (!emoteTag.isEmpty() && (prefixLen > 0 || trailLen > 0))
            emoteTag = shiftEmoteTag(emoteTag, prefixLen, core.size());
        emoteTag = emoteTagToUtf16(emoteTag, core);

        const QString nick = prefix.section(QLatin1Char('!'), 0, 0);
        QString name = tags.value(QStringLiteral("display-name"));
        if (name.isEmpty()) name = nick;
        // Sub-/Raid-Meldungen erscheinen als „System“ (gelbe Box), nicht unter dem Namen des Zuschauers
        if (cmd == QLatin1String("USERNOTICE")) name = QStringLiteral("System");
        const QString color = tags.value(QStringLiteral("color"));

        QStringList badgeKeys;
        const QString badges = tags.value(QStringLiteral("badges"));
        if (!badges.isEmpty()) {
            const auto parts = badges.split(QLatin1Char(','));
            for (const QString &b : parts) {
                const int s = b.indexOf(QLatin1Char('/'));
                badgeKeys << (s > 0 ? b.left(s) : b);
            }
        }

        // Falls Twitch eine eigene Nachricht doch zurückschickt: lokale Zeile anreichern statt duplizieren.
        if (m_selfEchoPending && !m_user.isEmpty()
            && nick.compare(m_user, Qt::CaseInsensitive) == 0 && core == m_selfEchoText) {
            m_selfEchoPending = false;
            for (int i = m_history.size() - 1; i >= 0; --i) {
                if (!m_history[i].value(QStringLiteral("self")).toBool()) continue;
                if (m_history[i].value(QStringLiteral("ts")).toLongLong() != m_selfEchoTs) continue;
                QStringList ids;
                const QVariantList segs = buildSegments(core, emoteTag, ids);
                QVariantMap &e = m_history[i];
                e.insert(QStringLiteral("segments"), segs);
                e.insert(QStringLiteral("raw"), core);
                e.insert(QStringLiteral("emotesTag"), emoteTag);
                e.insert(QStringLiteral("emoteIds"), QVariant(ids));
                emit messageUpdated(i);
                return;
            }
        }

        if (cmd == QLatin1String("PRIVMSG")) noteChatter(nick, name, color, badgeKeys, true);
        if (m_predictText && cmd == QLatin1String("PRIVMSG")) {
            ensureFrequencyWords();
            m_predictor.learn(core, false, [this](const QString &w) { return isKnownEmote(w); });
        }

        // Blockierte Konten: wie auf twitch.tv nicht anzeigen
        const QString senderId = tags.value(QStringLiteral("user-id"));
        if (cmd == QLatin1String("PRIVMSG")
            && ((!senderId.isEmpty() && m_blockedIds.contains(senderId)) || m_blockedLogins.contains(nick.toLower())))
            return;

        // Bei Emotes nicht trimmen – sonst verrutschen die Positionen.
        addMessage(type, cmd == QLatin1String("USERNOTICE") ? QString() : nick, name, color, badgeKeys,
                   emoteTag.isEmpty() ? core.trimmed() : core, emoteTag,
                   QVariantMap{{QStringLiteral("userId"), senderId},
                               {QStringLiteral("badgeTags"), badges},
                               {QStringLiteral("msgId"), cmd == QLatin1String("PRIVMSG") ? tags.value(QStringLiteral("id")) : QString()},
                               {QStringLiteral("noticeId"), cmd != QLatin1String("PRIVMSG") ? tags.value(QStringLiteral("msg-id")) : QString()},
                               {QStringLiteral("userNotice"), cmd == QLatin1String("USERNOTICE")},
                               // Bits = Twitchs eingebaute Spende; Bots kündigen Follows/Spenden an
                               {QStringLiteral("bits"), cmd == QLatin1String("PRIVMSG") ? tags.value(QStringLiteral("bits")).toInt() : 0},
                               {QStringLiteral("botName"), cmd == QLatin1String("PRIVMSG") ? eventBotLabel(nick) : QString()}});
        if (!m_history.isEmpty() && !m_token.isEmpty()
            && isOwnName(tags.value(QStringLiteral("reply-parent-user-login")))
            && !isOwnName(nick) && !m_history.last().value(QStringLiteral("mention")).toBool()) {
            m_history.last().insert(QStringLiteral("mention"), true);
            emit messageUpdated(int(m_history.size()) - 1);
        }
        return;
    }

    if (cmd == QLatin1String("366")) {                   // Ende des Namens-Lists → Beitritt bestätigt
        m_connected = true;
        m_backoffMs = 1000;
        setStatus(QStringLiteral("connected"));
        fetchPinned();
        fetchChannelEvents();
        fetchViewers();
        fetchCheermotes();
        fetchBadges();
        m_pinPoll->start();
        m_eventPoll->start();
        fetchHelixChatters(QString(), {});
        // Komplette Emote-Listen pro Kanal einmal holen (öffentlich, auch ohne Login).
        if (!m_channel.isEmpty() && m_catalogChannel != m_channel) {
            m_catalogChannel = m_channel;
            fetchEmoteCatalogs();
        }
        // BTTV-/FFZ-/7TV-Listen sind öffentlich: Global immer, Kanal sobald die Raum-ID bekannt ist.
        if (!m_channel.isEmpty()) {
            fetchThirdPartyGlobal();
            const QString rid = m_roomIds.value(m_channel);
            if (!rid.isEmpty()) fetchThirdPartyChannel(m_channel, rid);
        }
        return;
    }
    // Moderations-Ereignisse: Twitch schickt sie über IRC an alle, also auch an anonyme Leser.
    if (cmd == QLatin1String("CLEARCHAT") || cmd == QLatin1String("CLEARMSG")) {
        const int colon = rest.indexOf(QLatin1String(" :"));
        const QString trailing = colon < 0 ? QString() : rest.mid(colon + 2).trimmed();
        if (cmd == QLatin1String("CLEARCHAT")) {
            const QString target = trailing.toLower();
            if (target.isEmpty()) {
                addMessage(QStringLiteral("system"), QString(), QStringLiteral("tmi"), QString(), QStringList(),
                           QStringLiteral("Der Chat wurde von einem Moderator geleert."));
                markDeleted(QStringLiteral("clear"), [](const QVariantMap &) { return true; });
                return;
            }
            const int seconds = tags.value(QStringLiteral("ban-duration")).toInt();
            addMessage(QStringLiteral("system"), QString(), QStringLiteral("tmi"), QString(), QStringList(),
                       seconds > 0 ? QStringLiteral("%1 wurde für %2 stummgeschaltet (Timeout).").arg(target, durationText(seconds))
                                   : QStringLiteral("%1 wurde gebannt.").arg(target));
            markDeleted(seconds > 0 ? QStringLiteral("timeout") : QStringLiteral("ban"), [&target](const QVariantMap &m) { return m.value(QStringLiteral("user")).toString() == target; });
            return;
        }
        const QString login = tags.value(QStringLiteral("login")).toLower();
        const QString msgId = tags.value(QStringLiteral("target-msg-id"));
        QString text = trailing;
        if (text.startsWith(QChar(1)) && text.indexOf(QLatin1String("ACTION ")) == 1) text = text.mid(8);
        if (text.endsWith(QChar(1))) text.chop(1);
        if (text.size() > 120) text = text.left(117) + QStringLiteral("…");
        addMessage(QStringLiteral("system"), QString(), QStringLiteral("tmi"), QString(), QStringList(),
                   login.isEmpty() ? QStringLiteral("Eine Nachricht wurde von einem Moderator gelöscht.")
                   : text.isEmpty() ? QStringLiteral("Nachricht von %1 wurde gelöscht.").arg(login)
                                    : QStringLiteral("Nachricht von %1 wurde gelöscht: „%2“").arg(login, text));
        if (!msgId.isEmpty())
            markDeleted(QStringLiteral("deleted"), [&msgId](const QVariantMap &m) { return m.value(QStringLiteral("msgId")).toString() == msgId; });
        return;
    }
    // Leute im Chat: Twitch meldet Beitritte und Abgänge gesammelt, große Kanäle nur teilweise.
    if (cmd == QLatin1String("JOIN") || cmd == QLatin1String("PART")) {
        const QString nick = prefix.section(QLatin1Char('!'), 0, 0).toLower();
        if (cmd == QLatin1String("JOIN")) noteChatter(nick, QString(), QString(), {}, false);
        else if (m_chatters.remove(nick)) m_chatterNotify->start();
        return;
    }
    if (cmd == QLatin1String("353")) {
        const int colon = rest.indexOf(QLatin1String(" :"));
        const QStringList names = colon < 0 ? QStringList() : rest.mid(colon + 2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &n : names) noteChatter(n.toLower(), QString(), QString(), {}, false);
        return;
    }
    // 001, CAP, HOSTTARGET, NUMERIC … bewusst ignoriert
}

QObject *TwitchChatController::whispersObj() const { return m_whispers; }

void TwitchChatController::noteChatter(const QString &login, const QString &name, const QString &color,
                                       const QStringList &badges, bool wrote)
{
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9_]{2,25}$"));
    const QString key = login.toLower();
    // Man selbst steht nicht hier, sondern kommt in chatters() als feste Zeile dazu: Twitch meldet
    // einen in großen Kanälen gar nicht, und die Liste für @-Vorschläge soll einen nicht enthalten.
    if (!valid.match(key).hasMatch() || key.startsWith(QLatin1String("justinfan")) || isOwnName(key))
        return;
    Chatter &c = m_chatters[key];
    c.login = key;
    if (!name.isEmpty()) c.name = name;
    if (c.name.isEmpty()) c.name = key;
    if (!color.isEmpty()) c.color = color;
    if (!badges.isEmpty() || wrote) c.badges = badges;
    c.wrote = c.wrote || wrote;
    if (wrote) c.spoke = ++m_speakCounter;
    m_chatterNotify->start();
}

// Zuschauerzahl des Streams: öffentlich über gql.twitch.tv, ohne Login. Namen der Zuschauer
// gibt es so nicht – die liefert Twitch nur Moderatoren (fetchHelixChatters).
void TwitchChatController::fetchViewers()
{
    if (m_channel.isEmpty()) return;
    if (!m_net) m_net = new QNetworkAccessManager(this);
    // m_channel ist auf ^[a-z0-9_]{2,50}$ geprüft – sicher für die Abfrage.
    requestGql(QStringLiteral("{ user(login: \"%1\") { stream { viewersCount } } }").arg(m_channel),
               QStringLiteral("viewers"));
}

void TwitchChatController::applyViewersJson(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (!root.contains(QStringLiteral("data"))) return;           // Fehlerantwort: alten Stand behalten
    const QJsonValue stream = root.value(QStringLiteral("data")).toObject()
                                  .value(QStringLiteral("user")).toObject().value(QStringLiteral("stream"));
    const bool live = stream.isObject();
    const int count = live ? stream.toObject().value(QStringLiteral("viewersCount")).toInt() : 0;
    if (live == m_streamLive && count == m_viewerCount) return;
    m_streamLive = live;
    m_viewerCount = count;
    emit viewersChanged();
}

void TwitchChatController::resetViewers()
{
    if (m_viewerCount == -1 && !m_streamLive) return;
    m_viewerCount = -1;
    m_streamLive = false;
    emit viewersChanged();
}

void TwitchChatController::clearChatters()
{
    m_chatterPoll->stop();
    m_chattersComplete = false;
    m_chattersDenied = false;
    resetViewers();
    if (m_chatters.isEmpty()) return;
    m_chatters.clear();
    emit chattersChanged();
}

QVariantList TwitchChatController::chatters(const QString &filter) const
{
    const auto rank = [](const Chatter &c) {
        if (c.badges.contains(QStringLiteral("broadcaster"))) return 0;
        if (c.badges.contains(QStringLiteral("moderator"))) return 1;
        if (c.badges.contains(QStringLiteral("vip"))) return 2;
        return 3;
    };
    QVector<const Chatter *> list;
    const QString needle = filter.trimmed().toLower().remove(QLatin1Char('@'));
    for (auto it = m_chatters.constBegin(); it != m_chatters.constEnd(); ++it)
        if (needle.isEmpty() || it->login.contains(needle) || it->name.toLower().contains(needle)) list << &it.value();
    std::sort(list.begin(), list.end(), [&](const Chatter *a, const Chatter *b) {
        const int ra = rank(*a), rb = rank(*b);
        return ra != rb ? ra < rb : a->name.toLower() < b->name.toLower();
    });
    QVariantList out;
    if (selfListed()) {
        const QString login = ownLogin();
        // Anzeigename nur, wenn er zu diesem Konto gehört (nach einem Kontowechsel kann er noch alt sein)
        const QString name = m_ownDisplayName.compare(login, Qt::CaseInsensitive) == 0 ? m_ownDisplayName : login;
        if (needle.isEmpty() || login.contains(needle) || name.toLower().contains(needle))
            out << QVariantMap{{QStringLiteral("login"), login}, {QStringLiteral("name"), name},
                               {QStringLiteral("color"), m_ownColor}, {QStringLiteral("badges"), m_myBadges},
                               {QStringLiteral("wrote"), false}, {QStringLiteral("self"), true}};
    }
    for (const Chatter *c : std::as_const(list)) {
        if (out.size() >= 2000) break;
        out << QVariantMap{{QStringLiteral("login"), c->login}, {QStringLiteral("name"), c->name},
                           {QStringLiteral("color"), c->color}, {QStringLiteral("badges"), c->badges},
                           {QStringLiteral("wrote"), c->wrote}};
    }
    return out;
}

QString TwitchChatController::ownLogin() const
{
    const QString validated = m_whispers ? m_whispers->login() : QString();
    return (validated.isEmpty() ? m_user : validated).trimmed().toLower();
}

bool TwitchChatController::selfListed() const
{
    return !m_token.isEmpty() && !m_channel.isEmpty() && !ownLogin().isEmpty();
}

// Abzeichen-Bilder: Der Katalog ist öffentlich (gql.twitch.tv ohne Token, also auch für Gäste).
// DOUBLE = 36 px, wird für die Chatzeile verkleinert und bleibt auch bei großer Schrift scharf.
void TwitchChatController::fetchBadges()
{
    if (m_channel.isEmpty()) return;
    if (!m_net) m_net = new QNetworkAccessManager(this);
    // m_channel ist auf ^[a-z0-9_]{2,50}$ geprüft – sicher für die Abfrage.
    requestGql(QStringLiteral("{ user(login: \"%1\") { broadcastBadges { setID version imageURL(size: DOUBLE) } } }").arg(m_channel),
               QStringLiteral("badgesChannel"));
    if (!m_badgeGlobalTried) {
        m_badgeGlobalTried = true;
        requestGql(QStringLiteral("{ badges { setID version imageURL(size: DOUBLE) } }"), QStringLiteral("badgesGlobal"));
    }
}

void TwitchChatController::applyBadgesJson(const QByteArray &json, bool channel)
{
    const QJsonObject data = QJsonDocument::fromJson(json).object().value(QStringLiteral("data")).toObject();
    const QJsonArray list = channel ? data.value(QStringLiteral("user")).toObject().value(QStringLiteral("broadcastBadges")).toArray()
                                    : data.value(QStringLiteral("badges")).toArray();
    if (list.isEmpty()) {                     // Kanal ohne eigene Abzeichen oder Fehlerantwort
        if (!channel) m_badgeGlobalTried = false;   // beim nächsten Beitritt erneut versuchen
        return;
    }
    QHash<QString, QString> &into = channel ? m_badgeChannel : m_badgeGlobal;
    for (const QJsonValue &v : list) {
        const QJsonObject b = v.toObject();
        const QString url = b.value(QStringLiteral("imageURL")).toString();
        const QString set = b.value(QStringLiteral("setID")).toString();
        if (set.isEmpty() || !url.startsWith(QLatin1String("https://static-cdn.jtvnw.net/badges/"))) continue;
        into.insert(set + QLatin1Char('/') + b.value(QStringLiteral("version")).toString(), url);
    }
    ++m_badgeRevision;
    emit badgeImagesChanged();
}

QString TwitchChatController::badgeImage(const QString &tag)
{
    QString url = m_badgeChannel.value(tag);
    if (url.isEmpty()) url = m_badgeGlobal.value(tag);
    if (url.isEmpty()) return QString();
    // …/badges/v1/<uuid>/2 → die UUID bestimmt das Bild; gleiche Bilder liegen nur einmal im Cache.
    const QString key = QStringLiteral("badge_") + url.section(QLatin1Char('/'), -2, -2).remove(QLatin1Char('-'));
    const QString path = m_emotes->localPath(key);
    if (!path.isEmpty()) return QUrl::fromLocalFile(path).toString();
    m_emotes->requestUrls(key, {url});        // läuft schon oder gerade gescheitert → tut nichts
    return QString();
}

// Volle Liste nur für Moderatoren (Helix chat/chatters, Recht moderator:read:chatters).
void TwitchChatController::fetchHelixChatters(const QString &cursor, QSet<QString> seen)
{
    if (m_chattersDenied || m_roomId.isEmpty() || m_whispers->userId().isEmpty()
        || !m_whispers->hasScope(QStringLiteral("moderator:read:chatters")))
        return;
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QUrl url(QStringLiteral("https://api.twitch.tv/helix/chat/chatters"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("broadcaster_id"), m_roomId);
    query.addQueryItem(QStringLiteral("moderator_id"), m_whispers->userId());
    query.addQueryItem(QStringLiteral("first"), QStringLiteral("1000"));
    if (!cursor.isEmpty()) query.addQueryItem(QStringLiteral("after"), cursor);
    url.setQuery(query);
    QNetworkRequest req{url};
    req.setRawHeader("Client-Id", m_whispers->clientId().toUtf8());
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    QNetworkReply *reply = m_net->get(req);
    const QString room = m_roomId;
    connect(reply, &QNetworkReply::finished, this, [this, reply, room, seen]() mutable {
        reply->deleteLater();
        if (room != m_roomId) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || status == 403) {          // kein Moderator hier: bei IRC-Liste bleiben
            m_chattersDenied = true;
            m_chatterPoll->stop();
            return;
        }
        if (status != 200) return;
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        for (const QJsonValue &v : body.value(QStringLiteral("data")).toArray()) {
            const QJsonObject u = v.toObject();
            const QString login = u.value(QStringLiteral("user_login")).toString();
            seen.insert(login);
            noteChatter(login, u.value(QStringLiteral("user_name")).toString(), QString(), {}, false);
        }
        const QString next = body.value(QStringLiteral("pagination")).toObject().value(QStringLiteral("cursor")).toString();
        if (!next.isEmpty() && seen.size() < 10000) {
            fetchHelixChatters(next, seen);
            return;
        }
        // Wer nicht mehr in der Liste steht und nichts geschrieben hat, ist gegangen.
        for (auto it = m_chatters.begin(); it != m_chatters.end();)
            it = (!seen.contains(it.key()) && !it->wrote) ? m_chatters.erase(it) : std::next(it);
        m_chattersComplete = true;
        m_chatterNotify->start();
        if (!m_chatterPoll->isActive()) m_chatterPoll->start();
    });
}

QString TwitchChatController::unescapeTag(const QString &v)
{
    QString r = v;
    r.replace(QLatin1String("\\:"), QLatin1String(":"));
    r.replace(QLatin1String("\\s"), QLatin1String(" "));
    r.replace(QLatin1String("\\n"), QLatin1String("\n"));
    r.replace(QLatin1String("\\\\"), QLatin1String("\\"));
    return r;
}

// Bekannte Chat-Bots, die Follows, Spenden, Subs usw. im Chat ankündigen (Login → Anzeigename).
// Ihre Nachrichten stehen in der gelben Box mit dem Bot-Namen als Etikett.
static const QList<QPair<QString, QString>> &knownEventBots()
{
    static const QList<QPair<QString, QString>> bots = {
        {QStringLiteral("streamelements"), QStringLiteral("StreamElements")},
        {QStringLiteral("streamlabs"), QStringLiteral("Streamlabs")},
        {QStringLiteral("nightbot"), QStringLiteral("Nightbot")},
        {QStringLiteral("fossabot"), QStringLiteral("Fossabot")},
        {QStringLiteral("moobot"), QStringLiteral("Moobot")},
        {QStringLiteral("wizebot"), QStringLiteral("WizeBot")},
        {QStringLiteral("sery_bot"), QStringLiteral("Sery_Bot")},
        {QStringLiteral("kofistreambot"), QStringLiteral("Ko-fi")},
        {QStringLiteral("soundalerts"), QStringLiteral("SoundAlerts")},
        {QStringLiteral("streamstickers"), QStringLiteral("StreamStickers")},
        {QStringLiteral("streamlootsbot"), QStringLiteral("Streamloots")},
        {QStringLiteral("tipeeebot"), QStringLiteral("TipeeeStream")},
        {QStringLiteral("lumiastream"), QStringLiteral("Lumia Stream")},
        {QStringLiteral("blerp"), QStringLiteral("Blerp")},
        {QStringLiteral("pretzelrocks"), QStringLiteral("Pretzel")},
        {QStringLiteral("deepbot"), QStringLiteral("DeepBot")},
        {QStringLiteral("phantombot"), QStringLiteral("PhantomBot")},
        {QStringLiteral("botisimo"), QStringLiteral("Botisimo")},
        {QStringLiteral("coebot"), QStringLiteral("CoeBot")},
        {QStringLiteral("frostytoolsdotcom"), QStringLiteral("FrostyTools")},
        {QStringLiteral("own3d"), QStringLiteral("OWN3D")},
        {QStringLiteral("streamcaptainbot"), QStringLiteral("StreamCaptain")},
        {QStringLiteral("mixitupbot"), QStringLiteral("Mix It Up")},
        {QStringLiteral("songlistbot"), QStringLiteral("SongListBot")},
    };
    return bots;
}

// ------------------------------------------------------------ Benachrichtigungs-Boxen
// Farben je Box-Art und die Liste der Event-Bots sind im Panel (Glocke) einstellbar.
static const QStringList &boxKinds()
{
    static const QStringList kinds = {QStringLiteral("mention"), QStringLiteral("system"), QStringLiteral("follow"),
                                      QStringLiteral("bits"), QStringLiteral("bot"), QStringLiteral("twitch"),
                                      QStringLiteral("automod")};
    return kinds;
}

static QString cleanBoxColor(const QString &c)
{
    const QColor col(c.trimmed());
    return col.isValid() ? col.name(QColor::HexRgb) : QString();
}

static QString cleanBotLogin(const QString &l)
{
    QString login = l.trimmed().toLower();
    if (login.startsWith(QLatin1Char('@'))) login.remove(0, 1);
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9_]{1,25}$"));
    return valid.match(login).hasMatch() ? login : QString();
}

void TwitchChatController::loadBoxSettings(const QJsonObject &s)
{
    const QJsonObject colors = s.value(QStringLiteral("boxColors")).toObject();
    for (const QString &k : boxKinds()) {
        const QString c = cleanBoxColor(colors.value(k).toString());
        if (!c.isEmpty()) m_boxColors.insert(k, c);
    }
    m_eventBots.clear();
    if (s.contains(QStringLiteral("eventBots"))) {      // eigene Liste: gelöschte Standard-Bots bleiben weg
        for (const QJsonValue &v : s.value(QStringLiteral("eventBots")).toArray()) {
            const QJsonObject o = v.toObject();
            const QString login = cleanBotLogin(o.value(QStringLiteral("login")).toString());
            if (login.isEmpty() || !eventBotLabel(login).isNull()) continue;
            const QString label = o.value(QStringLiteral("label")).toString().trimmed();
            m_eventBots.append({login, label.isEmpty() ? login : label,
                                cleanBoxColor(o.value(QStringLiteral("color")).toString())});
        }
    } else {
        for (const auto &b : knownEventBots()) m_eventBots.append({b.first, b.second, QString()});
    }
}

QVariantMap TwitchChatController::defaultBoxColors() const
{
    // mention leer = Akzentfarbe des Players
    return {{QStringLiteral("mention"), QString()}, {QStringLiteral("system"), QStringLiteral("#f1c40f")},
            {QStringLiteral("follow"), QStringLiteral("#f1c40f")}, {QStringLiteral("bits"), QStringLiteral("#f1c40f")},
            {QStringLiteral("bot"), QStringLiteral("#f1c40f")}, {QStringLiteral("twitch"), QStringLiteral("#4a9eff")},
            {QStringLiteral("automod"), QStringLiteral("#4a9eff")}};
}

QVariantMap TwitchChatController::boxColors() const
{
    QVariantMap m = defaultBoxColors();
    for (auto it = m_boxColors.constBegin(); it != m_boxColors.constEnd(); ++it) m.insert(it.key(), it.value());
    return m;
}

void TwitchChatController::setBoxColor(const QString &kind, const QString &color)
{
    if (!boxKinds().contains(kind)) return;
    const QString c = cleanBoxColor(color);
    if (c.isEmpty() || c == defaultBoxColors().value(kind).toString()) m_boxColors.remove(kind);
    else m_boxColors.insert(kind, c);
    persist();
    emit boxSettingsChanged();
}

void TwitchChatController::resetBoxColors()
{
    m_boxColors.clear();
    persist();
    emit boxSettingsChanged();
}

QVariantList TwitchChatController::eventBots() const
{
    QVariantList l;
    for (const EventBot &b : m_eventBots)
        l.append(QVariantMap{{QStringLiteral("login"), b.login}, {QStringLiteral("label"), b.label},
                             {QStringLiteral("color"), b.color}});
    return l;
}

QString TwitchChatController::eventBotLabel(const QString &login) const
{
    const QString l = login.toLower();
    for (const EventBot &b : m_eventBots)
        if (b.login == l) return b.label;
    return QString();
}

bool TwitchChatController::setEventBot(const QString &login, const QString &label, const QString &color,
                                       const QString &oldLogin)
{
    const QString l = cleanBotLogin(login);
    if (l.isEmpty()) return false;
    const QString old = oldLogin.trimmed().toLower();
    QString shown = label.trimmed();
    if (shown.isEmpty()) shown = login.trimmed().remove(QLatin1Char('@'));
    // Eintrag, der bearbeitet wird (bisheriger Login), sonst einer mit gleichem Login
    int at = -1;
    for (int i = 0; i < m_eventBots.size() && at < 0; ++i)
        if (!old.isEmpty() && m_eventBots.at(i).login == old) at = i;
    for (int i = 0; i < m_eventBots.size() && at < 0; ++i)
        if (m_eventBots.at(i).login == l) at = i;
    // Umbenannt auf einen Login, den es schon gibt → Doppel entfernen
    for (int i = m_eventBots.size() - 1; i >= 0; --i)
        if (i != at && m_eventBots.at(i).login == l) {
            m_eventBots.removeAt(i);
            if (i < at) --at;
        }
    const EventBot b{l, shown, cleanBoxColor(color)};
    if (at >= 0) m_eventBots[at] = b;
    else m_eventBots.append(b);
    persist();
    emit boxSettingsChanged();
    return true;
}

void TwitchChatController::removeEventBot(const QString &login)
{
    const QString l = login.toLower();
    for (int i = 0; i < m_eventBots.size(); ++i)
        if (m_eventBots.at(i).login == l) {
            m_eventBots.removeAt(i);
            persist();
            emit boxSettingsChanged();
            return;
        }
}

void TwitchChatController::resetEventBots()
{
    m_eventBots.clear();
    for (const auto &b : knownEventBots()) m_eventBots.append({b.first, b.second, QString()});
    persist();
    emit boxSettingsChanged();
}

// Wer bin ich? Nicht nur der unter „Login“ eingetragene Name (früher von Hand, konnte vom echten
// Login abweichen), sondern auch, was Twitch meldet: bestätigter Login und Anzeigename.
QStringList TwitchChatController::ownNames() const
{
    QStringList names;
    for (const QString &n : {m_user, m_whispers ? m_whispers->login() : QString(), m_ownDisplayName}) {
        const QString clean = n.trimmed().remove(QLatin1Char('@'));
        if (!clean.isEmpty() && !names.contains(clean, Qt::CaseInsensitive)) names << clean;
    }
    return names;
}

bool TwitchChatController::isOwnName(const QString &name) const
{
    return !name.isEmpty() && ownNames().contains(name, Qt::CaseInsensitive);
}

bool TwitchChatController::mentionsAnyone(const QString &text)
{
    static const QRegularExpression mention(QStringLiteral("(^|[^\\w@])@\\w{2,}"), QRegularExpression::UseUnicodePropertiesOption);
    return mention.match(text).hasMatch();
}

bool TwitchChatController::mentionsMe(const QString &text) const
{
    if (m_token.isEmpty()) return false;
    const QStringList names = ownNames();
    if (names.isEmpty()) return false;
    QStringList escaped;
    for (const QString &n : names) escaped << QRegularExpression::escape(n);
    // @Name als ganzes Wort, ohne Groß-/Kleinschreibung; „@namefoo“ und „mail@name“ zählen nicht
    const QRegularExpression mention(QStringLiteral("(^|[^\\w@])@(?:%1)(?![\\w])").arg(escaped.join(QLatin1Char('|'))),
                                     QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return mention.match(text).hasMatch();
}

int TwitchChatController::addMessage(const QString &type, const QString &user, const QString &name,
                                     const QString &color, const QStringList &badges,
                                     const QString &raw, const QString &emoteTag, const QVariantMap &extra)
{
    if (raw.trimmed().isEmpty()) return -1;
    QStringList emoteIds;
    const QVariantList segments = buildSegments(raw, emoteTag, emoteIds, extra.value(QStringLiteral("bits")).toInt() > 0);
    noteObservedEmotes(raw, emoteTag);   // Name+ID merken → Emote-Browser füllt sich von selbst

    QVariantMap m;
    m.insert(QStringLiteral("type"), type);
    m.insert(QStringLiteral("user"), user);
    m.insert(QStringLiteral("name"), name);
    m.insert(QStringLiteral("color"), color);
    m.insert(QStringLiteral("badges"), badges);
    m.insert(QStringLiteral("badgeText"), badges.join(QLatin1Char(',')));   // fürs Nutzer-Menü (siehe ChatNative)
    m.insert(QStringLiteral("badgeTags"), badges.join(QLatin1Char(',')));   // „set/version“ fürs Bild; extra überschreibt mit Version
    m.insert(QStringLiteral("segments"), segments);
    m.insert(QStringLiteral("raw"), raw);
    m.insert(QStringLiteral("emotesTag"), emoteTag);
    m.insert(QStringLiteral("emoteIds"), QVariant(emoteIds));
    m.insert(QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch());
    m.insert(QStringLiteral("userId"), QString());    // feste Rollen fürs Panel-Modell
    m.insert(QStringLiteral("msgId"), QString());
    m.insert(QStringLiteral("automodId"), QString());       // AutoMod: zurückgehaltene Nachricht (feste Rollen)
    m.insert(QStringLiteral("automodStatus"), QString());   // pending | approved | denied | expired
    m.insert(QStringLiteral("automodReason"), QString());
    m.insert(QStringLiteral("automodBy"), QString());
    // Meldung von Twitch selbst (NOTICE von tmi.twitch.tv oder Systemzeile „tmi“) und deren msg-id
    m.insert(QStringLiteral("fromTwitch"), type == QLatin1String("system")
                                               && (name == QLatin1String("tmi") || name == QLatin1String("tmi.twitch.tv")));
    m.insert(QStringLiteral("noticeId"), QString());
    m.insert(QStringLiteral("userNotice"), false);           // Sub/Raid/Geschenk-Sub … (gelbe Box „System“)
    m.insert(QStringLiteral("bits"), 0);                      // Cheer mit Bits (gelbe Box „N Bits“)
    m.insert(QStringLiteral("botName"), QString());           // Nachricht eines bekannten Event-Bots (gelbe Box)
    m.insert(QStringLiteral("deleted"), false);               // von Moderatoren gelöscht / Timeout / Bann
    m.insert(QStringLiteral("deletedReason"), QString());
    // @deinname (oder Antwort auf dich) in fremden Chatnachrichten: im Panel als Kasten hervorgehoben
    // Fremde Nachricht an dich (@deinname) oder eigene Nachricht an jemanden (@Name): beides im Kasten
    m.insert(QStringLiteral("mention"), type != QLatin1String("system") && type != QLatin1String("automod")
                                            && (isOwnName(user) ? mentionsAnyone(raw) : mentionsMe(raw)));
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) m.insert(it.key(), it.value());
    m_history.append(m);
    while (m_history.size() > kHistoryMax) m_history.removeFirst();
    emit newMessage(m);
    return m_history.size() - 1;
}

// Nachrichten, die Moderatoren entfernt haben, bleiben sichtbar, werden aber ausgegraut.
void TwitchChatController::markDeleted(const QString &reason, const std::function<bool(const QVariantMap &)> &match)
{
    for (int i = 0; i < m_history.size(); ++i) {
        QVariantMap &m = m_history[i];
        const QString type = m.value(QStringLiteral("type")).toString();
        if (type != QLatin1String("chat") && type != QLatin1String("action")) continue;
        if (m.value(QStringLiteral("deleted")).toBool() || !match(m)) continue;
        m.insert(QStringLiteral("deleted"), true);
        m.insert(QStringLiteral("deletedReason"), reason);   // deleted | timeout | ban | clear
        emit messageUpdated(i);
    }
}

QString TwitchChatController::durationText(int seconds)
{
    if (seconds < 60) return QStringLiteral("%1 Sek.").arg(seconds);
    if (seconds < 3600) return QStringLiteral("%1 Min.").arg((seconds + 30) / 60);
    if (seconds < 86400) return QStringLiteral("%1 Std.").arg((seconds + 1800) / 3600);
    return QStringLiteral("%1 Tage").arg((seconds + 43200) / 86400);
}

QVariantMap TwitchChatController::historyItem(int index) const
{
    return (index >= 0 && index < m_history.size()) ? m_history[index] : QVariantMap();
}

QVariantList TwitchChatController::history() const
{
    QVariantList out;
    out.reserve(m_history.size());
    for (const QVariantMap &m : m_history) out.append(m);
    return out;
}

void TwitchChatController::clearHistory()
{
    m_history.clear();
    m_selfEchoPending = false;
    emit historyCleared();
}

// ------------------------------------------------------------ Auto-Kanal
void TwitchChatController::onMediaEvent(const QString &name, const QVariantMap &data)
{
    if (name == QLatin1String("media.loading")) {
        tryFollow(data.value(QStringLiteral("url")).toString(), false);
    } else if (name == QLatin1String("media.loaded")) {
        if (!tryFollow(data.value(QStringLiteral("source")).toString(), true))
            tryFollow(data.value(QStringLiteral("title")).toString(), true);
    }
}

static const QStringList &reservedChannelNames();   // weiter unten

void TwitchChatController::onMediaPath(const QString &path)
{
    if (tryFollow(path, false)) return;
    // Wiedergabe über einen Proxy (z. B. HLS2TCP): der Player meldet nur die aufgelöste Adresse
    // (tcp://host:port bzw. …/play.php?hls=1&s=…) ohne Kanalnamen. Der Proxy setzt den
    // Medientitel auf den Kanal – erst nach dem Laden, daher kurz nachfragen.
    if (m_autoChannel && isProxyStreamPath(path)) followFromMediaTitle(path, 0);
}

bool TwitchChatController::isProxyStreamPath(const QString &path)
{
    const QUrl u(path);
    const QString scheme = u.scheme().toLower();
    if (scheme == QLatin1String("tcp") || scheme == QLatin1String("udp")) return true;
    return (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
           && u.path().endsWith(QLatin1String("/play.php"), Qt::CaseInsensitive);
}

void TwitchChatController::followFromMediaTitle(const QString &path, int attempt)
{
    static const int delays[] = {250, 750, 1500, 3000, 6000};
    if (attempt >= int(std::size(delays))) return;
    QTimer::singleShot(delays[attempt], this, [this, path, attempt] {
        if (!m_host || !m_autoChannel || m_host->currentMediaPath() != path) return;   // inzwischen anderes Medium
        static const QRegularExpression loginRe{QStringLiteral("^[a-z0-9_]{2,25}$"), QRegularExpression::CaseInsensitiveOption};
        for (const char *prop : {"file-local-options/force-media-title", "media-title"}) {
            QString err;
            QString title = m_host->call(m_id, QStringLiteral("mpv.get"), {{QStringLiteral("name"), QString::fromLatin1(prop)}}, &err)
                                .toString().trimmed();
            if (title.startsWith(QLatin1Char('#'))) title.remove(0, 1);
            const QString ch = guessChannel(title);                       // vollständige Twitch-Adresse im Titel
            const QString login = !ch.isEmpty() ? ch
                                  : (loginRe.match(title).hasMatch() && !reservedChannelNames().contains(title.toLower())
                                         ? title.toLower() : QString());
            if (login.isEmpty()) continue;
            if (login != m_channel) {
                const bool wasManualClose = m_manualClose;
                setChannel(login);
                if (wasManualClose && !m_ws.isOpen()) openChat();
                m_host->log(m_id, QStringLiteral("info"), QStringLiteral("Auto-Kanal aus Medientitel erkannt: #%1").arg(login));
                m_host->showOsd(QStringLiteral("Twitch Chat: #%1").arg(login), 2500);
            }
            return;
        }
        followFromMediaTitle(path, attempt + 1);
    });
}

bool TwitchChatController::tryFollow(const QString &text, bool notify)
{
    if (!m_autoChannel || text.isEmpty()) return false;
    const QString ch = guessChannel(text);
    if (ch.isEmpty() || ch == m_channel) return false;
    const bool wasManualClose = m_manualClose;
    setChannel(ch);
    // Manuell getrennt? Bei neu abspieltem Twitch-Video trotzdem wieder verbinden.
    if (wasManualClose && !m_ws.isOpen()) openChat();
    if (m_host) {
        m_host->log(m_id, QStringLiteral("info"), QStringLiteral("Auto-Kanal aus Medien erkannt: #%1").arg(ch));
        if (notify) m_host->showOsd(QStringLiteral("Twitch Chat: #%1").arg(ch), 2500);
    }
    return true;
}

// ---------------------------------------------------- Kanal aus URL / Playlistdatei
static const QStringList &reservedChannelNames()
{
    static const QStringList r = {
        "videos", "clip", "clips", "community", "directory", "jobs", "p", "settings", "login",
        "inventory", "downloads", "about", "brand", "press", "dev", "privacy", "terms", "gdpr",
        "error", "404", "500", "subscriptions", "prime", "squadstream", "glitchcon", "music",
        "turbo", "r", "mobile", "blog", "contact", "security", "affiliates", "store", "academy",
        "data", "esports", "creators", "advertise", "tv", "embed", "search"
    };
    return r;
}

static QString channelFromUrl(const QString &text)
{
    // Aktuelle Twitch-Playlists: usher.ttvnw.net|usher.jtlnw.net /api/channel/hls/NAME.m3u8
    static const QRegularExpression usherRe{QStringLiteral(
        "usher\\.[a-z0-9.-]+/[a-z0-9/_.-]*?channel/hls/([a-z0-9_]{2,50})"), QRegularExpression::CaseInsensitiveOption};
    // Ältere API-Adressen: usher…/v2/NAME/…
    static const QRegularExpression legacyRe{QStringLiteral(
        "(?:usher\\.[a-z0-9.-]+|ttvnw\\.net|jtlnw\\.net|twitchcdn\\.net)[^\\s\"']*?/v2/([a-z0-9_]{2,50})/"),
        QRegularExpression::CaseInsensitiveOption};
    // Player-/Embed-Adressen
    static const QRegularExpression playerRe{QStringLiteral(
        "player\\.twitch\\.tv/?\\?channel=([a-z0-9_]{2,50})"), QRegularExpression::CaseInsensitiveOption};
    static const QRegularExpression embedRe{QStringLiteral(
        "twitch\\.tv/embed/([a-z0-9_]{2,50})"), QRegularExpression::CaseInsensitiveOption};
    // Normale Kanalseite: twitch.tv/NAME oder www./m. twitch.tv/NAME –
    // aber nicht clips.twitch.tv/… oder mytwitch.tv/… (dort ist twitch.tv nur Teil der Subdomain).
    static const QRegularExpression pageRe{QStringLiteral(
        "(?:^|[^a-z0-9.-])(?:www\\.|m\\.)?twitch\\.tv/([a-z0-9_]{2,50})(?:[/?#]|$)"),
        QRegularExpression::CaseInsensitiveOption};

    const auto m1 = usherRe.match(text);   if (m1.hasMatch()) return m1.captured(1).toLower();
    const auto m2 = legacyRe.match(text);  if (m2.hasMatch()) return m2.captured(1).toLower();
    const auto m3 = playerRe.match(text);  if (m3.hasMatch()) return m3.captured(1).toLower();
    const auto m4 = embedRe.match(text);   if (m4.hasMatch()) return m4.captured(1).toLower();
    const auto m5 = pageRe.match(text);
    if (m5.hasMatch()) {
        const QString c = m5.captured(1).toLower();
        if (!reservedChannelNames().contains(c)) return c;
    }
    return QString();
}

QString TwitchChatController::guessChannel(const QString &text)
{
    QString c = channelFromUrl(text);
    if (!c.isEmpty()) return c;

    // Lokale HLS-Playlists (.m3u8/.m3u/.strm): Twitch-Adresse in der Datei oder im Dateinamen.
    static const QRegularExpression fileRe{QStringLiteral("([a-z0-9_]{2,50})\\.(?:m3u8|m3u|strm)$"),
                                           QRegularExpression::CaseInsensitiveOption};
    const auto fm = fileRe.match(text);
    if (!fm.hasMatch()) return QString();

    QFile f(text);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray head = f.read(16384);
        f.close();
        c = channelFromUrl(QString::fromUtf8(head));
        if (!c.isEmpty()) return c;
    }
    const QString cand = fm.captured(1).toLower();
    return reservedChannelNames().contains(cand) ? QString() : cand;
}

// ------------------------------------------------------------ Persistenz
void TwitchChatController::setStatus(const QString &key, const QString &detail)
{
    m_statusKey = key;
    m_statusDetail = detail;
    emit statusChanged();
}

void TwitchChatController::persist()
{
    if (!m_host) return;
    // Lesen–Modifizieren–Schreiben: andere Plugin-Einstellungen bleiben erhalten.
    QJsonObject s = m_host->loadPluginSettings(m_id);
    s.insert(QStringLiteral("channel"), m_channel);
    s.insert(QStringLiteral("autoChannel"), m_autoChannel);
    s.insert(QStringLiteral("autoscroll"), m_autoScroll);
    s.insert(QStringLiteral("chatFontSize"), m_chatFontSize);
    s.insert(QStringLiteral("notifySound"), m_notifySound);
    s.insert(QStringLiteral("notifyVolume"), m_sound.volumePercent());
    QJsonObject boxColors;
    for (auto it = m_boxColors.constBegin(); it != m_boxColors.constEnd(); ++it)
        boxColors.insert(it.key(), it.value());
    s.insert(QStringLiteral("boxColors"), boxColors);
    QJsonArray bots;
    for (const EventBot &b : m_eventBots)
        bots.append(QJsonObject{{QStringLiteral("login"), b.login}, {QStringLiteral("label"), b.label},
                                {QStringLiteral("color"), b.color}});
    s.insert(QStringLiteral("eventBots"), bots);
    QJsonObject rooms;                                // merken, welche Raum-ID ein Kanal hat (7tv-Schnellstart)
    for (auto it = m_roomIds.constBegin(); it != m_roomIds.constEnd(); ++it)
        rooms.insert(it.key(), it.value());
    s.insert(QStringLiteral("roomIds"), rooms);
    s.remove(QStringLiteral("embedMode"));   // alter Einbettungs-Modus ist endgültig entfernt
    s.remove(QStringLiteral("user"));    // Tokens nicht mehr in Klartext-Einstellungen
    s.remove(QStringLiteral("token"));
    QJsonArray favs;
    for (auto it = m_favoriteEmotes.constBegin(); it != m_favoriteEmotes.constEnd(); ++it) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), it.key());
        o.insert(QStringLiteral("id"), it.value());
        favs.append(o);
    }
    s.insert(QStringLiteral("favoriteEmotes"), favs);
    s.insert(QStringLiteral("spellGerman"), m_spellGerman);
    s.insert(QStringLiteral("spellEnglish"), m_spellEnglish);
    QJsonArray spellWords;
    for (const QString &w : m_spellWords)
        spellWords.append(w);
    s.insert(QStringLiteral("spellWords"), spellWords);
    s.insert(QStringLiteral("grammarCheck"), m_grammarCheck);
    s.insert(QStringLiteral("grammarOnline"), m_grammarOnline);
    s.insert(QStringLiteral("predictText"), m_predictText);
    s.insert(QStringLiteral("hiddenPin"), m_hiddenPin);
    s.insert(QStringLiteral("pinCollapsed"), m_pinCollapsed);
    s.insert(QStringLiteral("pollCollapsed"), m_pollCollapsed);
    s.insert(QStringLiteral("predictionCollapsed"), m_predictionCollapsed);
    m_host->savePluginSettings(m_id, s);
}

// ------------------------------------------------------------ Segmente / Emoticons
// Jede Nachricht wird in eine Segmentfolge zerlegt, die das Panel als fließende Zeile
// darstellt (Text-Wörter und Emote-Bilder in einer Zeile, Zeilenumbruch wie bisher):
//   {t:"text",  s:"…"}
//   {t:"emote", name, key, src(file-URL), anim, overlays:[{name,src,anim},…]}
// anim:true → AnimatedImage zeigt Bewegung; overlays = Zero-Width-Emotes, die über dem
// Emote liegen. Native Twitch-Emotes kommen exakt positioniert aus dem „emotes“-Tag.
// Drittanbieter-Emotes haben keine Tags: Wie in den Browser-Erweiterungen zählt nur ein
// ganzes, durch Leerraum getrenntes Wort mit exakt gleicher Schreibweise.
// Zerlegt ein Wort in Satzzeichen davor, Link und Satzzeichen danach. Klickbar wird nur http(s)
// ohne Benutzerteil: „https://twitch.tv@boese.example“ führt nicht zu Twitch und bleibt Text.
// Leere url = kein Link.
struct LinkParts { QString before, link, after, url; };
static LinkParts findLink(const QString &word)
{
    static const QString leading = QStringLiteral("([{<\"'„“«‹");
    static const QString trailing = QStringLiteral(".,;:!?)]}>\"'“”»›…");
    int begin = 0;
    int end = int(word.size());
    while (begin < end && leading.contains(word.at(begin))) ++begin;
    while (end > begin && trailing.contains(word.at(end - 1))) {
        // Schließende Klammer gehört dazu, wenn der Link sie auch öffnet (Wikipedia).
        const QChar last = word.at(end - 1);
        if (last == QLatin1Char(')') && word.mid(begin, end - begin).count(QLatin1Char('('))
                                            >= word.mid(begin, end - begin).count(QLatin1Char(')')))
            break;
        --end;
    }
    LinkParts parts{word, QString(), QString(), QString()};
    const QString candidate = word.mid(begin, end - begin);
    if (candidate.isEmpty()) return parts;

    static const QRegularExpression scheme(QStringLiteral("^https?://\\S+$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression www(QStringLiteral("^www\\.[^\\s./]+\\.\\S+$"), QRegularExpression::CaseInsensitiveOption);
    // Ohne Schema nur bekannte Endungen, sonst würden „node.js“ oder „z.B“ zu Links.
    static const QRegularExpression bare(QStringLiteral("^(?:[a-z0-9-]+\\.)+(?:com|de|net|org|tv|gg|io|be|at|ch|eu|uk|me|co|app|info)(?:[/?#]\\S*)?$"),
                                         QRegularExpression::CaseInsensitiveOption);
    const bool hasScheme = scheme.match(candidate).hasMatch();
    if (!hasScheme && !www.match(candidate).hasMatch() && !bare.match(candidate).hasMatch()) return parts;

    const QUrl url(hasScheme ? candidate : QStringLiteral("https://") + candidate, QUrl::TolerantMode);
    const QString protocol = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
        || (protocol != QLatin1String("http") && protocol != QLatin1String("https")))
        return parts;

    parts.before = word.left(begin);
    parts.link = candidate;
    parts.after = word.mid(end);
    parts.url = url.toString(QUrl::FullyEncoded);   // Internationale Domains erscheinen als xn--…
    return parts;
}

// Ein Cheer-Wort wie „Cheer250“ oder „4Head1000“: Präfix bekannt, Rest nur Ziffern. Liefert den
// Bild-Schlüssel der passenden Stufe (größte Mindestmenge ≤ Anzahl) und die Länge des Präfixes.
QString TwitchChatController::cheerKey(const QString &word, int *prefixLength) const
{
    int digits = int(word.size());
    while (digits > 0 && word.at(digits - 1).isDigit()) --digits;
    if (digits == 0 || digits == word.size()) return QString();
    const QString prefix = word.left(digits).toLower();
    const auto tiers = m_cheermotes.constFind(prefix);
    if (tiers == m_cheermotes.constEnd() || tiers->isEmpty()) return QString();
    bool ok = false;
    const int amount = word.mid(digits).toInt(&ok);
    if (!ok || amount <= 0) return QString();
    int tier = tiers->first().minBits;
    for (const CheerTier &t : *tiers)
        if (t.minBits <= amount && t.minBits >= tier) tier = t.minBits;
    if (prefixLength) *prefixLength = digits;
    return QStringLiteral("cheer_%1_%2").arg(QString(prefix).remove(QRegularExpression(QStringLiteral("[^a-z0-9]")))).arg(tier);
}

// Kanal-eigene Cheer-Emotes (Partner) und die aktuellen globalen über Helix (angemeldet)
void TwitchChatController::fetchCheermotes()
{
    if (m_roomId.isEmpty() || m_token.isEmpty()) return;
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("broadcaster_id"), m_roomId);
    helix("GET", QStringLiteral("/bits/cheermotes"), q, QByteArray(), [this](int status, const QByteArray &body) {
        if (status != 200) return;
        bool changed = false;
        for (const QJsonValue &v : QJsonDocument::fromJson(body).object().value(QStringLiteral("data")).toArray()) {
            const QJsonObject c = v.toObject();
            const QString prefix = c.value(QStringLiteral("prefix")).toString().toLower();
            QVector<CheerTier> tiers;
            for (const QJsonValue &t : c.value(QStringLiteral("tiers")).toArray()) {
                const QJsonObject tier = t.toObject();
                const QString url = tier.value(QStringLiteral("images")).toObject().value(QStringLiteral("dark")).toObject()
                                        .value(QStringLiteral("animated")).toObject().value(QStringLiteral("2")).toString();
                if (!url.isEmpty()) tiers << CheerTier{tier.value(QStringLiteral("min_bits")).toInt(), url};
            }
            if (!prefix.isEmpty() && !tiers.isEmpty()) { m_cheermotes.insert(prefix, tiers); changed = true; }
        }
        if (changed) rebuildAllMessages();
    });
}

QVariantList TwitchChatController::buildSegments(const QString &msg, const QString &emoteTag,
                                                 QStringList &emoteKeys, bool cheers) const
{
    struct Range { int start, end; QString key; bool zeroWidth; };
    QVector<Range> native;
    if (!emoteTag.isEmpty()) {
        const auto groups = emoteTag.split(QLatin1Char('/'));
        for (const QString &g : groups) {
            const int c = g.indexOf(QLatin1Char(':'));
            if (c <= 0) continue;
            const QString id = g.left(c);
            const auto parts = g.mid(c + 1).split(QLatin1Char(','));
            for (const QString &p : parts) {
                const int d = p.indexOf(QLatin1Char('-'));
                if (d <= 0) continue;
                bool ok1 = false, ok2 = false;
                const int s = p.left(d).toInt(&ok1), e = p.mid(d + 1).toInt(&ok2);
                if (!ok1 || !ok2 || s < 0 || e < s || e >= msg.size()) continue;
                native.append({ s, e, id, false });
                if (!emoteKeys.contains(id)) emoteKeys << id;
            }
        }
        std::sort(native.begin(), native.end(),
                  [](const Range &a, const Range &b) { return a.start < b.start; });
    }

    QVector<Range> all = native;
    if (!m_tpLookup.isEmpty()) {
        int i = 0, ni = 0;
        while (i < msg.size()) {
            while (i < msg.size() && msg[i].isSpace()) ++i;
            int w = i;
            while (w < msg.size() && !msg[w].isSpace()) ++w;
            if (w == i) break;
            while (ni < native.size() && native[ni].end < i) ++ni;
            const bool coveredByTag = ni < native.size() && native[ni].start < w;
            if (!coveredByTag) {
                const auto hit = m_tpLookup.constFind(msg.mid(i, w - i));
                if (hit != m_tpLookup.constEnd()) {
                    all.append({ i, w - 1, hit->key, hit->zeroWidth });
                    if (!emoteKeys.contains(hit->key)) emoteKeys << hit->key;
                }
            }
            i = w;
        }
        std::sort(all.begin(), all.end(),
                  [](const Range &a, const Range &b) { return a.start < b.start; });
    }

    if (cheers && !m_cheermotes.isEmpty()) {
        int i = 0;
        while (i < msg.size()) {
            while (i < msg.size() && msg[i].isSpace()) ++i;
            int w = i;
            while (w < msg.size() && !msg[w].isSpace()) ++w;
            if (w == i) break;
            const bool covered = std::any_of(all.cbegin(), all.cend(), [&](const Range &r) { return r.start < w && r.end >= i; });
            int prefixLength = 0;
            const QString key = covered ? QString() : cheerKey(msg.mid(i, w - i), &prefixLength);
            if (!key.isEmpty()) {
                all.append({ i, i + prefixLength - 1, key, false });
                if (!emoteKeys.contains(key)) emoteKeys << key;
            }
            i = w;
        }
        std::sort(all.begin(), all.end(), [](const Range &a, const Range &b) { return a.start < b.start; });
    }

    QVariantList segs;
    int pos = 0;
    auto flushRun = [&](int upTo) {
        if (upTo <= pos) return;
        const QString run = msg.mid(pos, upTo - pos);
        int k = 0;
        while (k < run.size()) {
            while (k < run.size() && run[k].isSpace()) ++k;
            int w = k;
            while (w < run.size() && !run[w].isSpace()) ++w;
            if (w > k) {
                // o = Position des ersten Zeichens nach dem Trenn-Leerzeichen in der Rohnachricht:
                // Damit markiert das Panel genau die Zeichen, über die die Maus zieht.
                const auto addText = [&segs](const QString &s, int o, const QString &url = QString()) {
                    QVariantMap t;
                    t.insert(QStringLiteral("t"), QStringLiteral("text"));
                    t.insert(QStringLiteral("s"), s);
                    t.insert(QStringLiteral("o"), o);
                    if (!url.isEmpty()) t.insert(QStringLiteral("url"), url);
                    segs.append(t);
                };
                const int at = pos + k;      // Wortanfang in msg
                const QString space = segs.isEmpty() ? QString() : QStringLiteral(" ");   // Trennung zwischen Wortbildern
                const LinkParts parts = findLink(run.mid(k, w - k));
                if (parts.url.isEmpty()) {
                    addText(space + parts.before, at);
                } else {
                    // Das Leerzeichen bleibt außerhalb des Links, sonst wäre es mit unterstrichen.
                    if (!space.isEmpty() || !parts.before.isEmpty()) addText(space + parts.before, at);
                    addText(parts.link, at + int(parts.before.size()), parts.url);
                    if (!parts.after.isEmpty()) addText(parts.after, at + int(parts.before.size() + parts.link.size()));
                }
            }
            k = w;
        }
        pos = upTo;
    };
    for (const Range &r : all) {
        if (r.start < pos) continue;                      // Überlappungen sicher auslassen
        const QString file = m_emotes ? m_emotes->localPath(r.key) : QString();
        if (file.isEmpty()) continue;                     // Bild noch nicht da: Name bleibt Text
        const QString name = msg.mid(r.start, r.end - r.start + 1);
        const QString src = QUrl::fromLocalFile(file).toString();
        const bool anim = m_emotes->isAnimated(r.key);

        // Zero-Width: legt sich aufs Emote direkt davor. Ohne Emote davor steht es für sich.
        if (r.zeroWidth && !segs.isEmpty() && msg.mid(pos, r.start - pos).trimmed().isEmpty()) {
            QVariantMap base = segs.last().toMap();
            if (base.value(QStringLiteral("t")).toString() == QLatin1String("emote")) {
                QVariantMap overlay;
                overlay.insert(QStringLiteral("name"), name);
                overlay.insert(QStringLiteral("src"), src);
                overlay.insert(QStringLiteral("anim"), anim);
                QVariantList overlays = base.value(QStringLiteral("overlays")).toList();
                overlays.append(overlay);
                base.insert(QStringLiteral("overlays"), overlays);
                segs.last() = base;
                pos = r.end + 1;
                continue;
            }
        }
        flushRun(r.start);
        QVariantMap e;
        e.insert(QStringLiteral("t"), QStringLiteral("emote"));
        e.insert(QStringLiteral("name"), name);
        e.insert(QStringLiteral("key"), r.key);
        e.insert(QStringLiteral("src"), src);
        e.insert(QStringLiteral("anim"), anim);
        e.insert(QStringLiteral("o"), r.start);
        e.insert(QStringLiteral("n"), r.end - r.start + 1);
        segs.append(e);
        pos = r.end + 1;
    }
    flushRun(msg.size());

    // Fehlende Bilder anstoßen (Twitch-Cache oder CDN des Drittanbieters).
    if (m_emotes && !emoteKeys.isEmpty()) {
        QStringList twitchKeys;
        for (const QString &k : emoteKeys) {
            if (EmoteCache::providerOf(k) == QLatin1String("cheer")) {
                // Schlüssel cheer_<präfix>_<stufe> → Adresse aus der Stufenliste
                const QString rest = k.mid(6);
                const int cut = int(rest.lastIndexOf(QLatin1Char('_')));
                const int minBits = rest.mid(cut + 1).toInt();
                for (auto it = m_cheermotes.constBegin(); it != m_cheermotes.constEnd(); ++it) {
                    if (QString(it.key()).remove(QRegularExpression(QStringLiteral("[^a-z0-9]"))) != rest.left(cut)) continue;
                    for (const CheerTier &t : it.value())
                        if (t.minBits == minBits) m_emotes->requestUrls(k, {t.url});
                }
            } else if (!EmoteCache::providerOf(k).isEmpty())
                m_emotes->requestThirdParty(k, m_tpAnimated.value(k, false));
            else
                twitchKeys << k;
        }
        if (!twitchKeys.isEmpty()) m_emotes->request(twitchKeys);
    }
    return segs;
}

void TwitchChatController::rebuildMessage(int index)
{
    if (index < 0 || index >= m_history.size()) return;
    QVariantMap &e = m_history[index];
    QStringList keys;
    const QVariantList segs = buildSegments(e.value(QStringLiteral("raw")).toString(),
                                            e.value(QStringLiteral("emotesTag")).toString(), keys,
                                            e.value(QStringLiteral("bits")).toInt() > 0);
    e.insert(QStringLiteral("emoteIds"), QVariant(keys));
    // Unveränderte Zeilen nicht neu aufbauen – das würde ihre laufenden Animationen neu starten.
    if (e.value(QStringLiteral("segments")).toList() == segs) return;
    e.insert(QStringLiteral("segments"), segs);
    emit messageUpdated(index);
}

void TwitchChatController::rebuildAllMessages()
{
    // Katalog traf verspätet ein → alle Verlaufszeilen noch einmal durchsuchen.
    for (int i = 0; i < m_history.size(); ++i) rebuildMessage(i);
}

void TwitchChatController::onEmoteReady(const QString &id)
{
    if (id.startsWith(QLatin1String("badge_"))) {   // Abzeichen hängen an badgeRevision, nicht an den Segmenten
        ++m_badgeRevision;
        emit badgeImagesChanged();
        return;
    }
    // Bild ist da → alle Nachrichten mit diesem Emote-Schlüssel neu rendern
    for (int i = 0; i < m_history.size(); ++i) {
        const QVariantList ids = m_history[i].value(QStringLiteral("emoteIds")).toList();
        bool hit = false;
        for (const QVariant &v : ids) { if (v.toString() == id) { hit = true; break; } }
        if (hit) rebuildMessage(i);
    }
    if (m_pinned.value(QStringLiteral("emoteIds")).toStringList().contains(id)) rebuildPinned();
    emit emoteImageReady();
}

// ------------------------------------------------------ Emote-Katalog (Browser im Panel)
// Quelle 1 (immer verfügbar): jeder Chat-Nachrichten-Tag "emotes=ID:START-END/…"
// liefert Emote-ID UND Position – der Name steht in der Nachricht selbst. So
// entstehen die "beobachteten" Emotes, ganz ohne Login und mit echten IDs, für
// die static-cdn die Bilder hat.
// Quelle 2 (ohne Login): Kanal-, Global- und HypeTrain-Listen über gql.twitch.tv –
// dieselbe GraphQL-Schnittstelle, die die Twitch-Webseite selbst benutzt. Die verwendete
// Client-ID ist Twitchs öffentliche Web-Client-ID, kein Geheimnis; ein Client-Secret wird
// nirgends verwendet oder gespeichert. Scheitert die Abfrage, bleibt es bei Quelle 1.
static const char *kGqlClient = "kimne78kx3ncx6brgo4mv6wki5h1ko";   // öffentliche Twitch-Web-Client-ID

static QString emoteImageUrl(const QString &id)
{
    return QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/%1/default/dark/2.0").arg(id);
}

void TwitchChatController::noteObservedEmotes(const QString &raw, const QString &emoteTag)
{
    if (emoteTag.isEmpty()) return;
    bool added = false;
    const auto groups = emoteTag.split(QLatin1Char('/'));
    for (const QString &g : groups) {
        const int c = g.indexOf(QLatin1Char(':'));
        if (c <= 0) continue;
        const QString id = g.left(c);
        const auto parts = g.mid(c + 1).split(QLatin1Char(','));
        for (const QString &p : parts) {
            const int d = p.indexOf(QLatin1Char('-'));
            if (d <= 0) continue;
            bool ok1 = false, ok2 = false;
            const int s = p.left(d).toInt(&ok1), e = p.mid(d + 1).toInt(&ok2);
            if (!ok1 || !ok2 || s < 0 || e < s || e >= raw.size()) continue;
            const QString name = raw.mid(s, e - s + 1);
            if (name.isEmpty() || m_observedEmotes.value(name) == id) continue;
            m_observedEmotes.insert(name, id);
            added = true;
        }
    }
    if (added) emit emotesChanged();
}

QVariantList TwitchChatController::emoteList(const QString &kind) const
{
    // kind: "all" | "subs" | "twitch" | "7tv" | "bttv" | "ffz" | "favorites".
    // Jeder Eintrag trägt seinen Abschnitt („subs“, „twitch-global“, „7tv-channel“ …); die Liste ist
    // nach Abschnitt und darin nach Name sortiert. Ein Emote erscheint nur im ersten passenden.
    QVariantList out;
    QSet<QString> seen;
    QSet<QString> ownIds;
    for (const QString &id : m_ownEmotes) ownIds.insert(id);
    const auto addSection = [&](const QString &section, const QHash<QString, QString> &emotes) {
        QStringList names = emotes.keys();
        std::sort(names.begin(), names.end(),
                  [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
        for (const QString &n : names) {
            const QString key = emotes.value(n);
            if (seen.contains(key)) continue;
            seen.insert(key);
            const QString provider = EmoteCache::providerOf(key);
            const bool thirdParty = !provider.isEmpty();
            const bool cached = m_emotes && !m_emotes->localPath(key).isEmpty();
            const bool animated = (cached && m_emotes->isAnimated(key)) || m_tpAnimated.value(key, false);
            QVariantMap e;
            e.insert(QStringLiteral("name"), n);
            e.insert(QStringLiteral("id"), key);
            // Animierte Kacheln in 1x: 2x-Dateien erreichen mehrere MB, und AnimatedImage dekodiert
            // auf dem GUI-Thread – das würde den Chat ausbremsen. FFZ führt 2x nicht für jedes Emote.
            const int scale = (animated || provider == QLatin1String("ffz")) ? 1 : 2;
            e.insert(QStringLiteral("url"), thirdParty ? EmoteCache::remoteUrl(key, animated, scale)
                                                       : emoteImageUrl(key));
            e.insert(QStringLiteral("provider"), thirdParty ? provider : QStringLiteral("twitch"));
            e.insert(QStringLiteral("animated"), animated);
            e.insert(QStringLiteral("favorite"), m_favoriteEmotes.value(n) == key);
            e.insert(QStringLiteral("section"), section);
            // Kanal-Emote ohne passendes Abo: Twitch zeigt es beim Senden nur als Text.
            e.insert(QStringLiteral("locked"), section == QLatin1String("twitch-channel") && !ownIds.contains(key));
            out.append(e);
        }
    };
    const auto keysOf = [](const ThirdPartyCatalog &catalog) {
        QHash<QString, QString> emotes;
        for (auto it = catalog.constBegin(); it != catalog.constEnd(); ++it)
            emotes.insert(it.key(), it->key);
        return emotes;
    };

    if (kind == QLatin1String("favorites")) {
        // Favoriten nach Herkunft gruppieren, damit auch hier klar bleibt, was wovon stammt.
        QHash<QString, QHash<QString, QString>> bySection;
        for (auto it = m_favoriteEmotes.constBegin(); it != m_favoriteEmotes.constEnd(); ++it) {
            const QString provider = EmoteCache::providerOf(it.value());
            const QString section = !provider.isEmpty() ? provider
                : m_subEmotes.value(it.key()) == it.value() ? QStringLiteral("subs")
                                                            : QStringLiteral("twitch");
            bySection[section].insert(it.key(), it.value());
        }
        for (const char *section : { "subs", "twitch", "7tv", "bttv", "ffz" })
            addSection(QString::fromLatin1(section), bySection.value(QString::fromLatin1(section)));
        return out;
    }

    const bool all = kind == QLatin1String("all");
    if (all || kind == QLatin1String("subs"))
        addSection(QStringLiteral("subs"), m_subEmotes);
    if (all || kind == QLatin1String("twitch")) {
        addSection(QStringLiteral("twitch-channel"), m_channelEmotes);
        addSection(QStringLiteral("twitch-global"), m_globalEmotes);
        addSection(QStringLiteral("twitch-hype"), m_hypeOwned);
        addSection(QStringLiteral("twitch-seen"), m_observedEmotes);
    }
    static const struct { EmoteProvider provider; const char *id; } kProviders[] = {
        { Provider7tv, "7tv" }, { ProviderBttv, "bttv" }, { ProviderFfz, "ffz" } };
    for (const auto &p : kProviders) {
        const QString id = QString::fromLatin1(p.id);
        if (!all && kind != id) continue;
        addSection(id + QStringLiteral("-channel"), keysOf(m_tpChannel[p.provider]));
        addSection(id + QStringLiteral("-global"), keysOf(m_tpGlobal[p.provider]));
    }
    return out;
}

QString TwitchChatController::emoteFile(const QString &emoteId) const
{
    if (!m_emotes) return QString();
    const QString p = m_emotes->localPath(emoteId);
    return p.isEmpty() ? QString() : QUrl::fromLocalFile(p).toString();
}

void TwitchChatController::toggleEmoteFavorite(const QString &name, const QString &key)
{
    if (name.isEmpty()) return;
    // Mit Schlüssel: Gleichnamige Emotes verschiedener Anbieter bleiben unterscheidbar.
    if (m_favoriteEmotes.contains(name) && (key.isEmpty() || m_favoriteEmotes.value(name) == key)) {
        m_favoriteEmotes.remove(name);
    } else {
        // Ohne Schlüssel: Drittanbieter entscheiden, wenn ein Name beiden Seiten gehört
        QString id = key;
        if (!id.isEmpty())                        { /* von der Kachel vorgegeben */ }
        else if (m_tpLookup.contains(name))       id = m_tpLookup.value(name).key;
        else if (m_channelEmotes.contains(name))  id = m_channelEmotes.value(name);
        else if (m_globalEmotes.contains(name))   id = m_globalEmotes.value(name);
        else if (m_observedEmotes.contains(name)) id = m_observedEmotes.value(name);
        else if (m_subEmotes.contains(name))      id = m_subEmotes.value(name);
        else return;                             // Emote noch unbekannt → nichts zu favorisieren
        m_favoriteEmotes.insert(name, id);
    }
    persist();
    emit emotesChanged();
}

void TwitchChatController::fetchEmoteCatalogs()
{
    if (m_channel.isEmpty()) return;
    if (!m_net) m_net = new QNetworkAccessManager(this);
    // m_channel ist vorher bereits auf ^[a-z0-9_]{2,50}$ geprüft – sicher für die Abfrage.
    requestGql(QStringLiteral("{ user(login: \"%1\") { subscriptionProducts { emotes { token id assetType } } } }")
                   .arg(m_channel),
               QStringLiteral("channel"));
    if (!m_globalTried) {
        m_globalTried = true;
        requestGql(QStringLiteral("{ emoteSet(id: \"0\") { emotes { token id assetType } } }"),
                   QStringLiteral("global"));
    }
}

void TwitchChatController::requestGql(const QString &query, const QString &target)
{
    if (!m_net) return;
    // Bewusst ohne Token: Diese Kataloge sind öffentlich, und Twitch lehnt hier jeden Token ab,
    // der nicht für genau diese Client-ID ausgestellt wurde.
    QNetworkRequest req{QUrl(QStringLiteral("https://gql.twitch.tv/gql"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Client-ID", QByteArray(kGqlClient));
    QJsonObject body;
    body.insert(QStringLiteral("query"), query);
    QNetworkReply *reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    const QString requestedChannel = m_channel;
    connect(reply, &QNetworkReply::finished, this, [this, reply, target, requestedChannel] {
        const QNetworkReply::NetworkError err = reply->error();
        const QString why = reply->errorString();
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        // Antwort für einen Kanal, den der Nutzer inzwischen verlassen hat.
        if ((target == QLatin1String("channel") || target == QLatin1String("pinned") || target == QLatin1String("events")
             || target == QLatin1String("badgesChannel"))
            && requestedChannel != m_channel) return;
        if (target == QLatin1String("badgesChannel") || target == QLatin1String("badgesGlobal")) {
            applyBadgesJson(err == QNetworkReply::NoError ? data : QByteArray(), target == QLatin1String("badgesChannel"));
            return;
        }
        if (target == QLatin1String("events")) {
            if (err == QNetworkReply::NoError) applyChannelEventsJson(data);
            return;
        }
        if (target == QLatin1String("pinned")) {
            if (err == QNetworkReply::NoError) applyPinnedJson(data);
            return;
        }
        if (target == QLatin1String("viewers")) {
            if (requestedChannel == m_channel && err == QNetworkReply::NoError) applyViewersJson(data);
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(data).object();
        const QJsonObject d = root.value(QStringLiteral("data")).toObject();
        bool changed = false;
        const auto apply = [this, &changed](const QJsonArray &emotes, QHash<QString, QString> &into) {
            for (const QJsonValue &ev : emotes) {
                const QJsonObject e = ev.toObject();
                const QString name = e.value(QStringLiteral("token")).toString();
                const QString id = e.value(QStringLiteral("id")).toString();
                if (name.isEmpty() || id.isEmpty()) continue;
                if (e.value(QStringLiteral("assetType")).toString() == QLatin1String("ANIMATED"))
                    m_tpAnimated.insert(id, true);
                if (into.value(name) != id) { into.insert(name, id); changed = true; }
            }
        };
        const QHash<QString, QString> *catalog = &m_globalEmotes;
        if (target == QLatin1String("channel")) {
            catalog = &m_channelEmotes;
            const auto products = d.value(QStringLiteral("user")).toObject()
                                      .value(QStringLiteral("subscriptionProducts")).toArray();
            for (const QJsonValue &pv : products)
                apply(pv.toObject().value(QStringLiteral("emotes")).toArray(), m_channelEmotes);
        } else {
            apply(d.value(QStringLiteral("emoteSet")).toObject()
                      .value(QStringLiteral("emotes")).toArray(), m_globalEmotes);
        }
        if (changed) {
            emit emotesChanged();
            if (m_host) m_host->log(m_id, QStringLiteral("info"), QStringLiteral(
                "Emote-Katalog geladen (%1): %2 Emotes").arg(target).arg(catalog->size()));
        } else if (err != QNetworkReply::NoError || root.contains(QStringLiteral("errors"))) {
            if (target != QLatin1String("channel")) m_globalTried = false;   // beim nächsten Verbinden erneut
            // Kein Grund zur Sorge: ohne Vollliste zeigt der Emote-Browser die im Chat gesehenen Emotes.
            if (m_host) m_host->log(m_id, QStringLiteral("warn"), QStringLiteral(
                "Emote-Vollliste (%1) nicht abrufbar (%2) – der Emote-Browser zeigt die im Chat gesehenen Emotes.")
                .arg(target, err != QNetworkReply::NoError ? why
                                                           : QStringLiteral("Twitch meldet einen Fehler")));
        }
    });
}

// ------------------------------------------------------- Abo-Emotes des angemeldeten Kontos
// Der Chat-Server nennt nach dem Login im emote-sets-Tag alle Sets des Kontos. Helix liefert dazu
// die Emotes, verlangt aber die Client-ID, für die der Token ausgestellt wurde. Die nennt
// oauth2/validate, deshalb funktioniert das auch mit Tokens fremder Generatoren. Zusätzliche
// Token-Rechte sind nicht nötig.
void TwitchChatController::clearSubscriptionEmotes()
{
    ++m_subFetchGeneration;
    m_subSetIds.clear();
    m_emoteScopeMissing = false;
    m_ownEmotes.clear();
    m_subEmotes.clear();
    m_hypeOwned.clear();
    emit emotesChanged();
}

void TwitchChatController::setEmoteScopeMissing(bool missing)
{
    if (m_emoteScopeMissing == missing) return;
    m_emoteScopeMissing = missing;
    emit emotesChanged();
}

// Vollständige Liste des Kontos, auch einzeln freigeschaltete Emotes ohne Set (HypeTrain).
void TwitchChatController::requestUserEmotes(const QString &clientId, const QString &userId, const QString &cursor)
{
    QUrl url(QStringLiteral("https://api.twitch.tv/helix/chat/emotes/user"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("user_id"), userId);
    if (!cursor.isEmpty()) query.addQueryItem(QStringLiteral("after"), cursor);
    url.setQuery(query);
    QNetworkRequest req{url};
    req.setRawHeader("Client-Id", clientId.toUtf8());
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    QNetworkReply *reply = m_net->get(req);
    const quint64 generation = m_subFetchGeneration;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, clientId, userId] {
        reply->deleteLater();
        if (generation != m_subFetchGeneration) return;
        if (reply->error() != QNetworkReply::NoError) {
            if (m_host) m_host->log(m_id, QStringLiteral("warn"),
                QStringLiteral("Emotes des Kontos nicht abrufbar: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray data = reply->readAll();
        applySubscriptionEmotes(data);
        const QString next = QJsonDocument::fromJson(data).object().value(QStringLiteral("pagination")).toObject()
                                 .value(QStringLiteral("cursor")).toString();
        if (!next.isEmpty()) {
            requestUserEmotes(clientId, userId, next);
        } else if (m_host) {
            m_host->log(m_id, QStringLiteral("info"), QStringLiteral(
                "Emotes des Kontos geladen: %1 gesamt, %2 Abo, %3 TwitchHypeTrain")
                .arg(m_ownEmotes.size()).arg(m_subEmotes.size()).arg(m_hypeOwned.size()));
        }
    });
}

QString TwitchChatController::ownEmoteTag(const QString &message) const
{
    QStringList groups;
    int i = 0;
    while (i < message.size()) {
        while (i < message.size() && message[i].isSpace()) ++i;
        int w = i;
        while (w < message.size() && !message[w].isSpace()) ++w;
        const QString word = message.mid(i, w - i);
        // Global-Emotes darf jedes Konto benutzen.
        const QString id = m_ownEmotes.contains(word) ? m_ownEmotes.value(word) : m_globalEmotes.value(word);
        if (w > i && !id.isEmpty())
            groups << QStringLiteral("%1:%2-%3").arg(id).arg(i).arg(w - 1);
        i = w;
    }
    return groups.join(QLatin1Char('/'));
}

// Twitch meldet auch die Affen- und Roboter-Smileys (Sets 42 und 33, Kennungen 555555…) als
// „subscriptions“, obwohl sie zu keinem Kanal gehören. Ein echtes Abo-Emote gehört einem Kanal.
static bool isChannelSubscriptionEmote(const QJsonObject &e)
{
    if (e.value(QStringLiteral("emote_type")).toString() != QLatin1String("subscriptions")) return false;
    const QString set = e.value(QStringLiteral("emote_set_id")).toString();
    if (set == QLatin1String("0") || set == QLatin1String("33") || set == QLatin1String("42")) return false;
    if (e.value(QStringLiteral("id")).toString().startsWith(QLatin1String("555555"))) return false;
    const QString owner = e.value(QStringLiteral("owner_id")).toString();
    return owner != QLatin1String("0") && owner != QLatin1String("twitch") && owner != QLatin1String("12826");
}

void TwitchChatController::applySubscriptionEmotes(const QByteArray &json)
{
    const QJsonArray data = QJsonDocument::fromJson(json).object().value(QStringLiteral("data")).toArray();
    bool changed = false;
    for (const QJsonValue &v : data) {
        const QJsonObject e = v.toObject();
        const QString name = e.value(QStringLiteral("name")).toString();
        const QString id = e.value(QStringLiteral("id")).toString();
        if (name.isEmpty() || id.isEmpty()) continue;
        if (m_ownEmotes.value(name) != id) { m_ownEmotes.insert(name, id); changed = true; }   // Sperren im Browser
        const QString type = e.value(QStringLiteral("emote_type")).toString();
        // Das Konto „TwitchHypeTrain“ besitzt alle HypeTrain-Emotes, auch wenn der Typ anders lautet.
        const bool hype = type == QLatin1String("hypetrain")
                          || e.value(QStringLiteral("owner_id")).toString() == QLatin1String("477339272");
        QHash<QString, QString> *into = hype ? &m_hypeOwned
                                      : isChannelSubscriptionEmote(e) ? &m_subEmotes
                                                                      : nullptr;
        if (!into) continue;
        if (e.value(QStringLiteral("format")).toArray().contains(QStringLiteral("animated")))
            m_tpAnimated.insert(id, true);
        if (into->value(name) != id) { into->insert(name, id); changed = true; }
    }
    if (changed) emit emotesChanged();
}

void TwitchChatController::applyGlobalEmotes(const QByteArray &json)
{
    const QJsonArray data = QJsonDocument::fromJson(json).object().value(QStringLiteral("data")).toArray();
    bool changed = false;
    for (const QJsonValue &v : data) {
        const QJsonObject e = v.toObject();
        const QString name = e.value(QStringLiteral("name")).toString();
        const QString id = e.value(QStringLiteral("id")).toString();
        if (name.isEmpty() || id.isEmpty()) continue;
        if (e.value(QStringLiteral("format")).toArray().contains(QStringLiteral("animated")))
            m_tpAnimated.insert(id, true);
        if (m_globalEmotes.value(name) != id) { m_globalEmotes.insert(name, id); changed = true; }
    }
    if (changed) emit emotesChanged();
}

void TwitchChatController::requestGlobalEmotes(const QString &clientId)
{
    QNetworkRequest req{QUrl(QStringLiteral("https://api.twitch.tv/helix/chat/emotes/global"))};
    req.setRawHeader("Client-Id", clientId.toUtf8());
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    QNetworkReply *reply = m_net->get(req);
    const quint64 generation = m_subFetchGeneration;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_subFetchGeneration) return;
        if (reply->error() != QNetworkReply::NoError) {
            if (m_host) m_host->log(m_id, QStringLiteral("warn"),
                QStringLiteral("Twitch-Standard-Emotes nicht abrufbar: %1").arg(reply->errorString()));
            return;
        }
        applyGlobalEmotes(reply->readAll());
        if (m_host) m_host->log(m_id, QStringLiteral("info"),
            QStringLiteral("Twitch-Standard-Emotes geladen: %1").arg(m_globalEmotes.size()));
    });
}

void TwitchChatController::fetchSubscriptionEmotes(const QString &emoteSetsTag)
{
    if (m_token.isEmpty()) return;                       // anonymer Chat hat keine eigenen Sets
    static const QRegularExpression validSet(QStringLiteral("^[A-Za-z0-9_-]+$"));
    QStringList setIds;
    const QStringList parts = emoteSetsTag.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &id : parts)
        if (validSet.match(id).hasMatch() && !setIds.contains(id)) setIds << id;
    // Abos unverändert und bereits geladen → nicht neu fragen.
    if (!setIds.isEmpty() && setIds == m_subSetIds) return;

    clearSubscriptionEmotes();
    m_subSetIds = setIds;
    if (!m_net) m_net = new QNetworkAccessManager(this);

    const quint64 generation = m_subFetchGeneration;
    QNetworkRequest req{QUrl(QStringLiteral("https://id.twitch.tv/oauth2/validate"))};
    req.setRawHeader("Authorization", QByteArray("OAuth ") + m_token.toUtf8());
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, setIds] {
        reply->deleteLater();
        if (generation != m_subFetchGeneration) return;
        const QJsonObject info = QJsonDocument::fromJson(reply->readAll()).object();
        const QString clientId = info.value(QStringLiteral("client_id")).toString();
        if (reply->error() != QNetworkReply::NoError || clientId.isEmpty()) {
            m_subSetIds.clear();                         // beim nächsten Verbinden erneut versuchen
            if (m_host) m_host->log(m_id, QStringLiteral("warn"),
                QStringLiteral("Abo-Emotes nicht abrufbar: Twitch hat den Token nicht bestätigt (%1).")
                    .arg(reply->errorString()));
            return;
        }
        requestGlobalEmotes(clientId);
        const QString userId = info.value(QStringLiteral("user_id")).toString();
        const bool canListOwn = !userId.isEmpty()
            && info.value(QStringLiteral("scopes")).toArray().contains(QStringLiteral("user:read:emotes"));
        setEmoteScopeMissing(!canListOwn);
        if (canListOwn) {
            requestUserEmotes(clientId, userId, QString());
        } else if (m_host) {
            m_host->log(m_id, QStringLiteral("warn"), QStringLiteral(
                "Dem Token fehlt das Recht user:read:emotes: Freigeschaltete TwitchHypeTrain-Emotes sind nicht abrufbar."));
        }
        for (qsizetype i = 0; i < setIds.size(); i += 25)   // Helix erlaubt 25 Sets je Anfrage
            requestEmoteSets(clientId, setIds.mid(i, 25));
    });
}

void TwitchChatController::requestEmoteSets(const QString &clientId, const QStringList &setIds)
{
    QUrl url(QStringLiteral("https://api.twitch.tv/helix/chat/emotes/set"));
    QUrlQuery query;
    for (const QString &id : setIds) query.addQueryItem(QStringLiteral("emote_set_id"), id);
    url.setQuery(query);
    QNetworkRequest req{url};
    req.setRawHeader("Client-Id", clientId.toUtf8());
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    QNetworkReply *reply = m_net->get(req);
    const quint64 generation = m_subFetchGeneration;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_subFetchGeneration) return;
        if (reply->error() != QNetworkReply::NoError) {
            m_subSetIds.clear();                         // beim nächsten Verbinden erneut versuchen
            if (m_host) m_host->log(m_id, QStringLiteral("warn"),
                QStringLiteral("Abo-Emotes nicht abrufbar: %1").arg(reply->errorString()));
            return;
        }
        applySubscriptionEmotes(reply->readAll());
        if (m_host) m_host->log(m_id, QStringLiteral("info"),
            QStringLiteral("Abo-Emotes geladen: %1").arg(m_subEmotes.size()));
    });
}

// ------------------------------------------------------- Drittanbieter-Kataloge (anonym, ohne Login)
// BTTV, FFZ und 7TV kennen Kanäle nur unter der Ziffern-Raum-ID – Nachschlagen mit dem
// Anmeldenamen gibt 404. Diese Raum-ID schickt Twitch in jeder Nachricht als room-id-Markierung
// mit; wir merken sie dauerhaft, damit schon beim nächsten Start vor der ersten Nachricht
// gefragt werden kann.
static const char *const kProviderNames[ProviderCount] = { "FFZ", "BTTV", "7TV" };

static void parse7tvEmotes(const QJsonArray &emotes, ThirdPartyCatalog &out)
{
    for (const QJsonValue &v : emotes) {
        const QJsonObject e = v.toObject();
        const QString name = e.value(QStringLiteral("name")).toString();   // Kanal-Alias, sonst Originalname
        const QString id = e.value(QStringLiteral("id")).toString();
        if (name.isEmpty() || id.isEmpty()) continue;
        const QJsonObject data = e.value(QStringLiteral("data")).toObject();
        ThirdPartyEmote t;
        t.key = QStringLiteral("7tv_") + id;
        t.animated = data.value(QStringLiteral("animated")).toBool(false);
        // Kennung am aktiven Emote (Bit 0) oder am Emote selbst (Bit 8)
        t.zeroWidth = (e.value(QStringLiteral("flags")).toInt() & 1)
                      || (data.value(QStringLiteral("flags")).toInt() & 256);
        out.insert(name, t);
    }
}

// Ältere BTTV-Überlagerungen tragen keine API-Kennung; ihre IDs sind fest:
// cvHazmat, cvMask, SoSnowy, IceCold, CandyCane, ReinDeer, SantaHat, TopHat
static bool isBttvZeroWidth(const QString &id)
{
    static const QSet<QString> ids = {
        QStringLiteral("5e76d338d6581c3724c0f0b2"), QStringLiteral("5e76d399d6581c3724c0f0b8"),
        QStringLiteral("567b5b520e984428652809b6"), QStringLiteral("5849c9a4f52be01a7ee5f79d"),
        QStringLiteral("567b5c080e984428652809ba"), QStringLiteral("567b5dc00e984428652809bd"),
        QStringLiteral("58487cc6f52be01a7ee5f205"), QStringLiteral("5849c9c8f52be01a7ee5f79e") };
    return ids.contains(id);
}

static void parseBttvEmotes(const QJsonArray &emotes, ThirdPartyCatalog &out)
{
    for (const QJsonValue &v : emotes) {
        const QJsonObject e = v.toObject();
        const QString name = e.value(QStringLiteral("code")).toString();
        const QString id = e.value(QStringLiteral("id")).toString();
        if (name.isEmpty() || id.isEmpty() || e.value(QStringLiteral("modifier")).toBool(false)) continue;
        ThirdPartyEmote t;
        t.key = QStringLiteral("bttv_") + id;
        t.animated = e.value(QStringLiteral("animated")).toBool(false);
        t.zeroWidth = isBttvZeroWidth(id);
        out.insert(name, t);
    }
}

static void parseFfzSet(const QJsonObject &set, ThirdPartyCatalog &out)
{
    const QJsonArray emotes = set.value(QStringLiteral("emoticons")).toArray();
    for (const QJsonValue &v : emotes) {
        const QJsonObject e = v.toObject();
        const QString name = e.value(QStringLiteral("name")).toString();
        const qint64 id = e.value(QStringLiteral("id")).toInteger();
        if (name.isEmpty() || id <= 0) continue;
        const bool modifier = e.value(QStringLiteral("modifier")).toBool(false);
        // Modifier mit Bit 0 („versteckt“) sind reine Effekt-Befehle ohne eigenes Bild.
        if (modifier && (e.value(QStringLiteral("modifier_flags")).toInt() & 1)) continue;
        ThirdPartyEmote t;
        t.key = QStringLiteral("ffz_") + QString::number(id);
        t.animated = e.value(QStringLiteral("animated")).isObject();
        t.zeroWidth = modifier;
        out.insert(name, t);
    }
}

ThirdPartyCatalog TwitchChatController::parseCatalog(EmoteProvider provider, const QByteArray &json, bool global)
{
    ThirdPartyCatalog out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    const QJsonObject root = doc.object();
    switch (provider) {
    case Provider7tv: {
        // Global: das Set selbst; Kanal: Nutzerobjekt mit dem aktiven Set darin
        const QJsonObject set = global ? root : root.value(QStringLiteral("emote_set")).toObject();
        parse7tvEmotes(set.value(QStringLiteral("emotes")).toArray(), out);
        break;
    }
    case ProviderBttv:
        if (doc.isArray()) {                                // Global: einfache Liste
            parseBttvEmotes(doc.array(), out);
        } else {                                            // Kanal: eigene + geteilte
            parseBttvEmotes(root.value(QStringLiteral("channelEmotes")).toArray(), out);
            parseBttvEmotes(root.value(QStringLiteral("sharedEmotes")).toArray(), out);
        }
        break;
    case ProviderFfz: {
        const QJsonObject sets = root.value(QStringLiteral("sets")).toObject();
        if (global) {                                       // nur die für alle freigeschalteten Sets
            const QJsonArray defaults = root.value(QStringLiteral("default_sets")).toArray();
            for (const QJsonValue &d : defaults)
                parseFfzSet(sets.value(QString::number(d.toInteger())).toObject(), out);
        } else {
            for (auto it = sets.constBegin(); it != sets.constEnd(); ++it)
                parseFfzSet(it.value().toObject(), out);
        }
        break;
    }
    case ProviderCount:
        break;
    }
    return out;
}

void TwitchChatController::fetchThirdPartyGlobal()
{
    const QUrl urls[ProviderCount] = {
        QUrl(QStringLiteral("https://api.frankerfacez.com/v1/set/global")),
        QUrl(QStringLiteral("https://api.betterttv.net/3/cached/emotes/global")),
        QUrl(QStringLiteral("https://7tv.io/v3/emote-sets/global")) };
    for (int p = 0; p < ProviderCount; ++p) {
        if (m_tpGlobalTried[p]) continue;
        m_tpGlobalTried[p] = true;
        requestCatalog(EmoteProvider(p), true, QString(), urls[p]);
    }
}

void TwitchChatController::fetchThirdPartyChannel(const QString &login, const QString &roomId)
{
    static const QRegularExpression num(QStringLiteral("^[0-9]{1,20}$"));
    if (login.isEmpty() || !num.match(roomId).hasMatch()) return;
    const QUrl urls[ProviderCount] = {
        QUrl(QStringLiteral("https://api.frankerfacez.com/v1/room/id/%1").arg(roomId)),
        QUrl(QStringLiteral("https://api.betterttv.net/3/cached/users/twitch/%1").arg(roomId)),
        QUrl(QStringLiteral("https://7tv.io/v3/users/twitch/%1").arg(roomId)) };
    for (int p = 0; p < ProviderCount; ++p) {
        if (m_tpChannelRoom[p] == roomId) continue;         // nach dieser Raum-ID wurde schon gefragt
        m_tpChannelRoom[p] = roomId;
        requestCatalog(EmoteProvider(p), false, login, urls[p]);
    }
}

void TwitchChatController::requestCatalog(EmoteProvider provider, bool global, const QString &login, const QUrl &url)
{
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QNetworkRequest req(url);
    req.setTransferTimeout(20000);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, provider, global, login] {
        const QNetworkReply::NetworkError err = reply->error();
        const QString why = reply->errorString();
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        if (!global && login != m_channel) return;          // Kanalwechsel während der Anfrage → unbrauchbar

        const QString label = QLatin1String(kProviderNames[provider])
                              + (global ? QStringLiteral(" global") : QStringLiteral(" Kanal ") + login);
        if (err == QNetworkReply::ContentNotFoundError) {
            // Viele Kanäle haben schlicht keinen Eintrag beim Anbieter – kein Fehler für den Nutzer
            if (m_host) m_host->log(m_id, QStringLiteral("info"), label + QStringLiteral(": keine Emotes."));
            return;
        }
        if (err != QNetworkReply::NoError) {
            // Beim nächsten Beitritt/Reconnect erneut versuchen
            if (global) m_tpGlobalTried[provider] = false;
            else m_tpChannelRoom[provider].clear();
            if (m_host) m_host->log(m_id, QStringLiteral("warn"), QStringLiteral(
                "%1: nicht abrufbar (%2) – die übrigen Anbieter bleiben unberührt.").arg(label, why));
            return;
        }
        const ThirdPartyCatalog catalog = parseCatalog(provider, data, global);
        if (m_host) m_host->log(m_id, QStringLiteral("info"), QStringLiteral(
            "%1: %2 Emotes geladen.").arg(label).arg(catalog.size()));
        setThirdPartyCatalog(provider, global, catalog);
    });
}

void TwitchChatController::setThirdPartyCatalog(EmoteProvider provider, bool global, const ThirdPartyCatalog &catalog)
{
    (global ? m_tpGlobal : m_tpChannel)[provider] = catalog;
    for (auto it = catalog.constBegin(); it != catalog.constEnd(); ++it)
        m_tpAnimated.insert(it->key, it->animated);
    rebuildThirdPartyLookup();
    rebuildAllMessages();                                   // im Verlauf stecken vielleicht Namen des neuen Sets
    emit emotesChanged();
}

void TwitchChatController::clearChannelCatalogs()
{
    for (int p = 0; p < ProviderCount; ++p) {
        m_tpChannel[p].clear();
        m_tpChannelRoom[p].clear();                         // Rückkehr zum Kanal → darf neu geholt werden
    }
    m_channelEmotes.clear();
    m_catalogChannel.clear();
    m_badgeChannel.clear();
    rebuildThirdPartyLookup();
}

void TwitchChatController::rebuildThirdPartyLookup()
{
    m_tpLookup.clear();
    for (const ThirdPartyCatalog &c : m_tpGlobal) m_tpLookup.insert(c);
    for (const ThirdPartyCatalog &c : m_tpChannel) m_tpLookup.insert(c);   // Kanal schlägt global
}
