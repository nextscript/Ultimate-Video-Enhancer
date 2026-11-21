#include "WatchPartyPlugin.h"

#include "WatchPartyController.h"

namespace uve::watchparty {

bool WatchPartyPlugin::initialize(uve::IPlayerHost *host, QString *error)
{
    if (!host || !host->hasCommand(QStringLiteral("player.state")) || !host->hasCommand(QStringLiteral("mpv.get"))) {
        if (error) *error = QStringLiteral("player commands missing (player.state, mpv.get)");
        return false;
    }
    m_host = host;
    m_controller = new WatchPartyController(host, this);
    const QString lang = host->call(QString::fromLatin1(WatchPartyController::kPluginId), QStringLiteral("app.info")).toMap().value("language").toString();
    if (!lang.isEmpty()) m_controller->setLanguage(lang);
    return true;
}

void WatchPartyPlugin::activate()
{
    if (m_controller) m_controller->setActive(true);
}

void WatchPartyPlugin::suspend()
{
    if (m_controller) m_controller->setActive(false);
}

void WatchPartyPlugin::deactivate()
{
    // Leaving the room closes peer connections and frees the temporary cache
    if (m_controller) { m_controller->setActive(false); m_controller->shutdown(); }
}

QObject *WatchPartyPlugin::controller()
{
    return m_controller;
}

void WatchPartyPlugin::onEvent(const QString &name, const QVariantMap &data)
{
    if (m_controller) m_controller->onEvent(name, data);
}

} // namespace uve::watchparty
