// Prüft, ob die gebaute DLL vom Qt-Plugin-System als UVE-Player-Plugin erkannt wird:
// richtiger IID + eingebettetes Manifest + successfully ladbar.
// Aufruf: plugin_probe.exe <pfad\zu\kickchat.dll>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    if (argc < 2) { out << "Verwendung: plugin_probe.exe <kickchat.dll>\n"; return 2; }

    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    const QJsonObject meta = loader.metaData();
    const QString iid = meta.value(QStringLiteral("IID")).toString();

    out << "IID: " << iid << '\n';
    out << "Manifest eingebettet:\n"
        << QString::fromUtf8(QJsonDocument(meta.value(QStringLiteral("MetaData")).toObject()).toJson()) << '\n';

    QObject *inst = loader.instance();
    const bool iidOk = (iid == QStringLiteral("org.nextscript.uveplayer.IPlayerPlugin/1"));
    out << "IID korrekt: " << (iidOk ? "ja" : "NEIN") << '\n';
    out << "Instanz ladbar: " << (inst ? "ja" : "NEIN") << '\n';
    if (!loader.errorString().isEmpty()) out << "Fehler: " << loader.errorString() << '\n';

    return (iidOk && inst) ? 0 : 1;
}
