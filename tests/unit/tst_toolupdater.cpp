// Werkzeug-Updates (libmpv) mit einem frischen Benutzerordner – ohne Netzwerk (Download per file://).
// Regression: Ohne <Daten>/tools schlug das libmpv-Update mit "Temporärer Ordner nicht beschreibbar" fehl.
#include "services/ToolUpdater.h"

#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using namespace uve;

class TstToolUpdater : public QObject
{
    Q_OBJECT
private slots:
    void installMpvIntoFreshDataDir()
    {
        const QString archive = QStringLiteral(UVE_SOURCE_DIR "/deps/mpv-dev.7z");
        const QString appDir = QFileInfo(QStringLiteral(UVE_QM_DIR)).absolutePath();   // build/bin
        if (!QFileInfo::exists(archive)) QSKIP("deps/mpv-dev.7z nicht vorhanden (siehe docs/BUILD.md)");
        if (!QFileInfo::exists(appDir + "/tools/7zr.exe")) QSKIP("tools/7zr.exe nicht im Programmordner");

        QTemporaryDir data;   // frischer Benutzerordner: es gibt noch kein "tools"
        QVERIFY(data.isValid());
        QVERIFY(!QFileInfo::exists(data.filePath("tools")));

        ToolUpdater up(appDir, data.path());
        up.setReleaseInfo("libmpv", {{"latest", "20990101-test"}, {"url", QUrl::fromLocalFile(archive).toString()}});
        QSignalSpy done(&up, &ToolUpdater::installFinished);
        up.install("libmpv");
        // Fehler vor dem Download werden sofort (synchron) gemeldet – dann nicht auf ein weiteres Signal warten
        QVERIFY(done.count() > 0 || done.wait(240000));
        const auto args = done.takeFirst();
        QVERIFY2(args.at(1).toBool(), qPrintable(args.at(2).toString()));
        QVERIFY(QFileInfo::exists(data.filePath("tools/mpv/20990101-test/libmpv-2.dll")));
        // Der neue Build steht an erster Stelle der Kandidaten (wird nach dem Neustart verwendet)
        const auto candidates = ToolUpdater::mpvDllCandidates(appDir, data.path());
        QVERIFY(!candidates.isEmpty());
        QVERIFY(candidates.first().contains("20990101-test"));
        // Temporäre Ordner wurden aufgeräumt
        QVERIFY(QDir(data.filePath("tools")).entryList({".tmp-mpv-*"}, QDir::Dirs | QDir::Hidden).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstToolUpdater)
#include "tst_toolupdater.moc"
