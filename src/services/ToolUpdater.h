#pragma once
#include <QObject>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;

namespace uve {

// Prüft beim Start (nach Einwilligung in den Einstellungen) auf neue Versionen von yt-dlp und libmpv
// und installiert sie auf Wunsch in den Benutzerordner (<Daten>/tools/<werkzeug>/<version>/).
// Mitgelieferte Versionen bleiben unverändert und dienen als Rückfallebene.
class ToolUpdater : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap installed READ installed NOTIFY installedChanged)
public:
    ToolUpdater(const QString &appDir, const QString &dataDir, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QVariantMap installed() const;

    Q_INVOKABLE void check();
    Q_INVOKABLE void install(const QString &tool);
    // Für Tests: Release-Infos vorgeben (sonst aus check()), z. B. mit einer file://-URL
    void setReleaseInfo(const QString &tool, const QVariantMap &info) { m_latest.insert(tool, info); }

    // Kandidaten in absteigender Version (Benutzer-Updates vor mitgelieferter Version)
    static QStringList mpvDllCandidates(const QString &appDir, const QString &dataDir);
    static QString preferredYtdlp(const QString &appDir, const QString &dataDir);
    static QString versionOf(const QString &toolDir);
    static int compareToolVersions(const QString &a, const QString &b);

signals:
    void busyChanged();
    void installedChanged();
    void checkFinished(const QVariantList &updates, const QStringList &errors);
    void progress(const QString &tool, qint64 received, qint64 total);
    void installFinished(const QString &tool, bool ok, const QString &message, bool restartRequired);

private:
    void finishCheckIfDone();
    void setBusy(bool b);
    QNetworkReply *get(const QString &url);
    void installYtdlp(const QVariantMap &info);
    void installMpv(const QVariantMap &info);

    QString m_appDir, m_dataDir;
    QNetworkAccessManager *m_net;
    bool m_busy = false;
    int m_pending = 0;
    QVariantList m_updates;
    QStringList m_errors;
    QVariantMap m_latest; // tool → Release-Infos
};

} // namespace uve
