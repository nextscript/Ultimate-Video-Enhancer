#include "PluginManifest.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QDir>
#include <QRegularExpression>
#include <QSysInfo>

namespace uve {

static QStringList toStringList(const QJsonValue &v)
{
    QStringList out;
    for (const auto &x : v.toArray()) out << x.toString();
    return out;
}

static QJsonArray fromStringList(const QStringList &l)
{
    QJsonArray a;
    for (const auto &s : l) a.append(s);
    return a;
}

PluginManifest PluginManifest::fromJson(const QJsonObject &o)
{
    PluginManifest m;
    m.id = o.value("id").toString();
    m.name = o.value("name").toString();
    m.version = o.value("version").toString();
    m.apiVersion = o.value("apiVersion").toInt(0);
    m.minHostVersion = o.value("minHostVersion").toString();
    m.type = o.value("type").toString();
    m.entrypoint = o.value("entrypoint").toString();
    m.description = o.value("description");
    m.author = o.value("author").toString();
    m.license = o.value("license").toString();
    m.capabilities = toStringList(o.value("capabilities"));
    m.supportedPlatforms = toStringList(o.value("supportedPlatforms"));
    m.requiredBackends = toStringList(o.value("requiredBackends"));
    m.permissions = toStringList(o.value("permissions"));
    m.configurationSchema = o.value("configurationSchema").toObject();
    m.library = o.value("library").toString();
    m.main = o.value("main").toString();
    const QJsonObject libs = o.value("libraries").toObject();
    for (auto it = libs.constBegin(); it != libs.constEnd(); ++it)
        m.libraries.insert(it.key(), it.value().toString());
    m.contributes = o.value("contributes").toObject();
    return m;
}

QJsonObject PluginManifest::toJson() const
{
    QJsonObject o{
        {"id", id}, {"name", name}, {"version", version}, {"apiVersion", apiVersion},
        {"minHostVersion", minHostVersion}, {"type", type}, {"entrypoint", entrypoint},
        {"description", description}, {"author", author}, {"license", license},
        {"capabilities", fromStringList(capabilities)},
        {"supportedPlatforms", fromStringList(supportedPlatforms)},
        {"requiredBackends", fromStringList(requiredBackends)},
        {"permissions", fromStringList(permissions)},
        {"configurationSchema", configurationSchema},
        {"library", library}, {"main", main}, {"contributes", contributes},
    };
    if (!libraries.isEmpty()) {
        QJsonObject libs;
        for (auto it = libraries.constBegin(); it != libraries.constEnd(); ++it) libs.insert(it.key(), it.value());
        o.insert("libraries", libs);
    }
    return o;
}

QString localizedText(const QJsonValue &v, const QString &language)
{
    if (v.isString()) return v.toString();
    const QJsonObject o = v.toObject();
    if (o.isEmpty()) return {};
    for (const QString &k : {language, language.section('_', 0, 0), QStringLiteral("en"), QStringLiteral("de")})
        if (!k.isEmpty() && o.contains(k)) return o.value(k).toString();
    return o.begin().value().toString();
}

int compareVersions(const QString &a, const QString &b)
{
    auto parts = [](const QString &v) {
        QList<int> p;
        const auto core = v.section('-', 0, 0).section('+', 0, 0);
        for (const auto &s : core.split('.')) p << s.toInt();
        while (p.size() < 3) p << 0;
        return p;
    };
    const auto pa = parts(a), pb = parts(b);
    for (int i = 0; i < 3; ++i) {
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}

QString currentPlatformId()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QString targetKey(const QString &platform, const QString &qtCpuArchitecture)
{
    // Namen wie in den Paketen üblich: Windows "x64", Linux "x86_64"/"aarch64", macOS "arm64"/"x86_64"
    QString arch = qtCpuArchitecture;
    if (platform == QLatin1String("windows")) {
        if (arch == QLatin1String("x86_64")) arch = QStringLiteral("x64");
        else if (arch == QLatin1String("i386")) arch = QStringLiteral("x86");
    } else if (platform == QLatin1String("linux")) {
        if (arch == QLatin1String("arm64")) arch = QStringLiteral("aarch64");
    }
    return platform + QLatin1Char('-') + arch;
}

QString currentTargetKey()
{
    // buildCpuArchitecture: Architektur, für die der Player gebaut wurde = Architektur des laufenden Prozesses
    return targetKey(currentPlatformId(), QSysInfo::buildCpuArchitecture());
}

bool isSafeRelativePath(const QString &path, QString *cleaned)
{
    const QString p = QString(path).replace(QLatin1Char('\\'),QLatin1Char('/'));
    if (p.trimmed().isEmpty() || p.startsWith(QLatin1Char('/')) || p.contains(QLatin1Char(':')) || QDir::isAbsolutePath(p)) return false;
    const QString c = QDir::cleanPath(p);
    if (c == QLatin1String(".") || c == QLatin1String("..") || c.startsWith(QLatin1String("../"))) return false;
    if (cleaned) *cleaned = c;
    return true;
}

ModuleSelection selectModule(const PluginManifest &m, const QString &target)
{
    ModuleSelection s;
    QString raw;
    if (!m.libraries.isEmpty()) {
        if (!m.libraries.contains(target)) {
            s.error = QCoreApplication::translate("PluginManifest", "Plattform oder Architektur '%1' wird von diesem Paket nicht unterstützt (enthält: %2)")
                          .arg(target, m.libraries.keys().join(", "));
            return s;
        }
        s.kind = ModuleSelection::Library;
        s.fromLibraries = true;
        raw = m.libraries.value(target);
    } else if (!m.library.isEmpty()) {
        s.kind = ModuleSelection::Library;
        raw = m.library;
    } else if (!m.main.isEmpty()) {
        s.kind = ModuleSelection::Qml;
        raw = m.main;
    } else {
        s.error = QCoreApplication::translate("PluginManifest", "manifest.json braucht \"library\", \"libraries\" oder \"main\"");
        return s;
    }
    if (!isSafeRelativePath(raw, &s.path)) {
        s.error = QCoreApplication::translate("PluginManifest", "Ungültiger Pfad im Manifest: '%1' (muss im Plugin-Ordner liegen)").arg(raw);
        s.path.clear();
    }
    return s;
}

ManifestCheck validateManifest(const PluginManifest &m, const QString &hostVersion, int hostApiVersion,
                               const QString &platform, const QStringList &availableBackends)
{
    ManifestCheck r;
    static const QRegularExpression idRe(QStringLiteral("^[a-z0-9]+([.-][a-z0-9]+)+$"));
    static const QRegularExpression verRe(QStringLiteral("^\\d+\\.\\d+\\.\\d+([-+].*)?$"));

    if (m.id.isEmpty()) r.errors << QCoreApplication::translate("PluginManifest", "Feld 'id' fehlt");
    else if (!idRe.match(m.id).hasMatch()) r.errors << QCoreApplication::translate("PluginManifest", "Ungültige id '%1' (erwartet z. B. org.firma.plugin)").arg(m.id);
    if (m.name.trimmed().isEmpty()) r.errors << QCoreApplication::translate("PluginManifest", "Feld 'name' fehlt");
    if (!verRe.match(m.version).hasMatch()) r.errors << QCoreApplication::translate("PluginManifest", "Ungültige Plugin-Version '%1'").arg(m.version);
    if (m.apiVersion != hostApiVersion)
        r.errors << QCoreApplication::translate("PluginManifest", "Inkompatible apiVersion %1 (Host unterstützt %2)").arg(m.apiVersion).arg(hostApiVersion);
    if (!m.minHostVersion.isEmpty()) {
        if (!verRe.match(m.minHostVersion).hasMatch())
            r.errors << QCoreApplication::translate("PluginManifest", "Ungültige minHostVersion '%1'").arg(m.minHostVersion);
        else if (compareVersions(hostVersion, m.minHostVersion) < 0)
            r.errors << QCoreApplication::translate("PluginManifest", "Benötigt Host ≥ %1 (vorhanden %2)").arg(m.minHostVersion, hostVersion);
    }
    if (!m.supportedPlatforms.isEmpty() && !m.supportedPlatforms.contains(platform))
        r.errors << QCoreApplication::translate("PluginManifest", "Plattform '%1' nicht unterstützt (%2)").arg(platform, m.supportedPlatforms.join(", "));
    for (const auto &b : m.requiredBackends) {
        if (!availableBackends.contains(b)) r.errors << QCoreApplication::translate("PluginManifest", "Benötigtes Backend fehlt: %1").arg(b);
    }
    r.ok = r.errors.isEmpty();
    return r;
}

} // namespace uve
