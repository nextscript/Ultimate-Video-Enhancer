#pragma once
// Twitch Helix-Client (api.twitch.tv/helix) – nur GET, asynchron, Cursor-Pagination,
// Rate-Limit-Behandlung (Header + 429-Backoff). Reine JSON-Helfer sind statisch
// und ohne Netzwerk testbar. Geprüft gegen dev.twitch.tv (Stand 2026):
//   /streams/followed   first<=100, viewer_count absteigend, thumbnail {width}x{height}
//   /channels/followed  first<=100, Felder broadcaster_login/-name, followed_at
//   /search/channels    query Pflicht, live_only, first<=100
//   /users?id=          bis 100 IDs, Profilbilder
//   /channels?broadcaster_id=  Titel, game_name

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class TwitchHelix : public QObject
{
    Q_OBJECT
public:
    enum class Kind { Users, FollowedStreams, FollowedChannels, SearchChannels, ChannelInfo };
    Q_ENUM(Kind)

    explicit TwitchHelix(QNetworkAccessManager *nam, QObject *parent = nullptr);

    void setCredentials(const QString &clientId, const QString &accessToken,
                        qint64 expiresAtMs);
    bool ready() const { return !m_clientId.isEmpty() && !m.accessToken.isEmpty(); }

    // bis 100 Werte pro Aufruf; asId=true → id=…, sonst login=…
    int requestUsers(const QStringList &values, bool asId = true);
    int requestSelf();                                        // /users ohne Parameter
    int requestFollowedStreams(const QString &userId, const QString &cursor = {});
    int requestFollowedChannels(const QString &userId, const QString &cursor = {});
    int requestSearchChannels(const QString &query, bool liveOnly, const QString &cursor = {});
    int requestChannelInfo(const QString &userId);              // Titel/Spiele fürs Nachziehen

    void cancelAll();

    // Abgelaufenes Token? (30 s Puffer)
    bool tokenExpired() const;

    // ---------- reine Parser (Unit-Tests, ohne Netzwerk) ----------
    static QString expandThumbnail(const QString &url, int w, int h);
    static QString nextCursor(const QJsonObject &pagination);
    static QList<QVariantMap> followedStreamsToRows(const QJsonArray &data);
    static QList<QVariantMap> followedChannelsToRows(const QJsonArray &data);
    static QList<QVariantMap> searchChannelsToRows(const QJsonArray &data);
    static QList<QVariantMap> usersToRows(const QJsonArray &data);

signals:
    // data = fertig geparste Zeilen; cursor für „mehr laden“ (leer = Ende)
    void result(Kind kind, int requestId, const QList<QVariantMap> &rows,
                const QString &cursor);
    void failed(Kind kind, int requestId, const QString &reason, const QString &detail);
    void rateLimited(int secondsUntilReset);
    // 401: Aufrufer soll still refreschen und einmal wiederholen
    void unauthorized();

private:
    struct Pending { Kind kind; int id; QString path; };
    void enqueue(Kind kind, int id, const QString &path);
    void pump();
    void sendNext();
    void handleReply(QNetworkReply *reply, Kind kind, int id);
    static QList<QVariantMap> parseRows(Kind kind, const QJsonObject &body);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_clientId;
    struct { QString accessToken; qint64 expiresAtMs = 0; } m;
    QList<Pending> m_queue;
    QNetworkReply *m_currentReply = nullptr;
    bool m_busy = false;
    qint64 m_rateResetAfterMs = 0;   // Backoff-Fenster
    int m_backoffSec = 0;
};
