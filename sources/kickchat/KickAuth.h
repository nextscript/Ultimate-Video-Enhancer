#pragma once
// Anmeldung bei Kick (offizielle API, OAuth 2.1 mit PKCE) und Senden von Chat-Nachrichten.
//
// Alle Nutzer melden sich über die eingebaute Kick-App des Herausgebers an (KickAppDefaults.h).
// Die Tokens liegen nur DPAPI-verschlüsselt im TokenVault (tokens.dat), nie in den
// Einstellungen oder im Log.
//
// Ablauf: Browser öffnet id.kick.com/oauth/authorize → Kick leitet auf
// http://localhost:<port>/callback um → der lokale Mini-Server nimmt den Code an →
// Tausch gegen Access-/Refresh-Token → Konto über api.kick.com/public/v1/users.

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QTcpServer;
class TokenVault;

class KickAuth : public QObject
{
    Q_OBJECT
public:
    static constexpr quint16 kCallbackPort = 53135;
    static QString redirectUri();

    explicit KickAuth(QObject *parent = nullptr);

    void setVault(TokenVault *vault);

    QString clientId() const { return m_clientId; }
    bool hasCredentials() const { return !m_clientId.isEmpty() && !m_clientSecret.isEmpty(); }
    bool usingOwnApp() const;      // false = eingebaute App des Herausgebers
    void setCredentials(const QString &clientId, const QString &clientSecret);

    bool loggedIn() const { return !m_accessToken.isEmpty(); }
    QString userName() const { return m_userName; }
    qint64 userId() const { return m_userId; }

    QString state() const { return m_state; }      // idle | waiting | error
    QString error() const { return m_error; }
    QString authorizeUrl() const { return m_authorizeUrl; }

    void startLogin();
    void cancelLogin();
    void logout();

    // Nachricht in den Chat des Kanals (broadcasterUserId = user_id des Kanals).
    // done(ok, fehlertext)
    void sendChat(qint64 broadcasterUserId, const QString &content, const QString &replyToId,
                  const std::function<void(bool, const QString &)> &done);

    // Beliebiger Aufruf von api.kick.com (path z. B. "/public/v1/moderation/bans").
    // done(ok, httpStatus, antwort, fehlertext)
    void api(const QByteArray &method, const QString &path, const QJsonObject &body,
             const std::function<void(bool, int, const QByteArray &, const QString &)> &done);

signals:
    void changed();            // Anmeldung, Konto oder Zugangsdaten geändert
    void stateChanged();       // idle/waiting/error

private:
    void setState(const QString &state, const QString &error = QString());
    void applyDefaultCredentials();
    void onCallbackConnection();
    void exchangeCode(const QString &code);
    void applyTokenResponse(const QByteArray &body);
    void fetchUser();
    void refresh(const std::function<void(bool)> &done);
    void persist();
    void stopServer();
    static QByteArray randomUrlSafe(int bytes);

    TokenVault *m_vault = nullptr;
    QNetworkAccessManager *m_nam;
    QTcpServer *m_server = nullptr;
    QString m_clientId, m_clientSecret;
    QString m_accessToken, m_refreshToken;
    qint64 m_expiresAt = 0;       // ms seit Epoche
    QString m_userName;
    qint64 m_userId = 0;
    QString m_state = QStringLiteral("idle");
    QString m_error;
    QString m_authorizeUrl;
    QByteArray m_verifier, m_oauthState;
};
