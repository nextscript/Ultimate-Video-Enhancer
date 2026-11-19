#pragma once
// UVESpotify, Netzwerk: Spotify-Konto (OAuth mit PKCE, ohne Client-Secret) und Auflösung eines Links zu einer YouTube-Adresse.
#include "SpotifyCore.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QVariantList>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTcpSocket;
class QTimer;

namespace uve::spotify {

class SpotifyAccount : public QObject
{
    Q_OBJECT
public:
    using JsonCb = std::function<void(int status, const QJsonObject &json)>;
    using ItemsCb = std::function<void(const QJsonArray &items, int status)>;   // status 200 = alles gelesen

    explicit SpotifyAccount(QObject *parent = nullptr);

    QString clientId() const { return m_clientId; }
    void setClientId(const QString &id);
    int port() const { return m_port; }
    void setPort(int port);
    QString redirectUri() const;
    QString refreshToken() const { return m_refresh; }
    void setRefreshToken(const QString &token);

    bool loggedIn() const { return !m_refresh.isEmpty(); }
    bool loginPending() const { return m_server != nullptr; }
    QString userName() const { return m_user; }
    QString product() const { return m_product; }

    // Öffnet den Browser mit der Spotify-Anmeldung; die Antwort kommt an http://127.0.0.1:<port>/callback
    void login(QString *error);
    void cancelLogin();
    void logout();
    void loadProfile();

    void api(const QString &url, const JsonCb &cb);
    // Folgt "next" bis max Einträge
    void apiPaged(const QString &url, int max, const ItemsCb &cb);

signals:
    void changed();
    void refreshTokenChanged();
    void message(const QString &text, bool error);

private:
    void withToken(const std::function<void(const QString &error)> &cb, bool force = false);
    void tokenRequest(const QList<std::pair<QString, QString>> &form, const std::function<void(const QString &error)> &cb);
    void onCallback(QTcpSocket *socket, const QByteArray &request);
    void pagedStep(const QString &url, int max, QJsonArray items, const ItemsCb &cb);
    void request(const QString &url, const JsonCb &cb, bool retried);

    QNetworkAccessManager *m_nam;
    QTcpServer *m_server = nullptr;
    QTimer *m_loginTimeout;
    QString m_clientId;
    int m_port = 43821;
    QString m_verifier;
    QString m_state;
    QString m_access;
    QDateTime m_expires;
    QString m_refresh;
    QString m_user;
    QString m_product;
    bool m_refreshing = false;
    QList<std::function<void(const QString &)>> m_waiting;
};

struct Resolution
{
    QString url;            // YouTube-Adresse für mpv/yt-dlp
    QString title;          // "Interpret – Titel"
    QString collection;     // Name der Playlist/des Albums, sonst leer
    int count = 1;          // Titel insgesamt
    QStringList more;       // weitere Titel als Spotify-Links (werden beim Abspielen einzeln aufgelöst)
    QVariantList moreTitles;     // Anzeigename je weiterem Titel
    QVariantList moreDurations;  // Dauer in Sekunden je weiterem Titel
};

class ResolveJob : public QObject
{
    Q_OBJECT
public:
    ResolveJob(const QString &url, SpotifyAccount *account, bool preferVideo, QObject *parent = nullptr);
    void start();
    // Nur die Titelliste holen (ohne YouTube-Suche): Signal listed statt finished
    void setListOnly(bool listOnly) { m_listOnly = listOnly; }

signals:
    void finished(const uve::spotify::Resolution &result);
    void listed(const QList<uve::spotify::Track> &tracks, const QString &collection);
    void failed(const QString &error, bool definitive);

private:
    void get(const QString &url, const std::function<void(int status, const QString &text)> &cb, const QByteArray &cookie = {});
    void fail(const QString &error, bool definitive = false);
    void track(const QString &id);
    void collection(const Link &link);
    void collectionFromEmbed(const Link &link);
    void playlistFromApi(const QString &id, bool items);
    void begin(const QList<Track> &tracks);
    void search(const Track &track);

    QNetworkAccessManager *m_nam;
    QPointer<SpotifyAccount> m_account;
    QString m_url;
    bool m_preferVideo;
    bool m_done = false;
    bool m_listOnly = false;
    Resolution m_result;
};

} // namespace uve::spotify
