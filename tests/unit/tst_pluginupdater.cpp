// Online-Updates der Plugins (1.1.9): Katalog aus jsDelivr/REST-API, Zuordnung Paket ↔ Plugin, Rückfälle bei
// nicht erreichbaren Quellen, Prüfsummen, Plugin-ID im Paket und Zwischenspeicher. Netzwerk über file://-Adressen.
#include "core/AppSettings.h"
#include "player/MpvController.h"
#include "plugins/PluginManager.h"
#include "rendering/ShaderChain.h"
#include "services/PluginUpdater.h"
#include "../../plugins/uve/engine/MiniZip.h"

#include <QCryptographicHash>
#include <QJsonArray>
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

static QByteArray pkt(const QByteArray &line)
{
    return QByteArray::number(line.size() + 4, 16).rightJustified(4, '0') + line;
}

static QByteArray manifest(const QString &id, const QString &name, const QString &version)
{
    return QJsonDocument(QJsonObject{{"id", id}, {"name", name}, {"version", version}, {"apiVersion", 1}, {"main", "Main.qml"}}).toJson();
}

static const char *kCommit = "e1973d4321238f27f8e922bc874a5409b78363e9";

class TstPluginUpdater : public QObject
{
    Q_OBJECT
    QTemporaryDir m_tmp;

    // Platzhalter {ref}/{file} unverändert lassen (QUrl würde die Klammern kodieren)
    QString url(const QString &rel) const { return QStringLiteral("file:///") + m_tmp.filePath(rel); }

private slots:
    void packageNames()
    {
        QString key, version;
        QVERIFY(PluginUpdater::splitPackageName("kickchat-1.3.1.zip", &key, &version));
        QCOMPARE(key, QString("kickchat"));
        QCOMPARE(version, QString("1.3.1"));
        QVERIFY(PluginUpdater::splitPackageName("GLSLManager-1.0.0.zip", &key, &version));
        QCOMPARE(key, QString("glslmanager"));
        QVERIFY(PluginUpdater::splitPackageName("Twitch_Chat_v2.10.zip", &key, &version));
        QCOMPARE(key, QString("twitchchat"));
        QCOMPARE(version, QString("2.10"));
        QVERIFY(!PluginUpdater::splitPackageName("README.md", &key, &version));
        QVERIFY(!PluginUpdater::splitPackageName("gvf_filter_codes_2026-11-09_01-08.json", &key, &version));
    }

    void parseAndMatch()
    {
        // Antwort von jsDelivr (gekürzt, echte Einträge des Zweigs)
        const QByteArray jsd = R"({"type":"gh","files":[
            {"name":"/GLSLManager-1.0.0.zip","hash":"Q9BOyQwkFCrPMGH2EbFUIuWZzIWV7nbYBlNLtQLdYJs=","size":121038},
            {"name":"/gvf_filter_codes_2026-11-09_01-08.json","hash":"K8pp5ciicTeRZ+ntot3FuVNv+H++EaoLLG2tOGQhD5w=","size":42820},
            {"name":"/kickchat-1.3.1.zip","hash":"BoXY/Xzz3eElm4Iy9Y2lyYl88WyC2TTwVolKbbAsWGE=","size":2520976},
            {"name":"/kickchat-1.10.0.zip","hash":"","size":5},
            {"name":"/LumaScale-1.0.0.zip","hash":"Si7O36P1En9frRAdqlIRTy1FMoe88gq0Z50/JU6PlgA=","size":36019},
            {"name":"/old/twitchchat-9.9.9.zip","hash":"","size":1},
            {"name":"/twitchchat-2.2.0.zip","hash":"nkuDZO8cZAslHew3jz9EN5wyZTsUbhL+Yqv6Db5ruQE=","size":2630345}]})";
        const auto list = PluginUpdater::parseCatalog(jsd);
        QCOMPARE(list.size(), 5);   // JSON-Datei und Unterordner zählen nicht
        const int luma = PluginUpdater::findEntry(list, "org.lumascale.fsr", "LumaScale");
        QVERIFY(luma >= 0);
        QCOMPARE(list[luma].sha256, QString("4a2ecedfa3f5127f5fad101daa52114f2d453287bcf20ab4679d3f254e8f9600"));
        QCOMPARE(list[PluginUpdater::findEntry(list, "org.uveplayer.glsl-manager", "GLSL Manager")].file, QString("GLSLManager-1.0.0.zip"));
        QCOMPARE(list[PluginUpdater::findEntry(list, "cc.ts3x.twitch-chat", "Twitch Chat")].version, QString("2.2.0"));
        // höchste Version gewinnt (numerisch, nicht alphabetisch)
        QCOMPARE(list[PluginUpdater::findEntry(list, "cc.ts3x.kick-chat", "Kick Chat")].version, QString("1.10.0"));
        QCOMPARE(PluginUpdater::findEntry(list, "org.uve.enhancer", "Ultimate Video Enhancer"), -1);

        // Antwort der REST-API
        const QByteArray tree = R"({"sha":"x","tree":[
            {"path":"LumaScale-1.0.0.zip","type":"blob","sha":"cfba8f897b0b86f5bf1f03345e64bc98bb01f8d6","size":36019},
            {"path":"sub","type":"tree","sha":"0000000000000000000000000000000000000000"}]})";
        const auto apiList = PluginUpdater::parseCatalog(tree);
        QCOMPARE(apiList.size(), 1);
        QCOMPARE(apiList[0].gitSha, QString("cfba8f897b0b86f5bf1f03345e64bc98bb01f8d6"));
        QVERIFY(apiList[0].sha256.isEmpty());

        QVERIFY(PluginUpdater::parseCatalog("<html>rate limit</html>").isEmpty());
        QCOMPARE(PluginUpdater::gitBlobSha("hello\n"), QString("ce013625030ba8dba906f756967f9e9ca394464a"));
    }

    // Ablauf wie im Player: Git-Endpunkt liefert den Commit, jsDelivr fällt aus (Rückfall auf die Liste der REST-API),
    // erste Download-Quelle fehlt (Rückfall auf die zweite), Prüfsumme und ID stimmen → Paket vorgemerkt
    void checkAndInstall()
    {
        // installiertes Plugin (Benutzerordner)
        const QString user = m_tmp.filePath("user");
        writeFile(user + "/kick/manifest.json", manifest("cc.ts3x.kick-chat", "Kick Chat", "1.3.0"));
        writeFile(user + "/kick/Main.qml", "import QtQml\nQtObject {}\n");
        writeFile(user + "/luma/manifest.json", manifest("org.lumascale.fsr", "LumaScale", "1.0.0"));
        writeFile(user + "/luma/Main.qml", "import QtQml\nQtObject {}\n");
        writeFile(user + "/other/manifest.json", manifest("org.test.other", "Other", "1.0.0"));
        writeFile(user + "/other/Main.qml", "import QtQml\nQtObject {}\n");

        // „Server“: refs, Liste (nur API-Format), Pakete unter <commit>/
        writeFile(m_tmp.filePath("srv/refs"), pkt(QByteArray("# service=git-upload-pack\n")) + "0000"
                  + pkt(QByteArray(kCommit) + " refs/heads/main" + QByteArray(1, '\0') + "side-band-64k\n") + pkt(QByteArray(kCommit) + " refs/heads/uve-player-plugins\n") + "0000");
        const QByteArray kickZip = makeZip({{"manifest.json", manifest("cc.ts3x.kick-chat", "Kick Chat", "1.4.0")},
                                            {"Main.qml", "import QtQml\nQtObject {}\n"}});
        const QByteArray lumaZip = makeZip({{"manifest.json", manifest("org.wrong.id", "LumaScale", "1.1.0")},
                                            {"Main.qml", "import QtQml\nQtObject {}\n"}});
        writeFile(m_tmp.filePath(QString("srv2/%1/kickchat-1.4.0.zip").arg(kCommit)), kickZip);
        writeFile(m_tmp.filePath(QString("srv2/%1/LumaScale-1.1.0.zip").arg(kCommit)), lumaZip);
        QJsonArray tree;
        tree << QJsonObject{{"path", "kickchat-1.4.0.zip"}, {"type", "blob"}, {"sha", PluginUpdater::gitBlobSha(kickZip)}, {"size", kickZip.size()}};
        tree << QJsonObject{{"path", "kickchat-1.2.0.zip"}, {"type", "blob"}, {"sha", "00"}, {"size", 1}};
        tree << QJsonObject{{"path", "LumaScale-1.1.0.zip"}, {"type", "blob"}, {"sha", PluginUpdater::gitBlobSha(lumaZip)}, {"size", lumaZip.size()}};
        writeFile(m_tmp.filePath(QString("srv/tree-%1.json").arg(kCommit)), QJsonDocument(QJsonObject{{"tree", tree}}).toJson());

        AppSettings settings(m_tmp.filePath("data"));
        MpvController mpv;
        ShaderChain shaders(&mpv);
        QQmlEngine engine;
        PluginManager pm(&settings, &mpv, &shaders, {user}, false);
        pm.setQmlEngine(&engine);
        pm.discoverAndStart();
        QCOMPARE(pm.plugins().size(), 3);

        PluginUpdater up(&pm, m_tmp.filePath("data"));
        up.setEndpoints({url("srv/refs-fehlt"), url("srv/refs")},
                        {url("srv/jsdelivr-{ref}.json"), url("srv/tree-{ref}.json")},
                        {url("srv1/{ref}/{file}"), url("srv2/{ref}/{file}")});
        QSignalSpy checked(&up, &PluginUpdater::checkFinished);
        up.check(true);
        QVERIFY(checked.wait(5000));
        QCOMPARE(up.error(), QString());
        auto kick = up.status().value("cc.ts3x.kick-chat").toMap();
        QCOMPARE(kick.value("state").toString(), QString("available"));
        QCOMPARE(kick.value("latestVersion").toString(), QString("1.4.0"));
        QCOMPARE(up.status().value("org.lumascale.fsr").toMap().value("state").toString(), QString("available"));
        QCOMPARE(up.status().value("org.test.other").toMap().value("state").toString(), QString("unknown"));

        QSignalSpy installed(&up, &PluginUpdater::installed);
        QSignalSpy failed(&up, &PluginUpdater::installFailed);
        up.install("cc.ts3x.kick-chat");
        QVERIFY(installed.wait(10000));
        const QVariantMap info = installed.first().first().toMap();
        QCOMPARE(info.value("kind").toString(), QString("update"));
        QCOMPARE(info.value("version").toString(), QString("1.4.0"));
        QVERIFY(QFileInfo::exists(user + "/.pending/cc.ts3x.kick-chat/manifest.json"));
        kick = up.status().value("cc.ts3x.kick-chat").toMap();
        QCOMPARE(kick.value("state").toString(), QString("installed"));
        QCOMPARE(kick.value("pendingVersion").toString(), QString("1.4.0"));

        // Paket mit falscher Plugin-ID wird nicht installiert
        up.install("org.lumascale.fsr");
        QVERIFY(failed.wait(10000));
        QVERIFY(failed.first().at(1).toString().contains("org.wrong.id"));
        QCOMPARE(up.status().value("org.lumascale.fsr").toMap().value("state").toString(), QString("error"));
        QVERIFY(!QFileInfo::exists(user + "/.pending/org.lumascale.fsr"));

        // Zwischenspeicher: Ohne erreichbare Quellen gilt die gespeicherte Liste weiter
        PluginUpdater offline(&pm, m_tmp.filePath("data"));
        offline.setEndpoints({url("srv/nichts")}, {url("srv/nichts-{ref}")}, {});
        QCOMPARE(offline.status().value("org.test.other").toMap().value("state").toString(), QString("unknown"));
        QSignalSpy offlineChecked(&offline, &PluginUpdater::checkFinished);
        offline.check(true);
        if (offlineChecked.isEmpty()) QVERIFY(offlineChecked.wait(5000));
        QVERIFY(offline.error().contains("gespeicherte Liste"));
        QCOMPARE(offline.status().value("cc.ts3x.kick-chat").toMap().value("latestVersion").toString(), QString("1.4.0"));

        // Gleicher Commit: keine neue Liste nötig (Listenquellen dürfen fehlen)
        PluginUpdater sameCommit(&pm, m_tmp.filePath("data"));
        sameCommit.setEndpoints({url("srv/refs")}, {}, {});
        QSignalSpy sameChecked(&sameCommit, &PluginUpdater::checkFinished);
        sameCommit.check(true);
        if (sameChecked.isEmpty()) QVERIFY(sameChecked.wait(5000));
        QCOMPARE(sameCommit.error(), QString());
        QCOMPARE(sameCommit.status().value("cc.ts3x.kick-chat").toMap().value("state").toString(), QString("available"));

        // Frische Liste (unter 30 Minuten) ohne force: keine Anfrage, sofort fertig
        PluginUpdater cached(&pm, m_tmp.filePath("data"));
        cached.setEndpoints({}, {}, {});
        QSignalSpy cachedChecked(&cached, &PluginUpdater::checkFinished);
        cached.check(false);
        QCOMPARE(cachedChecked.size(), 1);
        QVERIFY(!cached.checking());
        pm.shutdown();
    }
};

QTEST_MAIN(TstPluginUpdater)
#include "tst_pluginupdater.moc"
