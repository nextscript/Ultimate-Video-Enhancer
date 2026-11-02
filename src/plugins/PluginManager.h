#pragma once
#include "PluginApi.h"
#include "PluginManifest.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>

class QPluginLoader;

namespace uve {

class AppSettings;
class MpvController;
class ShaderChain;

// Zustände nach Spezifikation: discover → verify → load → initialize → activate ⇄ suspend → deactivate → unload
enum class PluginState { Discovered, Rejected, Verified, Loaded, Initialized, Active, Suspended, Deactivated, Unloaded, Failed, SafeMode };

QString pluginStateName(PluginState s);

class PluginManager : public QObject, public IPlayerHost
{
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(bool safeMode READ safeMode NOTIFY pluginsChanged)
public:
    PluginManager(AppSettings *settings, MpvController *mpv, ShaderChain *shaders,
                  const QStringList &searchDirs, bool safeMode, QObject *parent = nullptr);
    ~PluginManager() override;

    // Ablauf beim Start: alle finden, prüfen, aktivierte laden/initialisieren/aktivieren
    void discoverAndStart();
    void shutdown();

    QVariantList plugins() const;
    bool safeMode() const { return m_safeMode; }

    Q_INVOKABLE bool setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void setSuspended(const QString &id, bool suspended);
    Q_INVOKABLE QString errorLogPath() const;
    void notifyMediaChanged(const QString &path);

    // IPlayerHost
    QString hostVersion() const override;
    int apiVersion() const override { return kPluginApiVersion; }
    QJsonObject loadPluginSettings(const QString &pluginId) const override;
    void savePluginSettings(const QString &pluginId, const QJsonObject &settings) override;
    QString pluginDataDir(const QString &pluginId) const override;
    void setShaderStage(const QString &pluginId, const QString &shaderFile) override;
    void showOsd(const QString &text, int durationMs) override;
    void log(const QString &pluginId, const QString &level, const QString &message) override;
    QString currentMediaPath() const override;
    double currentPosition() const override;
    bool isPaused() const override;
    void setPaused(bool paused) override;
    QImage grabVideoFrame(int maxWidth) override;

    QStringList availableBackends() const;

signals:
    void pluginsChanged();
    void osdRequested(const QString &text, int durationMs);

private:
    struct Entry
    {
        QString file;
        PluginManifest manifest;
        PluginState state = PluginState::Discovered;
        QString error;
        QPluginLoader *loader = nullptr;
        IPlayerPlugin *instance = nullptr;
        QPointer<QObject> object;
    };

    bool enabledInSettings(const Entry &e) const;
    bool loadAndInit(Entry &e);
    void fail(Entry &e, const QString &error);
    Entry *find(const QString &id);
    void writeErrorLog(const QString &id, const QString &message) const;

    AppSettings *m_settings;
    MpvController *m_mpv;
    ShaderChain *m_shaders;
    QStringList m_dirs;
    bool m_safeMode;
    QList<Entry> m_entries;
};

} // namespace uve
