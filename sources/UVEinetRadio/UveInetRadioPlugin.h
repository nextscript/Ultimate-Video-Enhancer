#pragma once
// UVEinetRadio: Internetradio über den Radio-Browser-Katalog, Wiedergabe im vorhandenen libmpv-Player.
#include "PluginApi.h"

#include <QObject>
#include <QPointer>

namespace uve::radio {

class RadioController;

class UveInetRadioPlugin : public QObject, public uve::IPlayerPlugin, public uve::IPluginEvents
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID UVE_PLAYER_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(uve::IPlayerPlugin uve::IPluginEvents)
public:
    bool initialize(uve::IPlayerHost *host, QString *error) override;
    void activate() override {}
    void suspend() override {}
    void deactivate() override;
    QObject *controller() override;
    QUrl panelQml() const override { return {}; }
    QString panelTitle() const override { return {}; }
    void onEvent(const QString &name, const QVariantMap &data) override;

private:
    uve::IPlayerHost *m_host = nullptr;
    QPointer<RadioController> m_controller;
};

} // namespace uve::radio
