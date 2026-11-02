#include "PluginManager.h"
#include "../core/AppSettings.h"
#include "../core/Logger.h"
#include "../player/MpvController.h"
#include "../rendering/ShaderChain.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QPluginLoader>
#include <QSet>

namespace uve {

QString pluginStateName(PluginState s)
{
    switch (s) {
    case PluginState::Discovered: return QStringLiteral("gefunden");
    case PluginState::Rejected: return QStringLiteral("abgelehnt");
    case PluginState::Verified: return QStringLiteral("geprüft");
    case PluginState::Loaded: return QStringLiteral("geladen");
    case PluginState::Initialized: return QStringLiteral("initialisiert");
    case PluginState::Active: return QStringLiteral("aktiv");
    case PluginState::Suspended: return QStringLiteral("pausiert");
    case PluginState::Deactivated: return QStringLiteral("deaktiviert");
    case PluginState::Unloaded: return QStringLiteral("entladen");
    case PluginState::Failed: return QStringLiteral("Fehler");
    case PluginState::SafeMode: return QStringLiteral("Safe Mode");
    }
    return {};
}

PluginManager::PluginManager(AppSettings *settings, MpvController *mpv, ShaderChain *shaders,
                             const QStringList &searchDirs, bool safeMode, QObject *parent)
    : QObject(parent), m_settings(settings), m_mpv(mpv), m_shaders(shaders), m_dirs(searchDirs), m_safeMode(safeMode)
{
    connect(m_shaders, &ShaderChain::errorOccurred, this, [this](const QString &owner, const QString &msg) {
        if (auto *e = find(owner)) {
            writeErrorLog(owner, tr("Shaderfehler, Stufe deaktiviert: ") + msg);
            if (e->instance) e->instance->onShaderError(msg);
            emit pluginsChanged();
        }
    });
}

PluginManager::~PluginManager() { shutdown(); }

QString PluginManager::hostVersion() const { return QCoreApplication::applicationVersion(); }

QStringList PluginManager::availableBackends() const
{
    // eigene Konvention: vorhandene Fähigkeiten des Hosts
    return {QStringLiteral("mpv-user-shaders"), QStringLiteral("mpv-render-api-opengl")};
}

bool PluginManager::enabledInSettings(const Entry &e) const
{
    const auto states = m_settings->object(QStringLiteral("plugins"));
    const auto st = states.value(e.manifest.id).toObject();
    return st.contains("enabled") ? st.value("enabled").toBool() : true;
}

void PluginManager::discoverAndStart()
{
    QSet<QString> seenIds;
    for (const auto &dir : m_dirs) {
        QDir d(dir);
        if (!d.exists()) continue;
#if defined(Q_OS_WIN)
        const QStringList filters{QStringLiteral("*.dll")};
#elif defined(Q_OS_MACOS)
        const QStringList filters{QStringLiteral("*.dylib")};
#else
        const QStringList filters{QStringLiteral("*.so")};
#endif
        for (const auto &fi : d.entryInfoList(filters, QDir::Files)) {
            // 1) discover: Metadaten lesen, ohne die Bibliothek zu laden
            QPluginLoader probe(fi.absoluteFilePath());
            const auto meta = probe.metaData();
            if (meta.value("IID").toString() != QLatin1String(UVE_PLAYER_PLUGIN_IID)) continue;
            Entry e;
            e.file = fi.absoluteFilePath();
            e.manifest = PluginManifest::fromJson(meta.value("MetaData").toObject());
            e.state = PluginState::Discovered;
            // 2) verify
            const auto check = validateManifest(e.manifest, hostVersion(), kPluginApiVersion, currentPlatformId(), availableBackends());
            if (!check.ok) {
                e.state = PluginState::Rejected;
                e.error = check.errors.join("; ");
                writeErrorLog(e.manifest.id.isEmpty() ? fi.fileName() : e.manifest.id, QStringLiteral("abgelehnt: ") + e.error);
            } else if (seenIds.contains(e.manifest.id)) {
                continue; // Benutzerordner hat Vorrang vor dem Programmordner (Reihenfolge der Suchpfade)
            } else {
                e.state = PluginState::Verified;
            }
            seenIds.insert(e.manifest.id);
            m_entries.append(e);
        }
    }

    for (auto &e : m_entries) {
        if (e.state != PluginState::Verified) continue;
        if (m_safeMode) { e.state = PluginState::SafeMode; continue; }
        if (!enabledInSettings(e)) { e.state = PluginState::Deactivated; continue; }
        if (loadAndInit(e)) {
            e.instance->activate();
            e.state = PluginState::Active;
            Logger::write("plugin", QStringLiteral("%1 %2 aktiv").arg(e.manifest.id, e.manifest.version));
        }
    }
    emit pluginsChanged();
}

bool PluginManager::loadAndInit(Entry &e)
{
    // 3) load
    if (!e.loader) e.loader = new QPluginLoader(e.file, this);
    QObject *obj = e.loader->instance();
    if (!obj) { fail(e, tr("Laden fehlgeschlagen: ") + e.loader->errorString()); return false; }
    auto *inst = qobject_cast<IPlayerPlugin *>(obj);
    if (!inst) { fail(e, tr("Bibliothek implementiert IPlayerPlugin nicht")); return false; }
    e.instance = inst;
    e.object = obj;
    e.state = PluginState::Loaded;
    // 4) initialize
    QString err;
    bool ok = false;
    try {
        ok = inst->initialize(this, &err);
    } catch (const std::exception &ex) {
        err = QString::fromUtf8(ex.what());
    } catch (...) {
        err = tr("unbekannte Ausnahme");
    }
    if (!ok) { fail(e, tr("Initialisierung fehlgeschlagen: ") + err); return false; }
    e.state = PluginState::Initialized;
    return true;
}

void PluginManager::fail(Entry &e, const QString &error)
{
    e.error = error;
    e.state = PluginState::Failed;
    writeErrorLog(e.manifest.id, error);
    if (e.instance) {
        try { e.instance->deactivate(); } catch (...) {}
    }
    setShaderStage(e.manifest.id, QString());
    e.instance = nullptr;
    if (e.loader) { e.loader->unload(); }
}

PluginManager::Entry *PluginManager::find(const QString &id)
{
    for (auto &e : m_entries) if (e.manifest.id == id) return &e;
    return nullptr;
}

bool PluginManager::setEnabled(const QString &id, bool enabled)
{
    Entry *e = find(id);
    if (!e) return false;
    auto states = m_settings->object(QStringLiteral("plugins"));
    states.insert(id, QJsonObject{{"enabled", enabled}});
    m_settings->setObject(QStringLiteral("plugins"), states);
    if (m_safeMode || e->state == PluginState::Rejected) { emit pluginsChanged(); return false; }

    if (enabled) {
        if (!e->instance) {
            if (e->state == PluginState::Failed) e->state = PluginState::Verified;
            if (!loadAndInit(*e)) { emit pluginsChanged(); return false; }
        }
        e->instance->activate();
        e->state = PluginState::Active;
        if (!m_mpv->path().isEmpty()) e->instance->onMediaChanged(m_mpv->path());
    } else if (e->instance) {
        e->instance->deactivate();
        setShaderStage(id, QString());
        e->state = PluginState::Deactivated;
    }
    emit pluginsChanged();
    return true;
}

void PluginManager::setSuspended(const QString &id, bool suspended)
{
    Entry *e = find(id);
    if (!e || !e->instance) return;
    if (suspended && e->state == PluginState::Active) { e->instance->suspend(); e->state = PluginState::Suspended; }
    else if (!suspended && e->state == PluginState::Suspended) { e->instance->activate(); e->state = PluginState::Active; }
    emit pluginsChanged();
}

void PluginManager::shutdown()
{
    for (auto &e : m_entries) {
        if (e.instance) {
            try { e.instance->deactivate(); } catch (...) {}
            e.instance = nullptr;
        }
        // Qt entlädt Plugins beim Programmende selbst; explizites unload() würde QML-Ressourcen
        // eines noch existierenden Panels entfernen.
        if (e.state != PluginState::Rejected && e.state != PluginState::Failed) e.state = PluginState::Unloaded;
    }
}

QVariantList PluginManager::plugins() const
{
    QVariantList out;
    for (const auto &e : m_entries) {
        QVariantMap m{
            {"id", e.manifest.id}, {"name", e.manifest.name.isEmpty() ? QFileInfo(e.file).fileName() : e.manifest.name},
            {"version", e.manifest.version}, {"apiVersion", e.manifest.apiVersion},
            {"description", e.manifest.description}, {"author", e.manifest.author}, {"license", e.manifest.license},
            {"type", e.manifest.type}, {"capabilities", e.manifest.capabilities}, {"permissions", e.manifest.permissions},
            {"state", pluginStateName(e.state)}, {"active", e.state == PluginState::Active},
            {"enabled", enabledInSettings(e)}, {"error", e.error}, {"file", e.file},
            {"shaderDisabled", m_shaders->disabledOwners().contains(e.manifest.id)},
        };
        if (e.instance && (e.state == PluginState::Active || e.state == PluginState::Suspended)) {
            m.insert("controller", QVariant::fromValue(e.instance->controller()));
            m.insert("panelUrl", e.instance->panelQml());
            m.insert("panelTitle", e.instance->panelTitle());
        }
        out << m;
    }
    return out;
}

void PluginManager::notifyMediaChanged(const QString &path)
{
    for (auto &e : m_entries) {
        if (e.instance && e.state == PluginState::Active) {
            try { e.instance->onMediaChanged(path); } catch (...) { fail(e, tr("Ausnahme in onMediaChanged")); }
        }
    }
}

QString PluginManager::errorLogPath() const
{
    return QDir(m_settings->dataDir()).filePath("logs/plugin-errors.log");
}

void PluginManager::writeErrorLog(const QString &id, const QString &message) const
{
    QDir().mkpath(QFileInfo(errorLogPath()).absolutePath());
    QFile f(errorLogPath());
    if (f.open(QIODevice::Append | QIODevice::Text))
        f.write(QStringLiteral("[%1] %2: %3\n").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate), id, message).toUtf8());
    Logger::write("plugin", id + ": " + message);
}

// ---------------------------------------------------------------- IPlayerHost
QJsonObject PluginManager::loadPluginSettings(const QString &pluginId) const
{
    return m_settings->object(QStringLiteral("pluginSettings")).value(pluginId).toObject();
}

void PluginManager::savePluginSettings(const QString &pluginId, const QJsonObject &settings)
{
    auto all = m_settings->object(QStringLiteral("pluginSettings"));
    all.insert(pluginId, settings);
    m_settings->setObject(QStringLiteral("pluginSettings"), all);
}

QString PluginManager::pluginDataDir(const QString &pluginId) const
{
    const QString dir = QDir(m_settings->dataDir()).filePath(QStringLiteral("plugin-data/") + pluginId);
    QDir().mkpath(dir);
    return dir;
}

void PluginManager::setShaderStage(const QString &pluginId, const QString &shaderFile)
{
    m_shaders->setStage(pluginId, shaderFile);
}

void PluginManager::showOsd(const QString &text, int durationMs) { emit osdRequested(text, durationMs); }

void PluginManager::log(const QString &pluginId, const QString &level, const QString &message)
{
    Logger::write(QStringLiteral("plugin.%1.%2").arg(pluginId, level), message);
}

QString PluginManager::currentMediaPath() const { return m_mpv->path(); }
double PluginManager::currentPosition() const { return m_mpv->position(); }
bool PluginManager::isPaused() const { return m_mpv->paused(); }
void PluginManager::setPaused(bool paused) { m_mpv->setPaused(paused); }

QImage PluginManager::grabVideoFrame(int maxWidth) { return m_mpv ? m_mpv->grabFrame(qBound(16, maxWidth, 4096)) : QImage(); }

} // namespace uve
