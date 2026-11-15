#pragma once
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantMap>
#include <functional>

class QNetworkAccessManager;

namespace uve {

class PluginManager;
class ResumableDownload;

// Online-Updates für installierte Plugins aus dem Zweig "uve-player-plugins" des GitHub-Repos.
// Dort liegen Pakete als "<Name>-<Version>.zip" (z. B. kickchat-1.3.1.zip).
//
// GitHub begrenzt api.github.com auf 60 Anfragen/Stunde je IP. Deshalb kommt die API nur als letzter Rückfall vor:
//   Commit des Zweigs:  <repo>.git/info/refs (Git-Endpunkt, kein API-Limit)        Rückfall: REST-API
//   Dateiliste:         data.jsdelivr.com/…@<commit> (mit SHA-256 je Datei)          Rückfall: REST-API git/trees/<commit>
//   Paket:              raw.githubusercontent.com/…/<commit>/… → cdn.jsdelivr.net → github.com/…/raw/…
// Die Liste wird je Commit auf der Festplatte zwischengespeichert: Eine Prüfung kostet meist nur die eine refs-Anfrage,
// innerhalb von 30 Minuten gar keine. Fällt alles aus, gilt die zuletzt gespeicherte Liste weiter.
// Vor der Installation werden Prüfsumme und Plugin-ID im Paket mit dem installierten Plugin verglichen.
class PluginUpdater : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    // Plugin-ID → {state, latestVersion, file, size, error, received, total, installedVersion}
    //   state: checking | current | available | unknown (nicht im Katalog) | downloading | installed | error
    Q_PROPERTY(QVariantMap status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)          // Fehler der letzten Prüfung (leer = ok)
    Q_PROPERTY(QString lastCheck READ lastCheck NOTIFY changed)  // Zeitpunkt der verwendeten Liste (lokal, lesbar)
public:
    struct CatalogEntry
    {
        QString file;      // Dateiname im Zweig
        QString key;       // normalisierter Name (z. B. "kickchat")
        QString version;
        qint64 size = -1;
        QString sha256;    // hex, aus jsDelivr (leer, wenn unbekannt)
        QString gitSha;    // Git-Blob-SHA-1, aus der REST-API (leer, wenn unbekannt)
    };

    PluginUpdater(PluginManager *plugins, const QString &dataDir, QObject *parent = nullptr);

    bool checking() const { return m_checking; }
    QVariantMap status() const { return m_status; }
    QString error() const { return m_error; }
    QString lastCheck() const;

    // force = false: eine höchstens 30 Minuten alte Liste genügt. Auch mit force höchstens eine Anfrage je Minute.
    Q_INVOKABLE void check(bool force = false);
    Q_INVOKABLE void install(const QString &pluginId);
    Q_INVOKABLE void cancel(const QString &pluginId);

    // Antwort von jsDelivr ({"files":[{name,hash(base64 SHA-256),size}]}) oder der REST-API ({"tree":[{path,sha,size}]})
    static QList<CatalogEntry> parseCatalog(const QByteArray &json);
    // "kickchat-1.3.1.zip" → key "kickchat", version "1.3.1"; false, wenn der Name nicht passt
    static bool splitPackageName(const QString &file, QString *key, QString *version);
    static QString normalizeKey(const QString &s);
    // Neueste passende Version für ein Plugin (Vergleich mit Name, letztem ID-Teil und ganzer ID); -1 = keine
    static int findEntry(const QList<CatalogEntry> &catalog, const QString &id, const QString &name);
    static QString gitBlobSha(const QByteArray &data);
    // ID aus der manifest.json eines ZIP-Pakets (leer = nicht lesbar)
    static QString packageId(const QByteArray &zip);

    // Für Tests
    void setRepository(const QString &owner, const QString &repo, const QString &branch);
    void setEndpoints(const QStringList &refsUrls, const QStringList &listUrlTemplates, const QStringList &fileUrlTemplates);
    void setMinInterval(int seconds) { m_minIntervalSec = seconds; }

signals:
    void changed();
    void checkFinished();
    void installed(const QVariantMap &info);              // Ergebnis von PluginManager::installPackageInfo
    void installFailed(const QString &pluginId, const QString &message);

private:
    using FetchDone = std::function<void(bool ok, int status, const QByteArray &body)>;
    void fetch(const QStringList &urls, FetchDone done);
    void onCommit(const QString &commit);
    void loadList(const QString &ref, const QStringList &templates, std::function<void(bool)> done);
    void finishCheck(const QString &error);
    void recompute();
    void setState(const QString &id, const QVariantMap &values);
    void tryDownload(const QString &id, int urlIndex);
    void onDownloaded(const QString &id, const QString &file);
    QString expand(const QString &tmpl, const QString &ref, const QString &file = {}) const;
    QString cacheFile() const;
    void loadCache();
    void saveCache() const;

    PluginManager *m_plugins;
    QString m_dataDir;
    QNetworkAccessManager *m_net;
    QString m_owner, m_repo, m_branch;
    QStringList m_refsUrls, m_listTemplates, m_fileTemplates;
    int m_minIntervalSec = 60;

    bool m_checking = false;
    QString m_error;
    QVariantMap m_status;
    QList<CatalogEntry> m_catalog;
    QString m_commit;              // Commit der Liste (leer = Zweigname verwendet)
    QDateTime m_fetchedAt;         // Zeitpunkt, zu dem die Liste zuletzt bestätigt wurde
    QDateTime m_lastAttempt;
    QHash<QString, QPointer<ResumableDownload>> m_downloads;
    QSet<QString> m_cancelled;
    QHash<QString, QString> m_installedVersions;   // in dieser Sitzung installiert (aktiv nach Neustart)
};

} // namespace uve
