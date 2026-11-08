#pragma once
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace uve {

class ResumableDownload;

// Update des Players selbst über GitHub – bewusst ohne das begrenzte api.github.com (60 Anfragen/Stunde je IP):
//   Commits und Tags:  Git-Endpunkt <repo>.git/info/refs (Rückfall: REST-API)
//   Commit-Liste:      Atom-Feed <repo>/commits/<zweig>.atom
//   Release-Dateien:   <repo>/releases/download/<tag>/… (SHA256SUMS.txt legt fest, was es gibt)
// Installierbar ist nur ein Release mit fertigen Paketen; neuere Commits ohne Release werden nur angezeigt.
class AppUpdater : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString installKind READ installKind CONSTANT)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString currentCommit READ currentCommit CONSTANT)
public:
    struct Build { QString version, commit, commitTime; };

    AppUpdater(const Build &build, const QString &appDir, const QString &dataDir, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString installKind() const { return m_kind; }
    QString currentVersion() const { return m_build.version; }
    QString currentCommit() const { return m_build.commit; }

    Q_INVOKABLE void check();
    Q_INVOKABLE void install();          // herunterladen, prüfen, Installation starten
    Q_INVOKABLE void cancelInstall();
    Q_INVOKABLE void openReleasePage() const;

    // setup | portable-exe | portable-zip | appimage | dmg | manual (nur Release-Seite öffnen)
    static QString detectInstallKind(const QString &appDir);
    static QString assetName(const QString &kind, const QString &version, const QString &cpuArch);
    // Antwort von info/refs (pkt-line) oder der REST-API (JSON-Liste): Ref-Name → Commit
    static QHash<QString, QString> parseRefs(const QByteArray &data);
    // Release-Tags "uve-player-v<version>": Version → Commit, neueste zuerst über sortedVersions
    static QStringList newerReleaseVersions(const QHash<QString, QString> &refs, const QString &currentVersion);
    static QVariantList parseCommitFeed(const QByteArray &atom);
    // Commits im Feed, die neuer sind als der eigene Stand; *truncated: eigener Commit nicht im Feed gefunden
    static QVariantList commitsSince(const QVariantList &feed, const QString &commit, const QString &commitTime, bool *truncated = nullptr);
    static QHash<QString, QString> parseSha256Sums(const QByteArray &text);

    // Für Tests
    void setInstallKind(const QString &kind) { m_kind = kind; }
    void setRepositoryBase(const QString &base) { m_repo = base; }
    void setRefsFallbackUrl(const QString &url) { m_refsFallback = url; }
    void setLaunchEnabled(bool enabled) { m_launchEnabled = enabled; }

signals:
    void busyChanged();
    void checkFinished(const QVariantMap &result, const QString &error);
    void progress(qint64 received, qint64 total);
    void retrying(int attempt, int maxAttempts, int delaySeconds);
    void installFailed(const QString &message);
    void packageReady(const QString &file);          // geladen und Prüfsumme stimmt
    void installHandedOff(const QString &message);   // z. B. macOS: Image wurde geöffnet, der Player läuft weiter
    void quitRequested();                            // Hilfsskript wartet auf das Ende dieses Prozesses
    void restartRequested();                         // Paket ist ersetzt, Neustart reicht

private:
    using FetchDone = std::function<void(bool ok, int status, const QByteArray &body)>;
    void fetch(const QStringList &urls, FetchDone done, int attempt = 0);
    void onRefs(const QByteArray &body);
    void probeRelease();
    void onReleaseFound(const QString &version, const QHash<QString, QString> &sums, qint64 size);
    void fetchCommits();
    void finishCheck();
    void setBusy(bool b);
    void startDownload(bool retryAfterMismatch);
    void onDownloaded(bool retryAfterMismatch);
    void launch(const QString &file);
    void launchWindowsHelper(const QString &file);
    void cleanupUpdateFolder(const QString &keepVersion);
    QString updateDir() const;

    Build m_build;
    QString m_appDir, m_dataDir;
    QString m_kind;
    QString m_repo;
    QString m_refsFallback;
    QNetworkAccessManager *m_net;
    QPointer<ResumableDownload> m_download;
    bool m_busy = false;
    bool m_launchEnabled = true;

    QHash<QString, QString> m_refs;
    QStringList m_candidates;
    QVariantMap m_result;
    QString m_error;
    QString m_sha256;
};

} // namespace uve
