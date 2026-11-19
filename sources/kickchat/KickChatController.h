#pragma once
// Kern des KickChat-Plugins:
//  • Kanal-Infos über kick.com/api/v2/channels/<slug> (Chatroom-ID, Kanal-/User-ID,
//    Abo-Abzeichen, Live-Status, Zuschauer) – alle 60 s aktualisiert.
//  • Live-Chat über Kicks Pusher-WebSocket (wss://ws-us2.pusher.com), anonym lesend:
//    Kanäle chatrooms.<id>.v2 und channel.<id>.
//  • Verlauf beim Verbinden über kick.com/api/v2/channels/<id>/messages.
//  • Emotes: Kick-Emotes stehen als „[emote:<id>:<name>]“ im Text; dazu 7TV (global +
//    Kanal über die Kick-User-ID). Nachrichten werden als Segment-Folge (Text/Link/Emote)
//    aufgebaut, damit animierte Emotes im Panel wirklich laufen.
//  • Senden über die offizielle Kick-API (KickAuth, eigene Kick-App nötig).

#include "MiniWebSocket.h"
#include "NotifySound.h"
#include "TextPredictor.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "PluginApi.h"

class KickAuth;
class SpellChecker;
class LanguageToolClient;
class KickEmoteCache;
class QNetworkAccessManager;
class TokenVault;

class KickChatController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY channelChanged)
    Q_PROPERTY(QString channelDisplay READ channelDisplay NOTIFY channelInfoChanged)
    Q_PROPERTY(bool autoChannel READ autoChannel WRITE setAutoChannel NOTIFY settingsChanged)
    Q_PROPERTY(bool autoScroll READ autoScroll WRITE setAutoScroll NOTIFY settingsChanged)
    Q_PROPERTY(bool showTimestamps READ showTimestamps WRITE setShowTimestamps NOTIFY settingsChanged)
    Q_PROPERTY(int chatFontSize READ chatFontSize WRITE setChatFontSize NOTIFY settingsChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY statusChanged)
    Q_PROPERTY(QString statusKey READ statusKey NOTIFY statusChanged)     // idle|connecting|connected|closed|error|notfound
    Q_PROPERTY(QString statusDetail READ statusDetail NOTIFY statusChanged)
    Q_PROPERTY(int viewerCount READ viewerCount NOTIFY channelInfoChanged)  // -1 = unbekannt
    Q_PROPERTY(bool streamLive READ streamLive NOTIFY channelInfoChanged)
    Q_PROPERTY(QString streamTitle READ streamTitle NOTIFY channelInfoChanged)
    Q_PROPERTY(QString chatModes READ chatModes NOTIFY channelInfoChanged)  // z. B. "slow:5,followers:10,subscribers,emotes"
    // Leute im Chat: alle, die seit dem Verbinden (inkl. Verlauf) geschrieben haben –
    // Kick gibt keine öffentliche Namensliste aller Zuschauer heraus.
    Q_PROPERTY(int chatterCount READ chatterCount NOTIFY chattersChanged)
    Q_PROPERTY(QVariantMap pinnedMessage READ pinnedMessage NOTIFY pinnedChanged)
    Q_PROPERTY(bool pinnedHidden READ pinnedHidden NOTIFY pinnedChanged)
    // Anmeldung (Senden)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY authChanged)
    Q_PROPERTY(QString user READ user NOTIFY authChanged)
    Q_PROPERTY(bool hasAppCredentials READ hasAppCredentials NOTIFY authChanged)
    Q_PROPERTY(QString appClientId READ appClientId NOTIFY authChanged)
    Q_PROPERTY(bool usingOwnApp READ usingOwnApp NOTIFY authChanged)
    Q_PROPERTY(QString redirectUri READ redirectUri CONSTANT)
    Q_PROPERTY(QString loginState READ loginState NOTIFY loginStateChanged)   // idle|waiting|error
    Q_PROPERTY(QString loginError READ loginError NOTIFY loginStateChanged)
    Q_PROPERTY(QString loginUrl READ loginUrl NOTIFY loginStateChanged)
    // Benachrichtigungen: Ton bei Erwähnung/Antwort an mich, Farben der Boxen je Art
    // (mention|system|follow|kicks|bot|mod), Event-Bots [{name,label,color}]
    Q_PROPERTY(bool notifySound READ notifySound WRITE setNotifySound NOTIFY notifyChanged)
    Q_PROPERTY(int notifyVolume READ notifyVolume WRITE setNotifyVolume NOTIFY notifyChanged)
    Q_PROPERTY(QVariantMap boxColors READ boxColors NOTIFY boxSettingsChanged)
    Q_PROPERTY(QVariantList eventBots READ eventBots NOTIFY boxSettingsChanged)
    Q_PROPERTY(bool showFollows READ showFollows WRITE setShowFollows NOTIFY boxSettingsChanged)
    // Eigene Rolle im Kanal: anonymous | user | vip | moderator | broadcaster
    Q_PROPERTY(QString myRole READ myRole NOTIFY roleChanged)
    Q_PROPERTY(QStringList blocked READ blocked NOTIFY blockedChanged)
    // Laufende Umfrage: {title, options:[{id,label,votes,percent,leading}], total, endsAt, ended}; leer = keine
    Q_PROPERTY(QVariantMap poll READ poll NOTIFY pollChanged)
    Q_PROPERTY(bool pollHidden READ pollHidden NOTIFY pollChanged)
    // Rechtschreibprüfung (Hunspell, lokal) und Wortvorschläge
    Q_PROPERTY(bool spellGerman READ spellGerman WRITE setSpellGerman NOTIFY spellSettingsChanged)
    Q_PROPERTY(bool spellEnglish READ spellEnglish WRITE setSpellEnglish NOTIFY spellSettingsChanged)
    Q_PROPERTY(bool predictText READ predictText WRITE setPredictText NOTIFY spellSettingsChanged)
    // Satzbau: lokale Regeln; online zusätzlich LanguageTool (schickt den Text an api.languagetool.org)
    Q_PROPERTY(bool grammarCheck READ grammarCheck WRITE setGrammarCheck NOTIFY spellSettingsChanged)
    Q_PROPERTY(bool grammarOnline READ grammarOnline WRITE setGrammarOnline NOTIFY spellSettingsChanged)

public:
    KickChatController(uve::IPlayerHost *host, const QString &pluginId, const QString &dataDir,
                       QObject *parent = nullptr);
    ~KickChatController() override;

    QString channel() const { return m_channel; }
    void setChannel(const QString &channel);
    QString channelDisplay() const { return m_channelDisplay.isEmpty() ? m_channel : m_channelDisplay; }
    bool autoChannel() const { return m_autoChannel; }
    void setAutoChannel(bool on);
    bool autoScroll() const { return m_autoScroll; }
    void setAutoScroll(bool on);
    bool showTimestamps() const { return m_showTimestamps; }
    void setShowTimestamps(bool on);
    int chatFontSize() const { return m_chatFontSize; }
    void setChatFontSize(int px);
    bool connected() const { return m_subscribed; }
    QString statusKey() const { return m_statusKey; }
    QString statusDetail() const { return m_statusDetail; }
    int viewerCount() const { return m_viewerCount; }
    bool streamLive() const { return m_live; }
    QString streamTitle() const { return m_streamTitle; }
    QString chatModes() const { return m_chatModes; }
    QVariantMap pinnedMessage() const { return m_pinned; }
    bool pinnedHidden() const;

    bool authenticated() const;
    QString user() const;
    bool hasAppCredentials() const;
    QString appClientId() const;
    bool usingOwnApp() const;
    QString redirectUri() const;
    QString loginState() const;
    QString loginError() const;
    QString loginUrl() const;

    void applyVault(TokenVault *vault);

    // ——— Benachrichtigungen
    void setNotificationSound(const QString &file) { m_sound.setFile(file); }
    bool notifySound() const { return m_notifySound; }
    void setNotifySound(bool on);
    int notifyVolume() const { return m_sound.volumePercent(); }
    void setNotifyVolume(int percent);
    Q_INVOKABLE void previewNotifySound();
    QVariantMap boxColors() const;
    Q_INVOKABLE QVariantMap defaultBoxColors() const;
    Q_INVOKABLE void setBoxColor(const QString &kind, const QString &color);
    Q_INVOKABLE void resetBoxColors();
    QVariantList eventBots() const;
    Q_INVOKABLE bool setEventBot(const QString &name, const QString &label, const QString &color,
                                 const QString &oldName = QString());
    Q_INVOKABLE void removeEventBot(const QString &name);
    Q_INVOKABLE void resetEventBots();
    bool showFollows() const { return m_showFollows; }
    void setShowFollows(bool on);
    int notificationPlays() const { return m_sound.playCount(); }

    // ——— Nutzer-Menü und Moderation
    QString myRole() const { return m_myRole; }
    QStringList blocked() const { return m_blocked; }
    Q_INVOKABLE bool isBlocked(const QString &name) const;
    // Konto-Infos über kick.com/api/v2/channels/<kanal>/users/<name> → userInfo(name, map)
    Q_INVOKABLE void requestUserInfo(const QString &name);
    // action: delete|timeout|ban|unban|block|unblock; target: {name,userId,messageId,duration(min),reason}
    Q_INVOKABLE void userAction(const QString &action, const QVariantMap &target);

    // ——— Umfrage
    QVariantMap poll() const { return m_poll; }
    bool pollHidden() const { return !m_poll.isEmpty() && m_poll.value(QStringLiteral("title")).toString() == m_hiddenPoll; }
    Q_INVOKABLE void hidePoll();
    Q_INVOKABLE void showPoll();
    void applyPollJson(const QJsonObject &data);   // auch für Tests

    // ——— Rechtschreibung und Wortvorschläge
    void setSpellDictionaryDir(const QString &dir);
    bool spellGerman() const { return m_spellGerman; }
    void setSpellGerman(bool on);
    bool spellEnglish() const { return m_spellEnglish; }
    void setSpellEnglish(bool on);
    bool predictText() const { return m_predictText; }
    void setPredictText(bool on);
    bool grammarCheck() const { return m_grammarCheck; }
    void setGrammarCheck(bool on);
    bool grammarOnline() const { return m_grammarOnline; }
    void setGrammarOnline(bool on);
    // Antwort kommt als spellResult(text, [{start,length,word}])
    Q_INVOKABLE void spellCheck(const QString &text, int cursor);
    Q_INVOKABLE void requestSpellSuggestions(const QString &word);   // → spellSuggestions
    Q_INVOKABLE void addSpellWord(const QString &word);              // eigenes Wörterbuch, dauerhaft
    Q_INVOKABLE QStringList predictWords(const QString &text, int cursor);
    Q_INVOKABLE int predictionStart(const QString &text, int cursor) const;

    Q_INVOKABLE void openChat();
    Q_INVOKABLE void closeChat();
    Q_INVOKABLE void reconnect();
    // Nachricht senden; replyToId = Kick-Nachrichten-ID für eine Antwort (optional).
    Q_INVOKABLE bool send(const QString &text, const QString &replyToId = QString());

    Q_INVOKABLE QVariantList history() const;
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE QVariantMap messageById(const QString &id) const;

    // Emote-Browser: kind = all|channel|global|emoji|7tv|favorites; filter = Suchtext
    // → [{name,key,src,anim,provider,group,favorite,subOnly}]
    Q_INVOKABLE QVariantList emoteList(const QString &kind, const QString &filter = QString());
    Q_INVOKABLE void toggleEmoteFavorite(const QString &name);
    // Eingabe so, wie sie im Chat erscheinen würde (Emote-Namen als Bild)
    Q_INVOKABLE QVariantList previewSegments(const QString &text);
    // Emote-Namen im Eingabefeld: [{start,length,src,anim}] (nur mit fertigem Bild; fehlende werden geladen)
    Q_INVOKABLE QVariantList inputEmotes(const QString &text);
    // Breite/Höhe eines Bildes (aus dem Dateikopf, gemerkt); 1, wenn unbekannt
    Q_INVOKABLE qreal imageAspect(const QString &source);
    // Eingabefeld: Emote-Namen unsichtbar und genau auf Bildbreite bringen; ranges = [{start,length,width}]
    Q_INVOKABLE void setInputEmotes(QObject *textEdit, const QVariantList &ranges);
    // @-Erwähnungen: zuletzt aktive Namen, gefiltert
    Q_INVOKABLE QStringList mentionCandidates(const QString &prefix) const;
    // [{name,user,color,role,roleLabel,count,lastTs,self}] – Streamer, Mods, VIPs, dann zuletzt aktiv
    Q_INVOKABLE QVariantList chatters(const QString &filter = QString()) const;
    int chatterCount() const;

    Q_INVOKABLE void hidePinned();
    Q_INVOKABLE void showPinned();

    Q_INVOKABLE void setAppCredentials(const QString &clientId, const QString &clientSecret);
    Q_INVOKABLE void startLogin();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void openInBrowser(const QString &url);
    // Meldet jeden Mausdruck im Fenster als windowPressed – auch außerhalb des Panels –,
    // damit das Panel den Tastaturfokus beim Wegklicken wieder abgibt.
    Q_INVOKABLE void watchWindowPresses(QObject *window);
    bool eventFilter(QObject *watched, QEvent *event) override;

    // Medien → Kanal
    static QString guessChannel(const QString &text);
    void startFollowing();
    void onMediaEvent(const QString &name, const QVariantMap &data);
    void onMediaPath(const QString &path);

    // Für Tests direkt aufrufbar
    void applyChannelJson(const QByteArray &json);
    void applyEmotesJson(const QByteArray &json);
    void applyHistoryJson(const QByteArray &json);
    void handlePusherFrame(const QString &text);
    QVariantMap buildChatMessage(const QJsonObject &msg);
    QVariantList buildSegments(const QString &content);
    qint64 chatroomId() const { return m_chatroomId; }
    qint64 channelId() const { return m_channelId; }
    qint64 channelUserId() const { return m_channelUserId; }
    int emoteCount() const { return int(m_emotes.size()); }

signals:
    void newMessage(const QVariantMap &message);
    void messageUpdated(const QString &id, const QVariantMap &message);
    void historyCleared();
    void channelChanged();
    void channelInfoChanged();
    void settingsChanged();
    void statusChanged();
    void pinnedChanged();
    void emotesChanged();
    void emoteImageReady();
    void authChanged();
    void loginStateChanged();
    void sendResult(bool ok, const QString &error);
    void chattersChanged();
    void notifyChanged();
    void boxSettingsChanged();
    void roleChanged();
    void blockedChanged();
    void pollChanged();
    void userInfo(const QString &name, const QVariantMap &info);
    void userActionDone(const QString &action, bool ok, const QString &error);
    void notified();
    void windowPressed(QObject *window, qreal x, qreal y);
    void spellSettingsChanged();
    void spellResult(const QString &text, const QVariantList &ranges);
    void spellSuggestions(const QString &word, const QStringList &suggestions);
    void predictionsChanged();
    // [{start,length,message,messageEn,replacements,source}]
    void grammarResult(const QString &text, const QVariantList &issues);                       // Ton gespielt (Erwähnung/Antwort)

private:
    struct Emote {
        QString name;
        QString key;          // kick_<id> | 7tv_<id>
        QString group;        // channel | global | emoji | 7tv
        bool subOnly = false;
        bool zeroWidth = false;
    };

    void fetchChannel();
    void fetchEmotes();
    void fetchHistory();
    void fetch7tv();
    void connectSocket();
    void subscribe();
    void scheduleReconnect();
    void setStatus(const QString &key, const QString &detail = QString());
    void persist();
    void loadSettings();
    bool tryFollow(const QString &text, bool notify);
    void appendMessage(const QVariantMap &message);
    void appendSystem(const QString &kind, const QString &text, const QString &textEn = QString());
    void onEmoteReady(const QString &key);
    void handleEvent(const QString &event, const QJsonObject &data);
    void setPinned(const QJsonObject &data);
    QVariantList buildBadges(const QJsonObject &identity, const QJsonArray &badgesV2);
    QString emoteSrc(const QString &key);
    QString resolveEmoteNames(const QString &text) const;
    void addEmote(const Emote &e);
    QNetworkAccessManager *nam();

    uve::IPlayerHost *m_host;
    QString m_id;
    QString m_dataDir;
    KickEmoteCache *m_cache;
    KickAuth *m_auth;
    QNetworkAccessManager *m_nam = nullptr;
    MiniWebSocket m_ws;
    QTimer m_reconnectTimer;
    QTimer m_infoTimer;
    QTimer m_pusherPing;
    int m_backoffMs = 1000;
    int m_keyIndex = 0;          // Pusher-Schlüssel (siehe kPusherKeys)
    bool m_wantOpen = false;
    bool m_subscribed = false;
    quint64 m_generation = 0;     // verwirft Antworten zu einem alten Kanal

    QString m_channel;            // slug, klein
    QString m_channelDisplay;
    bool m_autoChannel = true;
    bool m_autoScroll = true;
    bool m_showTimestamps = false;
    int m_chatFontSize = 13;

    qint64 m_chatroomId = 0;
    qint64 m_channelId = 0;
    qint64 m_channelUserId = 0;
    int m_viewerCount = -1;
    bool m_live = false;
    QString m_streamTitle;
    QString m_chatModes;
    QList<QPair<int, QString>> m_subBadges;   // Monate → Bild-URL (aufsteigend)

    QString m_statusKey = QStringLiteral("idle");
    QString m_statusDetail;

    QList<QVariantMap> m_history;
    QSet<QString> m_seenIds;
    QHash<QString, QSet<QString>> m_waitingKeys;   // Bild-Schlüssel → Nachrichten-IDs
    QStringList m_recentChatters;                  // zuletzt aktiv zuerst
    struct Chatter { QString name, user, color, role; int count = 0; qint64 lastTs = 0; };
    QHash<QString, Chatter> m_chatters;            // Name klein → Chatter
    QTimer m_chattersTimer;                        // bündelt chattersChanged

    QHash<QString, Emote> m_emotes;                // Name → Emote (Kick vor 7TV)
    QHash<QString, Emote> m_emotesById;            // kick_<id> → Emote (auch fremde Kanäle)
    QStringList m_favorites;
    QHash<QString, qreal> m_imageAspects;

    QVariantMap m_pinned;
    QString m_hiddenPin;
    qint64 m_lastSendMs = 0;

    struct EventBot { QString name, label, color; };
    void loadBoxSettings(const QJsonObject &s);
    void persistBoxSettings();
    QString eventBotLabel(const QString &name) const;   // null = kein Bot
    void fetchMyRole();
    void setMyRole(const QString &role);
    NotifySound m_sound;
    bool m_notifySound = true;
    bool m_showFollows = true;
    QHash<QString, QString> m_boxColors;
    QList<EventBot> m_eventBots;
    QString m_myRole = QStringLiteral("anonymous");
    QStringList m_blocked;
    QVariantMap m_poll;
    QString m_hiddenPoll;
    QTimer m_pollTimer;
    bool m_quiet = false;

    bool isKnownEmote(const QString &word) const { return m_emotes.contains(word); }
    QStringList emoteNamesWithPrefix(const QString &prefix) const;
    void ensureFrequencyWords();
    void learnOwnMessage(const QString &text);
    SpellChecker *m_spell = nullptr;
    TextPredictor m_predictor;
    QString m_predictFile;
    bool m_spellGerman = true;
    bool m_spellEnglish = true;
    bool m_predictText = true;
    bool m_frequencyRequested = false;
    QSet<QString> m_spellWords;
    void emitGrammarResult();
    LanguageToolClient *m_languageTool = nullptr;
    bool m_grammarCheck = true;
    bool m_grammarOnline = true;
    bool m_grammarOnlineChosen = false;
    bool m_languageToolFailureLogged = false;
    QString m_grammarText;
    QVariantList m_localIssues;
    QVariantList m_remoteIssues;            // Verlauf: kein Ton
};
