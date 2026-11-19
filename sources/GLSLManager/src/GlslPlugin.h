#pragma once
#include "GlslController.h"
#include "PluginApi.h"

namespace glslmgr {

class GlslPlugin : public QObject, public uve::IPlayerPlugin, public uve::IPluginEvents
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID UVE_PLAYER_PLUGIN_IID FILE "../manifest.json")
    Q_INTERFACES(uve::IPlayerPlugin uve::IPluginEvents)
public:
    bool initialize(uve::IPlayerHost *host, QString *error) override;
    void activate() override { if (m_ctrl) m_ctrl->activate(); }
    void suspend() override { if (m_ctrl) m_ctrl->suspend(); }
    void deactivate() override { if (m_ctrl) m_ctrl->suspend(); }
    QObject *controller() override { return m_ctrl; }
    QUrl panelQml() const override { return {}; }            // Seiten kommen aus contributes.panels
    QString panelTitle() const override { return QStringLiteral("GLSL"); }
    void onShaderError(const QString &message) override { if (m_ctrl) m_ctrl->onShaderError(message); }
    void onEvent(const QString &name, const QVariantMap &data) override { if (m_ctrl) m_ctrl->onEvent(name, data); }

private:
    GlslController *m_ctrl = nullptr;
};

} // namespace glslmgr
