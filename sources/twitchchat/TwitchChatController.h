#pragma once
// Kern des TwitchChat-Plugins: nativer IRC-Chat (IRCv3-Tags), Kanalverwaltung, Auto-Folge,
// Reconnect mit Backoff, Emote-Katalog (im Chat beobachtete + volle Twitch-Listen per
// Token-Abfrage + BTTV-/FFZ-/7TV-Global- und Kanal-Listen anonym per Ziffern-Room-ID), Favoriten,
// Auto-Scroll und Einstellungs-Persistenz – sprachneutral, damit der Panel übersetzen kann.
// Nachrichten werden als Segment-Folge (Text/Emote-Bild) aufgebaut, damit animierte
// Emotes (WebP/GIF) im Panel wirklich laufen – statt HTML-Richtext mit Standbildern.

#include "MiniWebSocket.h"
#include "NotifySound.h"
#include "TextPredictor.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <functional>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "PluginApi.h"

class EmoteCache;
class LanguageToolClient;
class WhisperService;
class TwitchAuthService;
class QTimer;
class SpellChecker;
class TokenVault;
class QNetworkAccessManager;
class QUrlQuery;

// Reihenfolge = Vorrang bei gleichem Namen: spätere Anbieter gewinnen
// (innerhalb derselben Ebene; ein Kanal-Emote schlägt immer ein globales).
enum EmoteProvider { ProviderFfz, ProviderBttv, Provider7tv, ProviderCount };

struct ThirdPartyEmote
{
    QString key;             // „bttv_<id>“ | „ffz_<id>“ | „7tv_<id>“
    bool animated = false;   // laut Katalog des Anbieters
    bool zeroWidth = false;  // wird über das Emote davor gelegt
};
using ThirdPartyCatalog = QHash<QString, ThirdPartyEmote>;   // Emote-Name → Emote

class TwitchChatController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString channel   READ channel   WRITE setChannel   NOTIFY channelChanged)
    Q_PROPERTY(bool autoChannel  READ autoChannel WRITE setAutoChannel NOTIFY settingsChanged)
    Q_PROPERTY(bool autoScroll   READ autoScroll  WRITE setAutoScroll  NOTIFY settingsChanged)
    Q_PROPERTY(bool connected    READ connected  NOTIFY statusChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY settingsChanged)
    Q_PROPERTY(QObject *browser READ browserObj CONSTANT)   // Kanal-Browser-Controller fürs QML-Panel
    Q_PROPERTY(QString user READ user NOTIFY settingsChanged)
    Q_PROPERTY(QString statusKey READ statusKey  NOTIFY statusChanged)
    Q_PROPERTY(QString statusDetail READ statusDetail NOTIFY statusChanged)
    Q_PROPERTY(bool spellGerman  READ spellGerman  WRITE setSpellGerman  NOTIFY spellSettingsChanged)
    Q_PROPERTY(bool spellEnglish READ spellEnglish WRITE setSpellEnglish NOTIFY spellSettingsChanged)
    Q_PROPERTY(bool grammarCheck READ grammarCheck WRITE setGrammarCheck NOTIFY spellSettingsChanged)
    Q_PROPERTY(bool grammarOnline READ grammarOnline WRITE setGrammarOnline NOTIFY spellSettingsChanged)
    Q_PROPERTY(bool predictText READ predictText WRITE setPredictText NOTIFY spellSettingsChanged)
    // Angeheftete Nachricht des Kanals: {id,text,sender,color,pinnedBy,endsAt,segments}, leer = keine
    Q_PROPERTY(QVariantMap pinnedMessage READ pinnedMessage NOTIFY pinnedChanged)
    Q_PROPERTY(bool pinnedHidden READ pinnedHidden NOTIFY pinnedChanged)
    Q_PROPERTY(bool pinnedCollapsed READ pinnedCollapsed WRITE setPinnedCollapsed NOTIFY pinnedChanged)
    // Laufende Umfrage / Vorhersage des Kanals (gql, ohne Login), leer = keine.
    // Umfrage:   {id,title,status,ended,endsAt,total,choices:[{id,title,votes,percent,leading}]}
    // Vorhersage:{id,title,status,endsAt,totalPoints,totalUsers,outcomes:[{id,title,color,points,users,percent,winner}]}
    Q_PROPERTY(QVariantMap channelPoll READ channelPoll NOTIFY channelEventsChanged)
    Q_PROPERTY(QVariantMap channelPrediction READ channelPrediction NOTIFY channelEventsChanged)
    Q_PROPERTY(bool pollHidden READ pollHidden NOTIFY channelEventsChanged)
    Q_PROPERTY(bool predictionHidden READ predictionHidden NOTIFY channelEventsChanged)
    Q_PROPERTY(bool pollCollapsed READ pollCollapsed WRITE setPollCollapsed NOTIFY channelEventsChanged)
    Q_PROPERTY(bool predictionCollapsed READ predictionCollapsed WRITE setPredictionCollapsed NOTIFY channelEventsChanged)
    // Chat-Schriftgröße in Pixeln, 0 = Standardschrift des Players
    Q_PROPERTY(int chatFontSize READ chatFontSize WRITE setChatFontSize NOTIFY chatFontSizeChanged)
    Q_PROPERTY(bool notifySound READ notifySound WRITE setNotifySound NOTIFY notifySoundChanged)   // Ton bei privater Nachricht
    Q_PROPERTY(int notifyVolume READ notifyVolume WRITE setNotifyVolume NOTIFY notifySoundChanged)  // 0–100 %
    // Benachrichtigungs-Boxen: Farbe je Art (mention/system/follow/bits/bot/twitch/automod; leer = Standard)
    Q_PROPERTY(QVariantMap boxColors READ boxColors NOTIFY boxSettingsChanged)
    // Event-Bots: [{login, label, color}] – Nachrichten dieser Konten stehen in der Bot-Box
    Q_PROPERTY(QVariantList eventBots READ eventBots NOTIFY boxSettingsChanged)
    // Private Nachrichten (Postfach) und Leute im Chat
    Q_PROPERTY(QObject *whispers READ whispersObj CONSTANT)
    Q_PROPERTY(int chatterCount READ chatterCount NOTIFY chattersChanged)
    Q_PROPERTY(bool chattersComplete READ chattersComplete NOTIFY chattersChanged)   // volle Helix-Liste (Moderator)
    Q_PROPERTY(int badgeRevision READ badgeRevision NOTIFY badgeImagesChanged)       // steigt, wenn neue Abzeichen-Bilder da sind
    // Zuschauer des Streams (öffentlich, ohne Login): -1 = unbekannt, streamLive = Kanal sendet gerade
    Q_PROPERTY(int viewerCount READ viewerCount NOTIFY viewersChanged)
    Q_PROPERTY(bool streamLive READ streamLive NOTIFY viewersChanged)
    // Eigene Rolle im Kanal: anonymous | user | vip | moderator | broadcaster (bestimmt das Nutzer-Menü)
    Q_PROPERTY(QString myRole READ myRole NOTIFY roleChanged)
    // „Mit Twitch anmelden“ (Device-Code): idle | waiting | error; Code und Link für die Anzeige
    Q_PROPERTY(QString loginState READ loginState NOTIFY loginStateChanged)
    Q_PROPERTY(QString loginCode READ loginCode NOTIFY loginStateChanged)
    Q_PROPERTY(QString loginUrl READ loginUrl NOTIFY loginStateChanged)
    Q_PROPERTY(QString loginError READ loginError NOTIFY loginStateChanged)
    Q_PROPERTY(bool autoRenew READ autoRenew NOTIFY loginStateChanged)   // Token erneuert sich selbst
    Q_PROPERTY(int blockedRevision READ blockedRevision NOTIFY blockedChanged)   // ändert sich mit jeder Blockierung
    // Live-Kanäle (Fenster neben der Zuschauerliste): [{login,name,title,game,viewers,thumb,avatar}]
    Q_PROPERTY(QVariantList liveStreams READ liveStreams NOTIFY liveStreamsChanged)
    Q_PROPERTY(QString liveTab READ liveTab NOTIFY liveStreamsChanged)        // follow | top | categories | category | search
    Q_PROPERTY(QVariantList liveCategories READ liveCategories NOTIFY liveStreamsChanged)   // [{name,title,viewers,boxArt}]
    Q_PROPERTY(QString liveCategory READ liveCategory NOTIFY liveStreamsChanged)           // gewählte Kategorie (Tab category)
    Q_PROPERTY(bool liveBusy READ liveBusy NOTIFY liveStreamsChanged)
    Q_PROPERTY(QString liveError READ liveError NOTIFY liveStreamsChanged)
    Q_PROPERTY(bool canListFollowed READ canListFollowed NOTIFY liveStreamsChanged)

public:
    explicit TwitchChatController(uve::IPlayerHost *host, const QString &pluginId, QObject *parent = nullptr);

    QString channel() const { return m_channel; }
    void setChannel(const QString &c);
    bool autoChannel() const { return m_autoChannel; }
    void setAutoChannel(bool on);
    bool autoScroll() const { return m_autoScroll; }      // Chat folgt neuen Nachrichten nach unten
    void setAutoScroll(bool on);
    int chatFontSize() const { return m_chatFontSize; }
    QObject *whispersObj() const;
    WhisperService *whisperService() const { return m_whispers; }
    int chatterCount() const { return int(m_chatters.size()) + (selfListed() ? 1 : 0); }
    int badgeRevision() const { return m_badgeRevision; }
    int viewerCount() const { return m_viewerCount; }
    bool streamLive() const { return m_streamLive; }
    void fetchViewers();

    // Nutzer-Menü: Aktionen über Helix. action = timeout|purge|ban|unban|delete|warn|shoutout|
    // mod|unmod|vip|unvip|raid|block|unblock; target = {login,name,userId,msgId,duration,reason}
    QString myRole() const;
    QString loginState() const { return m_loginState; }
    QString loginCode() const { return m_loginCode; }
    QString loginUrl() const { return m_loginUrl; }
    QString loginError() const { return m_loginError; }
    bool autoRenew() const { return !m_refreshToken.isEmpty(); }
    qint64 tokenExpiresAt() const { return m_tokenExpiresAt; }
    Q_INVOKABLE void startLogin();
    Q_INVOKABLE void cancelLogin();
    // Abschluss der Anmeldung (auch für Tests): Konto, Token, Erneuerungs-Schlüssel, Ablauf
    void completeLogin(const QString &login, const QString &accessToken, const QString &refreshToken, qint64 expiresAtMs);
    QString myUserId() const;
    Q_INVOKABLE void userAction(const QString &action, const QVariantMap &target);
    Q_INVOKABLE void requestUserInfo(const QString &userId, const QString &login);
    Q_INVOKABLE bool isBlocked(const QString &login) const;
    // AutoMod (nur Moderatoren/Streamer): zurückgehaltene Nachricht freigeben (allow) oder ablehnen
    Q_INVOKABLE void automodAction(const QString &automodId, bool allow);
    void handleAutomodEvent(const QString &type, const QJsonObject &event);   // auch für Tests
    void handleFollowEvent(const QJsonObject &event);                         // auch für Tests
    int blockedRevision() const { return m_blockedRevision; }
    using HelixHook = std::function<void(const QString &method, const QUrl &url, const QByteArray &body,
                                         const std::function<void(int, const QByteArray &)> &done)>;
    void setHelixHook(const HelixHook &hook) { m_helixHook = hook; }      // Tests
    void setTestIdentity(const QString &login, const QString &userId);   // Tests
    void applyViewersJson(const QByteArray &json);   // auch für die Tests

    // Live-Kanäle: „Gefolgt“ (Helix, Login mit user:read:follows), „Beliebt“ und „Suche“ (gql, ohne Login).
    // Ein Klick spielt den Kanal über den Player (player.load) und der Chat wechselt dorthin.
    QVariantList liveStreams() const { return m_liveStreams; }
    QString liveTab() const { return m_liveTab; }
    bool liveBusy() const { return m_liveBusy; }
    QString liveError() const { return m_liveError; }
    bool canListFollowed() const;
    // language: Twitch-Sprachcode ("de", "zh_hk" …), leer = alle – Beliebt/Kategorie filtert Twitch, Gefolgt/Suche das Plugin.
    // sort: "relevance" = „Für dich empfohlen“ (eigene Empfehlung: gefolgte Kanäle, im Fenster geschaute Kanäle und
    // Kategorien, dann Zuschauer – Twitch selbst liefert ohne Website-Login nur die Zuschauer-Reihenfolge) oder
    // "viewers" = Zuschauer viel → wenig. Gilt für alle Reiter.
    Q_INVOKABLE void loadLiveStreams(const QString &tab, const QString &query = QString(), const QString &language = QString(),
                                     const QString &sort = QString());
    // Spielt den Kanal und merkt sich Kanal und Kategorie für „Für dich empfohlen“
    Q_INVOKABLE bool playLiveChannel(const QString &login, const QString &game = QString());
    Q_INVOKABLE void noteLiveCategory(const QString &name);   // Klick auf eine Kategorie merken
    Q_INVOKABLE bool hasLiveProfile() const;     // gibt es schon etwas, woraus Empfehlungen entstehen?
    // Reihenfolge (auch für Tests): followed/history fließen nur bei "relevance" ein; language filtert
    static QVariantList rankLiveStreams(const QVariantList &rows, const QString &sort, const QString &language,
                                        const QHash<QString, int> &channelHistory, const QHash<QString, int> &gameHistory,
                                        const QSet<QString> &followed, const QSet<QString> &followedGames);
    static QVariantList rankLiveCategories(const QVariantList &categories, const QString &sort,
                                           const QHash<QString, int> &gameHistory, const QSet<QString> &followedGames,
                                           const QHash<QString, int> &categoryClicks = {});
    // Zuletzt gewählte Größe des Fensters „Live auf Twitch“ ({width,height}, leer = Vorgabe aus dem Manifest)
    Q_INVOKABLE QVariantMap liveWindowSize() const;
    Q_INVOKABLE void setLiveWindowSize(int width, int height);
    // Sprachfilter für „Beliebt“ und Kategorie-Kanäle (Twitch-Code wie "de", "zh_hk"; leer = alle Sprachen), gespeichert
    Q_INVOKABLE QString liveLanguage() const;
    Q_INVOKABLE void setLiveLanguage(const QString &code);
    Q_INVOKABLE QString liveSort() const;                 // gespeicherte Sortierung, Vorgabe "relevance"
    Q_INVOKABLE void setLiveSort(const QString &sort);
    // Parser (auch für die Tests): gql streams/searchStreams bzw. Helix streams/followed
    static QVariantList liveRowsFromGql(const QByteArray &json);
    static QVariantList liveRowsFromHelix(const QByteArray &json);
    static QVariantList liveCategoriesFromGql(const QByteArray &json);   // games bzw. searchFor.games → [{name,title,viewers,boxArt}]
    QVariantList liveCategories() const { return m_liveCategories; }
    QString liveCategory() const { return m_liveCategory; }
    Q_INVOKABLE void applyLiveResult(const QString &tab, const QVariantList &rows, const QString &error = QString(),
                                     const QVariantList &categories = QVariantList(), const QString &category = QString());
    bool chattersComplete() const { return m_chattersComplete; }
    // [{login,name,color,badges,wrote,self}], man selbst ganz oben, dann Streamer/Mods/VIPs;
    // filter vergleicht Login und Anzeigename
    Q_INVOKABLE QVariantList chatters(const QString &filter = QString()) const;
    // Abzeichen als Bild: tag = „set/version“ wie im IRC-Tag „badges“. Liefert die Datei-URL oder leer,
    // solange das Bild fehlt (startet dann den Download; badgeImagesChanged meldet die Ankunft).
    Q_INVOKABLE QString badgeImage(const QString &tag);
    void fetchBadges();
    void applyBadgesJson(const QByteArray &json, bool channel);   // auch für die Tests
    void setChatFontSize(int pixels);
    bool connected() const { return m_connected; }
    bool authenticated() const { return !m_token.isEmpty(); }
    QString user() const { return m_user; }
    QString statusKey() const { return m_statusKey; }     // idle|connecting|connected|closed|error
    QString statusDetail() const { return m_statusDetail; }

    Q_INVOKABLE void openChat();
    Q_INVOKABLE void closeChat();
    Q_INVOKABLE bool send(const QString &text);

    // Rechtschreibprüfung fürs Eingabefeld: Antworten kommen als spellResult/spellSuggestions.
    bool spellGerman() const { return m_spellGerman; }
    void setSpellGerman(bool on);
    bool spellEnglish() const { return m_spellEnglish; }
    void setSpellEnglish(bool on);
    bool grammarCheck() const { return m_grammarCheck; }
    void setGrammarCheck(bool on);
    bool grammarOnline() const { return m_grammarOnline; }    // schickt Text an LanguageTool
    void setGrammarOnline(bool on);
    bool predictText() const { return m_predictText; }
    void setPredictText(bool on);
    void setSpellDictionaryDir(const QString &dir);
    void setNotificationSound(const QString &file) { m_sound.setFile(file); }   // Ton bei privater Nachricht
    bool notifySound() const { return m_notifySound; }
    void setNotifySound(bool on);
    int notifyVolume() const { return m_sound.volumePercent(); }
    void setNotifyVolume(int percent);
    Q_INVOKABLE void previewNotifySound();     // Hörprobe beim Einstellen, auch wenn der Ton aus ist
    QVariantMap boxColors() const;
    QVariantList eventBots() const;
    Q_INVOKABLE QVariantMap defaultBoxColors() const;
    Q_INVOKABLE void setBoxColor(const QString &kind, const QString &color);   // leer = Standard
    Q_INVOKABLE void resetBoxColors();
    // Bot anlegen oder ändern; oldLogin = bisheriger Login beim Bearbeiten. false bei ungültigem Login.
    Q_INVOKABLE bool setEventBot(const QString &login, const QString &label, const QString &color,
                                 const QString &oldLogin = QString());
    Q_INVOKABLE void removeEventBot(const QString &login);
    Q_INVOKABLE void resetEventBots();
    QString eventBotLabel(const QString &login) const;   // null = kein Event-Bot
    const NotifySound &notificationSound() const { return m_sound; }
    Q_INVOKABLE QStringList predictWords(const QString &text, int cursor) const;
    // Eingabe so, wie sie im Chat erscheinen wird (Emotes als Bild); fehlende Bilder werden geladen
    Q_INVOKABLE QVariantList previewSegments(const QString &text) const;
    // Eingabefeld: Emote-Namen auf Bildbreite bringen; ranges = [{start,length,width}]
    Q_INVOKABLE void setInputEmotes(QObject *textEdit, const QVariantList &ranges);
    // Breite/Höhe eines Bildes im Cache (aus dem Dateikopf, gemerkt); 1, wenn unbekannt
    Q_INVOKABLE qreal imageAspect(const QString &source);
    // Meldet jeden Mausdruck im Fenster als windowPressed, auch außerhalb des Panels.
    Q_INVOKABLE void watchWindowPresses(QObject *window);
    bool eventFilter(QObject *watched, QEvent *event) override;
    QVariantMap pinnedMessage() const { return m_pinned; }
    bool pinnedHidden() const { return !m_pinned.isEmpty() && m_pinned.value(QStringLiteral("id")).toString() == m_hiddenPin; }
    Q_INVOKABLE void hidePinned();
    bool pinnedCollapsed() const { return m_pinCollapsed; }
    void setPinnedCollapsed(bool collapsed);
    Q_INVOKABLE void showPinned();
    void fetchPinned();                              // gql.twitch.tv, ohne Login
    void applyPinnedJson(const QByteArray &json);    // auch für die Tests

    QVariantMap channelPoll() const { return m_poll; }
    QVariantMap channelPrediction() const { return m_prediction; }
    bool pollHidden() const { return !m_poll.isEmpty() && m_poll.value(QStringLiteral("id")).toString() == m_hiddenPoll; }
    bool predictionHidden() const { return !m_prediction.isEmpty() && m_prediction.value(QStringLiteral("id")).toString() == m_hiddenPrediction; }
    bool pollCollapsed() const { return m_pollCollapsed; }
    void setPollCollapsed(bool collapsed);
    bool predictionCollapsed() const { return m_predictionCollapsed; }
    void setPredictionCollapsed(bool collapsed);
    Q_INVOKABLE void hidePoll();
    Q_INVOKABLE void hidePrediction();
    Q_INVOKABLE void showChannelEvents();
    void fetchChannelEvents();                       // gql.twitch.tv, ohne Login
    // nowMs < 0 → aktuelle Zeit; Tests geben eine feste Zeit vor
    void applyChannelEventsJson(const QByteArray &json, qint64 nowMs = -1);
    Q_INVOKABLE int predictionStart(const QString &text, int cursor) const;
    Q_INVOKABLE void spellCheck(const QString &text, int cursor);
    Q_INVOKABLE void requestSpellSuggestions(const QString &word);
    Q_INVOKABLE void addSpellWord(const QString &word);       // eigenes Wörterbuch, dauerhaft

    Q_INVOKABLE QVariantList history() const;
    Q_INVOKABLE QVariantMap historyItem(int index) const;   // für Aktualisierung einzelner Panel-Zeilen
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void setAuth(const QString &user, const QString &token);

    // Emote-Browser: Liste aus {name,id,url,favorite,provider,animated}, A–Z sortiert;
    // kind = "all" | "channel" | "global" | "favorites"; provider = "twitch" | "bttv" | "ffz" | "7tv".
    // Emote-Name einfach in die Nachricht setzen und mit normalem send() posten –
    // Twitch erkennt native Emotes am Namen; Drittanbieter-Emotes erkennen die Nutzer
    // der jeweiligen Erweiterung am Namen.
    // ☆ merkt Favoriten dauerhaft (Name+ID, kein Token).
    // Dem Token fehlt user:read:emotes: Einzeln freigeschaltete Emotes wie HypeTrain sind nicht abrufbar.
    Q_PROPERTY(bool emoteScopeMissing READ emoteScopeMissing NOTIFY emotesChanged)
    bool emoteScopeMissing() const { return m_emoteScopeMissing; }
    void setEmoteScopeMissing(bool missing);

    Q_INVOKABLE QVariantList emoteList(const QString &kind) const;
    Q_INVOKABLE void toggleEmoteFavorite(const QString &name, const QString &key = QString());
    Q_INVOKABLE QString emoteFile(const QString &emoteKey) const;   // lokale Cachedatei als file-URL, sonst leer

    // BTTV-/FFZ-/7TV-Kataloge (anonym, ohne Login): global + Kanal (Kanal nur mit der
    // Ziffern-Raum-ID, die Twitch in jeder Nachricht als room-id-Markierung mitschickt).
    // Auch für die Tests direkt aufrufbar.
    void fetchEmoteCatalogs();     // Twitch: Kanal- und Global-Listen (gql.twitch.tv, ohne Login)
    void fetchThirdPartyGlobal();
    void fetchThirdPartyChannel(const QString &login, const QString &roomId);
    void setThirdPartyCatalog(EmoteProvider provider, bool global, const ThirdPartyCatalog &catalog);
    // Antwort eines Anbieters → Katalog. Effekt-Befehle (BTTV-Modifier wie „h!“,
    // versteckte FFZ-Modifier) sind keine Bilder und bleiben deshalb draußen.
    static ThirdPartyCatalog parseCatalog(EmoteProvider provider, const QByteArray &json, bool global);
    // Helix-Antwort von chat/emotes/set: übernimmt nur Abo-Emotes (emote_type „subscriptions“).
    void applySubscriptionEmotes(const QByteArray &json);
    // Helix-Antwort von chat/emotes/global: Standard-Emotes, die jedes Konto benutzen darf.
    void applyGlobalEmotes(const QByteArray &json);
    // Twitch schickt eigene Nachrichten nicht zurück: emotes-Tag selbst bilden, aus den
    // Emotes des Kontos („id:start-ende/…“ in UTF-16-Positionen, wie buildSegments sie erwartet).
    QString ownEmoteTag(const QString &message) const;

    // Token-Ablage (DPAPI-Vault): Vault anwenden, einmalige Migration alter
    // Einstellungen; Browser-Login nur übernehmen, wenn Twitch-Scope chat:read
    // erlaubt und der Chat noch keinen eigenen Token hat.
    void applyVault(TokenVault *vault);
    void adoptAuthFromBrowser(const QString &user, const QString &token);

    void setBrowserController(QObject *browser) { m_browserObj = browser; }
    QObject *browserObj() const { return m_browserObj; }

    static QString guessChannel(const QString &text);       // Twitch-Kanal aus URL/Pfad (auch lokal .m3u8/.strm)

    void startFollowing();                                  // bei activate(): ggf. automatisch verbinden
    void onMediaEvent(const QString &name, const QVariantMap &data);
    void onMediaPath(const QString &path);

signals:
    void newMessage(const QVariantMap &message);            // {type,user,name,color,badges,segments,raw,emoteIds,ts}
    void messageUpdated(int index);                         // Emoticon-Bild/katalog → Zeile neu rendern
    void channelChanged();
    void statusChanged();
    void settingsChanged();
    void emotesChanged();          // Emote-Katalog gewachsen → Panel-Liste neu laden
    void spellSettingsChanged();   // Sprache oder eigenes Wörterbuch geändert → neu prüfen
    void spellResult(const QString &text, const QVariantList &ranges);   // [{start,length,word}]
    void spellSuggestions(const QString &word, const QStringList &suggestions);
    void grammarResult(const QString &text, const QVariantList &issues);   // [{start,length,message,messageEn,replacements,source}]
    void predictionsChanged();     // Wortlisten sind da → Vorschläge neu holen
    void pinnedChanged();
    void channelEventsChanged();   // Umfrage oder Vorhersage
    void chatFontSizeChanged();
    void notifySoundChanged();
    void boxSettingsChanged();
    void chattersChanged();
    void viewersChanged();
    void roleChanged();
    void loginStateChanged();
    void loginUrlReady(const QString &url);   // Link wurde im Browser geöffnet
    void emoteImageReady();                    // ein Emote-Bild ist angekommen (Vorschau neu zeichnen)
    void badgeImagesChanged();                 // Abzeichen-Katalog oder -Bild ist angekommen
    void windowPressed(QObject *window, qreal x, qreal y);   // Mausdruck in Fensterkoordinaten
    void blockedChanged();
    void userInfo(const QString &login, const QVariantMap &info);   // {createdAt(ms), userId}
    void userActionDone(const QString &action, bool ok);
    void historyCleared();         // Kanalwechsel → Panel leert die Chatliste
    void liveStreamsChanged();

private slots:
    void onOpened();
    void onText(const QString &line);
    void onClosed(const QString &why);
    void onFailed(const QString &why);
    void onEmoteReady(const QString &id);

private:
    void parseLine(const QString &line);
    void noteChatter(const QString &login, const QString &name, const QString &color, const QStringList &badges, bool wrote);
    void clearChatters();
    bool selfListed() const;                   // angemeldet und in einem Kanal → eigene Zeile in der Liste
    QString ownLogin() const;
    void fetchHelixChatters(const QString &cursor, QSet<QString> seen);
    bool isKnownEmote(const QString &word) const;
    QStringList emoteNamesWithPrefix(const QString &prefix) const;
    void emitGrammarResult();
    void ensureFrequencyWords();
    void learnOwnMessage(const QString &text);
    int  addMessage(const QString &type, const QString &user, const QString &name, const QString &color,
                    const QStringList &badges, const QString &raw, const QString &emoteTag = QString(),
                    const QVariantMap &extra = QVariantMap());
    void helix(const QByteArray &method, const QString &path, const QUrlQuery &query, const QByteArray &body,
               const std::function<void(int, const QByteArray &)> &done);
    void systemLine(const QString &text);
    void markDeleted(const QString &reason, const std::function<bool(const QVariantMap &)> &match);
    static QString durationText(int seconds);
    bool mentionsMe(const QString &text) const;
    static bool mentionsAnyone(const QString &text);   // enthält „@Name“ am Wortanfang
    QStringList ownNames() const;              // alle Namen des eigenen Kontos (für @-Erwähnungen)
    bool isOwnName(const QString &name) const;
    void fetchBlocked(const QString &cursor = QString());
    void setMyBadges(const QStringList &badges);
    void ensureAutomodSubscriptions();
    void setAutomodStatus(const QString &automodId, const QString &status, const QString &by);
    void joinNow();
    void persist();
    void setStatus(const QString &key, const QString &detail = QString());
    void scheduleReconnect();
    void sendAuthLines();
    bool tryFollow(const QString &text, bool notify);
    // Proxy-Adressen ohne Kanalnamen (tcp://…, …/play.php?…): Kanal aus dem mpv-Medientitel
    static bool isProxyStreamPath(const QString &path);
    void followFromMediaTitle(const QString &path, int attempt);
    static QString unescapeTag(const QString &v);
    void noteObservedEmotes(const QString &raw, const QString &emoteTag);  // Name→ID aus IRC-Emote-Tags merken
    void requestGql(const QString &query, const QString &target);
    void fetchSubscriptionEmotes(const QString &emoteSetsTag);   // Sets aus GLOBALUSERSTATE → Helix
    void requestEmoteSets(const QString &clientId, const QStringList &setIds);
    void requestGlobalEmotes(const QString &clientId);
    void requestUserEmotes(const QString &clientId, const QString &userId, const QString &cursor);
    void clearSubscriptionEmotes();

    // Nachricht → Segmentfolge [{t:"text",s:…} | {t:"emote",name,key,src,anim}] für das Panel.
    // Native Twitch-Emotes aus dem emotes-Tag zuerst; außerhalb davon wird jedes durch
    // Leerraum getrennte Wort exakt mit den Drittanbieter-Namen verglichen.
    QVariantList buildSegments(const QString &msg, const QString &emoteTag, QStringList &emoteKeys,
                               bool cheers = false) const;
    // Cheer-Emotes: Präfix (klein) → Stufen {Mindest-Bits, Bild-URL}; globale ab Werk, Kanal-eigene über Helix
    struct CheerTier { int minBits = 0; QString url; };
    QHash<QString, QVector<CheerTier>> m_cheermotes;
    void fetchCheermotes();
    QString cheerKey(const QString &word, int *prefixLength) const;   // „Cheer250“ → „cheer_cheer_100“
    void rebuildMessage(int index);            // Segmente aus gespeichertem raw+Tag neu berechnen
    void rebuildAllMessages();                 // Katalog traf ein → ganze Verlaufsansicht neu bauen
    void requestCatalog(EmoteProvider provider, bool global, const QString &login, const QUrl &url);
    void clearChannelCatalogs();
    void rebuildThirdPartyLookup();

    uve::IPlayerHost *m_host = nullptr;
    QString m_id;
    MiniWebSocket m_ws;
    EmoteCache *m_emotes = nullptr;
    SpellChecker *m_spell = nullptr;
    bool m_spellGerman = true;
    bool m_spellEnglish = true;
    QSet<QString> m_spellWords;                 // eigenes Wörterbuch (kleingeschrieben), in den Einstellungen
    bool m_grammarCheck = true;
    bool m_grammarOnline = true;
    bool m_predictText = true;
    LanguageToolClient *m_languageTool = nullptr;
    bool m_languageToolFailureLogged = false;
    QString m_grammarText;                      // Text, zu dem die Fundstellen unten gehören
    QVariantList m_localIssues, m_remoteIssues;
    TextPredictor m_predictor;
    bool m_frequencyRequested = false;
    QString m_predictFile;                      // nur eigene Wörter, keine fremden Nachrichten
    QVariantMap m_pinned;
    QString m_hiddenPin;                        // ID der verborgenen Anheftung (persistiert)
    bool m_pinCollapsed = false;                // Karte auf eine Zeile eingeklappt (persistiert)
    QTimer *m_pinPoll = nullptr;                // IRC meldet Anheftungen nicht → regelmäßig nachfragen
    QTimer *m_pinExpiry = nullptr;
    void clearPinned();
    QVariantMap m_poll;
    QVariantMap m_prediction;
    QString m_hiddenPoll;                       // nur für diese Sitzung
    QString m_hiddenPrediction;
    bool m_pollCollapsed = false;               // persistiert
    bool m_predictionCollapsed = false;         // persistiert
    QTimer *m_eventPoll = nullptr;              // 15 s solange abgestimmt wird, sonst 20 s
    void clearChannelEvents();
    void rebuildPinned();
    TokenVault *m_vault = nullptr;
    QObject *m_browserObj = nullptr;

    QString m_channel, m_joined, m_user, m_token;
    QString m_statusKey = QStringLiteral("idle"), m_statusDetail;
    bool m_autoChannel = true;
    bool m_autoScroll = true;
    int m_chatFontSize = 0;
    struct Chatter { QString login, name, color; QStringList badges; bool wrote = false; quint64 spoke = 0; };
    quint64 m_speakCounter = 0;                 // je höher Chatter::spoke, desto jünger die letzte Nachricht
    QStringList mentionSuggestions(const QString &text, int cursor) const;
    QHash<QString, Chatter> m_chatters;         // login → Leute im Chat
    bool m_chattersComplete = false;
    int m_viewerCount = -1;
    QStringList m_myBadges;                     // aus USERSTATE: eigene Abzeichen im aktuellen Kanal
    QHash<QString, qreal> m_imageAspects;       // Datei → Breite/Höhe
    QString m_myBadgeTags;                   // dasselbe mit Version („moderator/1,subscriber/12“) für die Bilder
    QString m_ownColor;                         // aus (GLOBAL)USERSTATE: eigene Namensfarbe
    QHash<QString, QString> m_badgeGlobal;      // „set/version“ → Bild-URL, für alle Kanäle
    QHash<QString, QString> m_badgeChannel;     // dasselbe vom aktuellen Kanal; schlägt global
    bool m_badgeGlobalTried = false;            // Netzfehler setzt zurück
    int m_badgeRevision = 0;
    QString m_ownDisplayName;                   // aus GLOBALUSERSTATE: Anzeigename laut Twitch
    QString m_testUserId;
    QSet<QString> m_blockedIds, m_blockedLogins;
    bool m_blockedFetched = false;
    int m_blockedRevision = 0;
    HelixHook m_helixHook;
    QString m_automodRoom;                      // Kanal, für den AutoMod-Abos bestehen
    QString m_automodSession;
    QStringList m_automodSubs;                  // EventSub-Abo-IDs (zum Abmelden beim Kanalwechsel)
    TwitchAuthService *m_auth = nullptr;
    QTimer *m_renewTimer = nullptr;
    QString m_loginState = QStringLiteral("idle"), m_loginCode, m_loginUrl, m_loginError;
    QString m_refreshToken;
    qint64 m_tokenExpiresAt = 0;
    bool m_loginInteractive = false;           // laufende Anmeldung (sonst: stille Erneuerung)
    void setLoginState(const QString &state, const QString &error = QString());
    void scheduleRenew();
    void storeRenewal();
    void renewedToken(const QString &accessToken, const QString &refreshToken, qint64 expiresAtMs);
    bool m_streamLive = false;
    void resetViewers();
    bool m_chattersDenied = false;              // Helix sagt: kein Moderator in diesem Kanal
    QTimer *m_chatterPoll = nullptr;
    QTimer *m_chatterNotify = nullptr;          // fasst Beitritte zu einer Meldung zusammen
    WhisperService *m_whispers = nullptr;
    NotifySound m_sound;
    bool m_notifySound = true;
    void loadBoxSettings(const QJsonObject &s);
    QHash<QString, QString> m_boxColors;        // Art → #rrggbb (nur Abweichungen vom Standard)
    struct EventBot { QString login, label, color; };
    QList<EventBot> m_eventBots;                // Reihenfolge wie im Menü
    QNetworkAccessManager *m_net = nullptr;     // GQL- und Drittanbieter-Abfragen für den Emote-Katalog
    QHash<QString, QString> m_observedEmotes;   // im Chat gesehene Emotes: Name → Emote-ID
    QHash<QString, QString> m_channelEmotes;    // Abo-Emotes des aktuellen Kanals (GQL, ohne Login)
    QHash<QString, QString> m_globalEmotes;     // globale Twitch-Emotes inkl. Smileys (GQL, ohne Login)
    QHash<QString, QString> m_subEmotes;        // Abo-Emotes des angemeldeten Kontos: Name → ID
    QHash<QString, QString> m_hypeOwned;        // HypeTrain-Emotes des angemeldeten Kontos: Name → ID
    QHash<QString, QString> m_ownEmotes;        // alle Emotes des Kontos (auch Follower, Bits, HypeTrain): Name → ID
    bool m_emoteScopeMissing = false;
    QStringList m_subSetIds;                    // zuletzt abgefragte Emote-Sets des Kontos
    quint64 m_subFetchGeneration = 0;           // verwirft Antworten nach Logout oder neuer Abfrage
    ThirdPartyCatalog m_tpGlobal[ProviderCount];    // globale Listen je Anbieter
    ThirdPartyCatalog m_tpChannel[ProviderCount];   // Kanal-Listen je Anbieter
    ThirdPartyCatalog m_tpLookup;                   // zusammengeführt nach Vorrang (für die Nachrichtensuche)
    QHash<QString, bool> m_tpAnimated;              // Schlüssel → animiert (bleibt für Favoriten nach Kanalwechsel)
    QHash<QString, QString> m_favoriteEmotes;   // ★-Favoriten: Name → ID, in den Einstellungen persistiert
    QString m_catalogChannel;                   // GQL-Katalog wurde für diesen Kanal schon angefragt
    bool m_globalTried = false;
    bool m_tpGlobalTried[ProviderCount] = {};   // Globalliste schon angefragt (Netzfehler setzt zurück)
    QString m_tpChannelRoom[ProviderCount];     // Raum-ID, für die der Anbieter schon gefragt wurde
    QString m_roomId;                           // Twitch-Raum-ID des aktuell verbundenen Kanals
    QHash<QString, QString> m_roomIds;          // login → Raum-ID (persistiert; Kanal-Emotes sofort beim Beitritt)
    bool m_manualClose = false;
    bool m_connected = false;
    int m_backoffMs = 1000;

    QVector<QVariantMap> m_history;
    QElapsedTimer m_sendClock;
    qint64 m_lastSendMs = -100000;

    // Eigene gesendete Nachricht: Echo von Twitch erkennen und lokale Zeile anreichern
    bool m_selfEchoPending = false;
    QString m_selfEchoText;
    qint64 m_selfEchoTs = 0;

    // Live-Kanäle
    QVariantList m_liveStreams;
    QVariantList m_liveCategories;
    QString m_liveCategory;
    QString m_liveTab = QStringLiteral("top");
    QString m_liveError;
    bool m_liveBusy = false;
    quint64 m_liveGeneration = 0;               // verwirft Antworten älterer Abfragen
    QString m_liveSortMode = QStringLiteral("relevance");
    QString m_liveLangFilter;
    QVariantList m_liveRawStreams, m_liveRawCategories;   // ungeordnet – zum Neuordnen, wenn Follows nachkommen
    QSet<QString> m_followedLive, m_followedGames;        // gefolgte Kanäle, die live sind, und ihre Kategorien
    bool m_followedKnown = false, m_followedFetching = false;
    void liveHistory(QHash<QString, int> &channels, QHash<QString, int> &games, QHash<QString, int> *categories = nullptr) const;
    QStringList liveTopCategories(int count) const;   // meistgeklickte Kategorien
    void fetchFollowedPage(quint64 gen, const QString &tab, const QString &cursor, QVariantList acc, int page);
    QStringList liveFavoriteGames(int count) const;
    void publishLive(const QString &tab, const QString &error = QString());   // Rohdaten ordnen und anzeigen
    void ensureFollowedKnown();
    void noteFollowed(const QVariantList &followedRows);
};
