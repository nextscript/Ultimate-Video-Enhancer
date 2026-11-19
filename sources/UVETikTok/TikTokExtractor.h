#pragma once
// Ermittelt aus einem TikTok-Link (Video, Kurzlink, LIVE oder Profil) die abspielbare Stream-Adresse.
// Es wird nichts gespeichert: Cookies leben nur im Speicher und nur für die Dauer der Anfrage.
// Es wird kein Code der Seite ausgeführt: Antworten werden nur als Text oder JSON gelesen.
#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace uve::tiktok {

struct Extraction
{
    QString url;            // abzuspielende Adresse
    QString title;
    QString referer;        // für die Wiedergabe
    QString userAgent;
    QString cookie;         // Cookies, ohne die TikTok die Videodatei verweigert (tt_chain_token)
    QStringList subtitles;  // externe Untertitel (WebVTT)
    bool live = false;
    // Profil mit mehreren Videos: weitere Videos (Seitenadressen) für die Wiedergabeliste, nach dem ersten
    QStringList more;
    QStringList moreTitles;
    QList<double> moreDurations;   // Sekunden, 0 = unbekannt
    QString collection;            // "@name", wenn ein Profil als Liste aufgelöst wurde
    int count = 0;                 // Anzahl Videos der Liste (inklusive erstem)
};

struct ProfileVideo
{
    QString id;
    QString user;           // ohne "@"
    QString title;
    double duration = 0;    // Sekunden
    qint64 createTime = 0;  // Unix-Sekunden
};

struct Variant
{
    int height = 0;         // kürzere Bildseite (TikTok ist meist Hochformat), 0 = unbekannt
    qint64 bitrate = 0;
    QString url;
};

enum class LinkKind { Unknown, Video, Live, Profile, Short };

struct Link
{
    LinkKind kind = LinkKind::Unknown;
    QString user;           // ohne "@"
    QString id;             // Video-ID
};

// Einzelne Schritte ohne Netzwerk (getestet in tst_uvetiktok.cpp)
Link parseLink(const QString &url);
QString scriptJson(const QString &html, const QString &scriptId);
int shortSide(int width, int height);
// Getrennte Tonspur (bitrateAudioInfo) mit der höchsten Bitrate, leer wenn keine
QString videoAudio(const QJsonObject &video);
// Bild- und Tonadresse als eine mpv-EDL-Quelle
QString mergeAudioVideo(const QString &videoUrl, const QString &audioUrl);
QList<Variant> videoVariants(const QJsonObject &video);
QString pickVariant(const QList<Variant> &variants, int maxHeight);
QStringList videoSubtitles(const QJsonObject &video);
QString videoTitle(const QJsonObject &item);
// stream_data (JSON-Text aus pull_data) → Varianten; format "flv" oder "hls"
QList<Variant> liveVariants(const QString &streamData, const QString &format);
// Ältere Antwort von webcast/room/info: stream_url.{flv_pull_url,hls_pull_url_map}
QList<Variant> legacyLiveVariants(const QJsonObject &streamUrl, const QString &format);
// Profilseite: webapp.user-detail → secUid und Anzahl Videos (-1 = unbekannt)
QString profileSecUid(const QString &html, int *videoCount = nullptr);
// Antwort von api/creator/item_list → Videos (ohne Foto-Slideshows); fallback = Name, falls author fehlt
QList<ProfileVideo> profileVideos(const QJsonObject &response, const QString &fallbackUser);
// Nächster Zeitstempel (ms) zum Weiterblättern, 0 = Ende
qint64 nextProfileCursor(const QJsonObject &response, qint64 oldCursor);
QString videoPageUrl(const QString &user, const QString &id);

class ExtractJob : public QObject
{
    Q_OBJECT
public:
    // maxHeight: höchste gewünschte Bildhöhe, 0 = beste. liveFormat: "flv" (geringe Verzögerung) oder "hls"
    ExtractJob(const QString &url, int maxHeight, const QString &liveFormat, QObject *parent = nullptr);

    void start();

signals:
    void finished(const uve::tiktok::Extraction &result);
    // definitive = true: Ergebnis steht fest (z. B. Kanal nicht live), ein Versuch mit yt-dlp bringt nichts
    void failed(const QString &error, bool definitive);

private:
    struct Response
    {
        int status = 0;
        QString text;
        QUrl finalUrl;
    };
    using Callback = std::function<void(const Response &)>;

    void get(const QString &url, const Callback &cb, bool lenient = false);
    void fail(const QString &error, bool definitive = false);
    void finish();

    void handle(const Link &link);
    void resolveShort();
    void video(const Link &link);
    void live(const QString &user, bool explicitLive);
    void liveFromRoomInfo(const QString &roomId);
    void profile(const QString &user);
    void profilePage(const QString &user, const QString &secUid, qint64 cursor, int page);
    void profileDone(const QString &user);

    QNetworkAccessManager *m_nam;
    QString m_url;
    int m_maxHeight;
    QString m_liveFormat;
    bool m_done = false;
    Extraction m_result;
    QList<ProfileVideo> m_profileVideos;
    int m_profileCount = -1;
    QString m_profileError;
};

} // namespace uve::tiktok
