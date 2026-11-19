#pragma once
// Lädt Emoticon-Bilder und hält sie im Plugin-Datenordner (host->pluginDataDir) –
// ein Emote wird genau einmal geladen.
// Der Anbieter steckt im Schlüssel:
//   „<id>“          Twitch   → static-cdn.jtvnw.net, Datei emotes/<id>.png
//                              (animierte Twitch-Emotes enthalten trotzdem GIF-Bytes –
//                               die Dateiendung .png ist dort Irreführung)
//   „7tv_<id>“      7TV      → cdn.7tv.app,           Datei emotes/7tv/<id>.<format>
//   „bttv_<id>“     BTTV     → cdn.betterttv.net,     Datei emotes/bttv/<id>.<format>
//   „ffz_<id>“      FFZ      → cdn.frankerfacez.com,  Datei emotes/ffz/<id>.<format>
// Drittanbieter-Dateien bekommen ihre Endung aus dem Dateiinhalt (webp/gif/png).

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;

class EmoteCache : public QObject
{
    Q_OBJECT
public:
    explicit EmoteCache(const QString &dir, QObject *parent = nullptr);

    // Leerer String, wenn (noch) nicht im Cache.
    QString localPath(const QString &key) const;
    // Startet Downloads für alle noch fehlenden Twitch-IDs (Cache wird übersprungen).
    void request(const QStringList &twitchIds);
    // Drittanbieter-Emote laden. animated kommt aus dem Katalog des Anbieters.
    void requestThirdParty(const QString &key, bool animated);
    // Bild unter fester Adresse laden (Cheer-Emotes „cheer_<prefix>_<stufe>“, Abzeichen „badge_<id>“)
    void requestUrls(const QString &key, const QStringList &urls);
    // Bewegt sich das Bild? Immer aus dem Dateiinhalt ermittelt (GIF mit NETSCAPE-
    // Schleife oder WebP mit Animations-Kennung im VP8X-Kopf).
    bool isAnimated(const QString &key);

    // „7tv“ | „bttv“ | „ffz“ | „cheer“ | „badge“ für Drittanbieter-Schlüssel, sonst leer (= Twitch).
    static QString providerOf(const QString &key);
    // Bild-URL beim Anbieter; scale 1 = Chatzeile, 2 = Browser-Kachel.
    static QString remoteUrl(const QString &key, bool animated, int scale);

signals:
    void ready(const QString &key);

private:
    void startDownload(const QString &emoteId);
    void startRemoteDownload(const QString &key, QStringList urls);
    void noteFailed(const QString &key);

    QString m_dir;
    QNetworkAccessManager *m_nam;
    QHash<QString, bool> m_pending;    // läuft bereits
    QHash<QString, int> m_failedAt;    // erneuter Versuch erst nach etwas Zeit
    QHash<QString, bool> m_animated;   // key → animiert (Dateiinhalt)
};
