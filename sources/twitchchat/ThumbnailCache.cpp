#include "ThumbnailCache.h"
#include "TwitchHelix.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

ThumbnailCache::ThumbnailCache(QNetworkAccessManager *nam, const QString &diskDir, QObject *parent)
    : QObject(parent), m_nam(nam), m_dir(diskDir)
{
    QDir().mkpath(m_dir);
    pruneDisk();
}

QString ThumbnailCache::keyFor(const QString &url)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString ThumbnailCache::request(const QString &url)
{
    if (url.isEmpty())
        return {};
    const QString key = keyFor(url);
    const QString file = m_dir + QLatin1Char('/') + key + QStringLiteral(".jpg");

    if (QFile::exists(file)) {
        if (QPixmap *px = m_ram.object(key)) {           // RAM-Treffer: bleibt LRU-oben
            Q_UNUSED(px)
            return QUrl::fromLocalFile(file).toString();
        }
        QPixmap px;
        if (px.load(file)) {
            m_ram.insert(key, new QPixmap(px));
            return QUrl::fromLocalFile(file).toString();
        }
        QFile::remove(file);                             // kaputter Cache-Eintrag
    }

    if (m_pending.contains(key))
        return {};                                       // läuft bereits
    if (m_failures.value(key, 0) >= 3)
        return {};                                       // dauerhaft fehlgeschlagen

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);   // Qt6-Nachfolger
    QNetworkReply *reply = m_nam->get(req);
    m_pending.insert(key, reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, file, url] {
        m_pending.remove(key);
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QByteArray bytes = reply->readAll();
        reply->deleteLater();
        if (ok && bytes.size() > 100) {
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile f(file);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(bytes);
                f.close();
            }
            QPixmap px;
            if (px.loadFromData(bytes))
                m_ram.insert(key, new QPixmap(px));
            emit ready(url);
        } else {
            m_failures[key] = m_failures.value(key, 0) + 1;
        }
    });
    return {};   // Karte zeigt Platzhalter; ready() aktualisiert sie
}

void ThumbnailCache::cancelPending()
{
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
        disconnect(it.value(), nullptr, this, nullptr);
        it.value()->abort();
        it.value()->deleteLater();
    }
    m_pending.clear();
}

// Beim Start: Dateien ohne Bezug entfernen (älter 30 Tage, Obergrenze 400 Dateien)
void ThumbnailCache::pruneDisk()
{
    QDir d(m_dir);
    const QFileInfoList entries = d.entryInfoList({QStringLiteral("*.jpg")}, QDir::Files,
                                                  QDir::Time /*neueste zuerst*/);
    const qint64 maxAgeDays = 30;
    const QDate today = QDate::currentDate();
    int kept = 0;
    for (const QFileInfo &fi : entries) {
        if (kept >= 400 || fi.lastModified().date().daysTo(today) > maxAgeDays) {
            QFile::remove(fi.absoluteFilePath());
        } else {
            ++kept;
        }
    }
}
