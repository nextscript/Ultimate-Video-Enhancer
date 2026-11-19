#pragma once
// Private Nachrichten (Twitch-Whispers) als Postfach.
//
// Empfangen: EventSub-WebSocket (wss://eventsub.wss.twitch.tv/ws), Ereignis user.whisper.message,
//            Recht user:read:whispers oder user:manage:whispers.
// Senden:    Helix POST /whispers, Recht user:manage:whispers. Twitch verlangt beim Absender eine
//            verifizierte Telefonnummer und erlaubt höchstens 40 verschiedene Empfänger pro Tag.
// Verlauf:   Twitch liefert keinen; das Postfach speichert selbst, verschlüsselt per DPAPI (TokenVault).
// Gruppen:   Twitch kennt keine. Eine Gruppe ist ein Verteiler im Plugin: Jede Nachricht geht einzeln
//            an jedes Mitglied. Antwortet ein Mitglied, landet das in der Gruppe, an die du ihm
//            zuletzt geschrieben hast – die anderen Mitglieder sehen davon nichts.

#include "MiniWebSocket.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <functional>
#include <memory>

class QNetworkAccessManager;
class TokenVault;

class WhisperService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int unreadTotal READ unreadTotal NOTIFY changed)
    // nologin | checking | scope | connecting | ready | error
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateDetail READ stateDetail NOTIFY stateChanged)
    Q_PROPERTY(bool canSend READ canSend NOTIFY stateChanged)
    Q_PROPERTY(QString login READ login NOTIFY stateChanged)

public:
    // Empfänger-Login, Text, Rückmeldung (ok, Fehlertext). Im Betrieb Helix, in Tests ersetzbar.
    using Sender = std::function<void(const QString &toLogin, const QString &text,
                                      const std::function<void(bool, const QString &)> &done)>;

    WhisperService(const QString &storePath, const QByteArray &entropy, QObject *parent = nullptr);
    ~WhisperService() override;

    void setToken(const QString &token);            // leer = abgemeldet
    QString clientId() const { return m_clientId; }
    QString userId() const { return m_userId; }
    QString login() const { return m_login; }
    bool hasScope(const QString &scope) const { return m_scopes.contains(scope); }

    int unreadTotal() const;
    QString state() const { return m_state; }
    QString stateDetail() const { return m_stateDetail; }
    bool canSend() const;

    // Liste fürs Postfach, neueste zuerst: {id,title,isGroup,members,lastText,lastFrom,lastTs,unread}
    Q_INVOKABLE QVariantList conversations() const;
    Q_INVOKABLE QVariantMap conversation(const QString &id) const;
    // {from,name,text,ts,outgoing,status:"sent"|"sending"|"failed",error}
    Q_INVOKABLE QVariantList messages(const QString &id) const;
    Q_INVOKABLE void markRead(const QString &id);
    Q_INVOKABLE QString openDirect(const QString &login, const QString &displayName = QString());
    Q_INVOKABLE QString createGroup(const QString &title, const QVariantList &members);   // [{login,name}]
    Q_INVOKABLE void setGroupMembers(const QString &id, const QVariantList &members);
    Q_INVOKABLE void renameGroup(const QString &id, const QString &title);
    Q_INVOKABLE void deleteConversation(const QString &id);
    Q_INVOKABLE bool send(const QString &id, const QString &text);
    static bool validLogin(const QString &login);

    // Für Tests und für die EventSub-Verarbeitung
    void handleEventSubText(const QString &json);
    void receiveWhisper(const QString &fromLogin, const QString &fromName, const QString &text,
                        const QString &whisperId = QString());
    void setSender(const Sender &sender) { m_sender = sender; }

    // Weitere EventSub-Ereignisse auf derselben Verbindung (z. B. AutoMod für Moderatoren)
    QString sessionId() const { return m_sessionId; }
    void subscribeEvent(const QString &type, const QString &version, const QJsonObject &condition,
                        const std::function<void(bool ok, const QString &subscriptionId, const QString &error)> &done);
    void unsubscribeEvent(const QString &subscriptionId);
    void flush();                                   // sofort speichern

signals:
    void changed();
    void messageAdded(const QString &conversationId, bool outgoing);
    void stateChanged();
    void sessionReady();                                            // neue EventSub-Sitzung (Abos neu anlegen)
    void eventNotification(const QString &type, const QJsonObject &event);   // alles außer Whispers

private:
    struct Member { QString login, name; };
    struct Message
    {
        QString from, name, text, status, error;
        qint64 ts = 0;
        bool outgoing = false;
    };
    struct Conversation
    {
        QString id, title;
        bool isGroup = false;
        QVector<Member> members;
        QVector<Message> messages;
        int unread = 0;
    };

    Conversation *find(const QString &id);
    const Conversation *find(const QString &id) const;
    void appendMessage(Conversation &c, const Message &m);
    void setState(const QString &state, const QString &detail = QString());
    void scheduleSave();
    void load();
    void validate();
    void connectEventSub(const QString &host, const QString &path);
    void subscribe(const QString &sessionId);
    void reconnectLater();
    void helixSend(const QString &toLogin, const QString &text, const std::function<void(bool, const QString &)> &done);
    void postWhisper(const QString &toId, const QString &text, const std::function<void(bool, const QString &)> &done);
    static QString explainError(int status, const QByteArray &body);
    static QVariantMap toVariant(const Message &m);

    std::unique_ptr<TokenVault> m_store;
    QNetworkAccessManager *m_net = nullptr;
    MiniWebSocket m_ws;
    QTimer m_keepalive, m_retry, m_saveTimer;
    int m_backoffMs = 5000;
    quint64 m_generation = 0;                    // verwirft Antworten nach Token-Wechsel

    QString m_token, m_clientId, m_userId, m_login;
    QStringList m_scopes;
    QString m_state = QStringLiteral("nologin"), m_stateDetail;
    Sender m_sender;

    QVector<Conversation> m_conversations;
    QHash<QString, QString> m_userIds;           // login → Twitch-ID
    QHash<QString, QString> m_groupRoute;        // login → Gruppe, an die zuletzt geschrieben wurde
    QStringList m_seenWhispers;                  // gegen doppelte Zustellung
    QString m_sessionId;
};
