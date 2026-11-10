// Disc-Navigation ohne Laufwerk: discnav-Weiterleitung nur nach Fähigkeits- und Zustandsprüfung,
// Tastenbelegung im Disc-Menü (Pfeile/Enter statt Spulen/Lautstärke), Fehlertexte, Dienst ohne aktive Disc.
#include "core/AppSettings.h"
#include "player/MpvController.h"
#include "services/HotkeyModel.h"
#include "services/PlaylistModel.h"
#include "services/optical/DiscNavigationBridge.h"
#include "services/optical/OpticalMediaService.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace uve;
using namespace uve::optical;

namespace {
DiscCapabilities caps(bool discnav)
{
    return DiscCapabilities::fromProbe({{"protocols", QStringList{"dvd", "bd"}},
                                        {"options", QStringList{"dvd-device", "bluray-device", "disc-menu"}},
                                        {"commands", discnav ? QStringList{"discnav"} : QStringList{}},
                                        {"cddaReader", true}});
}
} // namespace

class TstDiscNavigation : public QObject
{
    Q_OBJECT

private slots:
    void dispatchesOnlyWithActiveDiscAndCapability()
    {
        const auto c = caps(true);
        QCOMPARE(nav::command("up", DiscKind::Dvd, true, c), (QStringList{"discnav", "up"}));
        QCOMPARE(nav::command("select", DiscKind::Bluray, true, c), (QStringList{"discnav", "select"}));
        QCOMPARE(nav::command("title-menu", DiscKind::Dvd, true, c), (QStringList{"discnav", "title-menu"}));
        QCOMPARE(nav::command("popup", DiscKind::Bluray, true, c), (QStringList{"discnav", "popup"}));
        QVERIFY(nav::command("up", DiscKind::Dvd, false, c).isEmpty());          // keine Disc aktiv
        QVERIFY(nav::command("up", DiscKind::Dvd, true, caps(false)).isEmpty()); // Build ohne discnav
        QVERIFY(nav::command("up", DiscKind::AudioCd, true, c).isEmpty());       // Audio-CD hat keine Menüs
        QVERIFY(nav::command("up", DiscKind::Unknown, true, c).isEmpty());
        QVERIFY(nav::command("title-menu", DiscKind::Bluray, true, c).isEmpty());
        // Nur bekannte Aktionen – nichts anderes gelangt in den mpv-Befehl
        QVERIFY(nav::command("quit", DiscKind::Dvd, true, c).isEmpty());
        QVERIFY(nav::command("up;quit", DiscKind::Dvd, true, c).isEmpty());
        QVERIFY(nav::command("", DiscKind::Dvd, true, c).isEmpty());
        QVERIFY(nav::actions(DiscKind::AudioCd).isEmpty());
    }

    void menuKeysOverrideSeekAndVolumeOnlyInMenu()
    {
        for (const QString &k : nav::menuKeys()) QVERIFY2(nav::keyAction(k, false).isEmpty(), qPrintable(k));
        QCOMPARE(nav::keyAction("Up", true), QString("up"));
        QCOMPARE(nav::keyAction("Down", true), QString("down"));
        QCOMPARE(nav::keyAction("Left", true), QString("left"));
        QCOMPARE(nav::keyAction("Right", true), QString("right"));
        QCOMPARE(nav::keyAction("Return", true), QString("select"));
        QCOMPARE(nav::keyAction("Enter", true), QString("select"));
        QCOMPARE(nav::keyAction("Backspace", true), QString("prev"));
        QVERIFY(nav::keyAction("Space", true).isEmpty());   // Pause bleibt Pause
        QVERIFY(nav::keyAction("Ctrl+Left", true).isEmpty());

        // Standardbelegung: genau die Tasten für Spulen/Lautstärke werden im Menü umgeleitet; Belegungen bleiben gespeichert
        QTemporaryDir tmp;
        AppSettings s(tmp.path());
        HotkeyModel h(&s);
        QCOMPARE(h.shortcut("seek.back"), QString("Left"));
        QCOMPARE(h.shortcut("volume.up"), QString("Up"));
        QVERIFY(nav::menuKeys().contains(h.shortcut("seek.fwd")));
        QVERIFY(nav::menuKeys().contains(h.shortcut("volume.down")));
        QVERIFY(!nav::menuKeys().contains(h.shortcut("play.toggle")));
        QCOMPARE(h.shortcut("disc.menu"), QString("Ctrl+M"));
        QCOMPARE(h.shortcut("open.disc"), QString("Ctrl+Shift+D"));
    }

    void serviceWithoutDiscSendsNothing()
    {
        MpvController player;   // nicht initialisiert: es darf trotzdem nichts abstürzen oder gesendet werden
        PlaylistModel playlist;
        OpticalMediaService svc(&player, &playlist, QDir::tempPath());
        svc.setCapabilities(caps(true));
        QVERIFY(!svc.active());
        QVERIFY(!svc.navigationAvailable());
        QVERIFY(!svc.menuActive());
        QVERIFY(!svc.navigate("up"));
        QVERIFY(svc.menuKeyAction("Up").isEmpty());
        svc.selectChapter(1);
        svc.selectTitle(0);
        QString error;
        QVERIFY(!svc.load("uve-disc://dvd?dev=", 0, false, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(svc.failureText().isEmpty());
        // Disc-Informationen ohne Disc: nur Fähigkeiten
        const auto info = svc.info();
        QVERIFY(info.size() >= 8);
        QVERIFY(svc.capabilities().value("dvdMenus").toBool());
        QVERIFY(!svc.capabilities().value("bdj").toBool());
    }

    void everyErrorHasAText()
    {
        MpvController player;
        PlaylistModel playlist;
        OpticalMediaService svc(&player, &playlist, QDir::tempPath());
        QVERIFY(svc.errorText(int(DiscError::None)).isEmpty());
        QSet<QString> seen;
        for (int e = int(DiscError::NoDrive); e <= int(DiscError::InvalidImage); ++e) {
            const QString t = svc.errorText(e);
            QVERIFY2(!t.isEmpty(), qPrintable(QString::number(e)));
            QVERIFY(!seen.contains(t));
            seen.insert(t);
        }
        QCOMPARE(svc.kindLabel("dvd"), QString("DVD"));
        QCOMPARE(svc.kindLabel("cdda"), QString("Audio-CD"));
    }

    void openDriveRejectsUnknownDevices()
    {
        MpvController player;
        PlaylistModel playlist;
        OpticalMediaService svc(&player, &playlist, QDir::tempPath());
        QSignalSpy failed(&svc, &OpticalMediaService::openFailed);
        svc.openDrive("C:", false);   // nicht in der Laufwerksliste
        QCOMPARE(failed.size(), 1);
        QCOMPARE(playlist.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TstDiscNavigation)
#include "tst_disc_navigation.moc"
