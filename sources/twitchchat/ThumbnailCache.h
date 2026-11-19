#pragma once
// Thumbnail-Beschaffung für Kanal-Karten:
//  – {width}x{height}-Platzhalter werden durch echte Größe ersetzt (Helix-Helfer)
//  – asynchrone Downloads über QNetworkAccessManager (kein Blockieren der UI)
//  – LRU im RAM (QCache, begrenzte Speicherlast) + flache Disk-Cache-Dateien
//    dataDir/thumbs/<sha256>.jpg; verwaiste Dateien werden beim Start bereinigt
//  – ausstehende Downloads werden storniert, wenn der Browser-Tab verlässt wird

#include <QCache>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class ThumbnailCache : public QObject
{
    Q_OBJECT
public:
    ThumbnailCache(QNetworkAccessManager *nam, const QString &diskDir, QObject *parent = nullptr);

    // Fertige Datei (file://-URL, leer wenn noch nicht da); lädt bei Bedarf async.
    QString request(const QString &url);
    void cancelPending();

    static QString keyFor(const QString &url);

signals:
    void ready(const QString &url);   // Original-URL (QML aktualisiert die Karte)

private:
    void pruneDisk();

    QNetworkAccessManager *m_nam = nullptr;
    QString m_dir;
    QCache<QString, QPixmap> m_ram{40};          // LRU, ~5 MB
    QHash<QString, QNetworkReply *> m_pending;   // key → laufender Download
    QHash<QString, int> m_failures;              // key → Fehlversuche ( retries aus )
};
