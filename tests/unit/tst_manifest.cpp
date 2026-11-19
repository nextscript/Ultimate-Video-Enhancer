#include "plugins/PluginManifest.h"

#include <QFile>
#include <QJsonDocument>
#include <QtTest>

using namespace uve;

class TstManifest : public QObject
{
    Q_OBJECT
    static PluginManifest valid()
    {
        PluginManifest m;
        m.id = "org.example.test";
        m.name = "Test";
        m.version = "1.2.3";
        m.apiVersion = 1;
        m.minHostVersion = "1.0.0";
        m.supportedPlatforms = {"windows", "linux", "macos"};
        m.requiredBackends = {"mpv-user-shaders"};
        return m;
    }
    static ManifestCheck check(const PluginManifest &m, const QString &host = "1.0.0")
    {
        return validateManifest(m, host, 1, "windows", {"mpv-user-shaders"});
    }
private slots:
    void acceptsValid() { QVERIFY(check(valid()).ok); }
    void rejectsWrongApiVersion()
    {
        auto m = valid(); m.apiVersion = 2;
        const auto r = check(m);
        QVERIFY(!r.ok);
        QVERIFY(r.errors.join(' ').contains("apiVersion"));
    }
    void rejectsMissingFields()
    {
        PluginManifest m;
        const auto r = check(m);
        QVERIFY(!r.ok);
        QVERIFY(r.errors.size() >= 3);
    }
    void rejectsNewerHostRequirement()
    {
        auto m = valid(); m.minHostVersion = "2.0.0";
        QVERIFY(!check(m).ok);
        QVERIFY(check(m, "2.1.0").ok);
    }
    void rejectsPlatform()
    {
        auto m = valid(); m.supportedPlatforms = {"macos"};
        QVERIFY(!check(m).ok);
    }
    void rejectsMissingBackend()
    {
        auto m = valid(); m.requiredBackends = {"cuda"};
        QVERIFY(!check(m).ok);
    }
    void rejectsInvalidId()
    {
        auto m = valid(); m.id = "Bad ID";
        QVERIFY(!check(m).ok);
    }
    void versionCompare()
    {
        QVERIFY(compareVersions("1.0.0", "1.0.1") < 0);
        QVERIFY(compareVersions("1.10.0", "1.9.9") > 0);
        QCOMPARE(compareVersions("1.0", "1.0.0"), 0);
    }
    void localizedDescription()
    {
        const auto m = PluginManifest::fromJson(QJsonDocument::fromJson(
            R"({"description": {"de": "Hallo", "en": "Hello", "el": "Γεια"}})").object());
        QCOMPARE(localizedText(m.description, "el"), QString::fromUtf8("Γεια"));
        QCOMPARE(localizedText(m.description, "de_DE"), QString("Hallo"));
        QCOMPARE(localizedText(m.description, "pl"), QString("Hello"));   // Rückfall Englisch
        const auto plain = PluginManifest::fromJson(QJsonDocument::fromJson(R"({"description": "Text"})").object());
        QCOMPARE(localizedText(plain.description, "tr"), QString("Text"));
    }
    // Seit 1.2.9: ein Paket für mehrere Systeme ("libraries": Zielschlüssel → Bibliothek)
    void librariesRoundTrip()
    {
        const auto m = PluginManifest::fromJson(QJsonDocument::fromJson(R"({"id":"org.example.multi",
            "libraries": {"windows-x64": "bin/windows-x64/a.dll", "linux-x86_64": "bin/linux-x86_64/liba.so",
                          "macos-arm64": "bin/macos-arm64/liba.dylib"}})").object());
        QCOMPARE(m.libraries.size(), 3);
        QCOMPARE(m.libraries.value("linux-x86_64"), QString("bin/linux-x86_64/liba.so"));
        const auto again = PluginManifest::fromJson(m.toJson());
        QCOMPARE(again.libraries, m.libraries);
        QVERIFY(!valid().toJson().contains("libraries"));   // altes Format bleibt gleich
    }
    void targetKeys()
    {
        QCOMPARE(targetKey("windows", "x86_64"), QString("windows-x64"));
        QCOMPARE(targetKey("windows", "arm64"), QString("windows-arm64"));
        QCOMPARE(targetKey("windows", "i386"), QString("windows-x86"));
        QCOMPARE(targetKey("linux", "x86_64"), QString("linux-x86_64"));
        QCOMPARE(targetKey("linux", "arm64"), QString("linux-aarch64"));
        QCOMPARE(targetKey("macos", "arm64"), QString("macos-arm64"));
        QCOMPARE(targetKey("macos", "x86_64"), QString("macos-x86_64"));
        QVERIFY(currentTargetKey().startsWith(currentPlatformId() + "-"));
    }
    void safeRelativePaths()
    {
        QString c;
        QVERIFY(isSafeRelativePath("bin/windows-x64/a.dll", &c));
        QCOMPARE(c, QString("bin/windows-x64/a.dll"));
        QVERIFY(isSafeRelativePath("bin\\linux-x86_64\\./liba.so", &c));
        QCOMPARE(c, QString("bin/linux-x86_64/liba.so"));
        QVERIFY(isSafeRelativePath("a/../b.dll", &c));
        QCOMPARE(c, QString("b.dll"));
        for (const char *bad : {"", " ", ".", "..", "../a.dll", "bin/../../a.dll", "/etc/a.so", "\\\\server\\a.dll", "C:/a.dll", "C:a.dll"})
            QVERIFY2(!isSafeRelativePath(bad), bad);
    }
    void moduleSelection()
    {
        PluginManifest m = valid();
        // reines QML-Paket und bisherige Einzelbibliothek: Weg wie bisher
        m.main = "Main.qml";
        auto s = selectModule(m, "linux-x86_64");
        QVERIFY(s.ok());
        QCOMPARE(s.kind, ModuleSelection::Qml);
        QCOMPARE(s.path, QString("Main.qml"));
        m.library = "old.dll";
        s = selectModule(m, "windows-x64");
        QCOMPARE(s.kind, ModuleSelection::Library);
        QCOMPARE(s.path, QString("old.dll"));
        QVERIFY(!s.fromLibraries);
        // libraries vorhanden: nur der passende Eintrag zählt, "library" wird nicht verwendet
        m.libraries = {{"windows-x64", "bin/windows-x64/a.dll"}, {"linux-x86_64", "bin/linux-x86_64/liba.so"}};
        s = selectModule(m, "linux-x86_64");
        QVERIFY(s.ok());
        QCOMPARE(s.path, QString("bin/linux-x86_64/liba.so"));
        QVERIFY(s.fromLibraries);
        s = selectModule(m, "macos-arm64");
        QVERIFY(!s.ok());
        QVERIFY(s.error.contains("macos-arm64"));
        QVERIFY(s.error.contains("windows-x64"));
        // Pfad außerhalb des Plugin-Ordners
        m.libraries.insert("macos-arm64", "../../evil.dylib");
        s = selectModule(m, "macos-arm64");
        QVERIFY(!s.ok());
        QVERIFY(s.error.contains("evil.dylib"));
        m.library = "../old.dll";
        m.libraries.clear();
        QVERIFY(!selectModule(m, "windows-x64").ok());
        // weder noch
        QVERIFY(!selectModule(valid(), "windows-x64").ok());
    }
    void shippedManifestsAreValid()
    {
        for (const char *f : {UVE_TEST_DATA "/../manifest.json", UVE_TEST_DATA "/../../examples/sleeptimer/manifest.json"}) {
            QFile file(QString::fromUtf8(f));
            QVERIFY2(file.open(QIODevice::ReadOnly), f);
            const auto m = PluginManifest::fromJson(QJsonDocument::fromJson(file.readAll()).object());
            const auto r = validateManifest(m, "1.0.0", 1, "windows", {"mpv-user-shaders", "mpv-render-api-opengl"});
            QVERIFY2(r.ok, qPrintable(r.errors.join("; ")));
        }
    }
};

QTEST_GUILESS_MAIN(TstManifest)
#include "tst_manifest.moc"
