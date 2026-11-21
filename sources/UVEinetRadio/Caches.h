#pragma once
// Begrenzte Caches im Datenordner, getrennt von Favoriten und Verlauf löschbar:
//   cache/catalog/  – API-Antworten (Filterlisten, Suchergebnisse) für schnelle Wiederholung und Offline-Betrieb
//   cache/logos/    – Senderlogos
#include <QHash>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

class QNetworkAccessManager;

namespace uve::radio {

// Dateiablage mit Größen- und Anzahlgrenze; älteste Dateien fliegen zuerst raus
class FileCache
{
public:
    FileCache(const QString &dir, qint64 maxBytes, int maxFiles);
    QString dir() const { return m_dir; }
    QString pathFor(const QString &key, const QString &suffix = {}) const;
    // ttlSec < 0: Alter egal
    QByteArray get(const QString &key, int ttlSec, const QString &suffix = {}) const;
    QString put(const QString &key, const QByteArray &data, const QString &suffix = {});
    void prune();
    void clear();
    qint64 sizeBytes() const;

private:
    QString m_dir;
    qint64 m_maxBytes;
    int m_maxFiles;
    int m_writesSincePrune = 0;
};

class CatalogCache : public FileCache
{
public:
    explicit CatalogCache(const QString &dataDir);
};

class LogoCache : public QObject
{
    Q_OBJECT
public:
    LogoCache(const QString &dataDir, QNetworkAccessManager *nam, QObject *parent = nullptr);
    // Lokale Datei-URL des Logos oder leer; fehlt es, wird es im Hintergrund geladen (logoReady)
    QString logoUrl(const QString &remoteUrl);
    void clear();
    qint64 sizeBytes() const { return m_files.sizeBytes(); }

signals:
    void logoReady(const QString &remoteUrl);

private:
    void pump();
    QString suffixFor(const QByteArray &contentType, const QByteArray &data) const;

    FileCache m_files;
    QNetworkAccessManager *m_nam;
    QHash<QString, QString> m_known;    // remote → lokale URL
    QSet<QString> m_failed;             // in dieser Sitzung nicht erneut versuchen
    QStringList m_queue;
    QSet<QString> m_running;
};

} // namespace uve::radio
