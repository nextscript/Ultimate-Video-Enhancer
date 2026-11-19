// Tests ohne Player: Umwandlung, Filterverwaltung und die echten QML-Seiten (offscreen, mit Bildschirmfoto).
// GL-Kompilierung prüft der Lauf im echten Player (siehe scripts/selftest.ps1).
#include "GlslController.h"
#include "ShaderConverter.h"
#include "ShaderValidator.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace glslmgr;

namespace {
QString readSource(const QString &name)
{
    QFile f(QStringLiteral(GLSL_SOURCE_DIR "/") + name);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

class FakeHost : public uve::IPlayerHost
{
public:
    explicit FakeHost(const QString &dir) : m_dir(dir) {}
    QString hostVersion() const override { return "1.1.5"; }
    int apiVersion() const override { return 1; }
    QJsonObject loadPluginSettings(const QString &) const override { return m_settings; }
    void savePluginSettings(const QString &, const QJsonObject &s) override { m_settings = s; }
    QString pluginDataDir(const QString &) const override { return m_dir; }
    void setShaderStage(const QString &owner, const QString &file) override { stages.append({owner, file}); current.insert(owner, file); }
    void showOsd(const QString &text, int) override { osd = text; }
    void log(const QString &, const QString &, const QString &) override {}
    QString currentMediaPath() const override { return media; }
    double currentPosition() const override { return 0; }
    bool isPaused() const override { return false; }
    void setPaused(bool) override {}
    QImage grabVideoFrame(int) override { return {}; }
    QVariant call(const QString &, const QString &, const QVariantMap &, QString *) override { return 24.0; }

    QString m_dir, media = "video.mkv", osd;
    QJsonObject m_settings;
    QList<QPair<QString, QString>> stages;
    QMap<QString, QString> current;
};

// Ersatz für die Bridge des Hosts: öffnet die Editor-Seite wie modal.open
class FakeBridge : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QVariant call(const QString &name, const QVariantMap &args) { calls << name; lastArgs = args; emit modalOpened(args); return true; }
    Q_INVOKABLE QString lastError() const { return {}; }
    QStringList calls;
    QVariantMap lastArgs;
signals:
    void modalOpened(const QVariantMap &args);
};

class FakeModal : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariant data READ data NOTIFY dataChanged)
public:
    using QObject::QObject;
    QVariant data() const { return m_data; }
    void setData(const QVariant &d) { m_data = d; emit dataChanged(); }
    Q_INVOKABLE void close(const QVariant &result = {}) { closed = true; closeResult = result; }
    bool closed = false;
    QVariant closeResult;
signals:
    void dataChanged();
private:
    QVariant m_data;
};

QQuickItem *findNamed(QObject *root, const QString &name) { return root->findChild<QQuickItem *>(name); }

QImage grab(QQuickWindow *w)
{
    w->update();
    QTest::qWait(150);
    return w->grabWindow();
}
} // namespace

class TstGlsl : public QObject
{
    Q_OBJECT
private slots:
    void detection();
    void uniformsFromMatrix();
    void matrixConversion();
    void anime4kStaysMultipass();
    void sliderValuesAreBaked();
    void helperAndShadertoy();
    void validatorSources();
    void params();
    void blend();
    void listOperations();
    void importDuplicates();
    void exportRoundTrip();
    void stagesAndErrors();
    void orderFollowsList();
    void noVideoNoStages();
    void previewLifecycle();
    void hotkeyToggle();
    void hotkeyBeatsShortcut();
    void panelUi();
    void editorUi();
};

void TstGlsl::detection()
{
    QVERIFY(isMpvHook(readSource("anime4k.txt")));
    QVERIFY(!isMpvHook(readSource("matrix.txt")));
    QCOMPARE(detectType(readSource("matrix.txt")), QStringLiteral("webgl"));
    QCOMPARE(detectType(readSource("anime4k.txt")), QStringLiteral("webgl"));
    QCOMPARE(detectType("<feGaussianBlur stdDeviation=\"1\"/>"), QStringLiteral("svg"));
    QCOMPARE(descriptionOf(readSource("anime4k.txt")), QStringLiteral("Anime4K-v3.2-Denoise-Bilateral-Mode-Luma"));
}

void TstGlsl::uniformsFromMatrix()
{
    const auto defs = parseUniformDefs(readSource("matrix.txt"));
    QCOMPARE(defs.size(), 11);
    QCOMPARE(defs[0].toMap().value("name").toString(), QStringLiteral("u_mxr_charset"));
    QCOMPARE(defs[0].toMap().value("kind").toString(), QStringLiteral("select"));
    QCOMPARE(defs[0].toMap().value("options").toList().size(), 4);
    QCOMPARE(defs[1].toMap().value("max").toDouble(), 400.0);
    QCOMPARE(defs[1].toMap().value("label").toString(), QStringLiteral("Character Size"));
}

void TstGlsl::matrixConversion()
{
    const QString out = convertToMpv({readSource("matrix.txt"), {{"u_mxr_columns", 123.0}}, "normal", "Matrix", 24.0});
    QVERIFY(out.startsWith("//!HOOK MAIN\n//!BIND HOOKED\n"));
    QVERIFY(out.contains("#define u_mxr_columns 123.0"));
    QVERIFY(out.contains("#define u_mxr_speed 0.4"));
    QVERIFY(out.contains("void gvfUserMain()"));
    QVERIFY(!out.contains("void main("));
    QVERIFY(!out.contains("@uniform"));
    QVERIFY(out.contains("vec4 hook()"));
    QVERIFY(out.contains("(float(frame) / 24.0)"));
    QVERIFY(!out.contains(QRegularExpression("[^\\x00-\\x7e]")));
}

void TstGlsl::anime4kStaysMultipass()
{
    const QString src = readSource("anime4k.txt");
    const QString out = convertToMpv({src, {}, "normal", "A4K", 30.0});
    QCOMPARE(out.count("//!HOOK MAIN"), 2);
    QVERIFY(out.contains("//!SAVE LINELUMA"));
    QVERIFY(!out.contains("gvfUserMain"));
    QVERIFY(out.contains("#define INTENSITY_SIGMA 0.1 //Intensity"));

    const auto defs = parseUniformDefs(src);
    QStringList names;
    for (const auto &d : defs) names << d.toMap().value("name").toString();
    QVERIFY(names.contains("u_def_INTENSITY_SIGMA"));
    QVERIFY(!names.contains("u_def_KERNELSIZE"));
}

void TstGlsl::sliderValuesAreBaked()
{
    const QString out = convertToMpv({readSource("anime4k.txt"), {{"u_def_INTENSITY_SIGMA", 0.25}}, "normal", "A4K", 30.0});
    QVERIFY(out.contains("#define INTENSITY_SIGMA 0.25 //Intensity"));
    const QString a = convertToMpv({"#define COUNT 3\nvoid main(){ fragColor = vec4(float(COUNT)); }", {{"u_def_COUNT", 4.6}}, "normal", "x", 30.0});
    QVERIFY(a.contains("#define COUNT 5"));
    const QString n = convertToMpv({"// @uniform float W -0.1 -1 1\nvoid main(){ fragColor = vec4(W); }", {}, "normal", "x", 30.0});
    QVERIFY(n.contains("#define W (-0.1)"));
}

void TstGlsl::helperAndShadertoy()
{
    const QString helper = convertToMpv({"vec3 fx(sampler2D t, vec2 uv, vec2 r){ return texture(t, uv).rgb; }", {}, "normal", "h", 30.0});
    QVERIFY(helper.contains("fragColor = vec4(fx(u_video, v_uv, u_res), 1.0);"));
    QVERIFY(helper.contains("gvfTexture(t, uv)"));
    const QString toy = convertToMpv({"void mainImage(out vec4 c, in vec2 p){ c = texture(iChannel0, p / iResolution.xy); }", {}, "normal", "t", 30.0});
    QVERIFY(toy.contains("mainImage(fragColor, v_uv * u_res)"));
    const QString bare = convertToMpv({"fragColor = texture(u_video, v_uv);", {}, "normal", "b", 30.0});
    QVERIFY(bare.contains("void gvfUserMain(){\nfragColor"));
    const QString legacy = convertToMpv({"#version 300 es\nprecision highp float;\nuniform sampler2D u_video;\nin vec2 vUv;\nout vec4 col;\nvoid main(){ col = texture2D(u_video, vUv); gl_FragColor = col; }", {}, "normal", "l", 30.0});
    QVERIFY(!legacy.contains("#version"));
    QVERIFY(!legacy.contains("precision highp"));
    QVERIFY(legacy.contains("#define col fragColor"));
    QVERIFY(legacy.contains("#define vUv v_uv"));
    const QString stats = convertToMpv({"void main(){ fragColor = vec4(u_avg_lum); }", {}, "normal", "s", 30.0});
    QCOMPARE(stats.count("//!HOOK MAIN"), 2);
    QVERIFY(stats.contains("//!WIDTH 1"));
}

void TstGlsl::validatorSources()
{
    const QStringList passes = fragmentSources(convertToMpv({readSource("anime4k.txt"), {}, "normal", "A", 30.0}), 440);
    QCOMPARE(passes.size(), 2);
    QVERIFY(passes[0].startsWith("#version 440\n"));
    QVERIFY(passes[1].contains("uniform sampler2D LINELUMA_raw;"));
    QVERIFY(passes[1].contains("void main() { gvf_out_color = hook(); }"));
    QVERIFY(!passes[0].contains("LINELUMA_raw"));
    QCOMPARE(fragmentSources(convertToMpv({"void main(){ fragColor = vec4(u_avg_lum); }", {}, "normal", "s", 30.0}), 330).size(), 2);
    QVERIFY(fragmentSources("//!TEXTURE X\n//!SIZE 1 1\nff\n", 440).isEmpty());
}

void TstGlsl::params()
{
    const QString src = "//!PARAM strength\n//!TYPE float\n//!MINIMUM 0\n//!MAXIMUM 2\n0.5\n\n//!HOOK MAIN\n//!BIND HOOKED\nvec4 hook(){ return HOOKED_tex(HOOKED_pos) * strength; }\n\n//!HOOK MAIN\n//!BIND HOOKED\nvec4 hook(){ return HOOKED_tex(HOOKED_pos); }\n";
    const auto defs = parseUniformDefs(src);
    QCOMPARE(defs.size(), 1);
    QCOMPARE(defs[0].toMap().value("max").toDouble(), 2.0);
    const QString out = convertToMpv({src, {{"strength", 1.5}}, "normal", "p", 30.0});
    QVERIFY(!out.contains("//!PARAM"));
    QCOMPARE(out.count("#define strength 1.5"), 2);
    QVERIFY(out.indexOf("//!BIND HOOKED") < out.indexOf("#define strength"));
}

void TstGlsl::blend()
{
    const QString code = "void main(){ fragColor = vec4(1.0); }";
    for (const char *m : {"darken", "multiply", "color-burn", "lighten", "screen", "color-dodge", "overlay", "soft-light", "hard-light", "difference", "exclusion", "hue", "saturation", "color", "luminosity"}) {
        const QString out = convertToMpv({code, {}, QString::fromLatin1(m), "b", 30.0});
        QVERIFY2(out.contains("vec3 gvfBlend(vec3 b, vec3 s)"), m);
        QVERIFY2(out.contains("gvfBlend(clamp(src.rgb"), m);
    }
    QVERIFY(!convertToMpv({code, {}, "normal", "b", 30.0}).contains("gvfBlend"));
}

void TstGlsl::listOperations()
{
    QTemporaryDir dir;
    FakeHost host(dir.path());
    GlslController c(&host, "x");
    const QString a = c.addEntry({{"label", "A"}, {"code", "void main(){}"}, {"tags", "one, two ,"}, {"category", "cat"}});
    QTest::qWait(2);
    const QString b = c.addEntry({{"label", ""}, {"code", "fragColor=vec4(1.0);"}});
    QCOMPARE(c.entries().size(), 2);
    QCOMPARE(c.entry(a).value("tags").toStringList(), QStringList({"one", "two"}));
    QVERIFY(!c.entry(b).value("label").toString().isEmpty());
    QCOMPARE(c.entry(a).value("type").toString(), QStringLiteral("webgl"));

    QVERIFY(c.updateEntry(a, {{"label", "A2"}}));
    QCOMPARE(c.entry(a).value("label").toString(), QStringLiteral("A2"));
    QCOMPARE(c.entry(a).value("code").toString(), QStringLiteral("void main(){}"));

    QTest::qWait(2);
    const QString d = c.duplicateEntry(a);
    QCOMPARE(c.entries().at(1).toMap().value("id").toString(), d);
    QCOMPARE(c.entry(d).value("enabled").toBool(), false);

    c.moveEntry(0, 2);
    QCOMPARE(c.entries().at(2).toMap().value("id").toString(), a);
    c.removeEntry(d);
    QCOMPARE(c.entries().size(), 2);

    GlslController reload(&host, "x");   // Wird gespeichert und wieder geladen
    QCOMPARE(reload.entries().size(), 2);
    QCOMPARE(reload.entries().at(1).toMap().value("id").toString(), a);

    QVERIFY(!c.check("").isEmpty());
    QVERIFY(!c.check("void main(){").isEmpty());
    QVERIFY(c.check("// {\nvoid main(){}").isEmpty());
}

void TstGlsl::importDuplicates()
{
    QTemporaryDir dir;
    FakeHost host(dir.path());
    GlslController c(&host, "x");
    QFile f(QStringLiteral(GLSL_SOURCE_DIR "/tests/gvf_filter_codes_example.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString json = QString::fromUtf8(f.readAll());

    auto r = c.importText(json, "");
    QCOMPARE(r.value("added").toInt(), 4);
    QCOMPARE(c.entries().size(), 4);
    QCOMPARE(c.entries().at(1).toMap().value("hotkey").toString(), QStringLiteral("f9"));

    r = c.importText(json, "");
    QCOMPARE(r.value("dupes").toStringList().size(), 4);
    r = c.importText(json, "skip");
    QCOMPARE(r.value("skipped").toInt(), 4);
    QCOMPARE(c.entries().size(), 4);
    r = c.importText(json, "overwrite");
    QCOMPARE(r.value("replaced").toInt(), 4);
    QCOMPARE(c.entries().size(), 4);

    QVERIFY(c.importText("kein json", "").contains("error"));
    QVERIFY(c.importText("{\"codes\":[{\"label\":\"x\"}]}", "").contains("error"));
    QCOMPARE(c.importText("{\"label\":\"Einzeln\",\"code\":\"void main(){}\"}", "").value("added").toInt(), 1);
    QCOMPARE(c.importText("[{\"label\":\"Svg\",\"code\":\"<feBlend/>\"},{\"label\":\"T2\",\"code\":\"x\",\"tags\":\"a,b\",\"type\":\"canvas2d\"}]", "").value("added").toInt(), 2);
    const auto all = c.entries();
    QCOMPARE(all.at(5).toMap().value("type").toString(), QStringLiteral("svg"));
    QCOMPARE(all.at(5).toMap().value("playable").toBool(), false);
    QCOMPARE(all.at(6).toMap().value("tags").toStringList(), QStringList({"a", "b"}));
}

void TstGlsl::exportRoundTrip()
{
    QTemporaryDir dir;
    FakeHost host(dir.path());
    GlslController c(&host, "x");
    QVERIFY(c.exportFile(dir.filePath("empty.json")).contains("error"));
    c.addEntry({{"label", "A"}, {"code", "void main(){}"}, {"uniforms", QVariantMap{{"X", 0.5}}}, {"customField", "bleibt"}});
    const QString path = dir.filePath("out.json");
    QCOMPARE(c.exportFile(QUrl::fromLocalFile(path).toString()).value("count").toInt(), 1);

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const auto root = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(root.value("schema").toString(), QStringLiteral("gvf-custom-filter-codes"));
    QCOMPARE(root.value("codes").toArray().at(0).toObject().value("customField").toString(), QStringLiteral("bleibt"));
    QCOMPARE(root.value("codes").toArray().at(0).toObject().value("uniforms").toObject().value("X").toDouble(), 0.5);

    const auto read = c.readImportFile(QStringLiteral(GLSL_SOURCE_DIR "/anime4k.txt"));
    QCOMPARE(read.value("isJson").toBool(), false);
    QCOMPARE(read.value("label").toString(), QStringLiteral("Anime4K-v3.2-Denoise-Bilateral-Mode-Luma"));
    QCOMPARE(c.readImportFile(path).value("isJson").toBool(), true);
    QVERIFY(c.readImportFile(dir.filePath("gibtsnicht.json")).contains("error"));
}

void TstGlsl::stagesAndErrors()
{
    QTemporaryDir dir;
    FakeHost host(dir.path());
    GlslController c(&host, "x");
    const QString a = c.addEntry({{"label", "A"}, {"code", "void main(){ fragColor = vec4(1.0); }"}});
    QTest::qWait(2);
    const QString b = c.addEntry({{"label", "B"}, {"code", "void main(){ fragColor = vec4(0.5); }"}});
    c.activate();
    QTRY_COMPARE(host.stages.size(), 2);
    // Eine Stufe nach der anderen, damit ein Fehler der richtigen zugeordnet wird
    QCOMPARE(host.stages[0].first, QStringLiteral("x#s0"));
    QCOMPARE(host.stages[1].first, QStringLiteral("x#s1"));
    QVERIFY(QFile::exists(host.stages[0].second));
    QVERIFY(host.stages[0].second != host.stages[1].second);

    host.stages.clear();
    c.onShaderError("x#s0: boom");
    QVERIFY(!c.entries().at(0).toMap().value("error").toString().isEmpty());
    QTest::qWait(350);
    // Der Platz bleibt pausiert belegt: Weder er noch B werden neu gesetzt, weitere Zeilen ändern nichts
    QVERIFY(host.stages.isEmpty());
    const QString firstMessage = c.entries().at(0).toMap().value("error").toString();
    c.onShaderError("x#s0: zweite Zeile");
    QCOMPARE(c.entries().at(0).toMap().value("error").toString(), firstMessage);
    QTest::qWait(100);
    QVERIFY(host.stages.isEmpty());
    QCOMPARE(c.entries().at(1).toMap().value("error").toString(), QString());

    c.retry(a);
    QTRY_COMPARE(host.stages.size(), 1);
    QCOMPARE(host.stages[0].first, QStringLiteral("x#s0"));
    QVERIFY(c.entries().at(0).toMap().value("error").toString().isEmpty());

    host.stages.clear();
    c.setMasterEnabled(false);
    QTRY_COMPARE(host.stages.size(), 2);
    QVERIFY(host.current.value("x#s0").isEmpty() && host.current.value("x#s1").isEmpty());
    c.setMasterEnabled(true);
    QTRY_VERIFY(!host.current.value("x#s1").isEmpty());

    c.setEnabled(b, false);
    QTRY_VERIFY(host.current.value("x#s1").isEmpty());
    c.suspend();
    QVERIFY(host.current.value("x#s0").isEmpty());
}

void TstGlsl::orderFollowsList()
{
    QTemporaryDir dir;
    FakeHost host(dir.path());
    GlslController c(&host, "x");
    c.addEntry({{"label", "A"}, {"code", "void main(){ fragColor = vec4(1.0); }"}});
    QTest::qWait(2);
    c.addEntry({{"label", "B"}, {"code", "void main(){ fragColor = vec4(0.5); }"}});
    c.activate();
    QTRY_VERIFY(!host.current.value("x#s1").isEmpty());
    const QString first = host.current.value("x#s0"), second = host.current.value("x#s1");
    c.moveEntry(0, 1);
    QTRY_COMPARE(host.current.value("x#s1"), first);
    QCOMPARE(host.current.value("x#s0"), second);
}

void TstGlsl::noVideoNoStages()
{
    QTemporaryDir dir;
    FakeHost host(dir.path());
    host.media.clear();
    GlslController c(&host, "x");
    c.addEntry({{"label", "A"}, {"code", "void main(){ fragColor = vec4(1.0); }"}});
    c.activate();
    QTest::qWait(150);
    QVERIFY(host.stages.isEmpty());
    host.media = "video.mkv";
    c.onEvent("media.loaded", {});
    QTRY_COMPARE(host.stages.size(), 1);
}

void TstGlsl::previewLifecycle()
{
    QTemporaryDir dir;
    FakeHost host(dir.path());
    GlslController c(&host, "x");
    c.activate();
    // Neuer Filter ohne Speichern
    c.setPreview("", {{"code", "void main(){ fragColor = vec4(1.0); }"}, {"type", "webgl"}, {"label", "P"}});
    QCOMPARE(c.previewStatus(), QStringLiteral("checking"));
    QTRY_VERIFY(!host.current.value("x#s0").isEmpty());
    QVERIFY(c.entries().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(c.previewStatus(), QStringLiteral("ok"), 3000);

    c.setPreview("", {{"code", "void main(){ boom }"}, {"type", "webgl"}, {"label", "P"}});
    QTRY_VERIFY(host.stages.size() >= 2);
    c.onShaderError("x#s0: compile failed");
    QCOMPARE(c.previewStatus(), QStringLiteral("error"));
    QVERIFY(!c.previewError().isEmpty());

    c.clearPreview();
    QTRY_VERIFY(host.current.value("x#s0").isEmpty());
    QCOMPARE(c.previewStatus(), QString());

    // Vorschau für bestehenden Filter ändert ihn erst beim Speichern
    const QString a = c.addEntry({{"label", "A"}, {"code", "// @uniform float V 1.0 0.0 2.0\nvoid main(){ fragColor = vec4(V); }"}});
    QTRY_VERIFY(!host.current.value("x#s0").isEmpty());
    const QString saved = host.current.value("x#s0");
    c.setPreview(a, {{"code", c.entry(a).value("code")}, {"uniforms", QVariantMap{{"V", 0.25}}}});
    QTRY_VERIFY(host.current.value("x#s0") != saved);
    QFile f(host.current.value("x#s0"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(f.readAll().contains("#define V 0.25"));
    QVERIFY(!c.entry(a).contains("uniforms"));
    c.clearPreview();
    QTRY_COMPARE(host.current.value("x#s0"), saved);
}

void TstGlsl::hotkeyToggle()
{
    QTemporaryDir dir;
    FakeHost host(dir.path());
    GlslController c(&host, "x");
    const QString a = c.addEntry({{"label", "A"}, {"code", "void main(){}"}});
    c.activate();
    QWindow w;
    c.beginCapture(a);
    QKeyEvent over(QEvent::ShortcutOverride, Qt::Key_K, Qt::NoModifier, "k");
    QCoreApplication::sendEvent(&w, &over);
    QVERIFY(over.isAccepted());
    QKeyEvent press(QEvent::KeyPress, Qt::Key_K, Qt::NoModifier, "k");
    QCoreApplication::sendEvent(&w, &press);
    QCOMPARE(c.entry(a).value("hotkey").toString(), QStringLiteral("k"));
    QVERIFY(c.captureId().isEmpty());
    QVERIFY(c.entry(a).value("enabled").toBool());

    QKeyEvent toggle(QEvent::KeyPress, Qt::Key_K, Qt::NoModifier, "k");
    QCoreApplication::sendEvent(&w, &toggle);
    QCOMPARE(c.entry(a).value("enabled").toBool(), false);
    QVERIFY(host.osd.contains("A"));

    QKeyEvent modified(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier, "k");
    QCoreApplication::sendEvent(&w, &modified);
    QCOMPARE(c.entry(a).value("enabled").toBool(), false);

    QKeyEvent other(QEvent::KeyPress, Qt::Key_J, Qt::NoModifier, "j");
    QCoreApplication::sendEvent(&w, &other);
    QCOMPARE(c.entry(a).value("enabled").toBool(), false);

    c.beginCapture(a);
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&w, &esc);
    QCOMPARE(c.entry(a).value("hotkey").toString(), QStringLiteral("k"));
    c.clearHotkey(a);
    QVERIFY(!c.entry(a).contains("hotkey"));
}

// Echter Weg über das Fenster: Eine Filter-Taste hat Vorrang vor einem Player-Kürzel, Texteingaben haben Vorrang vor ihr
void TstGlsl::hotkeyBeatsShortcut()
{
    QTemporaryDir dir;
    FakeHost host(dir.path());
    GlslController c(&host, "x");
    const QString a = c.addEntry({{"label", "A"}, {"code", "void main(){}"}, {"hotkey", "k"}});
    c.activate();
    const QString file = dir.filePath("keys.qml");
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"qml(import QtQuick
Item {
    width: 200; height: 100
    property int k: 0
    property int j: 0
    Shortcut { sequence: "K"; onActivated: k++ }
    Shortcut { sequence: "J"; onActivated: j++ }
    TextInput { objectName: "input"; width: 100; height: 20 }
}
)qml");
    f.close();

    QQuickView view;
    view.setSource(QUrl::fromLocalFile(file));
    QVERIFY(view.status() == QQuickView::Ready);
    view.show();
    view.requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(&view));
    auto *root = view.rootObject();

    QTest::keyClick(&view, Qt::Key_J);
    QCOMPARE(root->property("j").toInt(), 1);   // Gegenprobe: Kürzel funktionieren hier

    QTest::keyClick(&view, Qt::Key_K);
    QCOMPARE(root->property("k").toInt(), 0);
    QCOMPARE(c.entry(a).value("enabled").toBool(), false);
    QTest::keyClick(&view, Qt::Key_K);
    QCOMPARE(c.entry(a).value("enabled").toBool(), true);

    auto *input = root->findChild<QQuickItem *>("input");
    input->forceActiveFocus();
    QTest::keyClick(&view, Qt::Key_K);
    QCOMPARE(c.entry(a).value("enabled").toBool(), true);
    QCOMPARE(input->property("text").toString(), QStringLiteral("k"));
    c.suspend();
}

// Delegates und Popup-Inhalte hängen nur im visuellen Baum
static void collect(QQuickItem *item, const QString &name, QList<QQuickItem *> &out)
{
    if (item->objectName() == name && item->isVisible()) out << item;
    for (auto *c : item->childItems()) collect(c, name, out);
}

static QQuickItem *visualItem(QQuickWindow *w, const QString &name)
{
    QList<QQuickItem *> found;
    collect(w->contentItem(), name, found);
    return found.value(0);
}

static QQuickItem *rowChild(QQuickItem *list, int row, const QString &name)
{
    QList<QQuickItem *> visible;
    collect(list, name, visible);
    std::sort(visible.begin(), visible.end(), [](QQuickItem *a, QQuickItem *b) { return a->mapToScene({0, 0}).y() < b->mapToScene({0, 0}).y(); });
    return row < visible.size() ? visible[row] : nullptr;
}

static void clickItem(QQuickWindow *w, QQuickItem *item)
{
    QVERIFY(item);
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
    QTest::qWait(30);
}

void TstGlsl::panelUi()
{
    QTemporaryDir dir;
    FakeHost host(dir.path());
    GlslController c(&host, "x");
    FakeBridge bridge;
    c.activate();
    QFile ex(QStringLiteral(GLSL_SOURCE_DIR "/tests/gvf_filter_codes_example.json"));
    QVERIFY(ex.open(QIODevice::ReadOnly));
    c.importText(QString::fromUtf8(ex.readAll()), "");
    c.addEntry({{"label", "Anime4K Denoise"}, {"code", readSource("anime4k.txt")}, {"category", "denoise"}});
    c.importText("[{\"label\":\"Altes SVG\",\"code\":\"<feBlend/>\"}]", "");

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(330, 760);
    view.setSource(QUrl::fromLocalFile(QStringLiteral(GLSL_SOURCE_DIR "/qml/GlslPanel.qml")));
    QVERIFY2(view.status() == QQuickView::Ready, qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
    auto *root = view.rootObject();
    root->setProperty("controller", QVariant::fromValue<QObject *>(&c));
    root->setProperty("plugin", QVariant::fromValue<QObject *>(&bridge));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(200);

    auto *list = findNamed(root, "filterList");
    QVERIFY(list);
    QTRY_COMPARE(list->property("count").toInt(), 6);
    grab(&view).save(QStringLiteral(GLSL_SOURCE_DIR "/build/shot-panel.png"));

    // Ein/Aus über die echte Checkbox
    const bool before = c.entries().at(0).toMap().value("enabled").toBool();
    clickItem(&view, rowChild(list, 0, "enableBox"));
    QCOMPARE(c.entries().at(0).toMap().value("enabled").toBool(), !before);

    // Neu und Bearbeiten öffnen das Fenster im Tab (Vorgaben aus dem Manifest: sidebar)
    clickItem(&view, findNamed(root, "newButton"));
    QCOMPARE(bridge.calls.last(), QStringLiteral("modal.open"));
    QCOMPARE(bridge.lastArgs.value("panel").toString(), QStringLiteral("editor"));
    QVERIFY(!bridge.lastArgs.contains("scope"));
    clickItem(&view, rowChild(list, 1, "editButton"));
    QCOMPARE(bridge.lastArgs.value("data").toMap().value("id").toString(), c.entries().at(1).toMap().value("id").toString());

    // Einfügen: Code geht in den Editor, JSON direkt in den Import
    QGuiApplication::clipboard()->setText("void main(){ fragColor = vec4(1.0); }");
    clickItem(&view, findNamed(root, "pasteButton"));
    QCOMPARE(bridge.lastArgs.value("data").toMap().value("preset").toMap().value("code").toString(), QStringLiteral("void main(){ fragColor = vec4(1.0); }"));
    QGuiApplication::clipboard()->setText("[{\"label\":\"Aus JSON\",\"code\":\"void main(){}\"}]");
    clickItem(&view, findNamed(root, "pasteButton"));
    QTRY_COMPARE(list->property("count").toInt(), 7);

    // Doppelte Namen fragen nach
    clickItem(&view, findNamed(root, "pasteButton"));
    auto *popup = root->findChild<QObject *>("duplicatePopup");
    QVERIFY(popup);
    QTRY_VERIFY(popup->property("opened").toBool());
    grab(&view).save(QStringLiteral(GLSL_SOURCE_DIR "/build/shot-duplicates.png"));
    auto *overwrite = visualItem(&view, "overwriteButton");
    clickItem(&view, overwrite);
    QTRY_VERIFY(!popup->property("opened").toBool());
    QCOMPARE(list->property("count").toInt(), 7);

    // Löschen fragt nach
    auto *del = root->findChild<QObject *>("deletePopup");
    clickItem(&view, rowChild(list, 0, "deleteButton"));
    QTRY_VERIFY(del->property("opened").toBool());
    clickItem(&view, visualItem(&view, "confirmDelete"));
    QTRY_COMPARE(list->property("count").toInt(), 6);

    // Duplizieren und Taste
    clickItem(&view, rowChild(list, 0, "copyButton"));
    QTRY_COMPARE(list->property("count").toInt(), 7);
    clickItem(&view, rowChild(list, 0, "hotkeyButton"));
    QVERIFY(!c.captureId().isEmpty());
    auto *hk = root->findChild<QObject *>("hotkeyPopup");
    QTRY_VERIFY(hk->property("opened").toBool());
    QKeyEvent press(QEvent::KeyPress, Qt::Key_M, Qt::NoModifier, "m");
    QCoreApplication::sendEvent(&view, &press);
    QTRY_VERIFY(!hk->property("opened").toBool());
    QCOMPARE(c.entries().at(0).toMap().value("hotkey").toString(), QStringLiteral("m"));
    c.clearHotkey(c.entries().at(0).toMap().value("id").toString());

    // Suche filtert nach Name und Kategorie
    root->setProperty("searchText", "denoise");
    QTRY_COMPARE(list->property("count").toInt(), 2);
    root->setProperty("searchText", "");
    root->setProperty("typeFilter", "svg");
    QTRY_COMPARE(list->property("count").toInt(), 1);
    root->setProperty("typeFilter", "");

    // Ziehen am Griff sortiert um
    QTRY_COMPARE(list->property("count").toInt(), 7);
    QTest::qWait(60);
    const QString firstId = c.entries().at(0).toMap().value("id").toString();
    auto *h0 = rowChild(list, 0, "dragHandle");
    auto *h2 = rowChild(list, 2, "dragHandle");
    QVERIFY(h0 && h2);
    const QPoint from = h0->mapToScene({h0->width() / 2, h0->height() / 2}).toPoint();
    const QPoint to = h2->mapToScene({h2->width() / 2, h2->height() / 2}).toPoint();
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&view, (from + to) / 2);
    QTest::mouseMove(&view, to);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, to);
    QTRY_COMPARE(c.entries().at(2).toMap().value("id").toString(), firstId);

    // Fehler wird in der Zeile gezeigt
    QTRY_VERIFY(!c.appliedFilesForTest().isEmpty());
    c.onShaderError("x#s0: 0:12(3): error: syntax error, unexpected '}'");
    QTest::qWait(100);
    grab(&view).save(QStringLiteral(GLSL_SOURCE_DIR "/build/shot-error.png"));
    c.suspend();
}

void TstGlsl::editorUi()
{
    QTemporaryDir dir;
    FakeHost host(dir.path());
    GlslController c(&host, "x");
    FakeBridge bridge;
    FakeModal modal;
    c.activate();
    const QString id = c.addEntry({{"label", "Matrix"}, {"code", readSource("matrix.txt")}, {"uniforms", QVariantMap{{"u_mxr_columns", 300.0}}}, {"tags", "rain, code"}, {"category", "overlay"}});

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(330, 760);
    view.setSource(QUrl::fromLocalFile(QStringLiteral(GLSL_SOURCE_DIR "/qml/GlslEditor.qml")));
    QVERIFY2(view.status() == QQuickView::Ready, qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
    auto *root = view.rootObject();
    modal.setData(QVariantMap{{"token", 1}, {"id", id}});
    // Reihenfolge wie im Host: theme, controller, plugin, modal
    root->setProperty("controller", QVariant::fromValue<QObject *>(&c));
    root->setProperty("plugin", QVariant::fromValue<QObject *>(&bridge));
    root->setProperty("modal", QVariant::fromValue<QObject *>(&modal));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTRY_VERIFY(!findNamed(root, "codeArea")->property("text").toString().isEmpty());
    QCOMPARE(findNamed(root, "labelField")->property("text").toString(), QStringLiteral("Matrix"));
    QTRY_COMPARE(root->property("defs").toList().size(), 11);
    QTest::qWait(150);
    grab(&view).save(QStringLiteral(GLSL_SOURCE_DIR "/build/shot-editor.png"));

    // "Größer" verschiebt das Fenster, der Inhalt bleibt
    clickItem(&view, findNamed(root, "sizeButton"));
    QCOMPARE(bridge.lastArgs.value("scope").toString(), QStringLiteral("video"));
    modal.setData(bridge.lastArgs.value("data"));
    QCOMPARE(findNamed(root, "labelField")->property("text").toString(), QStringLiteral("Matrix"));
    clickItem(&view, findNamed(root, "sizeButton"));
    QCOMPARE(bridge.lastArgs.value("scope").toString(), QStringLiteral("sidebar"));

    // Regler: Wert des Eintrags, Live-Vorschau ohne zu speichern
    QMetaObject::invokeMethod(root, "setValue", Q_ARG(QVariant, "u_mxr_columns"), Q_ARG(QVariant, 77.0));
    QTRY_COMPARE(c.previewStatus(), QStringLiteral("checking"));
    QTRY_VERIFY(!host.current.value("x#s0").isEmpty());
    QTRY_VERIFY([&] { QFile f(host.current.value("x#s0")); return f.open(QIODevice::ReadOnly) && f.readAll().contains("#define u_mxr_columns 77.0"); }());
    QCOMPARE(c.entry(id).value("uniforms").toMap().value("u_mxr_columns").toDouble(), 300.0);

    // Klammerfehler verhindert das Speichern
    auto *code = findNamed(root, "codeArea");
    const QString good = code->property("text").toString();
    code->setProperty("text", good + "\n}");
    clickItem(&view, findNamed(root, "saveButton"));
    QVERIFY(!modal.closed);
    QVERIFY(!findNamed(root, "statusLabel")->property("text").toString().isEmpty());

    code->setProperty("text", good);
    clickItem(&view, findNamed(root, "saveButton"));
    QVERIFY(modal.closed);
    QCOMPARE(c.entry(id).value("uniforms").toMap().value("u_mxr_columns").toDouble(), 77.0);
    QCOMPARE(c.previewStatus(), QString());

    // Neuer Filter mit Vorgabe aus "Einfügen", dann Hook-Erkennung
    modal.closed = false;
    modal.setData(QVariantMap{{"token", 2}, {"id", QString()}, {"preset", QVariantMap{{"code", readSource("anime4k.txt")}, {"label", "A4K"}}}});
    QTRY_VERIFY(code->property("text").toString().contains("LINELUMA"));
    QTRY_VERIFY(root->property("hook").toBool());
    QVERIFY(!findNamed(root, "blendBox")->isEnabled());
    QTRY_VERIFY(root->property("defs").toList().size() >= 4);
    QTest::qWait(150);
    grab(&view).save(QStringLiteral(GLSL_SOURCE_DIR "/build/shot-editor-hook.png"));
    clickItem(&view, findNamed(root, "saveButton"));
    QVERIFY(modal.closed);
    QCOMPARE(c.entries().size(), 2);
    QCOMPARE(c.entries().at(1).toMap().value("label").toString(), QStringLiteral("A4K"));
    c.suspend();
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc, argv);
    TstGlsl t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_glsl.moc"
