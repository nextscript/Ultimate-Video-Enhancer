// Optische Medien gegen die echte libmpv (vo=null, ao=null) – ohne physisches Laufwerk:
//  - Fähigkeiten der tatsächlich geladenen libmpv
//  - Audio-CD über den eigenen CD-Leser (uvecdda://) mit synthetischer CD (UVE_TEST_FAKE_CD)
//  - DVD-Video aus selbst erzeugter Testdisc (tests/fixtures/make-dvd-fixture.sh), Pfad über UVE_TEST_DVD_DIR;
//    ohne Testdisc werden diese Fälle übersprungen (Testbericht: BLOCKED)
//  - Fehlerfälle: unbekanntes Image, fehlendes Gerät, entferntes Medium
#include "player/MpvController.h"
#include "services/PlaylistModel.h"
#include "services/optical/OpticalMediaService.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <clocale>

using namespace uve;
using namespace uve::optical;

class TstOpticalPlayback : public QObject
{
    Q_OBJECT
    MpvController *m = nullptr;
    PlaylistModel *pl = nullptr;
    OpticalMediaService *svc = nullptr;
    QString dvdDir, dvdIso;

    bool loadIndex(int i)
    {
        QSignalSpy loaded(m, &MpvController::fileLoaded);
        QString error;
        if (!svc->load(pl->pathAt(i), 0, false, &error)) { qWarning() << error; return false; }
        return loaded.wait(15000) || loaded.size() > 0;
    }
    int openAndWait(const std::function<void()> &open)
    {
        QSignalSpy play(svc, &OpticalMediaService::playRequested);
        QSignalSpy failed(svc, &OpticalMediaService::openFailed);
        open();
        if (!QTest::qWaitFor([&] { return play.size() > 0 || failed.size() > 0; }, 15000)) return -2;
        return play.size() > 0 ? play.first().first().toInt() : -1;
    }

private slots:
    void initTestCase()
    {
        std::setlocale(LC_NUMERIC, "C");
        qputenv("UVE_TEST_FAKE_CD", "1");
        m = new MpvController(this);
        QString err;
        QVERIFY2(m->initialize({{"vo", "null"}, {"ao", "null"}, {"hwdec", "no"}}, &err), qPrintable(err));
        m->markRenderReady();
        pl = new PlaylistModel(this);
        svc = new OpticalMediaService(m, pl, QCoreApplication::applicationDirPath(), this);
        svc->attach();
        dvdDir = qEnvironmentVariable("UVE_TEST_DVD_DIR");
        if (!dvdDir.isEmpty()) dvdIso = QDir(dvdDir).filePath("uve-test-dvd.iso"), dvdDir = QDir(dvdDir).filePath("DVD");
    }

    void reportsRealCapabilities()
    {
        const auto c = svc->caps();
        qInfo().noquote() << "libmpv:" << c.mpvVersion << "| DVD" << c.supports(DiscKind::Dvd) << "Menüs" << c.menus(DiscKind::Dvd)
                          << "| BD" << c.supports(DiscKind::Bluray) << "Menüs" << c.menus(DiscKind::Bluray) << "BD-J" << c.bdj
                          << "| CDDA nativ" << c.cddaNative << "Leser" << c.cddaReader << "| discnav" << c.discnav
                          << "|" << c.buildFlags.join(' ');
        QVERIFY(c.cddaReader);   // Protokoll an der echten libmpv angemeldet
#ifdef Q_OS_WIN
        // Mitgelieferter Windows-Build (shinchiro): dvdnav + libbluray + discnav, kein libcdio
        QVERIFY(c.supports(DiscKind::Dvd));
        QVERIFY(c.supports(DiscKind::Bluray));
        QVERIFY(c.menus(DiscKind::Dvd));
        QVERIFY(c.discnav);
#endif
    }

    void audioCdFromSyntheticDrive()
    {
        QSignalSpy drives(svc, &OpticalMediaService::drivesChanged);
        svc->refreshDrives();
        QTRY_VERIFY_WITH_TIMEOUT(!svc->scanning(), 30000);
        QVariantMap test;
        for (const auto &d : svc->drives()) if (d.toMap().value("device") == syntheticCdDevice()) test = d.toMap();
        QCOMPARE(test.value("kind").toString(), QString("cdda"));

        const int at = openAndWait([&] { svc->openDrive(syntheticCdDevice(), false); });
        QCOMPARE(at, 0);
        QCOMPARE(pl->count(), 3);   // jede Spur ein Eintrag
        QVERIFY(pl->titleAt(0).endsWith("Track 01"));
        QVERIFY(loadIndex(0));
        QVERIFY(svc->active());
        QCOMPARE(svc->activeKind(), QString("cdda"));
        QVERIFY(!svc->navigationAvailable());   // keine Disc-Menüs/Hotkey-Umleitung bei Audio-CD
        QVERIFY(!svc->navigate("up"));
        QTRY_VERIFY_WITH_TIMEOUT(m->duration() > 0, 5000);
        QVERIFY2(qAbs(m->duration() - 9.0) < 0.1, qPrintable(QString::number(m->duration())));   // 8 s + 1 s (Spur 1)
        QCOMPARE(m->getProperty("audio-codec-name").toString(), QString("pcm_s16le"));
        QVERIFY(!m->hasVideo());
        QTRY_VERIFY_WITH_TIMEOUT(m->position() > 0.5, 8000);
        m->seek(6.0, true);
        QTRY_VERIFY_WITH_TIMEOUT(m->position() >= 6.0 && m->position() < 8.5, 5000);
        // Spurwechsel während der Wiedergabe: kein vorzeitiges Dateiende der neuen Spur (sonst springt die Liste weiter)
        QVERIFY(loadIndex(1));
        QSignalSpy ended(m, &MpvController::endOfFile);
        QTest::qWait(3000);
        for (const auto &e : ended) QVERIFY2(e.first().toString() != "eof", "Spur 2 endete sofort");
        QVERIFY(m->position() > 1.0);
        // Spur 3
        QVERIFY(loadIndex(2));
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(m->duration() - 11.0) < 0.1, 5000);
        m->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!svc->active(), 5000);
    }

    void rejectsUnknownImagesAndMissingDevices()
    {
        QTemporaryDir tmp;
        QFile f(tmp.filePath("holiday.iso"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(1 << 20, 'z'));
        f.close();
        QSignalSpy failed(svc, &OpticalMediaService::openFailed);
        const int count = pl->count();
        QCOMPARE(openAndWait([&] { svc->openSource(f.fileName(), true); }), -1);
        QCOMPARE(failed.first().first().toString(), svc->errorText(int(DiscError::InvalidImage)));
        QCOMPARE(pl->count(), count);

        // DVD-Eintrag auf ein nicht vorhandenes Gerät: libmpv meldet einen Fehler, kein Absturz
        DiscSource s{DiscKind::Dvd, tmp.filePath("gone"), "x", -1, false, false};
        pl->add({resolver::toEntry(s)});
        QSignalSpy ended(m, &MpvController::endOfFile);
        QString error;
        QVERIFY(svc->load(pl->pathAt(pl->count() - 1), 0, false, &error));
        QVERIFY(QTest::qWaitFor([&] { for (const auto &e : ended) if (e.first() == "error") return true; return false; }, 15000));
        QCOMPARE(svc->failureText(), svc->errorText(int(DiscError::Removed)));
    }

    void dvdFolderStartsInMenu()
    {
        if (dvdDir.isEmpty() || !QFileInfo::exists(dvdDir)) QSKIP("BLOCKED: keine DVD-Testdisc (UVE_TEST_DVD_DIR, tests/fixtures/make-dvd-fixture.sh)");
        QSignalSpy inspected(svc, &OpticalMediaService::inspected);
        svc->inspect(dvdDir);
        QVERIFY(inspected.wait(10000));
        QCOMPARE(inspected.first().at(1).toMap().value("kind").toString(), QString("dvd"));
        QVERIFY(inspected.first().at(1).toMap().value("menus").toBool());

        const int at = openAndWait([&] { svc->openSource(dvdDir, true); });
        QVERIFY(at >= 0);
        QVERIFY(pl->pathAt(at).contains("menu=1"));
        QVERIFY(loadIndex(at));
        QCOMPARE(svc->activeKind(), QString("dvd"));
        QVERIFY(svc->navigationAvailable());
        QTRY_VERIFY_WITH_TIMEOUT(svc->menuActive(), 8000);   // disc-menu-active von libmpv
        QCOMPARE(svc->menuKeyAction("Right"), QString("right"));
        QVERIFY(svc->navigate("right"));
        QVERIFY(svc->navigate("select"));
        QTRY_VERIFY_WITH_TIMEOUT(svc->titles().size() == 2, 5000);   // Titel 1 + „Disc Menu“ (edition-list)
        // Titelwahl über die Titelliste verlässt das Menü
        svc->selectTitle(0);
        QTRY_VERIFY_WITH_TIMEOUT(!svc->menuActive(), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(m->duration() - 12.0) < 0.5, 8000);
        QVERIFY(svc->menuKeyAction("Right").isEmpty());   // außerhalb des Menüs gelten wieder die normalen Tasten
        // Zurück ins Titelmenü der Disc (VMGM)
        QVERIFY(svc->navigate("title-menu"));
        QElapsedTimer back;
        back.start();
        QTRY_VERIFY_WITH_TIMEOUT(svc->menuActive(), 20000);
        qInfo() << "title-menu nach" << back.elapsed() << "ms aktiv";
        m->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!svc->active(), 5000);
    }

    void dvdIsoPlaysTitleWithChapters()
    {
        if (dvdIso.isEmpty() || !QFileInfo::exists(dvdIso)) QSKIP("BLOCKED: kein DVD-Testimage (UVE_TEST_DVD_DIR)");
        // Kopie, damit sich das Entfernen des Mediums nachstellen lässt
        QTemporaryDir tmp;
        const QString iso = tmp.filePath("copy.iso");
        QVERIFY(QFile::copy(dvdIso, iso));
        const int at = openAndWait([&] { svc->openSource(iso, false); });
        QVERIFY(at >= 0);
        QVERIFY(!pl->pathAt(at).contains("menu=1"));
        QVERIFY(loadIndex(at));
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(m->duration() - 12.0) < 0.5, 8000);
        QVERIFY(!svc->menuActive());
        QTRY_COMPARE_WITH_TIMEOUT(svc->chapters().size(), 2, 5000);
        QCOMPARE(svc->chapters().at(1).toMap().value("time").toDouble(), 6.0);
        QVERIFY(m->hasVideo());
        QCOMPARE(m->getProperty("video-params/w").toInt(), 720);
        svc->selectChapter(1);
        QTRY_VERIFY_WITH_TIMEOUT(svc->chapter() == 1 && m->position() >= 5.5, 8000);
        m->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!svc->active(), 5000);
        // Medium „entfernt“
        QVERIFY(QFile::remove(iso));
        QCOMPARE(svc->failureText(), svc->errorText(int(DiscError::Removed)));
    }
};

QTEST_GUILESS_MAIN(TstOpticalPlayback)
#include "tst_optical_playback.moc"
