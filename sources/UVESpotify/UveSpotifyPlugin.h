#pragma once
// UVESpotify: übernimmt Spotify-Links (Titel, Alben, Playlists, Künstler, Lieblingssongs) und spielt den jeweils passenden
// Titel von YouTube ab. Spotify-Ton selbst ist DRM-geschützt und wird nicht angefasst. Mit Anmeldung (OAuth/PKCE, eigene
// Client-ID der UVESpotify-App) zeigt die Seite die eigenen Playlists. Es wird nichts auf der Festplatte gespeichert außer der Anmeldung.
#include "PluginApi.h"
#include "SpotifyNet.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QObject>
#include <QPointer>
#include <QVariantList>

namespace uve::spotify {

class UveSpotifyController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY changed)
    Q_PROPERTY(bool loginPending READ loginPending NOTIFY changed)
    Q_PROPERTY(QString userName READ userName NOTIFY changed)
    Q_PROPERTY(QString product READ product NOTIFY changed)
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY playlistsChanged)
    Q_PROPERTY(bool playlistsLoading READ playlistsLoading NOTIFY playlistsChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString language READ language NOTIFY changed)
public:
    UveSpotifyController(uve::IPlayerHost *host, QObject *parent = nullptr);

    SpotifyAccount *account() const { return m_account; }
    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    bool loggedIn() const { return m_account->loggedIn(); }
    bool loginPending() const { return m_account->loginPending(); }
    QString userName() const { return m_account->userName(); }
    QString product() const { return m_account->product(); }
    QVariantList playlists() const { return m_playlists; }
    bool playlistsLoading() const { return m_playlistsLoading; }
    QString status() const { return m_status; }
    void setStatus(const QString &status);
    QString language() const { return m_language; }
    void setLanguage(const QString &language);
    QString text(const char *de, const char *en) const;

    Q_INVOKABLE void login();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void refreshPlaylists();
    Q_INVOKABLE void play(const QString &url);

signals:
    void changed();
    void playlistsChanged();

private:
    void save();

    uve::IPlayerHost *m_host;
    SpotifyAccount *m_account;
    QString m_mode = QStringLiteral("audio");
    QVariantList m_playlists;
    bool m_playlistsLoading = false;
    QString m_status;
    QString m_language = QStringLiteral("de");
    QPointer<ResolveJob> m_listJob;
};

class UveSpotifyPlugin : public QObject, public uve::IPlayerPlugin, public uve::IPluginEvents
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID UVE_PLAYER_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(uve::IPlayerPlugin uve::IPluginEvents)
public:
    bool initialize(uve::IPlayerHost *host, QString *error) override;
    void activate() override {}
    void suspend() override {}
    void deactivate() override;
    QObject *controller() override { return m_controller; }
    QUrl panelQml() const override { return {}; }
    QString panelTitle() const override { return {}; }
    void onEvent(const QString &name, const QVariantMap &data) override;

private:
    void resolve(quint64 requestId, const QString &url);
    void answer(quint64 requestId, const QString &url, const QString &error = {}, const QString &title = {});
    // Player ab 1.2.2 (capability "media-preload"): nächsten Titel vorab liefern, ohne die laufende Wiedergabe zu ändern
    void preload(quint64 requestId, const QString &url);
    void flushPreloads(const QString &trackId, const QString &error = {});
    void applyPlayback(const Resolution &result);
    void resetPlayback();
    // Nahtloser Übergang: nächsten Titel der Wiedergabeliste schon während der Wiedergabe bis zur Stream-Adresse auflösen
    void prefetchNext();
    void prefetch(const QString &trackId);
    void extractStream(const QString &trackId, const Resolution &resolved, const QString &mode);
    bool takeCached(const QString &url, Resolution *out);

    struct CachedStream
    {
        Resolution resolution;   // url = direkte Stream-Adresse (oder YouTube-Seite, wenn yt-dlp fehlt)
        QString mode;
        QDateTime validUntil;
    };

    uve::IPlayerHost *m_host = nullptr;
    UveSpotifyController *m_controller = nullptr;
    QHash<quint64, ResolveJob *> m_jobs;
    QHash<quint64, QString> m_jobUrls;
    QString m_lastResolved;
    QString m_savedFormat;
    bool m_playbackChanged = false;
    bool m_formatChanged = false;
    QHash<QString, CachedStream> m_streams;   // Spotify-Track-ID → vorgeladen
    QSet<QString> m_prefetching;
    QHash<QString, QList<quint64>> m_preloadWaiting;   // Track-ID → offene Vorab-Anfragen des Players
};

} // namespace uve::spotify
