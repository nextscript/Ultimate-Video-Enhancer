#pragma once
// Pro Plugin ein Brückenobjekt für QML: Plugin-Seiten (Panels) und reine QML-Plugins erreichen darüber
// den Befehls- und Ereignisweg des Hosts (siehe PluginApi.h / docs/PLUGIN_API.md).
//
//   plugin.call("player.load", { url: "https://…" })
//   plugin.hasCommand("player.load")
//   Connections { target: plugin; function onEvent(name, data) { … } }
#include <QObject>
#include <QVariantMap>

namespace uve {

class PluginManager;

class PluginBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString folder READ folder CONSTANT)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
public:
    PluginBridge(PluginManager *host, const QString &pluginId, const QString &folder, QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString folder() const { return m_folder; }
    QString dataDir() const;
    QString language() const;

    Q_INVOKABLE QVariant call(const QString &command, const QVariantMap &args = {});
    Q_INVOKABLE bool hasCommand(const QString &command) const;
    Q_INVOKABLE QStringList commands() const;
    Q_INVOKABLE QVariantMap settings() const;
    Q_INVOKABLE void saveSettings(const QVariantMap &settings);
    Q_INVOKABLE void log(const QString &message, const QString &level = QStringLiteral("info"));
    Q_INVOKABLE void osd(const QString &text, int durationMs = 1500);
    Q_INVOKABLE QString lastError() const { return m_lastError; }
    // Datei aus dem Plugin-Ordner als URL (für Bilder/QML im eigenen Ordner)
    Q_INVOKABLE QUrl resolve(const QString &relativePath) const;

    void deliver(const QString &name, const QVariantMap &data) { emit event(name, data); }
    void notifyLanguageChanged() { emit languageChanged(); }

signals:
    void event(const QString &name, const QVariantMap &data);
    void languageChanged();

private:
    PluginManager *m_host;
    QString m_id, m_folder, m_lastError;
};

} // namespace uve
