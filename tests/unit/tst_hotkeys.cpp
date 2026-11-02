#include "core/AppSettings.h"
#include "services/HotkeyModel.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace uve;

class TstHotkeys : public QObject
{
    Q_OBJECT
private slots:
    void defaultsHaveNoConflicts()
    {
        QSet<QString> seen;
        for (const auto &a : HotkeyModel::defaultActions()) {
            const QString n = QKeySequence(a.defaultSeq, QKeySequence::PortableText).toString(QKeySequence::PortableText);
            QVERIFY2(!seen.contains(n), qPrintable("doppelt: " + n));
            seen.insert(n);
        }
    }
    void conflictDetectionAndForce()
    {
        QTemporaryDir dir;
        AppSettings s(dir.path());
        HotkeyModel h(&s);
        QCOMPARE(h.conflictFor("mute", "Space"), QString("play.toggle"));
        QVERIFY(!h.setShortcut("mute", "Space"));
        QCOMPARE(h.shortcut("mute"), QString("M"));
        QVERIFY(h.setShortcut("mute", "Space", true));
        QCOMPARE(h.shortcut("mute"), QString("Space"));
        QCOMPARE(h.shortcut("play.toggle"), QString());
    }
    void persistAndReset()
    {
        QTemporaryDir dir;
        {
            AppSettings s(dir.path());
            HotkeyModel h(&s);
            QVERIFY(h.setShortcut("snapshot", "Ctrl+Shift+P"));
            s.flush();
        }
        AppSettings s2(dir.path());
        HotkeyModel h2(&s2);
        QCOMPARE(h2.shortcut("snapshot"), QString("Ctrl+Shift+P"));
        h2.resetAll();
        QCOMPARE(h2.shortcut("snapshot"), QString("Shift+S"));
    }
    void sequenceFromKey()
    {
        QTemporaryDir dir;
        AppSettings s(dir.path());
        HotkeyModel h(&s);
        QCOMPARE(h.sequenceFromKey(Qt::Key_U, Qt::ControlModifier | Qt::AltModifier), QString("Ctrl+Alt+U"));
        QCOMPARE(h.sequenceFromKey(Qt::Key_Control, Qt::ControlModifier), QString());
    }
    void corruptSettingsAreBackedUp()
    {
        QTemporaryDir dir;
        QFile f(dir.filePath("settings.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ kaputt");
        f.close();
        AppSettings s(dir.path());
        QCOMPARE(s.value("volume").toInt(), 100); // Standardwerte statt Absturz
        const auto backups = QDir(dir.path()).entryList({"settings.json.corrupt-*"});
        QCOMPARE(backups.size(), 1);              // Original bleibt erhalten
    }
};

QTEST_GUILESS_MAIN(TstHotkeys)
#include "tst_hotkeys.moc"
