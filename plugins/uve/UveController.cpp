#include "UveController.h"
#include "engine/ShaderBuilder.h"

#include "PluginApi.h"

#include <QCoreApplication>
#include <QEvent>
#include <QCryptographicHash>
#include <QDateTime>
#include <QTimeZone>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QtConcurrent>
#include <QElapsedTimer>
#include <functional>

namespace uve::fx {

namespace {

struct Field
{
    const char *key;
    std::function<QVariant(const Params &)> get;
    std::function<void(Params &, const QVariant &)> set;
};

#define NUM(member) Field{#member, [](const Params &p) { return QVariant(p.member); }, [](Params &p, const QVariant &v) { p.member = v.toDouble(); }}
#define INT(member) Field{#member, [](const Params &p) { return QVariant(p.member); }, [](Params &p, const QVariant &v) { p.member = qBound(0, int(qRound(v.toDouble())), 255); }}
#define BOOL(member) Field{#member, [](const Params &p) { return QVariant(p.member); }, [](Params &p, const QVariant &v) { p.member = v.toBool(); }}

const QList<Field> &fields()
{
    static const QList<Field> f{
        BOOL(baseTone), BOOL(darkMoody), BOOL(tealOrange), BOOL(vibrantSat),
        Field{"profile", [](const Params &p) { return QVariant(p.profile); }, [](Params &p, const QVariant &v) {
                  static const QStringList allowed{"off", "film", "anime", "gaming", "eyecare", "user"};
                  p.profile = allowed.contains(v.toString()) ? v.toString() : QStringLiteral("off"); }},
        NUM(sl), NUM(sr), NUM(bl), NUM(wl), NUM(dn), NUM(hdr), NUM(edge),
        NUM(contrast), NUM(black), NUM(white), NUM(highlights), NUM(shadows),
        NUM(saturation), NUM(vibrance), NUM(sharpen), NUM(gamma), NUM(grain), NUM(hue),
        INT(rGain), INT(gGain), INT(bGain),
        BOOL(gpuBase), NUM(lutStrength),
    };
    return f;
}

QVariantMap control(const char *key, const QString &label, const QString &section, double min, double max, double step,
                    double def, const QString &note = {})
{
    return {{"key", key}, {"label", label}, {"section", section}, {"type", "slider"}, {"min", min}, {"max", max},
            {"step", step}, {"def", def}, {"note", note}};
}
QVariantMap toggle(const char *key, const QString &label, const QString &section, bool def, const QString &note = {})
{
    return {{"key", key}, {"label", label}, {"section", section}, {"type", "switch"}, {"def", def}, {"note", note}};
}

} // namespace

UveController::UveController(IPlayerHost *host, const QString &pluginId, QObject *parent)
    : QObject(parent), m_host(host), m_id(pluginId)
{
    m_shaderTimer.setSingleShot(true);
    m_shaderTimer.setInterval(40);
    connect(&m_shaderTimer, &QTimer::timeout, this, &UveController::writeShader);
    m_library = new LutLibrary(m_host->pluginDataDir(m_id), this);
    connect(m_library, &LutLibrary::changed, this, [this] { emit libraryChanged(); emit valuesChanged(); scheduleShader(); });
    connect(m_library, &LutLibrary::busyChanged, this, &UveController::libraryStatusChanged);
    connect(m_library, &LutLibrary::statusChanged, this, &UveController::libraryStatusChanged);
    loadPresets();
    loadState();
    // Sprachwechsel: QCoreApplication::installTranslator sendet LanguageChange an die Anwendung
    qApp->installEventFilter(this);
}

bool UveController::eventFilter(QObject *o, QEvent *e)
{
    if (o == qApp && e->type() == QEvent::LanguageChange) {
        emit languageChanged();
        emit valuesChanged();
        emit presetsChanged();
        emit galleryChanged();
    }
    return QObject::eventFilter(o, e);
}

QVariantList UveController::controls() const
{
    // Nur tatsächlich implementierte Effekte (siehe docs/UVE_PARITY_MATRIX.md)
    return {
        toggle("baseTone", tr("UVE-Grundton"), "Basis", true, tr("Kontrast ×1.05, Sättigung ×1.15, Helligkeit ×1.02")),
        QVariantMap{{"key", "profile"}, {"label", tr("Profil")}, {"section", "Basis"}, {"type", "choice"}, {"def", "off"},
                    {"options", QVariantList{QVariantMap{{"value", "off"}, {"text", tr("Aus")}}, QVariantMap{{"value", "film"}, {"text", tr("Film")}},
                                             QVariantMap{{"value", "anime"}, {"text", tr("Anime")}}, QVariantMap{{"value", "gaming"}, {"text", tr("Gaming")}},
                                             QVariantMap{{"value", "eyecare"}, {"text", tr("Augenschonend")}}, QVariantMap{{"value", "user"}, {"text", tr("Benutzer")}}}}},
        control("bl", tr("BL – Schwarzpegel"), "Basis", -2, 2, 0.01, 0, tr("UVE-Hauptregler: höher = tiefere Schwarztöne")),
        control("wl", tr("WL – Weißpegel"), "Basis", -2, 2, 0.01, 0, tr("UVE-Hauptregler: hebt/senkt die Lichter ab 78 %")),
        control("contrast", tr("Kontrast"), "Basis", -10, 10, 0.1, 0, tr("S-Kurve um 50 %")),
        control("black", tr("Schwarzwert (Grading)"), "Basis", -10, 10, 0.1, 0),
        control("white", tr("Weißwert (Grading)"), "Basis", -10, 10, 0.1, 0),
        control("shadows", tr("Schatten"), "Basis", -10, 10, 0.1, 0, tr("höher = dunkler")),
        control("highlights", tr("Lichter"), "Basis", -10, 10, 0.1, 0),
        control("gamma", tr("Gamma (Mitteltöne)"), "Basis", -10, 10, 0.1, 0),
        control("saturation", tr("Sättigung"), "Farbe", -10, 10, 0.1, 0),
        control("vibrance", tr("Dynamik (Vibrance)"), "Farbe", -10, 10, 0.1, 0, tr("mit Hautschutz")),
        control("hue", tr("Farbton"), "Farbe", -10, 10, 0.1, 0, tr("±30°")),
        control("rGain", tr("Rot"), "Farbe", 0, 255, 1, 128),
        control("gGain", tr("Grün"), "Farbe", 0, 255, 1, 128),
        control("bGain", tr("Blau"), "Farbe", 0, 255, 1, 128),
        toggle("darkMoody", tr("Dark & Moody"), "Farbe", false),
        toggle("tealOrange", tr("Teal & Orange"), "Farbe", false, tr("GPU-Pfad: Farbton −5°")),
        toggle("vibrantSat", tr("Kräftige Farben"), "Farbe", false, tr("Sättigung ×1.35")),
        control("sl", tr("SL – Schärfe"), "Schärfe", -2, 2, 0.01, 0, tr("UVE-Hauptregler")),
        control("sr", tr("SR – Radius"), "Schärfe", -2, 2, 0.01, 0, tr("Radius der Detailschärfe (|SR|, mindestens 1 Pixel)")),
        control("sharpen", tr("Detail-Schärfe"), "Schärfe", -10, 10, 0.1, 0, tr("negativ = weicher")),
        control("edge", tr("Kanten betonen"), "Schärfe", 0, 1, 0.01, 0),
        control("hdr", tr("HDR-like"), "HDR-like", -1, 2, 0.01, 0, tr("wirkt bei Werten > 0, max. 0.65")),
        toggle("gpuBase", tr("Rauschfilter + CAS"), "Denoise", true, tr("Bilateral 3×3 + Contrast-Adaptive Sharpening")),
        control("dn", tr("Korn (DN negativ)"), "Denoise", -1.5, 1.5, 0.01, 0, tr("nur negative Werte wirken (Korn)")),
        control("grain", tr("Filmkorn"), "Denoise", -10, 10, 0.1, 0),
        control("lutStrength", tr("LUT-Stärke"), "LUT", 0, 1, 0.01, 1),
    };
}

QVariantMap UveController::values() const
{
    QVariantMap m;
    for (const auto &f : fields()) m.insert(QString::fromLatin1(f.key), f.get(m_params));
    m.insert("enabled", m_params.enabled);
    m.insert("compare", m_params.compare);
    m.insert("compareSplit", m_params.compareSplit);
    m.insert("lutFile", m_params.lutFile);
    m.insert("lutProfile", m_params.lutProfile);
    return m;
}

void UveController::beginGesture()
{
    if (m_gestureOpen) return;
    m_undo << m_params;
    if (m_undo.size() > 100) m_undo.removeFirst();
    m_redo.clear();
    m_gestureOpen = true;
    emit historyChanged();
}

void UveController::setValue(const QString &key, const QVariant &value)
{
    for (const auto &f : fields()) {
        if (key == QLatin1String(f.key)) {
            Params next = m_params;
            f.set(next, value);
            if (next == m_params) return;
            if (!m_gestureOpen) { m_undo << m_params; m_redo.clear(); emit historyChanged(); }
            m_params = next;
            m_activePreset.clear();
            changed(false);
            return;
        }
    }
}

void UveController::resetValue(const QString &key)
{
    for (const auto &c : controls()) {
        const auto m = c.toMap();
        if (m.value("key").toString() == key) { m_gestureOpen = false; setValue(key, m.value("def")); return; }
    }
}

void UveController::resetSection(const QString &section)
{
    Params next = m_params;
    for (const auto &c : controls()) {
        const auto m = c.toMap();
        if (m.value("section").toString() != section) continue;
        for (const auto &f : fields())
            if (m.value("key").toString() == QLatin1String(f.key)) f.set(next, m.value("def"));
    }
    if (section == "LUT") { next.lutFile.clear(); next.lutProfile.clear(); }
    if (next == m_params) return;
    m_undo << m_params; m_redo.clear();
    m_params = next;
    m_activePreset.clear();
    reloadLut();
    changed(false);
    emit historyChanged();
}

void UveController::resetAll()
{
    m_undo << m_params; m_redo.clear();
    const bool en = m_params.enabled;
    m_params = Params();
    m_params.enabled = en;
    m_activePreset = QStringLiteral("UVE Standard");
    reloadLut();
    changed(false);
    emit historyChanged();
}

void UveController::undo()
{
    if (m_undo.isEmpty()) return;
    m_redo << m_params;
    m_params = m_undo.takeLast();
    m_gestureOpen = false;
    reloadLut();
    changed(false);
    emit historyChanged();
}

void UveController::redo()
{
    if (m_redo.isEmpty()) return;
    m_undo << m_params;
    m_params = m_redo.takeLast();
    reloadLut();
    changed(false);
    emit historyChanged();
}

void UveController::endGesture() { m_gestureOpen = false; }

void UveController::changed(bool recordUndo)
{
    Q_UNUSED(recordUndo);
    emit valuesChanged();
    scheduleShader();
    persist();
}

void UveController::setEnabled(bool on)
{
    if (m_params.enabled == on) return;
    m_params.enabled = on;
    m_host->showOsd(on ? tr("Effekte an") : tr("Effekte aus (Original)"), 1200);
    changed(false);
}

void UveController::toggleEnabled() { setEnabled(!m_params.enabled); }

void UveController::setCompare(bool on)
{
    if (m_params.compare == on) return;
    m_params.compare = on;
    if (on && !m_params.enabled) m_params.enabled = true;
    m_host->showOsd(on ? tr("Vorher/Nachher: links Original, rechts mit Effekten") : tr("Vergleich aus"), 1800);
    changed(false);
}

void UveController::toggleCompare() { setCompare(!m_params.compare); }

void UveController::setCompareSplit(double split)
{
    split = qBound(0.0, split, 1.0);
    if (qFuzzyCompare(split + 1, m_params.compareSplit + 1)) return;
    m_params.compareSplit = split;
    emit valuesChanged();
    scheduleShader();
}

// ---------------------------------------------------------------- Shader
void UveController::setActive(bool active)
{
    m_active = active;
    if (active) writeShader();
    else m_host->setShaderStage(m_id, QString());
}

void UveController::scheduleShader()
{
    if (m_active) m_shaderTimer.start();
}

void UveController::writeShader()
{
    if (!m_active) return;
    if (!m_params.enabled) { m_host->setShaderStage(m_id, QString()); return; }
    ShaderBuildInput in;
    in.params = m_params;
    in.lut = m_lut.get();
    const LutProfile *lp = m_params.lutProfile.isEmpty() ? nullptr : m_library->find(m_params.lutProfile);
    if (lp) in.lutMatrix = &lp->matrix;
    const QString text = buildShader(in);
    const QDir dir(m_host->pluginDataDir(m_id));

    // mpv speichert Benutzer-Shader nach Dateipfad zwischen und liest eine bekannte Datei nicht erneut.
    // Daher erhält jede Änderung einen neuen, eindeutigen Namen (Sitzungskennung + laufende Nummer).
    if (m_shaderSession.isEmpty()) m_shaderSession = QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
    const QString file = dir.filePath(QStringLiteral("uve-%1-%2.glsl").arg(m_shaderSession).arg(++m_shaderSeq));
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(text.toUtf8()) < 0 || !f.commit()) {
        setError(tr("Shaderdatei nicht schreibbar: ") + file);
        return;
    }
    QStringList files;
    QString lutFile;
    if (m_lut) {
        // LUT-Textur separat und stabil benannt (Inhalt ändert sich nur mit dem LUT)
        const QByteArray key = QCryptographicHash::hash((m_lutLoadedFrom + QString::number(m_lut->size)).toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
        lutFile = dir.filePath(QStringLiteral("uve-lut-%1.glsl").arg(QString::fromLatin1(key)));
        if (!QFileInfo::exists(lutFile)) {
            QSaveFile lf(lutFile);
            if (!lf.open(QIODevice::WriteOnly) || lf.write(buildLutShader(*m_lut).toUtf8()) < 0 || !lf.commit()) {
                setError(tr("LUT-Shader nicht schreibbar: ") + lutFile);
                return;
            }
        }
        files << lutFile;
    }
    files << file;
    // Mehrere Dateien einer Stufe: durch Zeilenumbruch getrennt (siehe PluginApi.h)
    m_host->setShaderStage(m_id, files.join('\n'));

    // Ältere Dateien aufräumen (mpv hat sie bereits eingelesen bzw. nutzt sie nicht mehr)
    for (const auto &fi : dir.entryInfoList({QStringLiteral("uve-*.glsl")}, QDir::Files)) {
        const QString p = fi.absoluteFilePath();
        if (QDir::cleanPath(p) != QDir::cleanPath(file) && QDir::cleanPath(p) != QDir::cleanPath(lutFile)) QFile::remove(p);
    }
}

void UveController::onShaderError(const QString &message)
{
    setError(tr("Shaderfehler – Effekte vorübergehend deaktiviert, Wiedergabe läuft weiter: ") + message);
}

void UveController::reenableShader()
{
    setError(QString());
    writeShader();
}

QString UveController::pipelineInfo() const
{
    if (!m_params.enabled) return tr("Aus – Originalbild");
    QStringList parts;
    if (m_params.compare) parts << tr("Original sichern");
    if (m_params.gpuBase) parts << tr("Rauschfilter+CAS");
    parts << tr("Farbkorrektur");
    parts << tr("Detail/Kanten");
    if (m_lut) parts << tr("3D-LUT %1³").arg(m_lut->size);
    if (!m_params.lutProfile.isEmpty() && m_library->find(m_params.lutProfile)) parts << QStringLiteral("LUT-Profil");
    if (m_params.compare) parts << tr("Split");
    return parts.join(QStringLiteral(" → "));
}

void UveController::setError(const QString &e)
{
    if (m_lastError == e) return;
    m_lastError = e;
    if (!e.isEmpty()) m_host->log(m_id, "error", e);
    emit errorChanged();
}

// ---------------------------------------------------------------- LUT
QString UveController::toLocal(const QString &fileOrUrl)
{
    const QUrl u(fileOrUrl);
    return u.isLocalFile() ? u.toLocalFile() : fileOrUrl;
}

bool UveController::reloadLut()
{
    if (m_params.lutFile.isEmpty()) { m_lut.reset(); m_lutLoadedFrom.clear(); return true; }
    if (m_lut && m_lutLoadedFrom == m_params.lutFile) return true;
    auto r = loadCubeFile(m_params.lutFile);
    if (!r.ok) {
        setError(tr("LUT konnte nicht geladen werden: ") + r.error);
        m_lut.reset();
        m_lutLoadedFrom.clear();
        return false;
    }
    m_lut = std::make_unique<CubeLut>(std::move(r.lut));
    m_lutLoadedFrom = m_params.lutFile;
    return true;
}

bool UveController::loadLut(const QString &fileOrUrl)
{
    const QString file = toLocal(fileOrUrl);
    auto r = loadCubeFile(file);
    if (!r.ok) { setError(tr("LUT ungültig: ") + r.error); return false; }
    m_undo << m_params; m_redo.clear();
    m_params.lutFile = file;
    m_lut = std::make_unique<CubeLut>(std::move(r.lut));
    m_lutLoadedFrom = file;
    setError(QString());
    m_activePreset.clear();
    changed(false);
    emit historyChanged();
    m_host->showOsd(tr("LUT geladen: %1 (%2³)").arg(QFileInfo(file).fileName()).arg(m_lut->size), 1800);
    return true;
}

void UveController::clearLut()
{
    if (m_params.lutFile.isEmpty()) return;
    m_params.lutFile.clear();
    m_lut.reset();
    m_lutLoadedFrom.clear();
    changed(false);
}

QString UveController::lutInfo() const
{
    if (!m_lut) return m_params.lutFile.isEmpty() ? tr("kein .cube-LUT") : tr("LUT fehlt: ") + m_params.lutFile;
    return QStringLiteral("%1 · %2³%3").arg(QFileInfo(m_params.lutFile).fileName()).arg(m_lut->size)
        .arg(m_lut->title.isEmpty() ? QString() : " · " + m_lut->title);
}

// ---------------------------------------------------------------- Presets & Profile
void UveController::loadPresets()
{
    m_builtinPresets.clear();
    Params def;
    m_builtinPresets << qMakePair(QStringLiteral("Original"), QJsonObject{{"enabled", false}});
    m_builtinPresets << qMakePair(QStringLiteral("UVE Standard"), paramsToJson(def));
    const QList<QPair<QString, QString>> profiles{{"Film", "film"}, {"Anime", "anime"}, {"Gaming", "gaming"}, {"Augenschonend", "eyecare"}};
    for (const auto &pr : profiles) {
        Params p; p.profile = pr.second;
        m_builtinPresets << qMakePair(pr.first, paramsToJson(p));
    }
    // Mitgelieferte Profile/LUTs aus <App>/plugins/uve/
    const QString base = QDir(QCoreApplication::applicationDirPath()).filePath("plugins/uve");
    const QString lut = QDir(base).filePath("luts/NaturalBoost.cube");
    if (QFileInfo::exists(lut)) {
        Params p; p.lutFile = lut; p.lutStrength = 1.0;
        m_builtinPresets << qMakePair(QStringLiteral("Natürlich (LUT NaturalBoost)"), paramsToJson(p));
    }
    for (const auto &fi : QDir(QDir(base).filePath("presets")).entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        const auto r = paramsFromProfile(QJsonDocument::fromJson(f.readAll()).object());
        if (r.ok) m_builtinPresets << qMakePair(fi.completeBaseName().replace('_', ' '), paramsToJson(r.params));
    }
    // Eigene Presets
    m_userPresets.clear();
    const QString userDir = QDir(m_host->pluginDataDir(m_id)).filePath("presets");
    for (const auto &fi : QDir(userDir).entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
        QFile f(fi.absoluteFilePath());
        if (f.open(QIODevice::ReadOnly)) m_userPresets << qMakePair(fi.completeBaseName(), QJsonDocument::fromJson(f.readAll()).object());
    }
    emit presetsChanged();
}

QVariantList UveController::presets() const
{
    QVariantList out;
    for (const auto &p : m_builtinPresets) out << QVariantMap{{"name", p.first}, {"builtin", true}, {"isDefault", p.first == m_defaultPreset}};
    for (const auto &p : m_userPresets) out << QVariantMap{{"name", p.first}, {"builtin", false}, {"isDefault", p.first == m_defaultPreset}};
    return out;
}

Params UveController::presetParams(const QString &name, bool *found) const
{
    *found = true;
    for (const auto &p : m_builtinPresets + m_userPresets) {
        if (p.first != name) continue;
        Params out = paramsFromJson(p.second);
        if (p.second.contains("enabled")) out.enabled = p.second.value("enabled").toBool();
        else out.enabled = true;
        out.compare = false;
        return out;
    }
    *found = false;
    return {};
}

bool UveController::applyPreset(const QString &name)
{
    bool found = false;
    Params p = presetParams(name, &found);
    if (!found) return false;
    m_undo << m_params; m_redo.clear();
    p.compare = m_params.compare;
    p.compareSplit = m_params.compareSplit;
    m_params = p;
    m_activePreset = name;
    reloadLut();
    changed(false);
    emit historyChanged();
    return true;
}

bool UveController::savePreset(const QString &name)
{
    const QString clean = QString(name).remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]"))).trimmed();
    if (clean.isEmpty()) return false;
    const QString dir = QDir(m_host->pluginDataDir(m_id)).filePath("presets");
    QDir().mkpath(dir);
    QSaveFile f(QDir(dir).filePath(clean + ".json"));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(profileFromParams(m_params, m_passthrough)).toJson());
    if (!f.commit()) return false;
    m_activePreset = clean;
    loadPresets();
    emit valuesChanged();
    return true;
}

bool UveController::deletePreset(const QString &name)
{
    const QString file = QDir(QDir(m_host->pluginDataDir(m_id)).filePath("presets")).filePath(name + ".json");
    if (!QFile::exists(file)) return false;
    QFile::remove(file);
    if (m_defaultPreset == name) m_defaultPreset.clear();
    loadPresets();
    persist();
    return true;
}

void UveController::setDefaultPreset(const QString &name)
{
    m_defaultPreset = name;
    persist();
    emit presetsChanged();
}

bool UveController::mediaPresetActive() const
{
    return !m_mediaPath.isEmpty() && m_mediaPresets.contains(m_mediaPath.toLower());
}

void UveController::setMediaPreset(bool on)
{
    if (m_mediaPath.isEmpty()) return;
    const QString key = m_mediaPath.toLower();
    if (on) {
        if (m_activePreset.isEmpty()) {
            // Ungespeicherte Einstellung: als Preset "Datei: <Name>" sichern
            savePreset(QStringLiteral("Datei – ") + QFileInfo(m_mediaPath).completeBaseName().left(40));
        }
        m_mediaPresets.insert(key, m_activePreset);
    } else {
        m_mediaPresets.remove(key);
    }
    persist();
    emit mediaPresetChanged();
}

void UveController::onMediaChanged(const QString &path)
{
    m_mediaPath = path;
    const QString key = path.toLower();
    if (m_mediaPresets.contains(key)) applyPreset(m_mediaPresets.value(key).toString());
    else if (!m_defaultPreset.isEmpty() && m_activePreset != m_defaultPreset) applyPreset(m_defaultPreset);
    emit mediaPresetChanged();
}

bool UveController::importProfile(const QString &fileOrUrl)
{
    // Quelldatei wird nur gelesen, nie verändert
    QFile f(toLocal(fileOrUrl));
    if (!f.open(QIODevice::ReadOnly)) { setError(tr("Profil nicht lesbar")); return false; }
    QString error;
    if (!applyProfileJson(f.readAll(), QFileInfo(f.fileName()).fileName(), &error)) { setError(error); return false; }
    return true;
}

bool UveController::applyProfileJson(const QByteArray &json, const QString &label, QString *error)
{
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = tr("Profil ist kein gültiges JSON: ") + err.errorString();
        return false;
    }
    auto r = paramsFromProfile(doc.object(), Params());
    if (!r.ok) { *error = tr("Profil abgelehnt: ") + r.error; return false; }
    m_undo << m_params; m_redo.clear();
    r.params.enabled = true;
    r.params.compare = m_params.compare;
    r.params.compareSplit = m_params.compareSplit;
    m_params = r.params;
    m_passthrough = r.passthrough;
    m_activePreset.clear();
    setError(QString());
    reloadLut();
    changed(false);
    emit historyChanged();
    m_host->showOsd(tr("UVE-Profil geladen: ") + label, 1800);
    return true;
}

// ---------------------------------------------------------------- LUT-Profile / GitHub
QString UveController::lutProfileInfo() const
{
    if (m_params.lutProfile.isEmpty()) return tr("kein LUT-Profil");
    const QString shown = QString(m_params.lutProfile).replace(QStringLiteral("||"), QStringLiteral(" · "));
    if (!m_library->find(m_params.lutProfile))
        return tr("LUT-Profil fehlt: %1 – Download-Symbol neben der LUT-Auswahl").arg(shown);
    return shown;
}

void UveController::setLutProfile(const QString &key)
{
    if (key == m_params.lutProfile) return;
    m_undo << m_params; m_redo.clear();
    m_params.lutProfile = key;
    m_activePreset.clear();
    changed(false);
    emit historyChanged();
}

void UveController::downloadLutPackage()
{
    m_library->downloadLatestPackage([this](bool ok, const QString &msg) {
        if (!ok) setError(msg);
        m_host->showOsd(msg, 2500);
        scheduleShader();
    });
}

bool UveController::importLutProfiles(const QString &fileOrUrl)
{
    QString msg;
    const bool ok = m_library->importFile(toLocal(fileOrUrl), &msg);
    if (!ok) setError(msg);
    m_host->showOsd(msg, 2200);
    return ok;
}

bool UveController::buildGallery()
{
    // Vorschau: dekodierter Frame (ohne Effekte) + jeweiliges LUT-Profil – so, wie es beim Laden aussähe
    QElapsedTimer t; t.start();
    const QImage frame = m_host->grabVideoFrame(360);
    if (frame.isNull()) {
        m_galleryInfo = tr("Kein Videobild verfügbar – bitte zuerst ein Video öffnen.");
        emit galleryChanged();
        return false;
    }
    const QList<LutProfile> profiles = [this] {
        QList<LutProfile> l;
        for (const auto &v : m_library->list()) if (auto *p = m_library->find(v.toMap().value("key").toString())) l << *p;
        return l;
    }();
    QDir dir(QDir(m_host->pluginDataDir(m_id)).filePath(QStringLiteral("gallery")));
    dir.mkpath(QStringLiteral("."));
    for (const auto &fi : dir.entryInfoList({QStringLiteral("*.jpg")}, QDir::Files)) QFile::remove(fi.absoluteFilePath());
    const int gen = ++m_galleryGen;
    const QString orig = dir.filePath(QStringLiteral("g%1-original.jpg").arg(gen));
    frame.save(orig, "JPG", 88);
    const double strength = m_params.lutStrength;
    QList<int> idx;
    for (int i = 0; i < profiles.size(); ++i) idx << i;
    // Parallel berechnen und speichern (84 Bilder à 360 px: wenige hundert ms)
    QtConcurrent::blockingMap(idx, [&](int i) {
        applyLutMatrix(frame, profiles[i].matrix, strength).save(dir.filePath(QStringLiteral("g%1-%2.jpg").arg(gen).arg(i)), "JPG", 85);
    });
    QVariantList items;
    for (int i = 0; i < profiles.size(); ++i)
        items << QVariantMap{{"key", profiles[i].key()}, {"name", profiles[i].name}, {"group", profiles[i].group},
                             {"url", QUrl::fromLocalFile(dir.filePath(QStringLiteral("g%1-%2.jpg").arg(gen).arg(i))).toString()}};
    m_galleryItems = items;
    m_galleryOriginal = QUrl::fromLocalFile(orig).toString();
    m_galleryInfo = tr("%1 LUT-Profile · Frame bei %2 · Stärke %3 · %4 ms")
                        .arg(profiles.size()).arg(QDateTime::fromSecsSinceEpoch(qint64(m_host->currentPosition()), QTimeZone::UTC).toString(QStringLiteral("HH:mm:ss")))
                        .arg(strength, 0, 'f', 2).arg(t.elapsed());
    if (profiles.isEmpty()) m_galleryInfo = tr("Noch keine LUT-Profile – „LUT-Paket von GitHub“ laden.");
    emit galleryChanged();
    return true;
}

void UveController::loadExampleProfile()
{
    // Wie UVE „Load Example“: My_Profile.json aus dem Repository laden und anwenden
    m_library->fetchExampleProfile([this](bool ok, const QByteArray &json, const QString &msg) {
        if (!ok) { setError(msg); m_host->showOsd(msg, 2500); return; }
        QString error;
        if (!applyProfileJson(json, QStringLiteral("My_Profile.json (GitHub)"), &error)) { setError(error); return; }
        savePreset(QStringLiteral("UVE My Profile (GitHub)"));
        // Benötigtes LUT-Profil fehlt → zugehöriges Paket gleich mitladen (Teil derselben Benutzeraktion)
        if (!m_params.lutProfile.isEmpty() && !m_library->find(m_params.lutProfile)) downloadLutPackage();
    });
}

bool UveController::exportProfile(const QString &fileOrUrl)
{
    QSaveFile f(toLocal(fileOrUrl));
    if (!f.open(QIODevice::WriteOnly)) { setError(tr("Zieldatei nicht beschreibbar")); return false; }
    f.write(QJsonDocument(profileFromParams(m_params, m_passthrough)).toJson());
    if (!f.commit()) { setError(tr("Speichern fehlgeschlagen")); return false; }
    m_host->showOsd(tr("UVE-Profil exportiert"), 1500);
    return true;
}

// ---------------------------------------------------------------- Persistenz
void UveController::persist()
{
    QJsonObject o{{"params", paramsToJson(m_params)}, {"activePreset", m_activePreset}, {"defaultPreset", m_defaultPreset},
                  {"mediaPresets", m_mediaPresets}, {"passthrough", m_passthrough}, {"schemaVersion", 1}};
    m_host->savePluginSettings(m_id, o);
}

void UveController::loadState()
{
    const auto o = m_host->loadPluginSettings(m_id);
    if (o.contains("params")) {
        m_params = paramsFromJson(o.value("params").toObject());
        m_params.compare = false; // Vergleich startet immer aus
    }
    m_activePreset = o.value("activePreset").toString(QStringLiteral("UVE Standard"));
    m_defaultPreset = o.value("defaultPreset").toString();
    m_mediaPresets = o.value("mediaPresets").toObject();
    m_passthrough = o.value("passthrough").toObject();
    reloadLut();
}

} // namespace uve::fx
