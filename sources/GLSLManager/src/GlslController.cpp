#include "GlslController.h"
#include "ShaderConverter.h"
#include "ShaderValidator.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QKeySequence>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QWindow>

namespace glslmgr {

namespace {
const QString kPreviewId = QStringLiteral("__preview__");
const QString kBrokenSlot = QStringLiteral("!broken");
constexpr int kStagePauseMs = 250;
constexpr int kPreviewSettleMs = 1200;

QString localPath(const QString &urlOrPath)
{
    return urlOrPath.startsWith(QLatin1String("file:")) ? QUrl(urlOrPath).toLocalFile() : urlOrPath;
}

QStringList tagsFrom(const QJsonValue &v)
{
    QStringList tags;
    if (v.isArray()) {
        for (const auto &t : v.toArray()) if (!t.toString().trimmed().isEmpty()) tags << t.toString().trimmed();
    } else {
        for (const auto &t : v.toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) if (!t.trimmed().isEmpty()) tags << t.trimmed();
    }
    return tags;
}

bool playable(const QJsonObject &e) { return e.value("type").toString() == QLatin1String("webgl"); }

QString keyName(const QKeyEvent *ke)
{
    switch (ke->key()) {
    case Qt::Key_Left: return QStringLiteral("arrowleft");
    case Qt::Key_Right: return QStringLiteral("arrowright");
    case Qt::Key_Up: return QStringLiteral("arrowup");
    case Qt::Key_Down: return QStringLiteral("arrowdown");
    case Qt::Key_Space: return QStringLiteral(" ");
    default: break;
    }
    const QString text = ke->text();
    if (!text.isEmpty() && text.at(0).isPrint()) return text.toLower();
    return QKeySequence(ke->key()).toString(QKeySequence::PortableText).toLower();
}

bool textInputFocused()
{
    const QObject *f = QGuiApplication::focusObject();
    return f && (f->inherits("QQuickTextEdit") || f->inherits("QQuickTextInput"));
}
} // namespace

GlslController::GlslController(uve::IPlayerHost *host, const QString &pluginId, QObject *parent)
    : QObject(parent), m_host(host), m_id(pluginId)
{
    m_queueTimer.setSingleShot(true);
    m_queueTimer.setInterval(kStagePauseMs);
    connect(&m_queueTimer, &QTimer::timeout, this, &GlslController::processQueue);
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(60);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &GlslController::rebuild);
    m_previewTimer.setSingleShot(true);
    connect(&m_previewTimer, &QTimer::timeout, this, [this] {
        if (m_previewOn && m_previewStatus == QLatin1String("checking")) setPreviewState(QStringLiteral("ok"), {});
    });
    m_master = m_host->loadPluginSettings(m_id).value("masterEnabled").toBool(true);
    load();
}

QVariantList GlslController::parseDefs(const QString &code) { return parseUniformDefs(code); }

// ---------------------------------------------------------------- Laden und Speichern
void GlslController::load()
{
    m_entries.clear();
    QFile f(QDir(m_host->pluginDataDir(m_id)).filePath(QStringLiteral("filters.json")));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    for (const auto &v : doc.object().value("codes").toArray())
        if (!v.toObject().value("id").toString().isEmpty()) m_entries << v.toObject();
}

void GlslController::save() const
{
    QJsonArray codes;
    for (const auto &e : m_entries) codes.append(e);
    const QJsonObject root{{"schema", "gvf-custom-filter-codes"}, {"ver", 1}, {"codes", codes}};
    QSaveFile f(QDir(m_host->pluginDataDir(m_id)).filePath(QStringLiteral("filters.json")));
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(root).toJson()) < 0 || !f.commit())
        m_host->log(m_id, QStringLiteral("error"), QStringLiteral("filters.json not writable"));
}

void GlslController::changed()
{
    save();
    updateHotkeyFilter();
    emit entriesChanged();
    if (m_active) m_rebuildTimer.start();
}

// ---------------------------------------------------------------- Lebenszyklus
void GlslController::activate()
{
    m_active = true;
    updateHotkeyFilter();
    rebuild();
}

void GlslController::suspend()
{
    m_active = false;
    m_queueTimer.stop();
    m_rebuildTimer.stop();
    m_previewOn = false;
    updateHotkeyFilter();
    for (int i = 0; i < m_applied.size(); ++i)
        if (!m_applied[i].isEmpty()) m_host->setShaderStage(owner(i), QString());
    m_applied.clear();
    m_appliedIds.clear();
    m_desired.clear();
    m_desiredIds.clear();
}

void GlslController::onEvent(const QString &name, const QVariantMap &data)
{
    if (name == QLatin1String("media.loaded")) {
        const double fps = m_host->call(m_id, QStringLiteral("mpv.get"), {{"name", QStringLiteral("container-fps")}}).toDouble();
        if (fps > 1.0) m_fps = fps;
        emit entriesChanged();
        if (m_active) rebuild();
        if (m_previewOn && m_previewStatus == QLatin1String("novideo")) setPreviewState(QStringLiteral("checking"), {});
    } else if (name == QLatin1String("player.stopped")) {
        emit entriesChanged();
    } else if (name == QLatin1String("action.triggered")) {
        const QString id = data.value("id").toString();
        if (id == QLatin1String("toggle-all")) {
            setMasterEnabled(!m_master);
            m_host->showOsd(m_master ? tr("GLSL-Filter: an") : tr("GLSL-Filter: aus"));
        } else if (id == QLatin1String("open")) {
            m_host->call(m_id, QStringLiteral("panel.open"), {{"panel", QStringLiteral("main")}});
        }
    }
}

void GlslController::onShaderError(const QString &message)
{
    const QString prefix = m_id + QStringLiteral("#s");
    if (!message.startsWith(prefix)) return;
    const qsizetype colon = message.indexOf(QStringLiteral(": "));
    const int slot = message.mid(prefix.size(), colon - prefix.size()).toInt();
    Q_UNUSED(colon);
    if (slot < 0 || slot >= m_appliedIds.size()) return;
    const QString id = m_appliedIds[slot];
    if (id.isEmpty() || m_errors.contains(id)) return;

    // mpv liefert hier nur die erste Zeile seines Protokolls; die eigentliche Meldung holt der eigene Probelauf
    QString text;
    QFile applied(m_applied[slot]);
    if (applied.open(QIODevice::ReadOnly)) text = validateHookShader(QString::fromUtf8(applied.readAll())).error;
    if (text.isEmpty()) text = tr("Der GPU-Treiber hat den Shader abgelehnt (Details im Player-Protokoll).");

    // Der Host hat den Platz abgeschaltet. Er bleibt so belegt: Würde er geleert, käme jede weitere Protokollzeile
    // erneut als Fehler an, und die anderen Filter sollen nicht verrutschen.
    m_applied[slot] = kBrokenSlot;
    reportError(id, text);
    rebuild();
}

void GlslController::reportError(const QString &id, const QString &text)
{
    m_errors.insert(id, text);
    m_host->log(m_id, QStringLiteral("warning"), QStringLiteral("shader error in %1: %2").arg(id, text));
    if (id == kPreviewId || (m_previewOn && m_preview.value("id").toString() == id)) {
        setPreviewState(QStringLiteral("error"), text);
    } else {
        const int i = indexOf(id);
        m_host->showOsd(tr("Shaderfehler in „%1“").arg(i >= 0 ? m_entries[i].value("label").toString() : id), 3000);
    }
    emit entriesChanged();
}

bool GlslController::preflightFailed(const QJsonObject &e, const QString &file)
{
    if (file.isEmpty() || e.value("noPrecheck").toBool()) return false;
    auto it = m_preflight.constFind(file);
    if (it == m_preflight.constEnd()) {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const ValidationResult r = validateHookShader(QString::fromUtf8(f.readAll()));
        if (!r.checked) return false;
        if (m_preflight.size() > 500) m_preflight.clear();
        it = m_preflight.insert(file, r.error);
    }
    if (it.value().isEmpty()) return false;
    reportError(e.value("id").toString(), it.value());
    return true;
}

// ---------------------------------------------------------------- Shader-Stufen
QString GlslController::owner(int slot) const { return m_id + QStringLiteral("#s") + QString::number(slot); }

QString GlslController::writeShaderFile(const QJsonObject &e)
{
    ConvertInput in;
    in.code = e.value("code").toString();
    in.uniforms = e.value("uniforms").toObject().toVariantMap();
    in.blendMode = e.value("blendMode").toString(QStringLiteral("normal"));
    in.label = e.value("label").toString();
    in.fps = m_fps;
    const QByteArray text = convertToMpv(in).toUtf8();

    // mpv hält Shader nach Pfad vor: der Name folgt dem Inhalt
    const QDir dir(m_host->pluginDataDir(m_id));
    dir.mkpath(QStringLiteral("shaders"));
    const QString file = dir.filePath(QStringLiteral("shaders/glsl-%1.hook")
                                          .arg(QString::fromLatin1(QCryptographicHash::hash(text, QCryptographicHash::Sha1).toHex().left(20))));
    if (!QFileInfo::exists(file)) {
        QSaveFile f(file);
        if (!f.open(QIODevice::WriteOnly) || f.write(text) < 0 || !f.commit()) {
            reportError(e.value("id").toString(), tr("Shaderdatei nicht schreibbar: ") + file);
            return {};
        }
    }
    return file;
}

void GlslController::rebuild()
{
    if (!m_active) return;
    QStringList files, ids;
    const auto add = [&](const QJsonObject &e) {
        const QString id = e.value("id").toString();
        ids << id;
        QString file = m_errors.contains(id) ? QString() : writeShaderFile(e);
        if (preflightFailed(e, file)) file.clear();   // erreicht mpv gar nicht erst
        files << file;
    };
    const QString previewId = m_previewOn ? m_preview.value("id").toString() : QString();
    if (m_master || m_previewOn) {
        for (const auto &e : std::as_const(m_entries)) {
            if (m_previewOn && e.value("id").toString() == previewId) {
                if (playable(m_preview)) add(m_preview);
            } else if (m_master && e.value("enabled").toBool() && playable(e)) {
                add(e);
            }
        }
        if (m_previewOn && previewId == kPreviewId && playable(m_preview)) add(m_preview);
    }
    m_desired = files;
    m_desiredIds = ids;
    processQueue();
}

void GlslController::processQueue()
{
    if (!m_active) return;
    if (m_queueTimer.isActive()) return;   // die laufende Pause arbeitet die Liste danach ab
    const int count = int(std::max(m_desired.size(), m_applied.size()));
    while (m_applied.size() < count) { m_applied << QString(); m_appliedIds << QString(); }

    // Ungeprüfte Shader nur mit Video und einzeln: mpv kompiliert beim Zeichnen, und der Host ordnet einen Fehler
    // der zuletzt geänderten Stufe zu. Geprüfte Shader können gesammelt angewendet werden.
    const bool video = !m_host->currentMediaPath().isEmpty();
    for (int i = 0; i < count; ++i) {
        const QString want = m_desired.value(i), wantId = m_desiredIds.value(i);
        if (m_applied[i] == want) { m_appliedIds[i] = wantId; continue; }
        if (m_applied[i] == kBrokenSlot && want.isEmpty() && wantId == m_appliedIds[i]) continue;   // bleibt pausiert
        const bool verified = want.isEmpty() || m_preflight.contains(want);
        if (!verified && !video) continue;
        m_host->setShaderStage(owner(i), want);
        m_applied[i] = want;
        m_appliedIds[i] = wantId;
        if (verified) continue;
        m_queueTimer.start();
        return;
    }
    while (!m_applied.isEmpty() && m_applied.last().isEmpty() && m_desired.size() < m_applied.size()) { m_applied.removeLast(); m_appliedIds.removeLast(); }
    cleanShaderDir();
    if (m_previewOn && m_previewStatus == QLatin1String("checking")) m_previewTimer.start(kPreviewSettleMs);
}

void GlslController::cleanShaderDir()
{
    QSet<QString> keep;
    for (const auto &f : std::as_const(m_desired)) keep.insert(QDir::cleanPath(f));
    for (const auto &f : std::as_const(m_applied)) keep.insert(QDir::cleanPath(f));
    const QDir dir(QDir(m_host->pluginDataDir(m_id)).filePath(QStringLiteral("shaders")));
    for (const auto &fi : dir.entryInfoList({QStringLiteral("glsl-*.hook")}, QDir::Files))
        if (!keep.contains(QDir::cleanPath(fi.absoluteFilePath()))) QFile::remove(fi.absoluteFilePath());
}

// ---------------------------------------------------------------- Liste
QVariantList GlslController::entries() const
{
    QVariantList out;
    for (const auto &e : m_entries) {
        QVariantMap m = e.toVariantMap();
        m.remove("code");
        const QString code = e.value("code").toString();
        m.insert("playable", playable(e));
        m.insert("isHook", playable(e) && isMpvHook(code));
        m.insert("error", m_errors.value(e.value("id").toString()));
        m.insert("tags", tagsFrom(e.value("tags")));
        out << m;
    }
    return out;
}

int GlslController::activeCount() const
{
    int n = 0;
    for (const auto &e : m_entries) if (e.value("enabled").toBool() && playable(e)) ++n;
    return n;
}

bool GlslController::hasVideo() const { return !m_host->currentMediaPath().isEmpty(); }

void GlslController::setMasterEnabled(bool on)
{
    if (m_master == on) return;
    m_master = on;
    QJsonObject s = m_host->loadPluginSettings(m_id);
    s.insert("masterEnabled", on);
    m_host->savePluginSettings(m_id, s);
    emit entriesChanged();
    if (m_active) rebuild();
}

int GlslController::indexOf(const QString &id) const
{
    for (int i = 0; i < m_entries.size(); ++i) if (m_entries[i].value("id").toString() == id) return i;
    return -1;
}

QVariantMap GlslController::entry(const QString &id) const
{
    const int i = indexOf(id);
    if (i < 0) return {};
    QVariantMap m = m_entries[i].toVariantMap();
    m.insert("tags", tagsFrom(m_entries[i].value("tags")));
    return m;
}

QJsonObject GlslController::sanitized(const QVariantMap &fields, QJsonObject e) const
{
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        if (it.key() == QLatin1String("uniforms")) e.insert("uniforms", QJsonObject::fromVariantMap(it.value().toMap()));
        else if (it.key() == QLatin1String("tags")) e.insert("tags", QJsonArray::fromStringList(tagsFrom(QJsonValue::fromVariant(it.value()))));
        else e.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    const QString label = e.value("label").toString().trimmed();
    e.insert("label", label.isEmpty() ? tr("Unbenannt") : label);
    e.insert("code", e.value("code").toString().trimmed());
    if (e.value("type").toString().isEmpty()) e.insert("type", detectType(e.value("code").toString()));
    if (e.value("blendMode").toString().isEmpty()) e.insert("blendMode", QStringLiteral("normal"));
    return e;
}

QString GlslController::addEntry(const QVariantMap &fields)
{
    QJsonObject e = sanitized(fields, QJsonObject{{"enabled", true}});
    QString id = QStringLiteral("csvg_") + QString::number(QDateTime::currentMSecsSinceEpoch());
    while (indexOf(id) >= 0) id += QLatin1Char('x');
    e.insert("id", id);
    m_entries << e;
    changed();
    return id;
}

bool GlslController::updateEntry(const QString &id, const QVariantMap &fields)
{
    const int i = indexOf(id);
    if (i < 0) return false;
    m_entries[i] = sanitized(fields, m_entries[i]);
    m_errors.remove(id);
    changed();
    return true;
}

void GlslController::removeEntry(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0) return;
    m_entries.removeAt(i);
    m_errors.remove(id);
    changed();
}

QString GlslController::duplicateEntry(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0) return {};
    QJsonObject e = m_entries[i];
    QString nid = QStringLiteral("csvg_") + QString::number(QDateTime::currentMSecsSinceEpoch());
    while (indexOf(nid) >= 0) nid += QLatin1Char('x');
    e.insert("id", nid);
    e.insert("label", e.value("label").toString() + tr(" (Kopie)"));
    e.insert("enabled", false);
    e.remove("hotkey");
    m_entries.insert(i + 1, e);
    changed();
    return nid;
}

void GlslController::setEnabled(const QString &id, bool on)
{
    const int i = indexOf(id);
    if (i < 0) return;
    m_entries[i].insert("enabled", on);
    m_errors.remove(id);   // erneutes Einschalten ist ein neuer Versuch
    changed();
}

void GlslController::moveEntry(int from, int to)
{
    if (from < 0 || from >= m_entries.size() || to < 0 || to >= m_entries.size() || from == to) return;
    m_entries.move(from, to);
    changed();
}

void GlslController::retry(const QString &id)
{
    m_errors.remove(id);
    emit entriesChanged();
    if (m_active) rebuild();
}

// ---------------------------------------------------------------- Editor
QString GlslController::check(const QString &code) const
{
    const QString r = staticCheck(code);
    if (r == QLatin1String("empty")) return tr("Der Code darf nicht leer sein.");
    if (r == QLatin1String("braces")) return tr("Geschweifte Klammern { } passen nicht zusammen.");
    if (r == QLatin1String("parens")) return tr("Runde Klammern ( ) passen nicht zusammen.");
    return {};
}

QString GlslController::validate(const QVariantMap &fields) const
{
    if (fields.value("type").toString() != QLatin1String("webgl")) return {};
    ConvertInput in;
    in.code = fields.value("code").toString();
    in.uniforms = fields.value("uniforms").toMap();
    in.blendMode = fields.value("blendMode").toString();
    in.label = fields.value("label").toString();
    in.fps = m_fps;
    return validateHookShader(convertToMpv(in)).error;
}

QString GlslController::detect(const QString &code) const { return detectType(code); }
bool GlslController::isHook(const QString &code) const { return isMpvHook(code); }

void GlslController::setPreviewState(const QString &status, const QString &error)
{
    m_previewStatus = status;
    m_previewError = error;
    emit previewChanged();
}

void GlslController::setPreview(const QString &id, const QVariantMap &fields)
{
    const QString pid = id.isEmpty() ? kPreviewId : id;
    const int i = indexOf(id);
    QJsonObject base = i >= 0 ? m_entries[i] : QJsonObject{};
    m_preview = sanitized(fields, base);
    m_preview.insert("id", pid);
    m_preview.insert("enabled", true);
    m_previewOn = true;
    m_errors.remove(pid);
    if (!playable(m_preview)) setPreviewState(QString(), {});
    else setPreviewState(hasVideo() ? QStringLiteral("checking") : QStringLiteral("novideo"), {});
    m_previewTimer.stop();
    if (m_active) m_rebuildTimer.start();
}

void GlslController::clearPreview()
{
    if (!m_previewOn) return;
    m_previewOn = false;
    m_previewTimer.stop();
    m_errors.remove(kPreviewId);
    setPreviewState(QString(), {});
    if (m_active) m_rebuildTimer.start();
}

// ---------------------------------------------------------------- Austausch
QString GlslController::clipboardText() const { return QGuiApplication::clipboard() ? QGuiApplication::clipboard()->text() : QString(); }
void GlslController::copyText(const QString &text) const { if (QGuiApplication::clipboard()) QGuiApplication::clipboard()->setText(text); }

QVariantMap GlslController::importText(const QString &text, const QString &policy)
{
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError) return {{"error", tr("Ungültige JSON-Datei: ") + pe.errorString()}};

    QJsonArray incoming;
    if (doc.isArray()) incoming = doc.array();
    else if (doc.object().value("codes").isArray()) incoming = doc.object().value("codes").toArray();
    else if (doc.object().contains("code")) incoming.append(doc.object());

    QList<QJsonObject> valid;
    for (const auto &v : incoming) {
        const QJsonObject e = v.toObject();
        if (!e.value("label").toString().trimmed().isEmpty() && !e.value("code").toString().trimmed().isEmpty()) valid << e;
    }
    if (valid.isEmpty()) return {{"error", tr("Keine gültigen Filter in dieser Datei gefunden.")}};

    QStringList dupes;
    for (const auto &e : valid) {
        const QString label = e.value("label").toString().trimmed();
        for (const auto &c : std::as_const(m_entries)) if (c.value("label").toString() == label) { dupes << label; break; }
    }
    if (policy.isEmpty() && !dupes.isEmpty()) return {{"dupes", dupes}};

    int added = 0, replaced = 0, skipped = 0, seq = 0;
    QSet<QString> used;
    for (const auto &c : std::as_const(m_entries)) used.insert(c.value("id").toString());
    for (const auto &raw : valid) {
        QJsonObject e = raw;
        const QString label = e.value("label").toString().trimmed();
        const QString code = e.value("code").toString();
        static const QSet<QString> types{QStringLiteral("svg"), QStringLiteral("webgl"), QStringLiteral("canvas2d"), QStringLiteral("audio")};
        const QString type = types.contains(e.value("type").toString()) ? e.value("type").toString() : detectType(code);
        e.insert("label", label);
        e.insert("type", type);
        e.insert("enabled", e.value("enabled").toBool(true));
        e.insert("blendMode", e.value("blendMode").toString(QStringLiteral("normal")));
        e.insert("tags", QJsonArray::fromStringList(tagsFrom(e.value("tags"))));
        e.insert("category", e.value("category").toString().trimmed());

        int existing = -1;
        for (int i = 0; i < m_entries.size(); ++i) if (m_entries[i].value("label").toString() == label) { existing = i; break; }
        if (existing >= 0) {
            if (policy != QLatin1String("overwrite")) { ++skipped; continue; }
            const QString keepId = m_entries[existing].value("id").toString();
            e.insert("id", keepId);
            m_entries[existing] = e;
            m_errors.remove(keepId);
            ++replaced;
        } else {
            QString id = e.value("id").toString();
            if (id.isEmpty() || used.contains(id) || id == kPreviewId)
                id = QStringLiteral("csvg_%1_%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(seq++);
            used.insert(id);
            e.insert("id", id);
            m_entries << e;
            ++added;
        }
    }
    changed();
    return {{"added", added}, {"replaced", replaced}, {"skipped", skipped}};
}

QVariantMap GlslController::readImportFile(const QString &urlOrPath) const
{
    const QString path = localPath(urlOrPath);
    QFile f(path);
    if (f.size() > 32 * 1024 * 1024) return {{"error", tr("Datei ist zu groß.")}};
    if (!f.open(QIODevice::ReadOnly)) return {{"error", tr("Datei nicht lesbar: ") + path}};
    const QString text = QString::fromUtf8(f.readAll());
    const QString first = text.trimmed().left(1);
    const bool json = first == QLatin1String("{") || first == QLatin1String("[");
    const QString desc = descriptionOf(text);
    return {{"text", text}, {"isJson", json}, {"label", desc.isEmpty() ? QFileInfo(path).completeBaseName() : desc}};
}

QString GlslController::suggestedExportName() const
{
    return QStringLiteral("gvf_filter_codes_%1.json").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm")));
}

QVariantMap GlslController::exportFile(const QString &urlOrPath) const
{
    if (m_entries.isEmpty()) return {{"error", tr("Keine Filter zum Exportieren vorhanden.")}};
    QJsonArray codes;
    for (const auto &e : m_entries) codes.append(e);
    const QJsonObject root{{"schema", "gvf-custom-filter-codes"}, {"ver", 1}, {"exportedAt", QDateTime::currentMSecsSinceEpoch()}, {"codes", codes}};
    const QString path = localPath(urlOrPath);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(root).toJson()) < 0 || !f.commit())
        return {{"error", tr("Datei nicht schreibbar: ") + path}};
    return {{"count", int(codes.size())}, {"path", path}};
}

// ---------------------------------------------------------------- Tasten
void GlslController::beginCapture(const QString &id)
{
    m_captureId = id;
    updateHotkeyFilter();
    emit captureChanged();
}

void GlslController::cancelCapture()
{
    m_captureId.clear();
    updateHotkeyFilter();
    emit captureChanged();
}

void GlslController::clearHotkey(const QString &id)
{
    const int i = indexOf(id);
    if (i >= 0) { m_entries[i].remove("hotkey"); changed(); }
    cancelCapture();
}

void GlslController::updateHotkeyFilter()
{
    bool needed = m_active && !m_captureId.isEmpty();
    if (m_active) for (const auto &e : std::as_const(m_entries)) needed = needed || !e.value("hotkey").toString().isEmpty();
    if (needed == m_filterInstalled || !QCoreApplication::instance()) return;
    if (needed) QCoreApplication::instance()->installEventFilter(this);
    else QCoreApplication::instance()->removeEventFilter(this);
    m_filterInstalled = needed;
}

bool GlslController::eventFilter(QObject *watched, QEvent *event)
{
    const auto type = event->type();
    if (type != QEvent::KeyPress && type != QEvent::ShortcutOverride) return QObject::eventFilter(watched, event);
    // ShortcutOverride geht an das Fokus-Objekt und entscheidet nur, ob ein Player-Kürzel auslöst. Der Tastendruck
    // selbst kommt zuerst am Fenster an und läuft danach durch die Elemente: Er wird nur dort einmal ausgewertet.
    if (type == QEvent::KeyPress && !qobject_cast<QWindow *>(watched)) return false;
    auto *ke = static_cast<QKeyEvent *>(event);
    const int key = ke->key();
    const bool modifierKey = key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Shift || key == Qt::Key_Meta || key == Qt::Key_AltGr || key == Qt::Key_CapsLock;

    if (!m_captureId.isEmpty()) {
        if (modifierKey || key == Qt::Key_Tab) return false;
        event->accept();
        if (type == QEvent::ShortcutOverride) return true;   // Player-Kürzel beim Erfassen nicht auslösen
        if (key != Qt::Key_Escape) {
            const int i = indexOf(m_captureId);
            if (i >= 0) {
                m_entries[i].insert("hotkey", keyName(ke));
                save();
                emit entriesChanged();
            }
        }
        cancelCapture();
        return true;
    }

    if (ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier)) return false;
    if (ke->isAutoRepeat() || textInputFocused()) return false;
    const QString name = keyName(ke);
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].value("hotkey").toString().compare(name, Qt::CaseInsensitive) != 0) continue;
        event->accept();
        if (type == QEvent::ShortcutOverride) return true;
        const bool on = !m_entries[i].value("enabled").toBool();
        const QString label = m_entries[i].value("label").toString();
        setEnabled(m_entries[i].value("id").toString(), on);
        m_host->showOsd(label + (on ? tr(": an") : tr(": aus")));
        return true;
    }
    return false;
}

} // namespace glslmgr
