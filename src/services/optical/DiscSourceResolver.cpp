#include "DiscSourceResolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <functional>

namespace uve::optical::resolver {

namespace {

constexpr qint64 kSector = 2048;
constexpr qint64 kUdfScanBytes = 32 * 1024 * 1024;   // reines UDF: Verzeichnisdaten liegen am Anfang des Images

using Reader = std::function<QByteArray(qint64 offset, qint64 length)>;

// Eintrag im Ordner (Groß-/Kleinschreibung egal, wie auf Discs üblich)
QString childPath(const QString &dir, const QString &name)
{
    const QDir d(dir);
    if (!d.exists()) return {};
    for (const QString &e : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        if (e.compare(name, Qt::CaseInsensitive) == 0) return d.filePath(e);
    return {};
}

struct IsoEntry { QString name; qint64 extent = 0; qint64 size = 0; bool dir = false; };

quint32 le32(const QByteArray &b, int at)
{
    if (at + 4 > b.size()) return 0;
    const auto *p = reinterpret_cast<const uchar *>(b.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

// Verzeichnis eines ISO-9660-Images (ohne Joliet/Rock Ridge: Disc-Strukturen nutzen 8.3-Namen in Großbuchstaben)
QList<IsoEntry> isoDirectory(const Reader &read, qint64 block, qint64 extent, qint64 size)
{
    QList<IsoEntry> out;
    size = qMin<qint64>(size, 1024 * 1024);   // Schutz vor beschädigten Längenangaben
    const QByteArray data = read(extent * block, size);
    int pos = 0;
    while (pos < data.size()) {
        const int len = uchar(data[pos]);
        if (len == 0) {   // Rest des Sektors ist leer → nächster Sektor
            pos = int((pos / block + 1) * block);
            continue;
        }
        if (len < 34 || pos + len > data.size()) break;
        const int nameLen = uchar(data[pos + 32]);
        if (33 + nameLen <= len) {
            QString name = QString::fromLatin1(data.mid(pos + 33, nameLen));
            const qsizetype semicolon = name.indexOf(QLatin1Char(';'));
            if (semicolon >= 0) name.truncate(semicolon);
            if (name.endsWith(QLatin1Char('.'))) name.chop(1);
            if (nameLen > 1 || (uchar(data[pos + 33]) > 1))   // "\0" und "\1" sind . und ..
                out << IsoEntry{name, le32(data, pos + 2), le32(data, pos + 10), bool(uchar(data[pos + 25]) & 0x02)};
        }
        pos += len;
    }
    return out;
}

const IsoEntry *findEntry(const QList<IsoEntry> &list, const char *name, bool dir)
{
    for (const auto &e : list)
        if (e.dir == dir && e.name.compare(QLatin1String(name), Qt::CaseInsensitive) == 0) return &e;
    return nullptr;
}

// Name im UDF-Kopf (OSTA CS0: Kompressions-ID 8 = 8-Bit-Zeichen, 16 = UTF-16BE)
bool containsUdfName(const QByteArray &data, const char *name)
{
    QByteArray narrow(1, char(8));
    narrow += name;
    if (data.contains(narrow)) return true;
    QByteArray wide(1, char(16));
    for (const char *c = name; *c; ++c) { wide += char(0); wide += *c; }
    return data.contains(wide);
}

Detection detectWithReader(const Reader &read, qint64 fileSize)
{
    Detection d;
    d.image = true;
    if (fileSize < 18 * kSector) { d.error = DiscError::InvalidImage; return d; }

    // ISO 9660: Primary Volume Descriptor ab Sektor 16
    for (int s = 16; s < 32; ++s) {
        const QByteArray vd = read(s * kSector, kSector);
        if (vd.size() < 190 || vd.mid(1, 5) != "CD001") break;
        const int type = uchar(vd[0]);
        if (type == 255) break;          // Abschluss der Deskriptoren
        if (type != 1) continue;         // nur der primäre Deskriptor
        qint64 block = uchar(vd[128]) | (uchar(vd[129]) << 8);
        if (block != 512 && block != 1024 && block != 2048) block = kSector;
        const QByteArray rootRec = vd.mid(156, 34);
        const auto root = isoDirectory(read, block, le32(rootRec, 2), le32(rootRec, 10));
        d.encrypted = findEntry(root, "AACS", true) != nullptr;
        if (const IsoEntry *v = findEntry(root, "VIDEO_TS", true)) {
            if (findEntry(isoDirectory(read, block, v->extent, v->size), "VIDEO_TS.IFO", false)) { d.kind = DiscKind::Dvd; return d; }
        }
        if (const IsoEntry *b = findEntry(root, "BDMV", true)) {
            const auto bdmv = isoDirectory(read, block, b->extent, b->size);
            if (findEntry(bdmv, "INDEX.BDMV", false)) { d.kind = DiscKind::Bluray; d.bdj = findEntry(bdmv, "JAR", true) != nullptr; return d; }
        }
        break;   // ISO-Verzeichnis ohne Disc-Struktur: unten noch die UDF-Brücke prüfen
    }

    // UDF (DVD-Video als UDF-Bridge, Blu-ray als reines UDF 2.5): Volume Recognition Sequence ab Sektor 16
    bool udf = false;
    for (int s = 16; s < 32 && !udf; ++s) {
        const QByteArray vrs = read(s * kSector, 8);
        if (vrs.size() < 6) break;
        const QByteArray id = vrs.mid(1, 5);
        udf = id == "NSR02" || id == "NSR03";
    }
    if (!udf) { d.error = DiscError::InvalidImage; return d; }
    const QByteArray head = read(0, qMin(fileSize, kUdfScanBytes));
    d.encrypted = d.encrypted || containsUdfName(head, "AACS");
    if (containsUdfName(head, "VIDEO_TS") && containsUdfName(head, "VIDEO_TS.IFO")) d.kind = DiscKind::Dvd;
    else if (containsUdfName(head, "BDMV") && (containsUdfName(head, "index.bdmv") || containsUdfName(head, "INDEX.BDMV")))
        { d.kind = DiscKind::Bluray; d.bdj = containsUdfName(head, "JAR"); }
    else d.error = DiscError::Incompatible;
    return d;
}

QString encodeValue(const QString &v) { return QString::fromLatin1(QUrl::toPercentEncoding(v)); }
QString decodeValue(const QString &v) { return QUrl::fromPercentEncoding(v.toLatin1()); }

} // namespace

QString canonicalPath(const QString &path)
{
    QString p = path.trimmed();
    if (p.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) p = QUrl(p).toLocalFile();
    // Nur lokale Pfade: keine Schemata (http://, dvd:// …) über diesen Weg
    if (p.isEmpty() || (p.contains(QLatin1String("://")) && !p.startsWith(QLatin1String("//")))) return {};
    const QFileInfo fi(p);
    if (!fi.exists()) return {};
    const QString canonical = fi.canonicalFilePath();
    return QDir::toNativeSeparators(canonical.isEmpty() ? fi.absoluteFilePath() : canonical);
}

Detection detectFolder(const QString &path)
{
    Detection d;
    const QString dir = canonicalPath(path);
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) { d.error = DiscError::Incompatible; return d; }
    if (!QFileInfo(dir).isReadable()) { d.error = DiscError::NoAccess; return d; }
    const QString name = QFileInfo(dir).fileName();
    // Der Ordner VIDEO_TS bzw. BDMV selbst gewählt → die Disc-Wurzel ist der Ordner darüber
    QString root = dir;
    if (name.compare(QLatin1String("VIDEO_TS"), Qt::CaseInsensitive) == 0 || name.compare(QLatin1String("BDMV"), Qt::CaseInsensitive) == 0)
        root = QDir::toNativeSeparators(QFileInfo(dir).absolutePath());
    d.device = root;
    d.encrypted = !childPath(root, QStringLiteral("AACS")).isEmpty();
    const QString videoTs = childPath(root, QStringLiteral("VIDEO_TS"));
    if (!videoTs.isEmpty() && !childPath(videoTs, QStringLiteral("VIDEO_TS.IFO")).isEmpty()) { d.kind = DiscKind::Dvd; return d; }
    const QString bdmv = childPath(root, QStringLiteral("BDMV"));
    if (!bdmv.isEmpty() && !childPath(bdmv, QStringLiteral("index.bdmv")).isEmpty()) {
        d.kind = DiscKind::Bluray;
        const QString jar = childPath(bdmv, QStringLiteral("JAR"));
        d.bdj = !jar.isEmpty() && !QDir(jar).isEmpty();
        return d;
    }
    d.error = DiscError::Incompatible;
    return d;
}

Detection detectImageData(const QByteArray &head, qint64 fileSize)
{
    return detectWithReader([&head](qint64 off, qint64 len) { return off >= head.size() ? QByteArray() : head.mid(off, len); }, fileSize);
}

Detection detectImage(const QString &file)
{
    Detection d;
    const QString path = canonicalPath(file);
    if (path.isEmpty() || !QFileInfo(path).isFile()) { d.error = DiscError::InvalidImage; d.image = true; return d; }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { d.error = DiscError::NoAccess; d.image = true; return d; }
    d = detectWithReader([&f](qint64 off, qint64 len) { return f.seek(off) ? f.read(len) : QByteArray(); }, f.size());
    d.device = path;
    return d;
}

Detection detect(const QString &path)
{
    const QString p = canonicalPath(path);
    if (p.isEmpty()) { Detection d; d.error = DiscError::Incompatible; return d; }
    return QFileInfo(p).isDir() ? detectFolder(p) : detectImage(p);
}

QString toEntry(const DiscSource &s)
{
    QString e = QStringLiteral("uve-disc://") + kindId(s.kind) + QStringLiteral("?dev=") + encodeValue(s.device);
    if (s.title >= 0) e += QStringLiteral("&t=") + QString::number(s.title);
    if (s.menu) e += QStringLiteral("&menu=1");
    if (!s.name.isEmpty()) e += QStringLiteral("&name=") + encodeValue(s.name);
    return e;
}

bool isEntry(const QString &entry) { return entry.startsWith(QLatin1String("uve-disc://")); }

bool fromEntry(const QString &entry, DiscSource *out)
{
    if (!isEntry(entry)) return false;
    const QString rest = entry.mid(11);
    const qsizetype q = rest.indexOf(QLatin1Char('?'));
    DiscSource s;
    s.kind = kindFromId(q < 0 ? rest : rest.left(q));
    if (s.kind == DiscKind::Unknown) return false;
    if (q >= 0) {
        for (const QString &pair : rest.mid(q + 1).split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
            const QString key = pair.section(QLatin1Char('='), 0, 0);
            const QString value = decodeValue(pair.section(QLatin1Char('='), 1));
            if (key == QLatin1String("dev")) s.device = value;
            else if (key == QLatin1String("t")) { bool ok = false; const int t = value.toInt(&ok); s.title = ok && t >= 0 ? t : -1; }
            else if (key == QLatin1String("menu")) s.menu = value == QLatin1String("1");
            else if (key == QLatin1String("name")) s.name = value;
        }
    }
    if (s.device.isEmpty() || s.device.contains(QLatin1String("://"))) return false;   // nur Betriebssystempfade
    if (out) *out = s;
    return true;
}

QString quoteOptionValue(const QString &value)
{
    return QStringLiteral("%%1%").arg(value.toUtf8().size()) + value;
}

QString cdReaderUrl(const QString &device, int track)
{
    return QStringLiteral("uvecdda://") + encodeValue(device) + QLatin1Char('/') + QString::number(qMax(1, track));
}

bool parseCdReaderUrl(const QString &url, QString *device, int *track)
{
    if (!url.startsWith(QLatin1String("uvecdda://"))) return false;
    const QString rest = url.mid(10);
    const qsizetype slash = rest.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0) return false;
    bool ok = false;
    const int t = rest.mid(slash + 1).toInt(&ok);
    if (!ok || t < 1 || t > 99) return false;
    if (device) *device = decodeValue(rest.left(slash));
    if (track) *track = t;
    return true;
}

LoadPlan loadPlan(const DiscSource &s, const DiscCapabilities &caps)
{
    LoadPlan p;
    if (s.device.isEmpty()) { p.error = DiscError::NoMedium; return p; }
    switch (s.kind) {
    case DiscKind::Dvd:
    case DiscKind::Bluray: {
        if (!caps.supports(s.kind)) { p.error = DiscError::MissingFeature; return p; }
        const bool dvd = s.kind == DiscKind::Dvd;
        if (!dvd && s.encrypted) { p.error = DiscError::Encrypted; return p; }
        p.url = (dvd ? QStringLiteral("dvd://") : QStringLiteral("bd://")) + (s.title >= 0 ? QString::number(s.title) : QString());
        p.fileOptions << (dvd ? QStringLiteral("dvd-device=") : QStringLiteral("bluray-device=")) + quoteOptionValue(s.device);
        // Menüstart nur, wenn der Build Menüs und Navigation kann; sonst Titelwiedergabe (Hinweis zeigt der Dienst)
        p.menu = s.menu && s.title < 0 && caps.menus(s.kind);
        if (p.menu) p.fileOptions << QStringLiteral("disc-menu=yes");
        return p;
    }
    case DiscKind::AudioCd:
        if (caps.cddaReader) { p.url = cdReaderUrl(s.device, s.title > 0 ? s.title : 1); return p; }
        if (caps.cddaNative && caps.cddaDevice) {
            p.url = QStringLiteral("cdda://");
            p.fileOptions << QStringLiteral("cdda-device=") + quoteOptionValue(s.device);
            return p;
        }
        p.error = DiscError::MissingFeature;
        return p;
    default:
        p.error = DiscError::Incompatible;
        return p;
    }
}

} // namespace uve::optical::resolver
