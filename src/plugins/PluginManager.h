#pragma once
#include "PluginApi.h"
#include "PluginManifest.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRegularExpression>
#include <QVariantList>
#include <functional>

class QPluginLoader;
class QQmlEngine;
class QTranslator;

namespace uve {

class AppSettings;
class MpvController;
class ShaderChain;
class PluginBridge;

// Zustände nach Spezifikation: discover → verify → load → initialize → activate ⇄ suspend → deactivate → unload
enum class PluginState { Discovered, Rejected, Verified, Loaded, Initialized, Active, Suspended, Deactivated, Unloaded, Failed, SafeMode };

QString pluginStateName(PluginState s);

class PluginManager : public QObject, public IPlayerHost
{
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(QVariantList panels READ panels NOTIFY pluginsChanged)
    Q_PROPERTY(QVariantList menuItems READ menuItems NOTIFY pluginsChanged)
    Q_PROPERTY(bool safeMode READ safeMode NOTIFY pluginsChanged)
    Q_PROPERTY(QString userPluginDir READ userPluginDir CONSTANT)
public:
    // Befehl: (aufrufendes Plugin, Argumente, Fehlertext) → Ergebnis
    using Command = std::function<QVariant(const QString &pluginId, const QVariantMap &args, QString *error)>;

    PluginManager(AppSettings *settings, MpvController *mpv, ShaderChain *shaders,
                  const QStringList &searchDirs, bool safeMode, QObject *parent = nullptr);
    ~PluginManager() override;

    void setQmlEngine(QQmlEngine *engine) { m_engine = engine; }
    // Befehle des Hosts registrieren (vor discoverAndStart). Gleicher Name ersetzt den alten.
    void registerCommand(const QString &name, Command fn);

    // Ablauf beim Start: alle finden, prüfen, aktivierte laden/initialisieren/aktivieren
    void discoverAndStart();
    void shutdown();

    QVariantList plugins() const;
    QVariantList panels() const;       // Seiten aller aktiven Plugins (location: "sidebar" | "settings")
    QVariantList menuItems() const;    // Menüeinträge aller aktiven Plugins
    bool safeMode() const { return m_safeMode; }
    QString userPluginDir() const { return m_dirs.value(0); }

    Q_INVOKABLE bool setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void setSuspended(const QString &id, bool suspended);
    Q_INVOKABLE QString errorLogPath() const;
    // Menüeintrag oder Tastenkürzel eines Plugins ("plugin:<id>:<eintrag>")
    Q_INVOKABLE void triggerAction(const QString &actionId);
    // Plugin-Paket (.zip mit manifest.json oder Ordner) in den Benutzer-Plugin-Ordner kopieren.
    // Rückgabe: leer = ok (Neustart nötig), sonst Fehlermeldung
    Q_INVOKABLE QString installPackage(const QString &path);

    // Ereignis an alle aktiven Plugins (oder nur an eines)
    void emitEvent(const QString &name, const QVariantMap &data = {}, const QString &onlyPluginId = {});
    void notifyMediaChanged(const QString &path);
    void setLanguage(const QString &code);
    QString language() const { return m_language; }
    // Tastenkürzel aller aktiven Plugins: [{id, label, category, defaultSeq}]
    QVariantList hotkeyActions() const;

    // Adressauflösung über Plugins mit passendem contributes.urlHandlers-Muster:
    // Ereignis "media.resolve" {requestId, url[, quality]}; Antwort per Befehl "media.resolved"
    bool resolveMedia(quint64 requestId, const QString &url, const QString &quality = {});
    // Plugin übernimmt die Adresse und kann die Auflösung wählen (capabilities: "stream-quality")
    bool resolverSupportsQuality(const QString &url) const;
    void cancelResolve(quint64 requestId);

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
    void setVideoEqualizer(const QString &pluginId, double brightness, double contrast, double saturation, double hue) override;
    QImage sampleDisplayFrame() override;
    QVariant call(const QString &pluginId, const QString &command, const QVariantMap &args, QString *error) override;
    bool hasCommand(const QString &command) const override { return m_commands.contains(command); }
    QStringList commands() const override;

    QStringList availableBackends() const;

signals:
    void pluginsChanged();
    void osdRequested(const QString &text, int durationMs);
    void mediaResolved(quint64 requestId, const QString &resolvedUrl, const QString &error, const QString &pluginName);
    void panelRequested(const QString &panelKey);   // Befehl "panel.open"
    void hotkeysChanged();

private:
    struct Entry
    {
        QString file;                 // Bibliothek oder QML-Hauptdatei
        QString folder;               // Plugin-Ordner (leer bei einzelner Bibliothek im plugins-Ordner)
        PluginManifest manifest;
        PluginState state = PluginState::Discovered;
        QString error;
        QPluginLoader *loader = nullptr;
        IPlayerPlugin *instance = nullptr;   // native Plugins
        QPointer<QObject> object;            // Plugin-Objekt (nativ oder QML)
        QPointer<PluginBridge> bridge;
        QList<QTranslator *> translators;
        QList<QRegularExpression> urlHandlers;
        bool isQml() const { return !manifest.main.isEmpty() && manifest.library.isEmpty(); }
        bool running() const { return state == PluginState::Active; }
    };

    void discoverIn(const QString &dir, QSet<QString> &seenIds);
    void addCandidate(Entry e, QSet<QString> &seenIds);
    bool enabledInSettings(const Entry &e) const;
    bool loadAndInit(Entry &e);
    bool activateEntry(Entry &e);
    void deactivateEntry(Entry &e, PluginState newState);
    void fail(Entry &e, const QString &error);
    Entry *find(const QString &id);
    const Entry *find(const QString &id) const;
    void writeErrorLog(const QString &id, const QString &message) const;
    void loadTranslations(Entry &e);
    void unloadTranslations(Entry &e);
    void registerBuiltinCommands();
    QString text(const Entry &e, const QJsonValue &v) const;

    AppSettings *m_settings;
    MpvController *m_mpv;
    ShaderChain *m_shaders;
    QQmlEngine *m_engine = nullptr;
    QStringList m_dirs;
    bool m_safeMode;
    QString m_language = QStringLiteral("de");
    QList<Entry> m_entries;
    QHash<QString, Command> m_commands;
    QHash<quint64, QString> m_resolving;   // requestId → Plugin-ID
};

} // namespace uve
