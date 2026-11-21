#include "Caches.h"
#include "RadioTypes.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>

namespace uve::radio {

FileCache::FileCache(const QString &dir, qint64 maxBytes, int maxFiles) : m_dir(dir), m_maxBytes(maxBytes), m_maxFiles(maxFiles) {}

QString FileCache::pathFor(const QString &key, const QString &suffix) const
{
    const QString name = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
    return QDir(m_dir).filePath(suffix.isEmpty() ? name : name + QLatin1Char('.') + suffix);
}

QByteArray FileCache::get(const QString &key, int ttlSec, const QString &suffix) const
{
    QFile f(pathFor(key, suffix));
    if (!f.exists()) return {};
    if (ttlSec >= 0 && QFileInfo(f).lastModified().secsTo(QDateTime::currentDateTime()) > ttlSec) return {};
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

QString FileCache::put(const QString &key, const QByteArray &data, const QString &suffix)
{
    if (data.size() > m_maxBytes / 4) return {};
    QDir().mkpath(m_dir);
    const QString path = pathFor(key, suffix);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(data);
    if (!f.commit()) return {};
    if (++m_writesSincePrune >= 10) prune();
    return path;
}

void FileCache::prune()
{
    m_writesSincePrune = 0;
    QFileInfoList files = QDir(m_dir).entryInfoList(QDir::Files, QDir::Time);   // neueste zuerst
    qint64 total = 0;
    int count = 0;
    for (const QFileInfo &fi : files) {
        total += fi.size();
        ++count;
        if (total > m_maxBytes || count > m_maxFiles) QFile::remove(fi.absoluteFilePath());
    }
}

void FileCache::clear()
{
    QDir(m_dir).removeRecursively();
}

qint64 FileCache::sizeBytes() const
{
    qint64 total = 0;
    for (const QFileInfo &fi : QDir(m_dir).entryInfoList(QDir::Files)) total += fi.size();
    return total;
}

CatalogCache::CatalogCache(const QString &dataDir)
    : FileCache(QDir(dataDir).filePath(QStringLiteral("cache/catalog")), 8 * 1024 * 1024, 200)
{
}

// --------------------------------------------------------------------------- Logos
LogoCache::LogoCache(const QString &dataDir, QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent), m_files(QDir(dataDir).filePath(QStringLiteral("cache/logos")), 40 * 1024 * 1024, 600), m_nam(nam)
{
}

QString LogoCache::logoUrl(const QString &remoteUrl)
{
    if (!isStreamUrl(remoteUrl)) return {};
    if (const auto it = m_known.constFind(remoteUrl); it != m_known.constEnd()) return *it;
    for (const char *suffix : {"png", "jpg", "gif", "webp", "svg", "ico"}) {
        const QString p = m_files.pathFor(remoteUrl, QLatin1String(suffix));
        if (QFileInfo::exists(p)) {
            const QString url = QUrl::fromLocalFile(p).toString();
            m_known.insert(remoteUrl, url);
            return url;
        }
    }
    if (!m_failed.contains(remoteUrl) && !m_running.contains(remoteUrl) && !m_queue.contains(remoteUrl)) {
        m_queue << remoteUrl;
        if (m_queue.size() > 200) m_queue.removeFirst();   // nur die zuletzt angezeigten
        pump();
    }
    return {};
}

void LogoCache::pump()
{
    while (m_running.size() < 4 && !m_queue.isEmpty()) {
        const QString remote = m_queue.takeLast();   // zuletzt angefragt = gerade sichtbar
        m_running.insert(remote);
        QNetworkRequest req{QUrl(remote)};
        req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
        req.setTransferTimeout(10000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setMaximumRedirectsAllowed(4);
        QNetworkReply *reply = m_nam->get(req);
        // Logos über 1 MB sind keine Logos
        connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 got, qint64 total) {
            if (got > 1024 * 1024 || total > 1024 * 1024) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, remote] {
            reply->deleteLater();
            m_running.remove(remote);
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->error() == QNetworkReply::NoError && status < 300 ? reply->readAll() : QByteArray();
            const QString suffix = data.isEmpty() ? QString() : suffixFor(reply->header(QNetworkRequest::ContentTypeHeader).toByteArray(), data);
            const QString path = suffix.isEmpty() ? QString() : m_files.put(remote, data, suffix);
            if (path.isEmpty()) m_failed.insert(remote);
            else { m_known.insert(remote, QUrl::fromLocalFile(path).toString()); emit logoReady(remote); }
            pump();
        });
    }
}

QString LogoCache::suffixFor(const QByteArray &contentType, const QByteArray &d) const
{
    // Am Inhalt erkennen – Server liefern oft falsche Typen oder HTML-Fehlerseiten
    if (d.startsWith("\x89PNG")) return QStringLiteral("png");
    if (d.startsWith("\xFF\xD8\xFF")) return QStringLiteral("jpg");
    if (d.startsWith("GIF8")) return QStringLiteral("gif");
    if (d.startsWith("RIFF") && d.mid(8, 4) == "WEBP") return QStringLiteral("webp");
    if (d.startsWith(QByteArray("\x00\x00\x01\x00", 4))) return QStringLiteral("ico");
    const QByteArray head = d.left(512).trimmed().toLower();
    if (contentType.contains("svg") || ((head.startsWith("<?xml") || head.startsWith("<svg")) && head.contains("<svg")))
        return QStringLiteral("svg");
    return {};
}

void LogoCache::clear()
{
    m_files.clear();
    m_known.clear();
    m_failed.clear();
}

} // namespace uve::radio
