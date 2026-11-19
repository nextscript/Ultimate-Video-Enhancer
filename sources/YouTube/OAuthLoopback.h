#pragma once
// Loopback-Empfänger und PKCE-Helfer für Google OAuth 2.0 (Desktop-Apps, Authorization Code Flow mit PKCE, RFC 7636 / RFC 8252).
// Wird als QML-Typ "OAuthLoopback" aus dem Ordner oauth/ des YouTube-Plugins geladen.
#include <QObject>
#include <QPointer>
#include <QString>

class QTcpServer;
class QTcpSocket;

class OAuthLoopback : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int port READ port NOTIFY portChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY portChanged)
    Q_PROPERTY(QString successHtml READ successHtml WRITE setSuccessHtml)
    Q_PROPERTY(QString errorHtml READ errorHtml WRITE setErrorHtml)
public:
    explicit OAuthLoopback(QObject *parent = nullptr);
    ~OAuthLoopback() override;

    int port() const;
    bool listening() const;
    QString successHtml() const { return m_successHtml; }
    void setSuccessHtml(const QString &h) { m_successHtml = h; }
    QString errorHtml() const { return m_errorHtml; }
    void setErrorHtml(const QString &h) { m_errorHtml = h; }

    // Lauscht auf 127.0.0.1 an einem freien Port; liefert den Port (0 = Fehler)
    Q_INVOKABLE int listen();
    Q_INVOKABLE void close();

    // PKCE: zufälliger code_verifier (43–128 Zeichen, base64url) und code_challenge = BASE64URL(SHA256(verifier))
    Q_INVOKABLE QString randomVerifier(int bytes = 64) const;
    Q_INVOKABLE QString challenge(const QString &verifier) const;
    Q_INVOKABLE QString randomState() const;

signals:
    void portChanged();
    // Abfrageparameter der Weiterleitung (code, state oder error, error_description)
    void redirected(const QVariantMap &params);

private:
    void onNewConnection();
    void handle(QTcpSocket *socket);

    QPointer<QTcpServer> m_server;
    QString m_successHtml;
    QString m_errorHtml;
};
