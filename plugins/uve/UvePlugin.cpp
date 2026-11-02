#include "UvePlugin.h"
#include "UveController.h"

#include <QCoreApplication>

namespace uve::fx {

static const char *kId = "org.nextscript.ultimate-video-enhancer";

bool UvePlugin::initialize(uve::IPlayerHost *host, QString *error)
{
    if (!host || host->apiVersion() != uve::kPluginApiVersion) {
        if (error) *error = QCoreApplication::translate("UvePlugin", "Host-API nicht kompatibel");
        return false;
    }
    m_host = host;
    m_controller = new UveController(host, QString::fromLatin1(kId), this);
    host->log(QString::fromLatin1(kId), "info", QStringLiteral("initialisiert (Host %1)").arg(host->hostVersion()));
    return true;
}

void UvePlugin::activate() { if (m_controller) m_controller->setActive(true); }
void UvePlugin::suspend() { if (m_controller) m_controller->setActive(false); }
void UvePlugin::deactivate()
{
    if (!m_controller) return;
    m_controller->setActive(false);
    m_controller->releaseNetwork(); // vor dem Programmende freigeben (Plugin-Instanz lebt länger als QCoreApplication)
}

QObject *UvePlugin::controller() { return m_controller; }
QUrl UvePlugin::panelQml() const { return QUrl(QStringLiteral("qrc:/uve/EnhancementsPanel.qml")); }
QString UvePlugin::panelTitle() const { return QCoreApplication::translate("UvePlugin", "Effekte"); }

void UvePlugin::onMediaChanged(const QString &path) { if (m_controller) m_controller->onMediaChanged(path); }
void UvePlugin::onShaderError(const QString &message) { if (m_controller) m_controller->onShaderError(message); }

} // namespace uve::fx
