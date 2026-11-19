#pragma once
// UVETikTok: übernimmt TikTok-Links (Videos, Kurzlinks, LIVE, Profile) und lässt mpv den Stream direkt abspielen.
// Die Wiedergabe schreibt keine Datei: mpv puffert im Arbeitsspeicher, nach dem Schließen bleibt nichts übrig.
#include "PluginApi.h"
#include "TikTokExtractor.h"

#include <QHash>
#include <QObject>

namespace uve::tiktok {

class UveTikTokController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int maxHeight READ maxHeight WRITE setMaxHeight NOTIFY changed)
    Q_PROPERTY(QString liveFormat READ liveFormat WRITE setLiveFormat NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString language READ language NOTIFY changed)
public:
    explicit UveTikTokController(uve::IPlayerHost *host, QObject *parent = nullptr);

    int maxHeight() const { return m_maxHeight; }
    void setMaxHeight(int height);
    QString liveFormat() const { return m_liveFormat; }
    void setLiveFormat(const QString &format);
    QString status() const { return m_status; }
    void setStatus(const QString &status);
    QString language() const { return m_language; }
    void setLanguage(const QString &language);

signals:
    void changed();

private:
    void save();

    uve::IPlayerHost *m_host;
    int m_maxHeight = 0;
    QString m_liveFormat = QStringLiteral("flv");
    QString m_status;
    QString m_language = QStringLiteral("de");
};

class UveTikTokPlugin : public QObject, public uve::IPlayerPlugin, public uve::IPluginEvents
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
    QString text(const char *de, const char *en) const;
    void resolve(quint64 requestId, const QString &url);
    void answer(quint64 requestId, const QString &url, const QString &error = {}, const QString &title = {});
    void applyPlayback(const Extraction &result);
    void resetPlayback();

    uve::IPlayerHost *m_host = nullptr;
    UveTikTokController *m_controller = nullptr;
    QHash<quint64, ExtractJob *> m_jobs;
    QHash<quint64, QString> m_jobUrls;
    QString m_lastResolved;
    QString m_savedUserAgent;
    bool m_playbackChanged = false;
};

} // namespace uve::tiktok
