// Benachrichtigungston: spielt die mitgelieferte pn.mp3 wirklich über MCI ab, auch unter einem
// Pfad mit Leerzeichen und Umlauten, und prüft Sperre gegen Dauerklingeln und fehlende Datei.
#include "NotifySound.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

static int g_failures = 0;
static void expect(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "ok    " : "FEHLER", what);
    if (!ok) ++g_failures;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    const QString dir = temp.path() + QStringLiteral("/Jürgen Müller/plugins");
    QDir().mkpath(dir);
    const QString file = dir + QStringLiteral("/pn.mp3");
    expect(QFile::copy(QStringLiteral(SOUND_FILE), file), "Ton kopiert");

    NotifySound sound;
    sound.setFile(file);
    sound.setVolumePercent(30);
    expect(!sound.file().isEmpty(), "Datei erkannt");
    const bool played = sound.play();
    expect(played, "Windows spielt die MP3 ab");
    QThread::msleep(150);
    std::printf("       Zustand nach dem Start: „%s“\n", sound.mode().toUtf8().constData());
    expect(sound.mode() == QStringLiteral("playing"), "Ton läuft");
    std::printf("       Lautstärke laut Windows: %d von 1000\n", sound.deviceVolume());
    expect(qAbs(sound.deviceVolume() - 300) <= 10, "Lautstärke 30 % (Windows rundet leicht)");
    expect(sound.play() && sound.playCount() == 1, "zweite Nachricht im selben Moment klingelt nicht erneut");
    QElapsedTimer waited;
    waited.start();
    while (sound.mode() == QStringLiteral("playing") && waited.elapsed() < 5000) QThread::msleep(50);
    std::printf("       Ton nach %lld ms zu Ende\n", waited.elapsed() + 150);
    expect(sound.mode() == QStringLiteral("stopped"), "Ton endet von selbst");
    QThread::msleep(1000);
    expect(sound.play() && sound.playCount() == 2, "später klingelt es wieder");

    NotifySound missing;
    missing.setFile(dir + QStringLiteral("/gibt-es-nicht.mp3"));
    expect(missing.file().isEmpty() && !missing.play(), "fehlende Datei: stumm, kein Absturz");

    std::printf("\nERGEBNIS: %d Fehler\n", g_failures);
    return g_failures ? 1 : 0;
}
