// Integrationstest gegen die echte libmpv (ohne Fenster: vo=null, ao=null).
// Prüft Laden, Spurenerkennung, Audiospurwechsel, Untertitel, Sprung, Dateiende und Fehlerfall.
#include "player/MpvController.h"

#include <QSignalSpy>
#include <QtTest>
#include <clocale>

using namespace uve;

class TstMpvIntegration : public QObject
{
    Q_OBJECT
    MpvController *m = nullptr;
    const QString mkv = QStringLiteral(UVE_TEST_MEDIA "/test-2audio-sub.mkv");
    const QString avi = QStringLiteral(UVE_TEST_MEDIA "/test-legacy.avi");

private slots:
    void initTestCase()
    {
        std::setlocale(LC_NUMERIC, "C");
        m = new MpvController(this);
        QString err;
        QVERIFY2(m->initialize({{"vo", "null"}, {"ao", "null"}, {"hwdec", "no"}}, &err), qPrintable(err));
        m->markRenderReady(); // vo=null braucht keinen Render-Kontext
        QVERIFY(m->mpvVersion().startsWith("mpv"));
    }

    void loadsMkvWithTracks()
    {
        QSignalSpy loaded(m, &MpvController::fileLoaded);
        m->load(mkv);
        QVERIFY(loaded.wait(10000));
        QTRY_VERIFY_WITH_TIMEOUT(m->duration() > 9.5, 5000);
        QCOMPARE(m->audioTracks().size(), 2);
        QCOMPARE(m->subtitleTracks().size(), 1);
        QVERIFY(m->hasVideo());
        QTRY_COMPARE_WITH_TIMEOUT(m->state(), MpvController::Playing, 5000);
        // Bezeichnungen enthalten Sprache und Codec
        QVERIFY(m->audioTracks().at(1).toMap().value("label").toString().contains("ENG"));
        QVERIFY(m->audioTracks().at(1).toMap().value("label").toString().contains("AC3"));
    }

    void switchesAudioTrack()
    {
        m->setAudioTrack(2);
        QTRY_VERIFY_WITH_TIMEOUT(m->audioTracks().at(1).toMap().value("selected").toBool(), 5000);
        QCOMPARE(m->getProperty("audio-codec-name").toString(), QString("ac3"));
    }

    void subtitleToggle()
    {
        m->setSubtitleTrack(0);
        QTRY_VERIFY_WITH_TIMEOUT(!m->subtitleTracks().at(0).toMap().value("selected").toBool(), 3000);
        m->setSubtitleTrack(1);
        QTRY_VERIFY_WITH_TIMEOUT(m->subtitleTracks().at(0).toMap().value("selected").toBool(), 3000);
    }

    void seeksAndPauses()
    {
        m->seek(6.0, true);
        QTRY_VERIFY_WITH_TIMEOUT(std::fabs(m->position() - 6.0) < 0.6, 5000);
        m->setPaused(true);
        QTRY_COMPARE_WITH_TIMEOUT(m->state(), MpvController::Paused, 3000);
        m->setPaused(false);
        QTRY_COMPARE_WITH_TIMEOUT(m->state(), MpvController::Playing, 3000);
    }

    void reportsEndOfFile()
    {
        QSignalSpy end(m, &MpvController::endOfFile);
        m->seek(9.0, true);
        QVERIFY(end.wait(8000));
        QCOMPARE(end.first().first().toString(), QString("eof"));
        QCOMPARE(m->state(), MpvController::Ended);
    }

    void playsLegacyAvi()
    {
        QSignalSpy loaded(m, &MpvController::fileLoaded);
        m->load(avi);
        QVERIFY(loaded.wait(10000));
        QTRY_VERIFY_WITH_TIMEOUT(m->position() > 0.5, 6000);
        QCOMPARE(m->getProperty("video-codec").toString().contains("MPEG-4", Qt::CaseInsensitive) ||
                     m->getProperty("video-format").toString() == "mpeg4", true);
    }

    void reportsErrorForMissingFile()
    {
        QSignalSpy end(m, &MpvController::endOfFile);
        m->load(QStringLiteral(UVE_TEST_MEDIA "/gibt-es-nicht.mkv"));
        // Zuerst "stop" für die laufende Datei, danach "error" für die fehlende
        QTRY_COMPARE_WITH_TIMEOUT(end.isEmpty() ? QString() : end.last().first().toString(), QString("error"), 8000);
        QCOMPARE(m->state(), MpvController::Error);
        QVERIFY(!m->errorString().isEmpty());
    }
};

QTEST_MAIN(TstMpvIntegration)
#include "tst_mpv_integration.moc"
