#include "PluginBridge.h"
#include "PluginManager.h"

#include <QDir>
#include <QJsonObject>
#include <QUrl>

namespace uve {

PluginBridge::PluginBridge(PluginManager *host, const QString &pluginId, const QString &folder, QObject *parent)
    : QObject(parent), m_host(host), m_id(pluginId), m_folder(folder)
{
}

QString PluginBridge::dataDir() const { return m_host->pluginDataDir(m_id); }
QString PluginBridge::language() const { return m_host->language(); }

QVariant PluginBridge::call(const QString &command, const QVariantMap &args)
{
    m_lastError.clear();
    return m_host->call(m_id, command, args, &m_lastError);
}

bool PluginBridge::hasCommand(const QString &command) const { return m_host->hasCommand(command); }
QStringList PluginBridge::commands() const { return m_host->commands(); }
QVariantMap PluginBridge::settings() const { return m_host->loadPluginSettings(m_id).toVariantMap(); }
void PluginBridge::saveSettings(const QVariantMap &settings) { m_host->savePluginSettings(m_id, QJsonObject::fromVariantMap(settings)); }
void PluginBridge::log(const QString &message, const QString &level) { m_host->log(m_id, level, message); }
void PluginBridge::osd(const QString &text, int durationMs) { m_host->showOsd(text, durationMs); }

QUrl PluginBridge::resolve(const QString &relativePath) const
{
    if (m_folder.isEmpty()) return {};
    const QString p = QDir::cleanPath(QDir(m_folder).filePath(relativePath));
    if (!p.startsWith(QDir::cleanPath(m_folder))) return {};   // nur innerhalb des Plugin-Ordners
    return QUrl::fromLocalFile(p);
}

} // namespace uve
