#include "PluginManager.h"
#include "PluginBridge.h"
#include "../core/AppSettings.h"
#include "../core/Logger.h"
#include "../player/MpvController.h"
#include "../rendering/ShaderChain.h"
#include "../../plugins/uve/engine/MiniZip.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaMethod>
#include <QPluginLoader>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSet>
#include <QTranslator>

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

static QString pendingDir(const QString &userDir) { return QDir(userDir).filePath(QStringLiteral(".pending")); }

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
    if (m_mpv) connect(m_mpv, &MpvController::pausedChanged, this, [this] { emitEvent(QStringLiteral("playback.paused"), {{"paused", m_mpv->paused()}}); });
    registerBuiltinCommands();
}

PluginManager::~PluginManager() { shutdown(); }

QString PluginManager::hostVersion() const { return QCoreApplication::applicationVersion(); }

QStringList PluginManager::availableBackends() const
{
    // eigene Konvention: vorhandene Fähigkeiten des Hosts
    return {QStringLiteral("mpv-user-shaders"), QStringLiteral("mpv-render-api-opengl"), QStringLiteral("plugin-events"),
            QStringLiteral("plugin-commands"), QStringLiteral("qml-plugins")};
}

bool PluginManager::enabledInSettings(const Entry &e) const
{
    const auto states = m_settings->object(QStringLiteral("plugins"));
    const auto st = states.value(e.manifest.id).toObject();
    return st.contains("enabled") ? st.value("enabled").toBool() : true;
}

QString PluginManager::text(const Entry &e, const QJsonValue &v) const
{
    Q_UNUSED(e);
    return localizedText(v, m_language);
}

// ---------------------------------------------------------------- Entdecken
void PluginManager::discoverAndStart()
{
    // Über "Plugin installieren" vorgemerkte Pakete jetzt an ihren Platz bringen (DLLs sind noch nicht geladen)
    const QString pending = pendingDir(userPluginDir());
    for (const auto &fi : QDir(pending).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString target = QDir(userPluginDir()).filePath(fi.fileName());
        QDir(target).removeRecursively();
        if (QDir().rename(fi.absoluteFilePath(), target)) Logger::write("plugin", "installiert: " + target);
        else writeErrorLog(fi.fileName(), tr("Installation fehlgeschlagen: Ordner %1 ist gesperrt").arg(target));
    }

    QSet<QString> seenIds;
    for (const auto &dir : m_dirs) discoverIn(dir, seenIds);

    for (auto &e : m_entries) {
        if (e.state != PluginState::Verified) continue;
        if (m_safeMode) { e.state = PluginState::SafeMode; continue; }
        if (!enabledInSettings(e)) { e.state = PluginState::Deactivated; continue; }
        if (loadAndInit(e) && activateEntry(e))
            Logger::write("plugin", QStringLiteral("%1 %2 aktiv%3").arg(e.manifest.id, e.manifest.version, e.isQml() ? QStringLiteral(" (QML)") : QString()));
    }
    emit pluginsChanged();
    emit hotkeysChanged();
}

void PluginManager::discoverIn(const QString &dir, QSet<QString> &seenIds)
{
    QDir d(dir);
    if (!d.exists()) return;
#if defined(Q_OS_WIN)
    const QStringList filters{QStringLiteral("*.dll")};
#elif defined(Q_OS_MACOS)
    const QStringList filters{QStringLiteral("*.dylib")};
#else
    const QStringList filters{QStringLiteral("*.so")};
#endif
    // a) Einzelne Bibliothek im plugins-Ordner, Manifest eingebettet (bisheriges Format)
    for (const auto &fi : d.entryInfoList(filters, QDir::Files)) {
        QPluginLoader probe(fi.absoluteFilePath());
        const auto meta = probe.metaData();
        if (meta.value("IID").toString() != QLatin1String(UVE_PLAYER_PLUGIN_IID)) continue;
        Entry e;
        e.file = fi.absoluteFilePath();
        e.manifest = PluginManifest::fromJson(meta.value("MetaData").toObject());
        addCandidate(e, seenIds);
    }
    // b) Plugin-Ordner: plugins/<name>/manifest.json (+ Bibliothek oder QML, Sprachdateien, Seiten, Bilder)
    for (const auto &sub : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString mf = QDir(sub.absoluteFilePath()).filePath(QStringLiteral("manifest.json"));
        if (!QFileInfo::exists(mf)) continue;
        Entry e;
        e.folder = sub.absoluteFilePath();
        QFile f(mf);
        QJsonParseError pe{};
        const QJsonDocument doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &pe) : QJsonDocument();
        if (!doc.isObject()) {
            e.file = mf;
            e.manifest.id = sub.fileName();
            e.state = PluginState::Rejected;
            e.error = tr("manifest.json fehlerhaft: %1").arg(pe.errorString());
            writeErrorLog(e.manifest.id, e.error);
            m_entries.append(e);
            continue;
        }
        e.manifest = PluginManifest::fromJson(doc.object());
        QString err;
        if (!e.manifest.library.isEmpty()) {
            e.file = QDir(e.folder).filePath(e.manifest.library);
            if (!QFileInfo::exists(e.file)) err = tr("Bibliothek %1 fehlt").arg(e.manifest.library);
            else if (QPluginLoader(e.file).metaData().value("IID").toString() != QLatin1String(UVE_PLAYER_PLUGIN_IID))
                err = tr("%1 ist kein UVE-Player-Plugin").arg(e.manifest.library);
        } else if (!e.manifest.main.isEmpty()) {
            e.file = QDir(e.folder).filePath(e.manifest.main);
            if (!QFileInfo::exists(e.file)) err = tr("QML-Datei %1 fehlt").arg(e.manifest.main);
        } else {
            e.file = mf;
            err = tr("manifest.json braucht \"library\" oder \"main\"");
        }
        if (!err.isEmpty()) {
            e.state = PluginState::Rejected;
            e.error = err;
            writeErrorLog(e.manifest.id.isEmpty() ? sub.fileName() : e.manifest.id, QStringLiteral("abgelehnt: ") + err);
            if (!seenIds.contains(e.manifest.id)) { seenIds.insert(e.manifest.id); m_entries.append(e); }
            continue;
        }
        addCandidate(e, seenIds);
    }
}

void PluginManager::addCandidate(Entry e, QSet<QString> &seenIds)
{
    const auto check = validateManifest(e.manifest, hostVersion(), kPluginApiVersion, currentPlatformId(), availableBackends());
    if (!check.ok) {
        e.state = PluginState::Rejected;
        e.error = check.errors.join("; ");
        writeErrorLog(e.manifest.id.isEmpty() ? QFileInfo(e.file).fileName() : e.manifest.id, QStringLiteral("abgelehnt: ") + e.error);
    } else if (seenIds.contains(e.manifest.id)) {
        return; // Benutzerordner hat Vorrang vor dem Programmordner (Reihenfolge der Suchpfade)
    } else {
        e.state = PluginState::Verified;
    }
    seenIds.insert(e.manifest.id);
    for (const auto &v : e.manifest.contributes.value("urlHandlers").toArray()) {
        QRegularExpression re(v.toString(), QRegularExpression::CaseInsensitiveOption);
        if (re.isValid()) e.urlHandlers << re;
        else writeErrorLog(e.manifest.id, tr("Ungültiges URL-Muster: %1").arg(v.toString()));
    }
    m_entries.append(e);
}

// ---------------------------------------------------------------- Laden
bool PluginManager::loadAndInit(Entry &e)
{
    if (!e.bridge) e.bridge = new PluginBridge(this, e.manifest.id, e.folder, this);
    loadTranslations(e);   // vor initialize(), damit Texte schon übersetzt sind

    if (e.isQml()) {
        if (!m_engine) { fail(e, tr("Keine QML-Umgebung für QML-Plugins")); return false; }
        QQmlComponent comp(m_engine, QUrl::fromLocalFile(e.file));
        if (comp.isError()) { fail(e, tr("QML-Fehler: ") + comp.errorString()); return false; }
        QObject *obj = comp.beginCreate(m_engine->rootContext());
        if (!obj) { fail(e, tr("QML-Fehler: ") + comp.errorString()); return false; }
        if (obj->metaObject()->indexOfProperty("plugin") >= 0) obj->setProperty("plugin", QVariant::fromValue<QObject *>(e.bridge));
        comp.completeCreate();
        QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
        obj->setParent(this);
        e.object = obj;
        e.state = PluginState::Initialized;
        return true;
    }

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

bool PluginManager::activateEntry(Entry &e)
{
    try {
        if (e.instance) e.instance->activate();
        else if (e.object && e.object->metaObject()->indexOfMethod("activate()") >= 0) QMetaObject::invokeMethod(e.object, "activate");
    } catch (...) {
        fail(e, tr("Ausnahme beim Aktivieren"));
        return false;
    }
    e.state = PluginState::Active;
    return true;
}

void PluginManager::deactivateEntry(Entry &e, PluginState newState)
{
    try {
        if (e.instance) e.instance->deactivate();
        else if (e.object && e.object->metaObject()->indexOfMethod("deactivate()") >= 0) QMetaObject::invokeMethod(e.object, "deactivate");
    } catch (...) {}
    setShaderStage(e.manifest.id, QString());
    setVideoEqualizer(e.manifest.id, 0, 0, 0, 0);
    e.state = newState;
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
    setVideoEqualizer(e.manifest.id, 0, 0, 0, 0);
    e.instance = nullptr;
    if (e.isQml() && e.object) e.object->deleteLater();
    e.object = nullptr;
    if (e.loader) { e.loader->unload(); }
}

PluginManager::Entry *PluginManager::find(const QString &id)
{
    for (auto &e : m_entries) if (e.manifest.id == id) return &e;
    return nullptr;
}

const PluginManager::Entry *PluginManager::find(const QString &id) const
{
    for (const auto &e : m_entries) if (e.manifest.id == id) return &e;
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
        if (!e->instance && !e->object) {
            if (e->state == PluginState::Failed) e->state = PluginState::Verified;
            if (!loadAndInit(*e)) { emit pluginsChanged(); return false; }
        }
        if (!activateEntry(*e)) { emit pluginsChanged(); return false; }
        if (e->instance && !m_mpv->path().isEmpty()) e->instance->onMediaChanged(m_mpv->path());
    } else if (e->instance || e->object) {
        deactivateEntry(*e, PluginState::Deactivated);
    }
    emit pluginsChanged();
    emit hotkeysChanged();
    return true;
}

void PluginManager::setSuspended(const QString &id, bool suspended)
{
    Entry *e = find(id);
    if (!e || (!e->instance && !e->object)) return;
    if (suspended && e->state == PluginState::Active) {
        if (e->instance) e->instance->suspend();
        else if (e->object && e->object->metaObject()->indexOfMethod("suspend()") >= 0) QMetaObject::invokeMethod(e->object, "suspend");
        e->state = PluginState::Suspended;
    } else if (!suspended && e->state == PluginState::Suspended) {
        activateEntry(*e);
    }
    emit pluginsChanged();
}

void PluginManager::shutdown()
{
    emitEvent(QStringLiteral("app.quit"));
    for (auto &e : m_entries) {
        if (e.instance) {
            try { e.instance->deactivate(); } catch (...) {}
            e.instance = nullptr;
        } else if (e.object && e.object->metaObject()->indexOfMethod("deactivate()") >= 0) {
            QMetaObject::invokeMethod(e.object, "deactivate");
        }
        // Qt entlädt Plugins beim Programmende selbst; explizites unload() würde QML-Ressourcen
        // eines noch existierenden Panels entfernen.
        if (e.state != PluginState::Rejected && e.state != PluginState::Failed) e.state = PluginState::Unloaded;
    }
}

// ---------------------------------------------------------------- Sprachen
void PluginManager::loadTranslations(Entry &e)
{
    unloadTranslations(e);
    if (e.folder.isEmpty() || m_language == QLatin1String("de")) return;   // Quellsprache der Plugins: Deutsch
    const QString sub = e.manifest.contributes.value("translations").toString(QStringLiteral("lang"));
    const QDir dir(QDir(e.folder).filePath(sub));
    if (!dir.exists()) return;
    // Englisch als Rückfall zuerst installieren (Qt fragt zuletzt installierte Übersetzer zuerst)
    QStringList codes;
    if (m_language != QLatin1String("en")) codes << QStringLiteral("en");
    codes << m_language;
    for (const auto &code : codes) {
        for (const auto &fi : dir.entryInfoList({QStringLiteral("*_") + code + QStringLiteral(".qm")}, QDir::Files)) {
            auto *t = new QTranslator(this);
            if (t->load(fi.absoluteFilePath())) {
                QCoreApplication::installTranslator(t);
                e.translators << t;
            } else {
                delete t;
                writeErrorLog(e.manifest.id, tr("Sprachdatei fehlerhaft: %1").arg(fi.fileName()));
            }
        }
    }
}

void PluginManager::unloadTranslations(Entry &e)
{
    for (auto *t : std::as_const(e.translators)) { QCoreApplication::removeTranslator(t); t->deleteLater(); }
    e.translators.clear();
}

void PluginManager::setLanguage(const QString &code)
{
    if (code.isEmpty()) return;
    m_language = code;
    for (auto &e : m_entries) {
        if (e.instance || e.object) loadTranslations(e);
        if (e.bridge) e.bridge->notifyLanguageChanged();
    }
    emitEvent(QStringLiteral("language.changed"), {{"code", code}});
    emit pluginsChanged();   // Titel aus dem Manifest
    emit hotkeysChanged();
}

// ---------------------------------------------------------------- Listen für die Oberfläche
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
            {"enabled", enabledInSettings(e)}, {"error", e.error}, {"file", e.file}, {"folder", e.folder},
            {"kind", e.isQml() ? QStringLiteral("qml") : QStringLiteral("native")},
            {"shaderDisabled", m_shaders->disabledOwners().contains(e.manifest.id)},
        };
        if (e.state == PluginState::Active || e.state == PluginState::Suspended) {
            if (e.instance) {
                m.insert("controller", QVariant::fromValue(e.instance->controller()));
                m.insert("panelUrl", e.instance->panelQml());
                m.insert("panelTitle", e.instance->panelTitle());
            } else if (e.object) {
                m.insert("controller", QVariant::fromValue(e.object.data()));
            }
            m.insert("plugin", QVariant::fromValue<QObject *>(e.bridge.data()));
        }
        out << m;
    }
    return out;
}

QVariantList PluginManager::panels() const
{
    QVariantList out;
    for (const auto &e : m_entries) {
        if (e.state != PluginState::Active && e.state != PluginState::Suspended) continue;
        QObject *controller = e.instance ? e.instance->controller() : e.object.data();
        QVariantMap base{{"pluginId", e.manifest.id}, {"pluginName", e.manifest.name}, {"controller", QVariant::fromValue(controller)},
                         {"plugin", QVariant::fromValue<QObject *>(e.bridge.data())}};
        // Panel aus der Plugin-Klasse (bisherige Schnittstelle)
        if (e.instance && !e.instance->panelQml().isEmpty()) {
            QVariantMap p = base;
            p.insert("key", e.manifest.id + QStringLiteral("/main"));
            p.insert("title", e.instance->panelTitle());
            p.insert("url", e.instance->panelQml());
            p.insert("location", QStringLiteral("sidebar"));
            out << p;
        }
        // Seiten aus dem Manifest
        for (const auto &v : e.manifest.contributes.value("panels").toArray()) {
            const QJsonObject o = v.toObject();
            const QString qml = o.value("qml").toString();
            if (qml.isEmpty()) continue;
            QUrl url = qml.startsWith(QLatin1String("qrc:")) ? QUrl(qml) : QUrl::fromLocalFile(QDir(e.folder).filePath(qml));
            if (e.folder.isEmpty() && !qml.startsWith(QLatin1String("qrc:"))) continue;
            QVariantMap p = base;
            p.insert("key", e.manifest.id + QLatin1Char('/') + o.value("id").toString(qml));
            p.insert("title", text(e, o.value("title")).isEmpty() ? e.manifest.name : text(e, o.value("title")));
            p.insert("url", url);
            p.insert("location", o.value("location").toString(QStringLiteral("sidebar")));
            out << p;
        }
    }
    return out;
}

QVariantList PluginManager::menuItems() const
{
    QVariantList out;
    for (const auto &e : m_entries) {
        if (e.state != PluginState::Active) continue;
        for (const auto &v : e.manifest.contributes.value("menu").toArray()) {
            const QJsonObject o = v.toObject();
            const QString id = o.value("id").toString();
            if (id.isEmpty()) continue;
            out << QVariantMap{{"actionId", QStringLiteral("plugin:%1:%2").arg(e.manifest.id, id)}, {"title", text(e, o.value("title"))},
                               {"pluginName", e.manifest.name}};
        }
    }
    return out;
}

QVariantList PluginManager::hotkeyActions() const
{
    QVariantList out;
    for (const auto &e : m_entries) {
        if (e.state != PluginState::Active) continue;
        for (const auto &v : e.manifest.contributes.value("hotkeys").toArray()) {
            const QJsonObject o = v.toObject();
            const QString id = o.value("id").toString();
            if (id.isEmpty()) continue;
            out << QVariantMap{{"id", QStringLiteral("plugin:%1:%2").arg(e.manifest.id, id)}, {"label", text(e, o.value("title"))},
                               {"category", e.manifest.name}, {"defaultSeq", o.value("default").toString()}};
        }
    }
    return out;
}

void PluginManager::triggerAction(const QString &actionId)
{
    // plugin:<plugin-id>:<eintrag>  (Plugin-IDs enthalten keinen Doppelpunkt)
    if (!actionId.startsWith(QLatin1String("plugin:"))) return;
    const QString rest = actionId.mid(7);
    const int c = rest.indexOf(QLatin1Char(':'));
    if (c <= 0) return;
    emitEvent(QStringLiteral("action.triggered"), {{"id", rest.mid(c + 1)}}, rest.left(c));
}

// ---------------------------------------------------------------- Ereignisse
void PluginManager::emitEvent(const QString &name, const QVariantMap &data, const QString &onlyPluginId)
{
    for (auto &e : m_entries) {
        if (!e.running() || (!onlyPluginId.isEmpty() && e.manifest.id != onlyPluginId)) continue;
        if (e.instance && e.object) {
            if (auto *ev = qobject_cast<IPluginEvents *>(e.object.data())) {
                try { ev->onEvent(name, data); } catch (...) { fail(e, tr("Ausnahme in onEvent(%1)").arg(name)); continue; }
            }
        }
        if (e.bridge) e.bridge->deliver(name, data);   // Seiten und QML-Plugins
    }
}

void PluginManager::notifyMediaChanged(const QString &path)
{
    for (auto &e : m_entries) {
        if (e.instance && e.state == PluginState::Active) {
            try { e.instance->onMediaChanged(path); } catch (...) { fail(e, tr("Ausnahme in onMediaChanged")); }
        }
    }
}

bool PluginManager::resolveMedia(quint64 requestId, const QString &url)
{
    for (auto &e : m_entries) {
        if (!e.running()) continue;
        for (const auto &re : std::as_const(e.urlHandlers)) {
            if (!re.match(url).hasMatch()) continue;
            m_resolving.insert(requestId, e.manifest.id);
            emitEvent(QStringLiteral("media.resolve"), {{"requestId", QVariant::fromValue<qulonglong>(requestId)}, {"url", url}}, e.manifest.id);
            // Plugin könnte währenddessen ausgefallen sein
            if (!e.running()) { m_resolving.remove(requestId); return false; }
            return true;
        }
    }
    return false;
}

void PluginManager::cancelResolve(quint64 requestId)
{
    const QString id = m_resolving.take(requestId);
    if (!id.isEmpty())
        emitEvent(QStringLiteral("media.resolveCancelled"), {{"requestId", QVariant::fromValue<qulonglong>(requestId)}}, id);
}

// ---------------------------------------------------------------- Befehle
void PluginManager::registerCommand(const QString &name, Command fn) { m_commands.insert(name, std::move(fn)); }

QStringList PluginManager::commands() const
{
    QStringList l = m_commands.keys();
    l.sort();
    return l;
}

QVariant PluginManager::call(const QString &pluginId, const QString &command, const QVariantMap &args, QString *error)
{
    const auto it = m_commands.constFind(command);
    if (it == m_commands.constEnd()) {
        if (error) *error = QStringLiteral("unknown command: ") + command;
        return {};
    }
    try {
        return it.value()(pluginId, args, error);
    } catch (const std::exception &ex) {
        if (error) *error = QString::fromUtf8(ex.what());
    } catch (...) {
        if (error) *error = QStringLiteral("exception in command ") + command;
    }
    return {};
}

void PluginManager::registerBuiltinCommands()
{
    registerCommand(QStringLiteral("commands"), [this](const QString &, const QVariantMap &, QString *) { return QVariant(commands()); });
    registerCommand(QStringLiteral("app.info"), [this](const QString &, const QVariantMap &, QString *) {
        return QVariant(QVariantMap{{"version", hostVersion()}, {"apiVersion", kPluginApiVersion}, {"language", m_language},
                                    {"platform", currentPlatformId()}, {"backends", availableBackends()}});
    });
    registerCommand(QStringLiteral("osd.show"), [this](const QString &, const QVariantMap &a, QString *) {
        showOsd(a.value("text").toString(), a.value("ms", 1500).toInt());
        return QVariant(true);
    });
    registerCommand(QStringLiteral("log"), [this](const QString &id, const QVariantMap &a, QString *) {
        log(id, a.value("level", QStringLiteral("info")).toString(), a.value("message").toString());
        return QVariant(true);
    });
    registerCommand(QStringLiteral("settings.get"), [this](const QString &id, const QVariantMap &, QString *) {
        return QVariant(loadPluginSettings(id).toVariantMap());
    });
    registerCommand(QStringLiteral("settings.set"), [this](const QString &id, const QVariantMap &a, QString *) {
        savePluginSettings(id, QJsonObject::fromVariantMap(a.value("settings").toMap()));
        return QVariant(true);
    });
    registerCommand(QStringLiteral("plugin.dataDir"), [this](const QString &id, const QVariantMap &, QString *) { return QVariant(pluginDataDir(id)); });
    registerCommand(QStringLiteral("panel.open"), [this](const QString &id, const QVariantMap &a, QString *) {
        emit panelRequested(id + QLatin1Char('/') + a.value("panel", QStringLiteral("main")).toString());
        return QVariant(true);
    });
    // mpv-Zugriff für alles, wofür es (noch) keinen eigenen Befehl gibt
    registerCommand(QStringLiteral("mpv.get"), [this](const QString &, const QVariantMap &a, QString *) {
        return m_mpv ? m_mpv->getProperty(a.value("name").toString()) : QVariant();
    });
    registerCommand(QStringLiteral("mpv.set"), [this](const QString &, const QVariantMap &a, QString *) {
        if (m_mpv) m_mpv->setProperty(a.value("name").toString(), a.value("value"));
        return QVariant(m_mpv != nullptr);
    });
    registerCommand(QStringLiteral("mpv.command"), [this](const QString &, const QVariantMap &a, QString *) {
        return QVariant(m_mpv ? m_mpv->command(a.value("args").toStringList()) : false);
    });
    // Antwort auf "media.resolve": url = abzuspielende Adresse, leer + error = Fehler
    registerCommand(QStringLiteral("media.resolved"), [this](const QString &id, const QVariantMap &a, QString *error) {
        const quint64 req = a.value("requestId").toULongLong();
        if (m_resolving.value(req) != id) { if (error) *error = QStringLiteral("unknown requestId"); return QVariant(false); }
        m_resolving.remove(req);
        const Entry *e = find(id);
        emit mediaResolved(req, a.value("url").toString(), a.value("error").toString(), e ? e->manifest.name : id);
        return QVariant(true);
    });
}

// ---------------------------------------------------------------- Installation
QString PluginManager::installPackage(const QString &path)
{
    const QString userDir = userPluginDir();
    if (userDir.isEmpty()) return tr("Kein Benutzer-Plugin-Ordner");
    QString p = path;
    if (p.startsWith(QLatin1String("file:"))) p = QUrl(p).toLocalFile();
    const QFileInfo fi(p);

    // Dateien des Pakets einsammeln: relativer Pfad → Inhalt
    QHash<QString, QByteArray> files;
    if (fi.isDir() || fi.fileName() == QLatin1String("manifest.json")) {
        const QDir root(fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath());
        QDirIterator it(root.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            QFile in(f);
            if (!in.open(QIODevice::ReadOnly)) return tr("Datei nicht lesbar: %1").arg(f);
            files.insert(root.relativeFilePath(f), in.readAll());
        }
    } else if (fi.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0) {
        QFile in(fi.absoluteFilePath());
        if (!in.open(QIODevice::ReadOnly)) return tr("Datei nicht lesbar: %1").arg(fi.absoluteFilePath());
        const auto zip = fx::readZip(in.readAll(), [](const QString &n) { return !n.endsWith(QLatin1Char('/')) && !n.endsWith(QLatin1Char('\\')); },
                                     qint64(128) << 20, qint64(512) << 20, 20000);
        if (!zip.ok) return tr("ZIP-Datei fehlerhaft: %1").arg(zip.error);
        // Windows PowerShell 5.1 (Compress-Archive) schreibt Pfade mit Backslash
        for (const auto &z : zip.entries) files.insert(QString(z.name).replace(QLatin1Char('\\'), QLatin1Char('/')), z.data);
    } else {
        return tr("Bitte eine .zip-Datei oder einen Ordner mit manifest.json wählen");
    }

    // manifest.json im Wurzelverzeichnis oder in genau einem Unterordner
    QString prefix;
    if (!files.contains(QStringLiteral("manifest.json"))) {
        QStringList found;
        for (auto it = files.constBegin(); it != files.constEnd(); ++it)
            if (it.key().count(QLatin1Char('/')) == 1 && it.key().endsWith(QLatin1String("/manifest.json"))) found << it.key();
        if (found.size() != 1) return tr("Keine manifest.json im Paket gefunden");
        prefix = found.first().left(found.first().size() - int(strlen("manifest.json")));
    }
    const QJsonDocument doc = QJsonDocument::fromJson(files.value(prefix + QStringLiteral("manifest.json")));
    const PluginManifest m = PluginManifest::fromJson(doc.object());
    const auto check = validateManifest(m, hostVersion(), kPluginApiVersion, currentPlatformId(), availableBackends());
    if (!check.ok) return check.errors.join(QStringLiteral("; "));
    if (m.library.isEmpty() && m.main.isEmpty()) return tr("manifest.json braucht \"library\" oder \"main\"");

    // In den Vormerk-Ordner schreiben; beim nächsten Start wird er an seinen Platz verschoben
    const QString target = QDir(pendingDir(userDir)).filePath(m.id);
    QDir(target).removeRecursively();
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (!it.key().startsWith(prefix)) continue;
        const QString rel = QDir::cleanPath(it.key().mid(prefix.size()));
        if (rel.isEmpty() || rel.startsWith(QLatin1String("..")) || QDir::isAbsolutePath(rel) || rel.contains(QLatin1Char(':')))
            return tr("Ungültiger Pfad im Paket: %1").arg(it.key());
        const QString out = QDir(target).filePath(rel);
        QDir().mkpath(QFileInfo(out).absolutePath());
        QFile f(out);
        if (!f.open(QIODevice::WriteOnly) || f.write(it.value()) != it.value().size())
            return tr("Schreiben fehlgeschlagen: %1").arg(out);
    }
    Logger::write("plugin", QStringLiteral("Paket vorgemerkt: %1 %2 → %3").arg(m.id, m.version, target));
    return {};
}

// ---------------------------------------------------------------- Protokoll
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

void PluginManager::setVideoEqualizer(const QString &pluginId, double brightness, double contrast, double saturation, double hue)
{
    Q_UNUSED(pluginId);   // derzeit nutzt nur das UVE-Plugin den Equalizer
    if (!m_mpv) return;
    auto b = [](double v) { return qBound(-100.0, v, 100.0); };
    // asynchron: die Oberfläche wartet nicht auf den mpv-Kern
    m_mpv->setPropertyAsync(QStringLiteral("brightness"), b(brightness));
    m_mpv->setPropertyAsync(QStringLiteral("contrast"), b(contrast));
    m_mpv->setPropertyAsync(QStringLiteral("saturation"), b(saturation));
    m_mpv->setPropertyAsync(QStringLiteral("hue"), b(hue));
}

QImage PluginManager::sampleDisplayFrame()
{
    if (!m_mpv) return {};
    QImage last = m_mpv->displaySample();
    m_mpv->requestDisplaySample();
    return last;
}

} // namespace uve
