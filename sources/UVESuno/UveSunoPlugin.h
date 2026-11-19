#pragma once
// UVESuno: übernimmt Links von suno.com (Songs, Playlists, Profile, Kurzlinks) und spielt sie als Stream ab.
// Ab UVE Player 1.2.2 löst es den nächsten Titel vorab auf (capability "media-preload") – nahtlose Übergänge.
#include "PluginApi.h"
#include "SunoNet.h"

#include <QHash>
#include <QObject>

namespace uve::suno {

class UveSunoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString language READ language NOTIFY changed)
public:
    UveSunoController(uve::IPlayerHost *host, QObject *parent = nullptr);

    QString status() const { return m_status; }
    void setStatus(const QString &status);
    QString language() const { return m_language; }
    void setLanguage(const QString &language);
    QString text(const char *de, const char *en) const;

signals:
    void changed();

private:
    QString m_status;
    QString m_language = QStringLiteral("de");
};

class UveSunoPlugin : public QObject, public uve::IPlayerPlugin, public uve::IPluginEvents
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
    void resolve(quint64 requestId, const QString &url, bool preload);
    void answer(quint64 requestId, const QString &url, const QString &error = {}, const QString &title = {});
    void setTitle(const QString &title);

    uve::IPlayerHost *m_host = nullptr;
    UveSunoController *m_controller = nullptr;
    QHash<quint64, ResolveJob *> m_jobs;
    QString m_lastResolved;
    bool m_titleSet = false;
};

} // namespace uve::suno
