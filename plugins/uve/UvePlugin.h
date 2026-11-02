#pragma once
#include "PluginApi.h"

#include <QObject>
#include <QPointer>

namespace uve::fx {

class UveController;

class UvePlugin : public QObject, public uve::IPlayerPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID UVE_PLAYER_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(uve::IPlayerPlugin)

public:
    bool initialize(uve::IPlayerHost *host, QString *error) override;
    void activate() override;
    void suspend() override;
    void deactivate() override;
    QObject *controller() override;
    QUrl panelQml() const override;
    QString panelTitle() const override;
    void onMediaChanged(const QString &path) override;
    void onShaderError(const QString &message) override;

private:
    uve::IPlayerHost *m_host = nullptr;
    QPointer<UveController> m_controller;
};

} // namespace uve::fx
