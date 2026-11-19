// Lädt ein Plugin-Modul so, wie der UVE Player es lädt (QPluginLoader), und prüft IID und Schnittstelle.
// Läuft mit den Qt-Bibliotheken aus dem veröffentlichten Player-Paket: Fehlende oder unpassende
// Laufzeitabhängigkeiten fallen hier auf statt erst beim Benutzer.
//   uve-plugin-loadcheck <modul> [<manifest.json>]
#include "PluginApi.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QLibraryInfo>
#include <QPluginLoader>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2) { std::fprintf(stderr, "usage: uve-plugin-loadcheck <module> [manifest.json]\n"); return 2; }
    std::printf("Qt %s from %s\n", qVersion(), qPrintable(QLibraryInfo::path(QLibraryInfo::LibrariesPath)));

    QPluginLoader loader(args.at(1));
    const QJsonObject meta = loader.metaData();
    const QString iid = meta.value("IID").toString();
    std::printf("IID: %s\n", qPrintable(iid));
    if (iid != QLatin1String(UVE_PLAYER_PLUGIN_IID)) {
        std::fprintf(stderr, "error: IID is not %s\n", UVE_PLAYER_PLUGIN_IID);
        return 1;
    }
    const QJsonObject embedded = meta.value("MetaData").toObject();
    if (args.size() > 2) {   // eingebettetes Manifest = Paket-Manifest (id, version)
        QFile f(args.at(2));
        const QJsonObject pkg = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
        for (const char *key : {"id", "version"}) {
            if (embedded.value(key) != pkg.value(key)) {
                std::fprintf(stderr, "error: embedded manifest %s '%s' differs from package manifest '%s'\n", key,
                             qPrintable(embedded.value(key).toString()), qPrintable(pkg.value(key).toString()));
                return 1;
            }
        }
    }
    QObject *obj = loader.instance();
    if (!obj) {
        std::fprintf(stderr, "error: load failed: %s\n", qPrintable(loader.errorString()));
        return 1;
    }
    if (!qobject_cast<uve::IPlayerPlugin *>(obj)) {
        std::fprintf(stderr, "error: module does not implement uve::IPlayerPlugin\n");
        return 1;
    }
    std::printf("ok: %s %s loads, class %s\n", qPrintable(embedded.value("id").toString()),
                qPrintable(embedded.value("version").toString()), obj->metaObject()->className());
    return 0;
}
