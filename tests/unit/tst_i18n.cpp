// Sprachdateien (lang/uve_*.ts → bin/lang/uve_*.qm) und Sprachumschaltung
#include "core/Translation.h"

#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

using namespace uve;

class TstI18n : public QObject
{
    Q_OBJECT
    static QStringList placeholders(const QString &s)
    {
        QStringList l;
        auto it = QRegularExpression(QStringLiteral("%\\d")).globalMatch(s);
        while (it.hasNext()) l << it.next().captured();
        l.sort();
        return l;
    }

private slots:
    void tsFiles_data()
    {
        QTest::addColumn<QString>("file");
        for (const auto &fi : QDir(QStringLiteral(UVE_LANG_DIR)).entryInfoList({"uve_*.ts"}, QDir::Files))
            QTest::newRow(qPrintable(fi.fileName())) << fi.absoluteFilePath();
        QVERIFY(QDir(QStringLiteral(UVE_LANG_DIR)).entryList({"uve_*.ts"}).size() >= 6);
    }
    void tsFiles()
    {
        QFETCH(QString, file);
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QDomDocument doc;
        QVERIFY(bool(doc.setContent(&f)));
        const auto msgs = doc.elementsByTagName("message");
        QVERIFY(msgs.size() > 600);
        int unfinished = 0;
        QStringList menuMnemonics;
        for (int i = 0; i < msgs.size(); ++i) {
            const auto m = msgs.at(i).toElement();
            const QString src = m.firstChildElement("source").text();
            const auto tr = m.firstChildElement("translation");
            if (tr.attribute("type") == "unfinished" || tr.text().isEmpty()) { ++unfinished; continue; }
            const QString v = tr.text();
            QVERIFY2(placeholders(src) == placeholders(v), qPrintable(QStringLiteral("Platzhalter: %1 → %2").arg(src, v)));
            QVERIFY2(src.startsWith(' ') == v.startsWith(' ') && src.endsWith(' ') == v.endsWith(' '),
                     qPrintable(QStringLiteral("Randleerzeichen: '%1' → '%2'").arg(src, v)));
            if (m.parentNode().firstChildElement("name").text() == "Main" && (src.startsWith('&') || src == "A&nsicht")) {
                const int a = v.indexOf('&');
                QVERIFY2(a >= 0 && a + 1 < v.size(), qPrintable("Menü ohne &: " + v));
                menuMnemonics << v.at(a + 1).toLower();
            }
        }
        QCOMPARE(unfinished, 0);
        QCOMPARE(menuMnemonics.size(), 9);                                                            // 9 Hauptmenüs
        QCOMPARE(QSet<QString>(menuMnemonics.begin(), menuMnemonics.end()).size(), menuMnemonics.size()); // Alt+Buchstabe eindeutig
    }

    void switchingAndFallback()
    {
        LanguageManager lm({QStringLiteral(UVE_QM_DIR)});
        const auto langs = lm.available();
        QStringList codes;
        for (const auto &v : langs) codes << v.toMap().value("code").toString();
        for (const char *c : {"de", "en", "es", "it", "pl", "el", "tr"}) QVERIFY2(codes.contains(c), c);

        QVERIFY(lm.apply("en"));
        QCOMPARE(QCoreApplication::translate("Main", "&Datei"), QString("&File"));
        QCOMPARE(QCoreApplication::translate("PlayerApp", "Fortgesetzt bei %1").arg("1:23"), QString("Resumed at 1:23"));
        QVERIFY(lm.apply("el"));
        QCOMPARE(QCoreApplication::translate("Main", "&Datei"), QString("&Αρχείο"));
        QVERIFY(lm.apply("tr"));
        QCOMPARE(QCoreApplication::translate("SettingsDialog", "Einstellungen"), QString("Ayarlar"));
        QVERIFY(lm.apply("de"));
        QCOMPARE(QCoreApplication::translate("Main", "&Datei"), QString("&Datei"));    // Quelltexte
        QVERIFY(!lm.apply("zz"));                                                        // unbekannt → Deutsch

        // Benutzerordner hat Vorrang; leere/defekte .qm wird ignoriert
        QTemporaryDir user;
        QFile bad(user.filePath("uve_xx.qm"));
        QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("x"); bad.close();
        LanguageManager lm2({QStringLiteral(UVE_QM_DIR), user.path()});
        QString err;
        QVERIFY(!lm2.apply("xx", &err));
    }
};

QTEST_GUILESS_MAIN(TstI18n)
#include "tst_i18n.moc"
