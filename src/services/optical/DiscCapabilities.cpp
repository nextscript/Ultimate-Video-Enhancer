#include "DiscCapabilities.h"

#include "player/MpvController.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace uve::optical {

QString kindId(DiscKind kind)
{
    switch (kind) {
    case DiscKind::Dvd: return QStringLiteral("dvd");
    case DiscKind::Bluray: return QStringLiteral("bluray");
    case DiscKind::AudioCd: return QStringLiteral("cdda");
    default: return {};
    }
}

DiscKind kindFromId(const QString &id)
{
    if (id == QLatin1String("dvd")) return DiscKind::Dvd;
    if (id == QLatin1String("bluray")) return DiscKind::Bluray;
    if (id == QLatin1String("cdda")) return DiscKind::AudioCd;
    return DiscKind::Unknown;
}

bool DiscCapabilities::supports(DiscKind kind) const
{
    switch (kind) {
    case DiscKind::Dvd: return dvd && dvdDevice;
    case DiscKind::Bluray: return bluray && blurayDevice;
    case DiscKind::AudioCd: return cddaReader || (cddaNative && cddaDevice);
    default: return false;
    }
}

bool DiscCapabilities::menus(DiscKind kind) const
{
    if (kind != DiscKind::Dvd && kind != DiscKind::Bluray) return false;
    return supports(kind) && discMenuOption && discnav;
}

DiscCapabilities DiscCapabilities::fromProbe(const QVariantMap &probe)
{
    const QStringList protocols = probe.value("protocols").toStringList();
    const QStringList options = probe.value("options").toStringList();
    const QStringList commands = probe.value("commands").toStringList();
    const QStringList properties = probe.value("properties").toStringList();
    DiscCapabilities c;
    c.dvd = protocols.contains(QLatin1String("dvd")) || protocols.contains(QLatin1String("dvdnav"));
    c.bluray = protocols.contains(QLatin1String("bd")) || protocols.contains(QLatin1String("bluray"));
    c.cddaNative = protocols.contains(QLatin1String("cdda"));
    c.cddaReader = probe.value("cddaReader").toBool();
    c.dvdDevice = options.contains(QLatin1String("dvd-device"));
    c.blurayDevice = options.contains(QLatin1String("bluray-device"));
    c.cddaDevice = options.contains(QLatin1String("cdda-device"));
    c.discMenuOption = options.contains(QLatin1String("disc-menu"));
    c.discMenuActive = properties.contains(QLatin1String("disc-menu-active"));
    c.discnav = commands.contains(QLatin1String("discnav"));
    c.bdj = c.bluray && probe.value("bdj").toBool();
    c.bdjReason = probe.value("bdjReason").toString();
    c.mpvVersion = probe.value("mpvVersion").toString();
    // "-Ddvdnav=enabled -Dlibbluray=enabled …" (meson) bzw. "+dvdnav +libbluray" (waf, ältere Builds)
    static const QRegularExpression flag(QStringLiteral("(?:-D|\\+)(dvdnav|dvdread|libbluray|cdda|libcdio)(?:=(\\w+))?"));
    auto it = flag.globalMatch(probe.value("configuration").toString());
    while (it.hasNext()) {
        const auto m = it.next();
        c.buildFlags << m.captured(1) + QLatin1Char('=') + (m.captured(2).isEmpty() ? QStringLiteral("enabled") : m.captured(2));
    }
    return c;
}

QVariantMap DiscCapabilities::probe(const MpvController *player)
{
    QVariantMap p;
    if (!player || !player->handle()) return p;
    const auto names = [](const QVariant &list, const char *key) {
        QStringList out;
        for (const auto &v : list.toList()) out << (key ? v.toMap().value(QLatin1String(key)).toString() : v.toString());
        return out;
    };
    p.insert("protocols", names(player->getProperty(QStringLiteral("protocol-list")), nullptr));
    QStringList options;
    for (const char *o : {"dvd-device", "bluray-device", "cdda-device", "disc-menu"})
        if (player->getProperty(QStringLiteral("option-info/%1/name").arg(QLatin1String(o))).isValid()) options << QLatin1String(o);
    p.insert("options", options);
    p.insert("commands", names(player->getProperty(QStringLiteral("command-list")), "name"));
    p.insert("properties", names(player->getProperty(QStringLiteral("property-list")), nullptr));
    p.insert("configuration", player->getProperty(QStringLiteral("mpv-configuration")).toString());
    p.insert("mpvVersion", player->mpvVersion());
    return p;
}

bool DiscCapabilities::findBdjRuntime(const QString &appDir, QString *reason)
{
#ifndef UVE_WITH_BDJ
    Q_UNUSED(appDir);
    if (reason) *reason = QStringLiteral("build");   // ohne UVE_WITH_BDJ gebaut
    return false;
#else
    // libbluray sucht die JAR über LIBBLURAY_CP oder neben der Bibliothek; Java über JAVA_HOME bzw. PATH
    QStringList jarDirs{appDir, QDir(appDir).filePath(QStringLiteral("mpv")), QDir(appDir).filePath(QStringLiteral("bdj"))};
    if (qEnvironmentVariableIsSet("LIBBLURAY_CP")) jarDirs.prepend(QFileInfo(qEnvironmentVariable("LIBBLURAY_CP")).absolutePath());
    bool jar = false;
    for (const auto &d : jarDirs)
        if (!QDir(d).entryList({QStringLiteral("libbluray-j2se-*.jar")}, QDir::Files).isEmpty()) { jar = true; break; }
    if (!jar) { if (reason) *reason = QStringLiteral("jar"); return false; }
    const QString javaHome = qEnvironmentVariable("JAVA_HOME");
    const bool java = (!javaHome.isEmpty() && QDir(javaHome).exists())
                   || !QStandardPaths::findExecutable(QStringLiteral("java")).isEmpty();
    if (!java) { if (reason) *reason = QStringLiteral("java"); return false; }
    if (reason) reason->clear();
    return true;
#endif
}

QVariantMap DiscCapabilities::toMap() const
{
    return {
        {"dvd", supports(DiscKind::Dvd)}, {"bluray", supports(DiscKind::Bluray)}, {"audioCd", supports(DiscKind::AudioCd)},
        {"cddaNative", cddaNative}, {"cddaReader", cddaReader},
        {"dvdMenus", menus(DiscKind::Dvd)}, {"blurayMenus", menus(DiscKind::Bluray)},
        {"discnav", discnav}, {"discMenuOption", discMenuOption}, {"discMenuActive", discMenuActive},
        {"bdj", bdj}, {"bdjReason", bdjReason}, {"mpvVersion", mpvVersion}, {"buildFlags", buildFlags},
    };
}

} // namespace uve::optical
