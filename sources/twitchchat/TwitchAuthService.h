#pragma once
// Twitch OAuth 2.0 – Device Code Grant (öffentlicher Client, kein Secret),
// Token-Auffrischung und Widerruf. Geprüft gegen dev.twitch.tv (Stand 2026).
//
// Ablauf:
//   begin()  → POST id.twitch.tv/oauth2/device   {device_code, user_code,
//              verification_uri, expires_in, interval}
//   Polling  → POST id.twitch.tv/oauth2/token    grant=…/device_code
//              400 authorization_pending  → weiter warten
//              400 slow_down              → Intervall erhöhen
//              400 expired_token / access_denied / bad_request → Fehler
//              200 → {access_token, refresh_token, expires_in, scope}
// Refresh:   grant=refresh_token (Refresh-Token ist einmalig verwendbar!)
// Revoke:    POST id.twitch.tv/oauth2/revoke {token, client_id}

#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class TwitchAuthService : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, AwaitingUser, Polling, SignedIn, Error };
    Q_ENUM(State)

    explicit TwitchAuthService(QNetworkAccessManager *nam, QObject *parent = nullptr);

    void begin(const QString &clientId, const QString &scopes);
    void cancel();

    // Session aus dem TokenVault wiederbeleben: Token erneuern, wenn abgelaufen.
    void resume(const QString &clientId, const QString &accessToken,
                const QString &refreshToken, qint64 expiresAtMs);

    // Explizit erneuern (Button „Token erneuern“).
    void refreshNow(const QString &clientId);

    void revoke(const QString &clientId, const QString &accessToken); // Server-Widerruf

    State state() const { return m_state; }
    QString userCode() const { return m_userCode; }
    QString verificationUri() const { return m_verificationUri; }

    // Reines Parsen/Klassifizieren – ohne Netzwerk, in Unit-Tests geprüft.
    struct DeviceInfo { QString deviceCode, userCode, verificationUri, verificationUriComplete;
                        int expiresIn = 0; int interval = 5; };
    static bool parseDeviceResponse(const QByteArray &json, DeviceInfo *out, QString *error);
    struct TokenInfo { QString accessToken, refreshToken; qint64 expiresAtMs = 0; QStringList scope; };
    static bool parseTokenResponse(const QByteArray &json, TokenInfo *out, QString *error);
    // "authorization_pending" | "slow_down" | "expired_token" | "access_denied" | ...
    static QString tokenFlowError(const QByteArray &json);
    static bool scopeAllows(const QStringList &granted, const QString &needed);

signals:
    void stateChanged();
    void deviceReady(const QString &userCode, const QString &verificationUri);
    void signedIn(const QString &accessToken, const QString &refreshToken,
                  qint64 expiresAtMs, const QStringList &scope);
    void authFailed(const QString &reason, const QString &detail);

private:
    void setState(State s);
    void schedulePoll(int seconds);
    void pollNow();
    void postToken(const QByteArray &formBody);
    void handleDeviceReply(QNetworkReply *reply);
    void handleTokenReply(QNetworkReply *reply);

    QNetworkAccessManager *m_nam = nullptr;
    QTimer *m_pollTimer = nullptr;
    State m_state = State::Idle;

    QString m_clientId;
    QString m_deviceCode;
    QString m_userCode;
    QString m_verificationUri;
    int m_interval = 5;
    qint64 m_deviceExpiresAtMs = 0;

    // Refresh-Pfad
    QString m_refreshToken;
    bool m_refreshPending = false;
};
