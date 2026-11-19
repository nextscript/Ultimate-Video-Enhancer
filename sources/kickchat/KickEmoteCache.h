#pragma once
// Lädt Bilder (Kick-Emotes, 7TV-Emotes, Abzeichen) einmal herunter und hält sie im
// Datenordner des Plugins. Der Schlüssel nennt den Anbieter:
//   „kick_<id>“   → https://files.kick.com/emotes/<id>/fullsize   Datei emotes/kick/<id>.<ext>
//   „7tv_<id>“    → https://cdn.7tv.app/emote/<id>/2x.webp        Datei emotes/7tv/<id>.<ext>
//   „img_<hash>“  → beliebige Adresse (Abzeichen)                 Datei emotes/img/<hash>.<ext>
// Die Endung kommt aus dem Dateiinhalt (gif/png/webp/jpg); animiert wird ebenfalls
// am Inhalt erkannt (GIF mit mehreren Bildern, WebP mit Animations-Kennung).

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class QNetworkAccessManager;

class KickEmoteCache : public QObject
{
    Q_OBJECT
public:
    explicit KickEmoteCache(const QString &dir, QObject *parent = nullptr);

    // file:-URL der lokalen Datei; leer, solange sie fehlt (dann startet der Download).
    QString fileUrl(const QString &key);
    // Wie fileUrl, aber ohne Download anzustoßen.
    QString cachedFileUrl(const QString &key) const;
    bool isAnimated(const QString &key) const { return m_animated.value(key, false); }

    // Schlüssel für eine freie Adresse (Abzeichen), merkt sich die Adresse.
    QString keyForUrl(const QString &url);
    static QString remoteUrl(const QString &key);

    static QString imageExtension(const QByteArray &data);
    static bool imageIsAnimated(const QByteArray &data);

signals:
    void ready(const QString &key);

private:
    QString localPath(const QString &key) const;
    void startDownload(const QString &key);

    QString m_dir;
    QNetworkAccessManager *m_nam;
    QSet<QString> m_pending;
    QHash<QString, qint64> m_failedAt;
    QHash<QString, bool> m_animated;
    mutable QHash<QString, QString> m_known;     // key → lokaler Pfad
    QHash<QString, QString> m_urls;              // img_<hash> → Adresse
};
