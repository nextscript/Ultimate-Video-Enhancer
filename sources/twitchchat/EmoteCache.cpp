#include "EmoteCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

static const int kRetryAfterSeconds = 60;   // Fehler-Pfad nicht hitzen

// Nur sichere Zeichen für Dateinamen (Buchstaben, Ziffern, Unterstrich –
// Unterstriche kommen bei Twitch-Channel-IDs vor, z. B. „ep_2222“).
static bool validKeyPart(const QString &part)
{
    if (part.isEmpty()) return false;
    for (const QChar c : part) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('_'))
            return false;
    }
    return true;
}

// Die Endung kommt aus dem Inhalt; alles außer einem bekannten Bildformat
// (z. B. eine HTML-Fehlerseite mit Status 200) gilt als fehlgeschlagen.
static QString imageExtension(const QByteArray &d)
{
    if (d.startsWith(QByteArrayLiteral("GIF8"))) return QStringLiteral(".gif");
    if (d.startsWith(QByteArrayLiteral("\x89PNG"))) return QStringLiteral(".png");
    if (d.startsWith(QByteArrayLiteral("RIFF")) && d.mid(8, 4) == QByteArrayLiteral("WEBP"))
        return QStringLiteral(".webp");
    return QString();
}

static bool imageIsAnimated(const QByteArray &d)
{
    if (d.startsWith(QByteArrayLiteral("GIF8")))
        return d.contains(QByteArrayLiteral("NETSCAPE"));
    // WebP: erweiterter Kopf „VP8X“ an Byte 12, Kennungsbyte an 20, Bit 1 = Animation
    if (d.startsWith(QByteArrayLiteral("RIFF")) && d.mid(8, 4) == QByteArrayLiteral("WEBP"))
        return d.size() > 20 && d.mid(12, 4) == QByteArrayLiteral("VP8X") && (d[20] & 0x02);
    return false;
}

EmoteCache::EmoteCache(const QString &dir, QObject *parent)
    : QObject(parent), m_dir(dir), m_nam(new QNetworkAccessManager(this))
{
}

QString EmoteCache::providerOf(const QString &key)
{
    const int u = key.indexOf(QLatin1Char('_'));
    if (u <= 0) return QString();
    const QString p = key.left(u);
    return (p == QLatin1String("7tv") || p == QLatin1String("bttv") || p == QLatin1String("ffz")
            || p == QLatin1String("cheer") || p == QLatin1String("badge"))
               ? p : QString();
}

QString EmoteCache::remoteUrl(const QString &key, bool animated, int scale)
{
    const QString provider = providerOf(key);
    const QString id = key.mid(provider.size() + 1);
    if (provider == QLatin1String("7tv"))
        return QStringLiteral("https://cdn.7tv.app/emote/%1/%2x.webp").arg(id).arg(scale);
    if (provider == QLatin1String("bttv"))
        return QStringLiteral("https://cdn.betterttv.net/emote/%1/%2x.webp").arg(id).arg(scale);
    if (provider == QLatin1String("ffz"))
        return animated ? QStringLiteral("https://cdn.frankerfacez.com/emote/%1/animated/%2").arg(id).arg(scale)
                        : QStringLiteral("https://cdn.frankerfacez.com/emote/%1/%2").arg(id).arg(scale);
    return QString();
}

QString EmoteCache::localPath(const QString &key) const
{
    if (m_dir.isEmpty() || key.isEmpty()) return QString();
    const QString provider = providerOf(key);
    if (!provider.isEmpty()) {
        const QString id = key.mid(provider.size() + 1);
        if (!validKeyPart(id)) return QString();
        const QString base = m_dir + QLatin1Char('/') + provider + QLatin1Char('/') + id;
        for (const QString &ext : {QStringLiteral(".webp"), QStringLiteral(".gif"), QStringLiteral(".png")}) {
            if (QFile::exists(base + ext)) return base + ext;
        }
        return QString();
    }
    if (!validKeyPart(key)) return QString();
    const QString p = m_dir + QLatin1Char('/') + key + QStringLiteral(".png");
    return QFile::exists(p) ? p : QString();
}

void EmoteCache::request(const QStringList &twitchIds)
{
    if (m_dir.isEmpty()) return;
    for (const QString &id : twitchIds) {
        if (!validKeyPart(id) || !providerOf(id).isEmpty()) continue;  // Drittanbieter: requestThirdParty
        if (!localPath(id).isEmpty() || m_pending.contains(id)) continue;
        const int lastTry = m_failedAt.value(id, 0);
        if (lastTry && QDateTime::currentSecsSinceEpoch() - lastTry < kRetryAfterSeconds) continue;
        startDownload(id);
    }
}

void EmoteCache::requestThirdParty(const QString &key, bool animated)
{
    const QString provider = providerOf(key);
    if (m_dir.isEmpty() || provider.isEmpty() || !validKeyPart(key.mid(provider.size() + 1))) return;
    if (!localPath(key).isEmpty() || m_pending.contains(key)) return;
    const int lastTry = m_failedAt.value(key, 0);
    if (lastTry && QDateTime::currentSecsSinceEpoch() - lastTry < kRetryAfterSeconds) return;

    // WebP zuerst: volle Transparenz und mehr Farben als GIF, Qt spielt beides ab.
    // Die Ausweichadresse greift, falls ein Anbieter die erste Variante nicht führt.
    QStringList urls{ remoteUrl(key, animated, 1) };
    const QString id = key.mid(provider.size() + 1);
    if (provider == QLatin1String("7tv") && animated)
        urls << QStringLiteral("https://cdn.7tv.app/emote/%1/1x.gif").arg(id);
    else if (provider == QLatin1String("bttv"))
        urls << QStringLiteral("https://cdn.betterttv.net/emote/%1/1x").arg(id);
    else if (provider == QLatin1String("ffz") && animated)
        urls << remoteUrl(key, false, 1);
    startRemoteDownload(key, urls);
}

void EmoteCache::requestUrls(const QString &key, const QStringList &urls)
{
    const QString provider = providerOf(key);
    if (m_dir.isEmpty() || provider.isEmpty() || urls.isEmpty() || !validKeyPart(key.mid(provider.size() + 1))) return;
    if (!localPath(key).isEmpty() || m_pending.contains(key)) return;
    const int lastTry = m_failedAt.value(key, 0);
    if (lastTry && QDateTime::currentSecsSinceEpoch() - lastTry < kRetryAfterSeconds) return;
    startRemoteDownload(key, urls);
}

bool EmoteCache::isAnimated(const QString &key)
{
    auto it = m_animated.constFind(key);
    if (it != m_animated.constEnd()) return it.value();
    const QString p = localPath(key);
    if (p.isEmpty()) return false;                     // noch nicht da: später erneut prüfen
    bool a = false;
    QFile f(p);
    if (f.open(QIODevice::ReadOnly)) a = imageIsAnimated(f.readAll());
    m_animated.insert(key, a);
    return a;
}

void EmoteCache::startDownload(const QString &emoteId)
{
    QDir().mkpath(m_dir);
    const QUrl url(QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/%1/default/dark/1.0").arg(emoteId));
    QNetworkReply *reply = m_nam->get(QNetworkRequest(url));
    m_pending.insert(emoteId, true);
    connect(reply, &QNetworkReply::finished, this, [this, reply, emoteId] {
        const bool ok = reply->error() == QNetworkReply::NoError
                        && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        const QByteArray data = ok ? reply->readAll() : QByteArray();
        reply->deleteLater();
        m_pending.remove(emoteId);
        if (!ok || data.isEmpty()) {
            noteFailed(emoteId);
            return;
        }
        m_failedAt.remove(emoteId);
        QFile f(m_dir + QLatin1Char('/') + emoteId + QStringLiteral(".png"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(data);
            f.close();
            // Twitch liefert animierte Emotes als GIF in einer .png-Datei – der Inhalt entscheidet.
            m_animated.insert(emoteId, imageIsAnimated(data));
            emit ready(emoteId);
        }
    });
}

void EmoteCache::startRemoteDownload(const QString &key, QStringList urls)
{
    const QString provider = providerOf(key);
    const QString dir = m_dir + QLatin1Char('/') + provider;
    QDir().mkpath(dir);
    QNetworkRequest req{QUrl(urls.takeFirst())};
    req.setTransferTimeout(20000);
    QNetworkReply *reply = m_nam->get(req);
    m_pending.insert(key, true);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, provider, dir, urls] {
        const bool ok = reply->error() == QNetworkReply::NoError
                        && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        const QByteArray data = ok ? reply->readAll() : QByteArray();
        reply->deleteLater();
        m_pending.remove(key);
        const QString ext = imageExtension(data);
        if (ext.isEmpty()) {
            if (!urls.isEmpty()) startRemoteDownload(key, urls);
            else noteFailed(key);
            return;
        }
        m_failedAt.remove(key);
        QFile f(dir + QLatin1Char('/') + key.mid(provider.size() + 1) + ext);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(data);
            f.close();
            m_animated.insert(key, imageIsAnimated(data));
            emit ready(key);
        }
    });
}

void EmoteCache::noteFailed(const QString &key)
{
    m_failedAt.insert(key, int(QDateTime::currentSecsSinceEpoch()));
}
