// Optische Medien ohne Laufwerk: Quellenerkennung (Ordner, ISO 9660, UDF), Pfadnormalisierung, Playlist-Einträge,
// libmpv-Ladeplan, Fähigkeiten aus der Laufzeitabfrage und der Audio-CD-Datenstrom (synthetische CD).
#include "services/MediaFiles.h"
#include "services/PlaylistModel.h"
#include "services/optical/CdAudioStream.h"
#include "services/optical/DiscCapabilities.h"
#include "services/optical/DiscSourceResolver.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

using namespace uve;
using namespace uve::optical;

namespace {

void touch(const QString &path, const QByteArray &data = "x")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

// Minimales ISO-9660-Image: PVD in Sektor 16, Wurzel in Sektor 20, Unterordner ab Sektor 21
struct IsoBuilder
{
    QByteArray img = QByteArray(40 * 2048, '\0');
    int nextSector = 21;

    static QByteArray record(const QByteArray &name, quint32 extent, quint32 size, bool dir)
    {
        QByteArray r(33 + name.size() + (name.size() % 2 == 0 ? 1 : 0), '\0');
        r[0] = char(r.size());
        qToLittleEndian(extent, r.data() + 2);
        qToBigEndian(extent, r.data() + 6);
        qToLittleEndian(size, r.data() + 10);
        qToBigEndian(size, r.data() + 14);
        r[25] = char(dir ? 2 : 0);
        r[32] = char(name.size());
        std::memcpy(r.data() + 33, name.constData(), size_t(name.size()));
        return r;
    }
    void writeDir(int sector, const QList<QByteArray> &records)
    {
        QByteArray d = record(QByteArray(1, '\0'), quint32(sector), 2048, true) + record(QByteArray(1, '\1'), 20, 2048, true);
        for (const auto &r : records) d += r;
        img.replace(sector * 2048, d.size(), d);
    }
    // root: {Name → Liste von Dateien (leer = Datei statt Ordner)}
    QByteArray build(const QList<QPair<QByteArray, QList<QByteArray>>> &root)
    {
        QByteArray pvd(2048, '\0');
        pvd[0] = 1;
        std::memcpy(pvd.data() + 1, "CD001", 5);
        pvd[6] = 1;
        pvd[128] = 0; pvd[129] = char(8);   // Blockgröße 2048 (LE)
        const QByteArray rootRec = record(QByteArray(1, '\0'), 20, 2048, true);
        pvd.replace(156, rootRec.size(), rootRec);
        img.replace(16 * 2048, 2048, pvd);
        QByteArray term(2048, '\0');
        term[0] = char(-1);   // Typ 255: Abschluss der Deskriptoren
        std::memcpy(term.data() + 1, "CD001", 5);
        img.replace(17 * 2048, 2048, term);
        QList<QByteArray> rootRecords;
        for (const auto &e : root) {
            if (e.second.isEmpty()) { rootRecords << record(e.first + ";1", 30, 10, false); continue; }
            const int s = nextSector++;
            QList<QByteArray> files;
            for (const auto &f : e.second) files << (f.endsWith('/') ? record(f.chopped(1), 31, 2048, true) : record(f + ";1", 30, 100, false));
            writeDir(s, files);
            rootRecords << record(e.first, quint32(s), 2048, true);
        }
        writeDir(20, rootRecords);
        return img;
    }
};

QByteArray udfImage(const QList<const char *> &names)
{
    QByteArray img(64 * 2048, '\0');
    std::memcpy(img.data() + 16 * 2048 + 1, "BEA01", 5);
    std::memcpy(img.data() + 17 * 2048 + 1, "NSR02", 5);
    std::memcpy(img.data() + 18 * 2048 + 1, "TEA01", 5);
    int at = 40 * 2048;
    for (const char *n : names) { img[at] = 8; std::memcpy(img.data() + at + 1, n, strlen(n)); at += 64; }
    return img;
}

// Laufwerk, das nach einigen Lesebefehlen „entfernt“ wird
class FailingCd : public CdAudioDevice
{
public:
    int readsLeft = 2;
    std::unique_ptr<CdAudioDevice> inner = createSyntheticCd(2, 5);
    QList<CdTrack> tracks(QString *e) override { return inner->tracks(e); }
    bool read(qint64 lba, int count, char *buf) override { return readsLeft-- > 0 && inner->read(lba, count, buf); }
};

class DataTrackCd : public CdAudioDevice
{
public:
    QList<CdTrack> tracks(QString *) override { return {CdTrack{1, 0, 750, false}, CdTrack{2, 900, 750, true}}; }
    bool read(qint64, int count, char *buf) override { std::memset(buf, 0, size_t(count) * kCdSectorBytes); return true; }
};

DiscCapabilities fullCaps()
{
    return DiscCapabilities::fromProbe({
        {"protocols", QStringList{"file", "dvd", "dvdnav", "bd", "bluray", "http"}},
        {"options", QStringList{"dvd-device", "bluray-device", "disc-menu"}},
        {"commands", QStringList{"loadfile", "discnav"}},
        {"properties", QStringList{"disc-menu-active", "chapter-list"}},
        {"configuration", "-Dlibmpv=true -Dlibbluray=enabled -Ddvdnav=enabled -Dlua=enabled"},
        {"mpvVersion", "mpv v0.41.0"},
        {"cddaReader", true},
    });
}

} // namespace

class TstOpticalSources : public QObject
{
    Q_OBJECT

private slots:
    void detectsDvdFolder()
    {
        QTemporaryDir tmp;
        touch(tmp.filePath("FILM/VIDEO_TS/VIDEO_TS.IFO"));
        touch(tmp.filePath("FILM/VIDEO_TS/VTS_01_1.VOB"));
        auto d = resolver::detectFolder(tmp.filePath("FILM"));
        QCOMPARE(d.kind, DiscKind::Dvd);
        QCOMPARE(d.error, DiscError::None);
        QCOMPARE(QDir::fromNativeSeparators(d.device), QDir(tmp.filePath("FILM")).canonicalPath());
        // VIDEO_TS selbst gewählt → Disc-Wurzel ist der Ordner darüber
        d = resolver::detectFolder(tmp.filePath("FILM/VIDEO_TS"));
        QCOMPARE(d.kind, DiscKind::Dvd);
        QCOMPARE(QDir::fromNativeSeparators(d.device), QDir(tmp.filePath("FILM")).canonicalPath());
    }

    void detectsBlurayFolderCaseInsensitiveWithAacsAndBdj()
    {
        QTemporaryDir tmp;
        touch(tmp.filePath("BD/bdmv/INDEX.BDMV"));
        auto d = resolver::detectFolder(tmp.filePath("BD"));
        QCOMPARE(d.kind, DiscKind::Bluray);
        QVERIFY(!d.encrypted);
        QVERIFY(!d.bdj);
        touch(tmp.filePath("BD/AACS/Unit_Key_RO.inf"));
        touch(tmp.filePath("BD/bdmv/JAR/00000.jar"));
        d = resolver::detectFolder(tmp.filePath("BD/BDMV"));
        QCOMPARE(d.kind, DiscKind::Bluray);
        QVERIFY(d.encrypted);
        QVERIFY(d.bdj);
    }

    void rejectsFoldersWithoutDiscStructure()
    {
        QTemporaryDir tmp;
        touch(tmp.filePath("X/VIDEO_TS/readme.txt"));   // VIDEO_TS ohne VIDEO_TS.IFO
        QCOMPARE(resolver::detectFolder(tmp.filePath("X")).error, DiscError::Incompatible);
        QCOMPARE(resolver::detectFolder(tmp.filePath("missing")).error, DiscError::Incompatible);
        QCOMPARE(resolver::detect(tmp.filePath("missing")).kind, DiscKind::Unknown);
    }

    void detectsIso9660Images()
    {
        IsoBuilder dvd;
        auto d = resolver::detectImageData(dvd.build({{"VIDEO_TS", {"VIDEO_TS.IFO", "VTS_01_1.VOB"}}, {"README.TXT", {}}}), 40 * 2048);
        QCOMPARE(d.kind, DiscKind::Dvd);
        QVERIFY(d.image);

        IsoBuilder bd;
        d = resolver::detectImageData(bd.build({{"BDMV", {"INDEX.BDMV", "JAR/"}}, {"AACS", {"MKB_RO.INF"}}}), 40 * 2048);
        QCOMPARE(d.kind, DiscKind::Bluray);
        QVERIFY(d.encrypted);
        QVERIFY(d.bdj);

        IsoBuilder data;
        d = resolver::detectImageData(data.build({{"DOCS", {"A.PDF"}}}), 40 * 2048);
        QCOMPARE(d.kind, DiscKind::Unknown);
        QCOMPARE(d.error, DiscError::InvalidImage);   // weder Disc-Struktur noch UDF
    }

    void detectsUdfImagesAndRejectsGarbage()
    {
        auto d = resolver::detectImageData(udfImage({"VIDEO_TS", "VIDEO_TS.IFO"}), 64 * 2048);
        QCOMPARE(d.kind, DiscKind::Dvd);
        d = resolver::detectImageData(udfImage({"BDMV", "index.bdmv", "AACS"}), 64 * 2048);
        QCOMPARE(d.kind, DiscKind::Bluray);
        QVERIFY(d.encrypted);
        d = resolver::detectImageData(udfImage({"PHOTOS"}), 64 * 2048);
        QCOMPARE(d.error, DiscError::Incompatible);

        QByteArray garbage(64 * 2048, 'Q');
        QCOMPARE(resolver::detectImageData(garbage, garbage.size()).error, DiscError::InvalidImage);
        QCOMPARE(resolver::detectImageData(QByteArray(100, '\0'), 100).error, DiscError::InvalidImage);   // abgeschnitten

        // Datei auf der Platte: .iso mit Videodaten wird nicht als Video „geraten“
        QTemporaryDir tmp;
        touch(tmp.filePath("fake.iso"), garbage);
        QCOMPARE(resolver::detectImage(tmp.filePath("fake.iso")).error, DiscError::InvalidImage);
        touch(tmp.filePath("dvd.iso"), IsoBuilder().build({{"VIDEO_TS", {"VIDEO_TS.IFO"}}}));
        const auto ok = resolver::detect(tmp.filePath("dvd.iso"));
        QCOMPARE(ok.kind, DiscKind::Dvd);
        QCOMPARE(QDir::fromNativeSeparators(ok.device), QFileInfo(tmp.filePath("dvd.iso")).canonicalFilePath());
    }

    void canonicalPathRejectsSchemes()
    {
        QTemporaryDir tmp;
        QVERIFY(!resolver::canonicalPath(tmp.path()).isEmpty());
        QVERIFY(!resolver::canonicalPath(QUrl::fromLocalFile(tmp.path()).toString()).isEmpty());
        QVERIFY(resolver::canonicalPath("dvd://").isEmpty());
        QVERIFY(resolver::canonicalPath("http://example.com/a.iso").isEmpty());
        QVERIFY(resolver::canonicalPath(tmp.filePath("nope")).isEmpty());
        QVERIFY(resolver::canonicalPath("").isEmpty());
    }

    void entriesRoundTrip_data()
    {
        QTest::addColumn<QString>("device");
        QTest::newRow("drive") << "D:";
        QTest::newRow("special characters") << "C:\\Filme & Serien\\a=b,c;d?e#f%20 Ü\\film.iso";
        QTest::newRow("linux device") << "/dev/sr0";
    }

    void entriesRoundTrip()
    {
        QFETCH(QString, device);
        DiscSource s{DiscKind::Dvd, device, QStringLiteral("DVD – Ä & B"), 3, true, false};
        const QString e = resolver::toEntry(s);
        QVERIFY(resolver::isEntry(e));
        QVERIFY(media::isDiscEntry(e));
        DiscSource back;
        QVERIFY(resolver::fromEntry(e, &back));
        QCOMPARE(back.kind, DiscKind::Dvd);
        QCOMPARE(back.device, device);
        QCOMPARE(back.name, s.name);
        QCOMPARE(back.title, 3);
        QVERIFY(back.menu);
        // Wiedergabeliste: Name aus dem Eintrag, Eintrag bleibt beim Wiederherstellen der Sitzung erhalten
        PlaylistModel pl;
        pl.add({e});
        QCOMPARE(pl.titleAt(0), s.name);
        QCOMPARE(media::expand({e}), QStringList{e});
    }

    void rejectsForeignEntries()
    {
        DiscSource s;
        QVERIFY(!resolver::fromEntry("uve-disc://vhs?dev=D%3A", &s));
        QVERIFY(!resolver::fromEntry("uve-disc://dvd?dev=", &s));
        QVERIFY(!resolver::fromEntry("uve-disc://dvd?dev=http%3A%2F%2Fevil", &s));   // keine Adressen als Gerät
        QVERIFY(!resolver::fromEntry("dvd://", &s));
        QVERIFY(!media::isAllowedUrlScheme("uve-disc://dvd?dev=D%3A"));            // nicht über den URL-Dialog
    }

    void quotesOptionValuesByUtf8Length()
    {
        QCOMPARE(resolver::quoteOptionValue("D:"), QString("%2%D:"));
        QCOMPARE(resolver::quoteOptionValue("a,b=c"), QString("%5%a,b=c"));
        QCOMPARE(resolver::quoteOptionValue(QString::fromUtf8("Ü")), QString::fromUtf8("%2%Ü"));
    }

    void buildsLoadPlans()
    {
        const DiscCapabilities caps = fullCaps();
        DiscSource dvd{DiscKind::Dvd, QStringLiteral("D:"), {}, -1, true, false};
        auto p = resolver::loadPlan(dvd, caps);
        QCOMPARE(p.error, DiscError::None);
        QCOMPARE(p.url, QString("dvd://"));
        QCOMPARE(p.fileOptions, (QStringList{"dvd-device=%2%D:", "disc-menu=yes"}));
        QVERIFY(p.menu);

        dvd.menu = false;
        dvd.title = 2;
        p = resolver::loadPlan(dvd, caps);
        QCOMPARE(p.url, QString("dvd://2"));
        QCOMPARE(p.fileOptions, QStringList{"dvd-device=%2%D:"});

        DiscSource bd{DiscKind::Bluray, QStringLiteral("/media/bd"), {}, -1, true, false};
        p = resolver::loadPlan(bd, caps);
        QCOMPARE(p.url, QString("bd://"));
        QCOMPARE(p.fileOptions, (QStringList{"bluray-device=%9%/media/bd", "disc-menu=yes"}));
        bd.encrypted = true;
        QCOMPARE(resolver::loadPlan(bd, caps).error, DiscError::Encrypted);

        DiscSource cd{DiscKind::AudioCd, QStringLiteral("E:"), {}, 4, false, false};
        p = resolver::loadPlan(cd, caps);
        QCOMPARE(p.url, QString("uvecdda://E%3A/4"));
        QVERIFY(p.fileOptions.isEmpty());
    }

    void loadPlansDegradeWithoutFeatures()
    {
        // Build ohne Menüs: DVD spielt den Titel, keine disc-menu-Option
        DiscCapabilities noMenus = DiscCapabilities::fromProbe({{"protocols", QStringList{"dvd", "cdda"}},
                                                                {"options", QStringList{"dvd-device", "cdda-device"}}});
        DiscSource dvd{DiscKind::Dvd, QStringLiteral("/dev/sr0"), {}, -1, true, false};
        auto p = resolver::loadPlan(dvd, noMenus);
        QCOMPARE(p.error, DiscError::None);
        QVERIFY(!p.menu);
        QVERIFY(!p.fileOptions.contains("disc-menu=yes"));
        // natives cdda:// (Linux-Distributionen)
        p = resolver::loadPlan(DiscSource{DiscKind::AudioCd, QStringLiteral("/dev/sr0"), {}, -1, false, false}, noMenus);
        QCOMPARE(p.url, QString("cdda://"));
        QCOMPARE(p.fileOptions, QStringList{"cdda-device=%8%/dev/sr0"});
        // fehlende Bibliothek: sichtbarer Fehler statt Fehlversuch
        QCOMPARE(resolver::loadPlan(DiscSource{DiscKind::Bluray, QStringLiteral("x"), {}, -1, false, false}, noMenus).error, DiscError::MissingFeature);
        QCOMPARE(resolver::loadPlan(DiscSource{DiscKind::AudioCd, QStringLiteral("x"), {}, -1, false, false}, DiscCapabilities{}).error, DiscError::MissingFeature);
        QCOMPARE(resolver::loadPlan(DiscSource{DiscKind::Dvd, QString(), {}, -1, false, false}, noMenus).error, DiscError::NoMedium);
        QCOMPARE(resolver::loadPlan(DiscSource{DiscKind::Unknown, QStringLiteral("x"), {}, -1, false, false}, noMenus).error, DiscError::Incompatible);
    }

    void capabilitiesFromProbe()
    {
        const DiscCapabilities c = fullCaps();
        QVERIFY(c.supports(DiscKind::Dvd));
        QVERIFY(c.supports(DiscKind::Bluray));
        QVERIFY(c.supports(DiscKind::AudioCd));   // eigener Leser
        QVERIFY(!c.cddaNative);
        QVERIFY(c.menus(DiscKind::Dvd));
        QVERIFY(c.menus(DiscKind::Bluray));
        QVERIFY(!c.menus(DiscKind::AudioCd));
        QVERIFY(!c.bdj);
        QVERIFY(c.buildFlags.contains("libbluray=enabled"));
        QVERIFY(c.buildFlags.contains("dvdnav=enabled"));

        const DiscCapabilities none = DiscCapabilities::fromProbe({});
        for (auto k : {DiscKind::Dvd, DiscKind::Bluray, DiscKind::AudioCd, DiscKind::Unknown}) {
            QVERIFY(!none.supports(k));
            QVERIFY(!none.menus(k));
        }
        // Protokoll ohne Geräteoption ist nicht nutzbar (Quelle ließe sich nicht übergeben)
        QVERIFY(!DiscCapabilities::fromProbe({{"protocols", QStringList{"dvd"}}}).supports(DiscKind::Dvd));
        // discnav fehlt → keine Menüs, Titel weiter möglich
        auto noNav = DiscCapabilities::fromProbe({{"protocols", QStringList{"dvd"}}, {"options", QStringList{"dvd-device", "disc-menu"}}});
        QVERIFY(noNav.supports(DiscKind::Dvd));
        QVERIFY(!noNav.menus(DiscKind::Dvd));
        // BD-J nur mit Blu-ray und gefundener Laufzeit
        QVERIFY(!DiscCapabilities::fromProbe({{"bdj", true}}).bdj);
        QString reason;
        QCOMPARE(DiscCapabilities::findBdjRuntime(QDir::tempPath(), &reason), false);
        QVERIFY(!reason.isEmpty());
    }

    void cdReaderUrls()
    {
        QString dev; int t = 0;
        QVERIFY(resolver::parseCdReaderUrl(resolver::cdReaderUrl("D:", 12), &dev, &t));
        QCOMPARE(dev, QString("D:"));
        QCOMPARE(t, 12);
        QVERIFY(!resolver::parseCdReaderUrl("uvecdda://D%3A/0", &dev, &t));
        QVERIFY(!resolver::parseCdReaderUrl("uvecdda://D%3A/100", &dev, &t));
        QVERIFY(!resolver::parseCdReaderUrl("uvecdda://", &dev, &t));
        QVERIFY(!resolver::parseCdReaderUrl("cdda://D:/1", &dev, &t));
    }

    void cdTrackStreamDeliversWav()
    {
        auto device = createSyntheticCd(3, 4);
        QString err;
        const auto tracks = device->tracks(&err);
        QCOMPARE(tracks.size(), 3);
        const CdTrack t2 = tracks[1];
        auto ref = createSyntheticCd(3, 4);
        auto s = CdTrackStream::open(std::move(device), 2, &err);
        QVERIFY2(s, qPrintable(err));
        QCOMPARE(s->size(), 44 + t2.sectors * kCdSectorBytes);
        QCOMPARE(s->track().seconds(), double(t2.sectors) / 75.0);

        QByteArray all(s->size(), '\0');
        qint64 got = 0;
        while (got < all.size()) {
            const qint64 n = s->read(all.data() + got, 10007);   // krumme Blockgröße: über Cache-Grenzen
            QVERIFY(n > 0);
            got += n;
        }
        QCOMPARE(s->read(all.data(), 10), qint64(0));            // Ende
        QCOMPARE(all.left(4), QByteArray("RIFF"));
        QCOMPARE(all.mid(8, 8), QByteArray("WAVEfmt "));
        QCOMPARE(qFromLittleEndian<quint16>(all.constData() + 22), quint16(2));
        QCOMPARE(qFromLittleEndian<quint32>(all.constData() + 24), quint32(44100));
        QCOMPARE(qFromLittleEndian<quint32>(all.constData() + 40), quint32(t2.sectors * kCdSectorBytes));
        QByteArray expect(t2.sectors * kCdSectorBytes, '\0');
        QVERIFY(ref->read(t2.startLba, int(t2.sectors), expect.data()));
        QVERIFY(all.mid(44) == expect);

        // Sprung in die Mitte einer Sektorgrenze hinein
        const qint64 pos = 44 + 37 * kCdSectorBytes + 100;
        QCOMPARE(s->seek(pos), pos);
        QByteArray part(5000, '\0');
        QCOMPARE(s->read(part.data(), part.size()), qint64(part.size()));
        QVERIFY(part == all.mid(pos, part.size()));
        QCOMPARE(s->seek(-1), qint64(-1));
        QCOMPARE(s->seek(s->size() + 1), qint64(-1));
    }

    void cdTrackStreamErrors()
    {
        QString err;
        QVERIFY(!CdTrackStream::open(createSyntheticCd(2, 3), 5, &err));
        QCOMPARE(err, QString("no-such-track"));
        err.clear();
        QVERIFY(!CdTrackStream::open(std::make_unique<DataTrackCd>(), 1, &err));
        QCOMPARE(err, QString("data-track"));
        QVERIFY(CdTrackStream::open(std::make_unique<DataTrackCd>(), 2, &err));
        QVERIFY(!CdTrackStream::open(nullptr, 1, &err));

        // Medium während der Wiedergabe entfernt: Lesefehler statt Absturz oder Endlosschleife
        auto s = CdTrackStream::open(std::make_unique<FailingCd>(), 1, &err);
        QVERIFY(s);
        QByteArray buf(1 << 20, '\0');
        qint64 total = 0, r = 0;
        for (int i = 0; i < 100 && (r = s->read(buf.data(), buf.size())) > 0; ++i) total += r;
        QCOMPARE(r, qint64(-1));
        QVERIFY(total > 0);
        QVERIFY(total < s->size());
    }

    void syntheticCdOnlyWithTestSwitch()
    {
        qunsetenv("UVE_TEST_FAKE_CD");
        QString err;
        QVERIFY(!openCdDevice(syntheticCdDevice(), &err));
        qputenv("UVE_TEST_FAKE_CD", "1");
        QVERIFY(openCdDevice(syntheticCdDevice(), &err));
        qunsetenv("UVE_TEST_FAKE_CD");
        // Nur Laufwerksbuchstaben, keine beliebigen Gerätepfade
        QVERIFY(!openSystemCdDevice("\\\\.\\PhysicalDrive0", &err));
        QVERIFY(!openSystemCdDevice("C:\\Windows", &err));
    }
};

QTEST_GUILESS_MAIN(TstOpticalSources)
#include "tst_optical_sources.moc"
