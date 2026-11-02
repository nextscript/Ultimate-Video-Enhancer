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
