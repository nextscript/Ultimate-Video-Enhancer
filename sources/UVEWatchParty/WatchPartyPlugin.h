#pragma once
// UVE Watch Party: watch the same film in sync with friends. Rooms with copyable ID and optional password,
// signaling over WSS to a free hosted service, video blocks peer-to-peer over WebRTC – no port forwarding.
#include "PluginApi.h"

#include <QObject>

namespace uve::watchparty {

class WatchPartyController;

class WatchPartyPlugin : public QObject, public uve::IPlayerPlugin, public uve::IPluginEvents
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID UVE_PLAYER_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(uve::IPlayerPlugin uve::IPluginEvents)
public:
    bool initialize(uve::IPlayerHost *host, QString *error) override;
    void activate() override;
    void suspend() override;
    void deactivate() override;
    QObject *controller() override;
    QUrl panelQml() const override { return {}; }
    QString panelTitle() const override { return {}; }
    void onEvent(const QString &name, const QVariantMap &data) override;

private:
    uve::IPlayerHost *m_host = nullptr;
    WatchPartyController *m_controller = nullptr;
};

} // namespace uve::watchparty
