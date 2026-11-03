// Plugin-Erweiterungsmodell (seit 1.0.6): Plugin-Ordner, reine QML-Plugins, Manifest-Beiträge,
// Befehle/Ereignisse, Adressauflösung und Installation von Paketen.
#include "core/AppSettings.h"
#include "player/MpvController.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginManifest.h"
#include "rendering/ShaderChain.h"
#include "../../plugins/uve/engine/MiniZip.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace uve;

static void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

// Minimaler ZIP-Schreiber (Methode "stored") für Testpakete; Namen unverändert (auch mit Backslash)
static QByteArray makeZip(const QList<QPair<QString, QByteArray>> &entries)
{
    auto u16 = [](QByteArray &b, quint16 v) { b.append(char(v & 0xff)).append(char(v >> 8)); };
    auto u32 = [&](QByteArray &b, quint32 v) { u16(b, quint16(v & 0xffff)); u16(b, quint16(v >> 16)); };
    QByteArray out, central;
    for (const auto &[name, data] : entries) {
        const QByteArray n = name.toUtf8();
        const quint32 crc = fx::crc32(data), off = quint32(out.size());
        u32(out, 0x04034b50); u16(out, 20); u16(out, 0x800); u16(out, 0); u16(out, 0); u16(out, 0);
        u32(out, crc); u32(out, quint32(data.size())); u32(out, quint32(data.size())); u16(out, quint16(n.size())); u16(out, 0);
        out += n + data;
        u32(central, 0x02014b50); u16(central, 20); u16(central, 20); u16(central, 0x800); u16(central, 0); u16(central, 0); u16(central, 0);
        u32(central, crc); u32(central, quint32(data.size())); u32(central, quint32(data.size())); u16(central, quint16(n.size()));
        u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0); u32(central, 0); u32(central, off);
        central += n;
    }
    const quint32 cdOff = quint32(out.size());
    out += central;
    u32(out, 0x06054b50); u16(out, 0); u16(out, 0); u16(out, quint16(entries.size())); u16(out, quint16(entries.size()));
    u32(out, quint32(central.size())); u32(out, cdOff); u16(out, 0);
    return out;
}

// Testdaten stehen am Dateiende (Rohstring-Literale vor der Klasse verwirren moc)
extern const char *kManifest;
extern const char *kMainQml;

class TstPlugins : public QObject
{
    Q_OBJECT
    QTemporaryDir m_tmp;
    QString pluginsDir() const { return m_tmp.filePath("plugins"); }

private slots:
    void initTestCase()
    {
        QCoreApplication::setApplicationVersion("1.0.6");
        QVERIFY(m_tmp.isValid());
        const QString dir = QDir(pluginsDir()).filePath("hello");
        writeFile(dir + "/manifest.json", kManifest);
        writeFile(dir + "/Main.qml", kMainQml);
        writeFile(dir + "/Page.qml", "import QtQuick\nItem {}\n");
        // fehlerhafter Plugin-Ordner: weder library noch main
        writeFile(QDir(pluginsDir()).filePath("broken/manifest.json"),
                  R"({"id":"org.test.broken","name":"Broken","version":"1.0.0","apiVersion":1})");
    }

    void localizedText()
    {
        QCOMPARE(uve::localizedText(QJsonValue("x"), "en"), QString("x"));
        const QJsonObject o{{"de", "Hallo"}, {"en", "Hello"}};
        QCOMPARE(uve::localizedText(o, "en"), QString("Hello"));
        QCOMPARE(uve::localizedText(o, "de"), QString("Hallo"));
        QCOMPARE(uve::localizedText(o, "pl"), QString("Hello"));   // Rückfall Englisch
        QCOMPARE(uve::localizedText(QJsonObject{{"it", "Ciao"}}, "pl"), QString("Ciao"));
    }

    void qmlPluginFromFolder()
    {
        AppSettings settings(m_tmp.filePath("data"));
        MpvController mpv;
        ShaderChain shaders(&mpv);
        QQmlEngine engine;
        PluginManager pm(&settings, &mpv, &shaders, {pluginsDir()}, false);
        pm.setQmlEngine(&engine);
        pm.registerCommand("test.echo", [](const QString &id, const QVariantMap &a, QString *) {
            return QVariant(id + ":" + a.value("text").toString());
        });
        pm.discoverAndStart();

        // Zustände: QML-Plugin aktiv, fehlerhafter Ordner abgelehnt
        QVariantMap hello, broken;
        for (const auto &v : pm.plugins()) {
            const auto m = v.toMap();
            if (m.value("id") == "org.test.hello") hello = m;
            if (m.value("id") == "org.test.broken") broken = m;
        }
        QCOMPARE(hello.value("state").toString(), QString("aktiv"));
        QCOMPARE(hello.value("kind").toString(), QString("qml"));
        QCOMPARE(broken.value("state").toString(), QString("abgelehnt"));
        QObject *logic = hello.value("controller").value<QObject *>();
        QVERIFY(logic);
        QVERIFY(logic->property("activated").toBool());

        // Seiten, Menü, Tastenkürzel aus dem Manifest (Sprache Deutsch, dann Englisch)
        const auto panels = pm.panels();
        QCOMPARE(panels.size(), 2);
        QCOMPARE(panels[0].toMap().value("key").toString(), QString("org.test.hello/page"));
        QCOMPARE(panels[0].toMap().value("title").toString(), QString("Seite"));
        QCOMPARE(panels[1].toMap().value("location").toString(), QString("settings"));
        QVERIFY(panels[0].toMap().value("url").toUrl().toLocalFile().endsWith("hello/Page.qml"));
        pm.setLanguage("en");
        QCOMPARE(pm.panels()[0].toMap().value("title").toString(), QString("Page"));
        QCOMPARE(pm.menuItems().size(), 1);
        QCOMPARE(pm.menuItems()[0].toMap().value("title").toString(), QString("Say"));
        QCOMPARE(pm.menuItems()[0].toMap().value("actionId").toString(), QString("plugin:org.test.hello:say"));
        QCOMPARE(pm.hotkeyActions().size(), 1);
        QCOMPARE(pm.hotkeyActions()[0].toMap().value("defaultSeq").toString(), QString("Ctrl+Alt+H"));

        // Befehle
        QString err;
        QCOMPARE(pm.call("org.test.hello", "test.echo", {{"text", "hi"}}, &err).toString(), QString("org.test.hello:hi"));
        QVERIFY(err.isEmpty());
        QVERIFY(!pm.call("org.test.hello", "does.not.exist", {}, &err).isValid());
        QVERIFY(err.contains("unknown command"));
        QVERIFY(pm.hasCommand("player.state") == false);   // registriert erst PlayerApp
        QVERIFY(pm.hasCommand("media.resolved"));
        QVERIFY(pm.commands().contains("mpv.get"));

        // Ereignisse: Menüeintrag → action.triggered nur an dieses Plugin
        pm.triggerAction("plugin:org.test.hello:say");
        pm.emitEvent("playback.paused", {{"paused", true}});
        const QStringList got = logic->property("received").toStringList();
        QVERIFY(got.contains("action.triggered"));
        QVERIFY(got.contains("playback.paused"));
        QVERIFY(got.contains("language.changed"));

        // Adressauflösung über urlHandlers; Antwort per Befehl media.resolved
        QSignalSpy spy(&pm, &PluginManager::mediaResolved);
        QVERIFY(!pm.resolveMedia(1, "https://other.test/a"));
        QVERIFY(pm.resolveMedia(7, "https://example.test/kanal"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), 7ull);
        QCOMPARE(spy.at(0).at(1).toString(), QString("tcp://resolved.test:8111"));
        QCOMPARE(spy.at(0).at(3).toString(), QString("Hello"));
        // Antwort ohne passende Anfrage wird abgelehnt
        QCOMPARE(pm.call("org.test.hello", "media.resolved", {{"requestId", 99}, {"url", "x"}}, &err).toBool(), false);

        // Abschalten zur Laufzeit: keine Beiträge mehr
        QVERIFY(pm.setEnabled("org.test.hello", false));
        QVERIFY(!logic->property("activated").toBool());
        QCOMPARE(pm.panels().size(), 0);
        QCOMPARE(pm.menuItems().size(), 0);
        QVERIFY(!pm.resolveMedia(8, "https://example.test/kanal"));
        pm.shutdown();
    }

    void installPackage()
    {
        // Paket als Ordner: wird vorgemerkt und beim nächsten Start installiert
        QTemporaryDir pkg;
        writeFile(pkg.filePath("manifest.json"),
                  R"({"id":"org.test.installed","name":"Installed","version":"2.0.0","apiVersion":1,"main":"Main.qml"})");
        writeFile(pkg.filePath("Main.qml"), "import QtQml\nQtObject {}\n");
        writeFile(pkg.filePath("lang/installed_en.qm"), "x");

        QTemporaryDir user;
        AppSettings settings(m_tmp.filePath("data2"));
        MpvController mpv;
        ShaderChain shaders(&mpv);
        QQmlEngine engine;
        {
            PluginManager pm(&settings, &mpv, &shaders, {user.path()}, false);
            QCOMPARE(pm.installPackage(pkg.path()), QString());
            QVERIFY(QFileInfo::exists(user.filePath(".pending/org.test.installed/Main.qml")));
            QVERIFY(QFileInfo::exists(user.filePath(".pending/org.test.installed/lang/installed_en.qm")));
            // ungültige Pakete
            QVERIFY(!pm.installPackage(m_tmp.filePath("gibt-es-nicht.txt")).isEmpty());
            QTemporaryDir bad;
            writeFile(bad.filePath("manifest.json"), R"({"id":"org.test.bad","name":"Bad","version":"1.0.0","apiVersion":99,"main":"M.qml"})");
            QVERIFY(pm.installPackage(bad.path()).contains("apiVersion"));
        }
        {
            // ZIP wie von Windows PowerShell (Compress-Archive): Unterordner, Backslash-Pfade, Ordnereinträge
            PluginManager pm(&settings, &mpv, &shaders, {user.path()}, false);
            const QString zip = m_tmp.filePath("zipped.zip");
            const QByteArray zippedManifest =
                "{\"id\":\"org.test.zipped\",\"name\":\"Zipped\",\"version\":\"1.0.0\",\"apiVersion\":1,\"main\":\"Main.qml\"}";
            writeFile(zip, makeZip({{"zipped\\", {}},
                                    {"zipped\\manifest.json", zippedManifest},
                                    {"zipped\\Main.qml", "import QtQml\nQtObject {}\n"},
                                    {"zipped\\lang\\zipped_en.qm", "x"}}));
            QCOMPARE(pm.installPackage(zip), QString());
            QVERIFY(QFileInfo::exists(user.filePath(".pending/org.test.zipped/Main.qml")));
            QVERIFY(QFileInfo::exists(user.filePath(".pending/org.test.zipped/lang/zipped_en.qm")));
            // Pfad-Traversal wird abgelehnt
            const QString evil = m_tmp.filePath("evil.zip");
            const QByteArray evilManifest =
                "{\"id\":\"org.test.evil\",\"name\":\"Evil\",\"version\":\"1.0.0\",\"apiVersion\":1,\"main\":\"Main.qml\"}";
            writeFile(evil, makeZip({{"manifest.json", evilManifest}, {"../../evil.txt", "x"}}));
            QVERIFY(pm.installPackage(evil).contains("evil.txt"));
        }
        PluginManager pm(&settings, &mpv, &shaders, {user.path()}, false);
        pm.setQmlEngine(&engine);
        pm.discoverAndStart();
        QVERIFY(QFileInfo::exists(user.filePath("org.test.installed/manifest.json")));
        QVERIFY(QFileInfo::exists(user.filePath("org.test.zipped/lang/zipped_en.qm")));
        QVERIFY(!QFileInfo::exists(user.filePath(".pending/org.test.installed")));
        bool found = false;
        for (const auto &v : pm.plugins())
            if (v.toMap().value("id") == "org.test.installed") found = v.toMap().value("state") == "aktiv";
        QVERIFY(found);
        pm.shutdown();
    }
};

const char *kManifest = R"({
  "id": "org.test.hello", "name": "Hello", "version": "1.0.0", "apiVersion": 1,
  "type": "utility", "main": "Main.qml",
  "contributes": {
    "panels": [ { "id": "page", "title": { "de": "Seite", "en": "Page" }, "qml": "Page.qml" },
                { "id": "prefs", "title": "Optionen", "qml": "Page.qml", "location": "settings" } ],
    "menu": [ { "id": "say", "title": { "de": "Sagen", "en": "Say" } } ],
    "hotkeys": [ { "id": "hk", "title": "Hallo-Taste", "default": "Ctrl+Alt+H" } ],
    "urlHandlers": [ "^https://example\\.test/" ]
  }
})";

// Plugin-Logik als reines QML: protokolliert Ereignisse und beantwortet media.resolve
const char *kMainQml = R"(import QtQml
QtObject {
    id: root
    property var plugin
    property var received: []
    property bool activated: false
    function activate() { activated = true }
    function deactivate() { activated = false }
    property Connections c: Connections {
        target: root.plugin
        function onEvent(name, data) {
            root.received = root.received.concat([name])
            if (name === "media.resolve")
                root.plugin.call("media.resolved", { requestId: data.requestId, url: "tcp://resolved.test:8111" })
        }
    }
}
)";

QTEST_GUILESS_MAIN(TstPlugins)
#include "tst_plugins.moc"
