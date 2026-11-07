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

    void prefersListOrderOverExactLanguageMatch_data()
    {
        QTest::addColumn<QString>("langA");
        QTest::addColumn<QString>("langB");
        QTest::addColumn<int>("selected");
        QTest::addColumn<QString>("languages");
        QTest::addColumn<int>("expected");
        const QString standard = QStringLiteral("de,ger,deu,en,eng");
        QTest::newRow("regional tag loses to exact match in mpv") << "de-DE" << "en" << 2 << standard << 1;
        QTest::newRow("three-letter track code") << "ger" << "en" << 2 << standard << 1;
        QTest::newRow("three-letter preference") << "de-DE" << "en" << 2 << QStringLiteral("ger,en") << 1;
        QTest::newRow("preferred already selected") << "de-DE" << "en" << 1 << standard << 0;
        QTest::newRow("list prefers selected english") << "de-DE" << "en" << 2 << QStringLiteral("en,de") << 0;
        QTest::newRow("same language, other region stays") << "de-DE" << "de-AT" << 2 << standard << 0;
        QTest::newRow("no track in the list") << "fr" << "it" << 1 << standard << 0;
        QTest::newRow("selected track not in the list") << "de" << "fr" << 2 << QStringLiteral("de") << 0;
        QTest::newRow("empty list") << "de-DE" << "en" << 2 << QString() << 0;
        QTest::newRow("nothing selected") << "de-DE" << "en" << 0 << standard << 0;
    }

    void prefersListOrderOverExactLanguageMatch()
    {
        QFETCH(QString, langA);
        QFETCH(QString, langB);
        QFETCH(int, selected);
        QFETCH(QString, languages);
        QFETCH(int, expected);
        const QVariantList tracks{
            QVariantMap{{"id", 1}, {"type", "video"}, {"selected", true}},
            QVariantMap{{"id", 1}, {"type", "audio"}, {"lang", langA}, {"selected", selected == 1}},
            QVariantMap{{"id", 2}, {"type", "audio"}, {"lang", langB}, {"selected", selected == 2}},
        };
        QCOMPARE(MpvController::preferredTrackId(tracks, "audio", languages.split(',', Qt::SkipEmptyParts)), expected);
    }

    void languagePreferenceKeepsExternalAndPicksDefaultTrack()
    {
        // Externe Datei hat Vorrang, auch wenn eine eingebettete Spur besser passt
        QCOMPARE(MpvController::preferredTrackId(
                     {QVariantMap{{"id", 1}, {"type", "sub"}, {"lang", "de-DE"}},
                      QVariantMap{{"id", 2}, {"type", "sub"}, {"lang", "en"}, {"selected", true}, {"external", true}}},
                     "sub", QStringList{"de", "en"}), 0);
        // Mehrere Spuren in der gewünschten Sprache: die als Standard markierte
        QCOMPARE(MpvController::preferredTrackId(
                     {QVariantMap{{"id", 1}, {"type", "sub"}, {"lang", "de-DE"}},
                      QVariantMap{{"id", 2}, {"type", "sub"}, {"lang", "en"}, {"selected", true}},
                      QVariantMap{{"id", 3}, {"type", "sub"}, {"lang", "de-DE"}, {"default", true}}},
                     "sub", QStringList{"de", "en"}), 3);
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
