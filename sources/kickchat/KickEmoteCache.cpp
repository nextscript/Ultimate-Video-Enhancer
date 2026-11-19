#include "KickEmoteCache.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

static const qint64 kRetryAfterMs = 60 * 1000;    // Fehlerpfad nicht dauernd anfragen
static const char *const kExtensions[] = {".gif", ".png", ".webp", ".jpg"};

static bool validId(const QString &id)
{
    if (id.isEmpty() || id.size() > 64) return false;
    for (const QChar c : id)
        if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != QLatin1Char('_') && c != QLatin1Char('-'))
            return false;
    return true;
}

static QString providerOf(const QString &key)
{
    const int u = key.indexOf(QLatin1Char('_'));
    return u > 0 ? key.left(u) : QString();
}

KickEmoteCache::KickEmoteCache(const QString &dir, QObject *parent)
    : QObject(parent), m_dir(dir), m_nam(new QNetworkAccessManager(this))
{
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

QString KickEmoteCache::imageExtension(const QByteArray &d)
{
    if (d.startsWith(QByteArrayLiteral("GIF8"))) return QStringLiteral(".gif");
    if (d.startsWith(QByteArrayLiteral("\x89PNG"))) return QStringLiteral(".png");
    if (d.startsWith(QByteArrayLiteral("RIFF")) && d.mid(8, 4) == QByteArrayLiteral("WEBP"))
        return QStringLiteral(".webp");
    if (d.startsWith(QByteArrayLiteral("\xFF\xD8\xFF"))) return QStringLiteral(".jpg");
    return QString();
}

bool KickEmoteCache::imageIsAnimated(const QByteArray &d)
{
    if (d.startsWith(QByteArrayLiteral("RIFF")) && d.mid(8, 4) == QByteArrayLiteral("WEBP"))
        return d.size() > 20 && d.mid(12, 4) == QByteArrayLiteral("VP8X") && (d[20] & 0x02);
    if (d.startsWith(QByteArrayLiteral("GIF8"))) {
        if (d.contains(QByteArrayLiteral("NETSCAPE2.0"))) return true;
        QByteArray copy = d;
        QBuffer buf(&copy);
        buf.open(QIODevice::ReadOnly);
        QImageReader reader(&buf, "gif");
        return reader.imageCount() > 1;
    }
    return false;
}

QString KickEmoteCache::remoteUrl(const QString &key)
{
    const QString provider = providerOf(key);
    const QString id = key.mid(provider.size() + 1);
    if (provider == QLatin1String("kick"))
        return QStringLiteral("https://files.kick.com/emotes/%1/fullsize").arg(id);
    if (provider == QLatin1String("7tv"))
        return QStringLiteral("https://cdn.7tv.app/emote/%1/2x.webp").arg(id);
    return QString();
}

QString KickEmoteCache::keyForUrl(const QString &url)
{
    const QUrl u(url);
    if (!u.isValid() || u.scheme() != QLatin1String("https")) return QString();
    const QString key = QStringLiteral("img_")
        + QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
    m_urls.insert(key, url);
    return key;
}

QString KickEmoteCache::localPath(const QString &key) const
{
    const auto known = m_known.constFind(key);
    if (known != m_known.constEnd()) return known.value();
    const QString provider = providerOf(key);
    const QString id = key.mid(provider.size() + 1);
    if (m_dir.isEmpty() || provider.isEmpty() || !validId(id)) return QString();
    const QString base = m_dir + QLatin1Char('/') + provider + QLatin1Char('/') + id;
    for (const char *ext : kExtensions) {
        const QString path = base + QLatin1String(ext);
        if (QFileInfo::exists(path)) {
            m_known.insert(key, path);
            return path;
        }
    }
    return QString();
}

QString KickEmoteCache::cachedFileUrl(const QString &key) const
{
    const QString path = localPath(key);
    return path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString();
}

QString KickEmoteCache::fileUrl(const QString &key)
{
    const QString path = localPath(key);
    if (!path.isEmpty()) {
        if (!m_animated.contains(key)) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly)) m_animated.insert(key, imageIsAnimated(f.readAll()));
        }
        return QUrl::fromLocalFile(path).toString();
    }
    startDownload(key);
    return QString();
}

void KickEmoteCache::startDownload(const QString &key)
{
    if (m_pending.contains(key)) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_failedAt.contains(key) && now - m_failedAt.value(key) < kRetryAfterMs) return;

    const QString provider = providerOf(key);
    const QString id = key.mid(provider.size() + 1);
    if (!validId(id)) return;
    const QString url = provider == QLatin1String("img") ? m_urls.value(key) : remoteUrl(key);
    if (url.isEmpty()) return;

    m_pending.insert(key);
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("UVE-Player-KickChat/1.0"));
    req.setTransferTimeout(20000);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, provider, id]() {
        reply->deleteLater();
        m_pending.remove(key);
        const QByteArray data = reply->readAll();
        const QString ext = imageExtension(data);
        if (reply->error() != QNetworkReply::NoError || ext.isEmpty() || data.size() > 8 * 1024 * 1024) {
            m_failedAt.insert(key, QDateTime::currentMSecsSinceEpoch());
            return;
        }
        const QString folder = m_dir + QLatin1Char('/') + provider;
        QDir().mkpath(folder);
        const QString path = folder + QLatin1Char('/') + id + ext;
        QFile f(path + QStringLiteral(".part"));
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            m_failedAt.insert(key, QDateTime::currentMSecsSinceEpoch());
            return;
        }
        f.close();
        QFile::remove(path);
        if (!QFile::rename(path + QStringLiteral(".part"), path)) {
            m_failedAt.insert(key, QDateTime::currentMSecsSinceEpoch());
            return;
        }
        m_known.insert(key, path);
        m_animated.insert(key, imageIsAnimated(data));
        emit ready(key);
    });
}
