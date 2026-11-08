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

    void shaderSubStagesKeepOrderAndCleanUp()
    {
        const QDir dir(m_tmp.path());
        const QString a = dir.filePath("stage-a.glsl"), b = dir.filePath("stage-b.glsl"), other = dir.filePath("stage-c.glsl");
        for (const auto &f : {a, b, other}) writeFile(f, "// leer");
        MpvController mpv;
        ShaderChain chain(&mpv);
        const QString dn = "org.test.fx#denoise", fx = "org.test.fx";
        // Leere Anmeldung legt nur die Reihenfolge fest; bestehende Stufen werden nicht umsortiert
        chain.setStage(dn, QString());
        chain.setStage(fx, a);
        chain.setStage("org.test.other", other);
        QCOMPARE(chain.activeShaders(), QStringList({a, other}));
        chain.setStage(dn, b);
        QCOMPARE(chain.activeShaders(), QStringList({b, a, other}));
        // "Aus" entfernt ausschließlich die eigene Stufe
        chain.setStage(dn, QString());
        QCOMPARE(chain.activeShaders(), QStringList({a, other}));
        chain.setStage(dn, b);
        chain.removeSubStages(fx);
        QCOMPARE(chain.activeShaders(), QStringList({a, other}));
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
        QVERIFY(!pm.resolverSupportsQuality("https://other.test/a"));
        QVERIFY(pm.resolverSupportsQuality("https://example.test/kanal"));
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

    // Neue Version eines installierten Plugins ersetzt die alte, auch wenn deren Ordner anders heißt
    void modalRequests()
    {
        QTemporaryDir user, data;
        writeFile(user.filePath("m/manifest.json"), R"({"id":"org.test.modal","name":"M","version":"1.0.0","apiVersion":1,"main":"Main.qml",
            "contributes":{"panels":[{"id":"dlg","title":"Dialog","qml":"Dlg.qml","location":"modal","modal":{"scope":"video","mode":"fixed","width":300}}]}})");
        writeFile(user.filePath("m/Main.qml"), R"(import QtQml
QtObject {
    property var plugin
    property var closed: []
    property Connections c: Connections { target: plugin; function onEvent(name, data) { if (name === "modal.closed") closed = closed.concat([data.panel + ":" + data.result]) } }
}
)");
        writeFile(user.filePath("m/Dlg.qml"), "import QtQuick\nItem {}\n");
        AppSettings settings(data.path());
        MpvController mpv;
        ShaderChain shaders(&mpv);
        QQmlEngine engine;
        PluginManager pm(&settings, &mpv, &shaders, {user.path()}, false);
        pm.setQmlEngine(&engine);
        pm.discoverAndStart();

        QSignalSpy opened(&pm, &PluginManager::modalRequested);
        QSignalSpy closed(&pm, &PluginManager::modalCloseRequested);
        const QString id = "org.test.modal";
        QString err;

        // Vorgaben aus dem Manifest
        QVERIFY(pm.call(id, "modal.open", {{"panel", "dlg"}}, &err).toBool());
        auto r = opened.last().at(0).toMap();
        QCOMPARE(r.value("key").toString(), QString("org.test.modal/dlg"));
        QCOMPARE(r.value("scope").toString(), QString("video"));
        QCOMPARE(r.value("mode").toString(), QString("fixed"));
        QCOMPARE(r.value("width").toDouble(), 300.0);

        // Argumente haben Vorrang; ohne Vorgabe gilt der ganze Player und verschiebbar
        QVERIFY(pm.call(id, "modal.open", {{"panel", "dlg"}, {"scope", "player"}, {"mode", "movable"}, {"maxWidth", 500}, {"width", -5},
                                            {"title", "T"}, {"data", QVariantMap{{"a", 1}}}}, &err).toBool());
        r = opened.last().at(0).toMap();
        QCOMPARE(r.value("scope").toString(), QString("player"));
        QCOMPARE(r.value("mode").toString(), QString("movable"));
        QCOMPARE(r.value("maxWidth").toDouble(), 500.0);
        QCOMPARE(r.value("width").toDouble(), 0.0);
        QCOMPARE(r.value("title").toString(), QString("T"));
        QCOMPARE(r.value("data").toMap().value("a").toInt(), 1);

        // Abgelehnt: unbekannte Seite, fremdes Plugin, unbekannter Bereich oder Modus
        const int before = opened.count();
        QVERIFY(!pm.call(id, "modal.open", {{"panel", "nope"}}, &err).isValid());
        QVERIFY(err.contains("unknown panel"));
        QVERIFY(!pm.call("org.test.other", "modal.open", {{"panel", "dlg"}}, &err).isValid());
        QVERIFY(!pm.call(id, "modal.open", {{"panel", "dlg"}, {"scope", "desktop"}}, &err).isValid());
        QVERIFY(err.contains("unknown scope"));
        QVERIFY(!pm.call(id, "modal.open", {{"panel", "dlg"}, {"mode", "wild"}}, &err).isValid());
        QVERIFY(err.contains("unknown mode"));
        QCOMPARE(opened.count(), before);

        QVERIFY(pm.call(id, "modal.close", {{"panel", "dlg"}}, &err).toBool());
        QCOMPARE(closed.last().at(1).toString(), QString("org.test.modal/dlg"));
        QVERIFY(pm.call(id, "modal.close", {}, &err).toBool());
        QCOMPARE(closed.last().at(1).toString(), QString());

        // Ereignis an das Plugin
        pm.notifyModalClosed("org.test.modal/dlg", "ok");
        QObject *logic = pm.plugins().first().toMap().value("controller").value<QObject *>();
        QVERIFY(logic);
        QCOMPARE(logic->property("closed").toStringList(), QStringList{"dlg:ok"});
    }

    void panelPlacement()
    {
        QTemporaryDir user, data;
        writeFile(user.filePath("p/manifest.json"), R"({"id":"org.test.place","name":"P","version":"1.0.0","apiVersion":1,"main":"Main.qml",
            "contributes":{"panels":[{"id":"side","title":"Side","qml":"S.qml"},
                                     {"id":"conf","title":"Conf","qml":"S.qml","location":"settings"},
                                     {"id":"dlg","title":"Dlg","qml":"S.qml","location":"modal"}]}})");
        writeFile(user.filePath("p/Main.qml"), "import QtQml\nQtObject {}\n");
        writeFile(user.filePath("p/S.qml"), "import QtQuick\nItem {}\n");
        AppSettings settings(data.path());
        MpvController mpv;
        ShaderChain shaders(&mpv);
        QQmlEngine engine;
        const auto panel = [](const PluginManager &pm, const QString &key) {
            for (const auto &v : pm.panels())
                if (v.toMap().value("key") == key) return v.toMap();
            return QVariantMap();
        };
        const QString side = "org.test.place/side";
        {
            PluginManager pm(&settings, &mpv, &shaders, {user.path()}, false);
            pm.setQmlEngine(&engine);
            pm.discoverAndStart();
            QCOMPARE(panel(pm, side).value("location").toString(), QString("sidebar"));
            QVERIFY(panel(pm, side).value("movable").toBool());
            QVERIFY(!panel(pm, "org.test.place/conf").value("movable").toBool());
            QVERIFY(!panel(pm, "org.test.place/dlg").value("movable").toBool());

            QSignalSpy changed(&pm, &PluginManager::pluginsChanged);
            QVERIFY(pm.setPanelPlacement(side, "hidden"));
            QCOMPARE(changed.count(), 1);
            QCOMPARE(panel(pm, side).value("location").toString(), QString("hidden"));
            QVERIFY(pm.setPanelPlacement(side, "settings"));
            QCOMPARE(panel(pm, side).value("location").toString(), QString("settings"));

            // Abgelehnt: unbekannte Werte und Seiten, die nicht in die Seitenleiste gehören
            QVERIFY(!pm.setPanelPlacement(side, "desktop"));
            QVERIFY(!pm.setPanelPlacement("org.test.place/nope", "hidden"));
            QVERIFY(!pm.setPanelPlacement("org.test.place/conf", "hidden"));
            QVERIFY(!pm.setPanelPlacement("org.test.place/dlg", "sidebar"));
            QCOMPARE(panel(pm, "org.test.place/conf").value("location").toString(), QString("settings"));
            QCOMPARE(panel(pm, "org.test.place/dlg").value("location").toString(), QString("modal"));
        }
        {   // Bleibt nach einem Neustart erhalten
            PluginManager pm(&settings, &mpv, &shaders, {user.path()}, false);
            pm.setQmlEngine(&engine);
            pm.discoverAndStart();
            QCOMPARE(panel(pm, side).value("location").toString(), QString("settings"));
            QVERIFY(pm.setPanelPlacement(side, "sidebar"));
            QCOMPARE(panel(pm, side).value("location").toString(), QString("sidebar"));
            QVERIFY(settings.object("pluginPanelPlacements").isEmpty());
            QVERIFY(pm.setPanelPlacement(side, "hidden"));
            // Unbrauchbarer gespeicherter Wert: Seite bleibt in der Seitenleiste
            auto all = settings.object("pluginPanelPlacements");
            all.insert(side, "kaputt");
            settings.setObject("pluginPanelPlacements", all);
            QCOMPARE(panel(pm, side).value("location").toString(), QString("sidebar"));
            QVERIFY(pm.setPanelPlacement(side, "hidden"));
            QVERIFY(pm.removePlugin("org.test.place", true).value("ok").toBool());
        }
        {   // Entfernen mit Daten löscht auch die Wahl
            PluginManager pm(&settings, &mpv, &shaders, {user.path()}, false);
            pm.setQmlEngine(&engine);
            pm.discoverAndStart();
            QVERIFY(settings.object("pluginPanelPlacements").isEmpty());
        }
    }

    void removePluginFromUserFolder()
    {
        const auto manifest = [](const char *id) {
            return "{\"id\":\"" + QByteArray(id) + "\",\"name\":\"X\",\"version\":\"1.0.0\",\"apiVersion\":1,\"main\":\"Main.qml\","
                   "\"contributes\":{\"panels\":[{\"id\":\"s\",\"title\":\"S\",\"qml\":\"S.qml\",\"location\":\"settings\"}]}}";
        };
        QTemporaryDir user, bundled, data, outside;
        for (const auto &[dir, id] : QList<QPair<QString, const char *>>{{user.filePath("own"), "org.test.own"}, {user.filePath("keep"), "org.test.keep"},
                                                                          {bundled.filePath("shipped"), "org.test.shipped"}}) {
            writeFile(dir + "/manifest.json", manifest(id));
            writeFile(dir + "/Main.qml", "import QtQml\nQtObject {}\n");
        }
        writeFile(outside.filePath("wichtig.txt"), "bleibt");
        AppSettings settings(data.path());
        MpvController mpv;
        ShaderChain shaders(&mpv);
        QQmlEngine engine;
        const auto info = [](const PluginManager &pm, const QString &id) {
            for (const auto &v : pm.plugins())
                if (v.toMap().value("id") == id) return v.toMap();
            return QVariantMap();
        };
        {
            PluginManager pm(&settings, &mpv, &shaders, {user.path(), bundled.path()}, false);
            pm.setQmlEngine(&engine);
            pm.discoverAndStart();
            QVERIFY(info(pm, "org.test.own").value("removable").toBool());
            QVERIFY(!info(pm, "org.test.shipped").value("removable").toBool());
            QVERIFY(!pm.removePlugin("org.test.shipped").value("ok").toBool());
            QVERIFY(!pm.removePlugin("org.test.gibtsnicht").value("ok").toBool());

            pm.savePluginSettings("org.test.own", QJsonObject{{"a", 1}});
            pm.savePluginSettings("org.test.keep", QJsonObject{{"b", 2}});
            writeFile(QDir(pm.pluginDataDir("org.test.own")).filePath("x.txt"), "x");
            writeFile(QDir(pm.pluginDataDir("org.test.keep")).filePath("y.txt"), "y");

            // Sofort deaktiviert, Seiten weg; rückgängig machen stellt alles wieder her
            QVERIFY(pm.removePlugin("org.test.own", true).value("ok").toBool());
            QCOMPARE(info(pm, "org.test.own").value("state").toString(), QString("deaktiviert"));
            QVERIFY(info(pm, "org.test.own").value("pendingRemoval").toBool());
            QVERIFY(QFileInfo::exists(user.filePath("own/manifest.json")));
            const auto hasPanel = [&pm](const QString &id) {
                for (const auto &v : pm.panels()) if (v.toMap().value("pluginId") == id) return true;
                return false;
            };
            QVERIFY(!hasPanel("org.test.own"));
            QVERIFY(pm.cancelRemoval("org.test.own"));
            QCOMPARE(info(pm, "org.test.own").value("state").toString(), QString("aktiv"));
            QVERIFY(!info(pm, "org.test.own").value("pendingRemoval").toBool());
            QVERIFY(hasPanel("org.test.own"));
            QVERIFY(!pm.cancelRemoval("org.test.own"));

            QVERIFY(pm.removePlugin("org.test.own", true).value("ok").toBool());
            QVERIFY(pm.removePlugin("org.test.keep", false).value("ok").toBool());
        }
        // Eine manipulierte Vormerkung darf nichts außerhalb des Benutzerordners löschen
        auto marks = settings.object("pluginRemovals");
        marks.insert("org.test.evil", QJsonObject{{"path", outside.path()}, {"deleteData", true}});
        marks.insert("..", QJsonObject{{"path", user.filePath("keep")}, {"deleteData", true}});
        settings.setObject("pluginRemovals", marks);
        {
            PluginManager pm(&settings, &mpv, &shaders, {user.path(), bundled.path()}, false);
            pm.setQmlEngine(&engine);
            pm.discoverAndStart();
            QVERIFY(!QFileInfo::exists(user.filePath("own")));
            QVERIFY(!QFileInfo::exists(user.filePath("keep")));
            QVERIFY(info(pm, "org.test.own").isEmpty());
            QVERIFY(info(pm, "org.test.keep").isEmpty());
            QCOMPARE(info(pm, "org.test.shipped").value("state").toString(), QString("aktiv"));
            QVERIFY(pm.loadPluginSettings("org.test.own").isEmpty());
            QCOMPARE(pm.loadPluginSettings("org.test.keep").value("b").toInt(), 2);
            QVERIFY(!QFileInfo::exists(QDir(data.path()).filePath("plugin-data/org.test.own/x.txt")));
            QVERIFY(QFileInfo::exists(QDir(data.path()).filePath("plugin-data/org.test.keep/y.txt")));
            QVERIFY(QFileInfo::exists(outside.filePath("wichtig.txt")));
            QVERIFY(settings.object("pluginRemovals").isEmpty());
        }
    }

    void updateReplacesOldVersion()
    {
        const auto manifest = [](const char *version) {
            return "{\"id\":\"org.test.upd\",\"name\":\"Upd\",\"version\":\"" + QByteArray(version) + "\",\"apiVersion\":1,\"main\":\"Main.qml\"}";
        };
        QTemporaryDir user, data;
        writeFile(user.filePath("von-hand-abgelegt/manifest.json"), manifest("1.0.0"));
        writeFile(user.filePath("von-hand-abgelegt/Main.qml"), "import QtQml\nQtObject {}\n");
        writeFile(user.filePath("von-hand-abgelegt/nur-alt.txt"), "alt");

        AppSettings settings(data.path());
        MpvController mpv;
        ShaderChain shaders(&mpv);
        QQmlEngine engine;
        const auto version = [](PluginManager &pm) {
            for (const auto &v : pm.plugins())
                if (v.toMap().value("id") == "org.test.upd") return v.toMap().value("version").toString();
            return QString();
        };
        const auto install = [&](PluginManager &pm, const char *v) {
            QTemporaryDir pkg;
            writeFile(pkg.filePath("manifest.json"), manifest(v));
            writeFile(pkg.filePath("Main.qml"), "import QtQml\nQtObject {}\n");
            return pm.installPackageInfo(pkg.path());
        };
        {
            PluginManager pm(&settings, &mpv, &shaders, {user.path()}, false);
            pm.setQmlEngine(&engine);
            pm.discoverAndStart();
            QCOMPARE(version(pm), QStringLiteral("1.0.0"));
            const auto r = install(pm, "2.0.0");
            QVERIFY2(r.value("ok").toBool(), qPrintable(r.value("error").toString()));
            QCOMPARE(r.value("kind").toString(), QStringLiteral("update"));
            QCOMPARE(r.value("previousVersion").toString(), QStringLiteral("1.0.0"));
            QCOMPARE(r.value("version").toString(), QStringLiteral("2.0.0"));
            QCOMPARE(r.value("name").toString(), QStringLiteral("Upd"));
            pm.shutdown();
        }
        {
            PluginManager pm(&settings, &mpv, &shaders, {user.path()}, false);
            pm.setQmlEngine(&engine);
            pm.discoverAndStart();
            QCOMPARE(version(pm), QStringLiteral("2.0.0"));
            QVERIFY(!QFileInfo::exists(user.filePath("von-hand-abgelegt")));
            QVERIFY(!QFileInfo::exists(user.filePath("org.test.upd/nur-alt.txt")));
            QVERIFY(!QFileInfo::exists(user.filePath(".pending/org.test.upd")));
            QVERIFY(!QFileInfo::exists(user.filePath(".pending-backup/org.test.upd")));
            QCOMPARE(install(pm, "2.0.0").value("kind").toString(), QStringLiteral("reinstall"));
            QCOMPARE(install(pm, "1.5.0").value("kind").toString(), QStringLiteral("downgrade"));
            pm.shutdown();
        }
    }
};

const char *kManifest = R"({
  "id": "org.test.hello", "name": "Hello", "version": "1.0.0", "apiVersion": 1,
  "type": "utility", "main": "Main.qml", "capabilities": ["stream-quality"],
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
