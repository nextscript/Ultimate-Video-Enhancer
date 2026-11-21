#include "UveInetRadioPlugin.h"
#include "RadioController.h"

#include <QDir>

namespace uve::radio {

bool UveInetRadioPlugin::initialize(uve::IPlayerHost *host, QString *error)
{
    if (!host) { if (error) *error = QStringLiteral("no host"); return false; }
    m_host = host;
    const QString dataDir = host->pluginDataDir(QLatin1String(kPluginId));
    if (dataDir.isEmpty()) { if (error) *error = QStringLiteral("no data folder"); return false; }
    QDir().mkpath(dataDir);
    m_controller = new RadioController(host, dataDir, this);
    return true;
}

void UveInetRadioPlugin::deactivate()
{
    // Laufende Wiedergabe gehört dem Player und läuft weiter; nur eigene Anfragen beenden
    if (m_controller) m_controller->shutdown();
}

QObject *UveInetRadioPlugin::controller() { return m_controller; }

void UveInetRadioPlugin::onEvent(const QString &name, const QVariantMap &data)
{
    if (name == QLatin1String("action.triggered")) {
        if (data.value(QStringLiteral("id")).toString() == QLatin1String("open") && m_host)
            m_host->call(QLatin1String(kPluginId), QStringLiteral("panel.open"), {{"panel", QStringLiteral("main")}});
        return;
    }
    if (m_controller) m_controller->handleEvent(name, data);
}

} // namespace uve::radio
