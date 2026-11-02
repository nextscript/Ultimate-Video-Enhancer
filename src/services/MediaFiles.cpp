#include "MediaFiles.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QUrl>

namespace uve::media {

QStringList videoExtensions()
{
    return {"mp4", "m4v", "mkv", "mk3d", "webm", "mov", "avi", "wmv", "flv", "ts", "m2ts", "mts", "mpg", "mpeg",
            "3gp", "ogv", "vob", "divx", "rm", "rmvb", "asf", "f4v", "hevc", "264", "265"};
}
QStringList audioExtensions()
{
    return {"mp3", "m4a", "aac", "flac", "wav", "ogg", "oga", "opus", "wma", "ac3", "eac3", "dts", "mka", "aiff", "alac", "ape", "wv"};
}
QStringList subtitleExtensions() { return {"srt", "ass", "ssa", "vtt", "sub", "idx", "sup"}; }
QStringList playlistExtensions() { return {"m3u", "m3u8", "pls"}; }

QString fileDialogFilter()
{
    auto pat = [](const QStringList &e) { QStringList o; for (const auto &x : e) o << "*." + x; return o.join(' '); };
    return QStringLiteral("Medien (%1);;Video (%2);;Audio (%3);;Wiedergabelisten (%4);;Alle Dateien (*)")
        .arg(pat(videoExtensions() + audioExtensions() + playlistExtensions()), pat(videoExtensions()),
             pat(audioExtensions()), pat(playlistExtensions()));
}

static const QStringList kSchemes{"http", "https", "ftp", "tcp", "udp", "rtp", "rtmp", "rtmps", "rtmpt",
                                  "rtsp", "rtsps", "srt", "mms", "mmsh", "ytdl"};

bool isUrl(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*://"));
    return re.match(s).hasMatch();
}

bool isAllowedUrlScheme(const QString &s)
{
    const QUrl u(s);
    return u.isValid() && kSchemes.contains(u.scheme().toLower()) && !u.host().isEmpty();
}

static QString ext(const QString &p) { return QFileInfo(p).suffix().toLower(); }
bool isMediaFile(const QString &p) { return videoExtensions().contains(ext(p)) || audioExtensions().contains(ext(p)); }
bool isSubtitleFile(const QString &p) { return subtitleExtensions().contains(ext(p)); }
bool isPlaylistFile(const QString &p) { return playlistExtensions().contains(ext(p)); }

QStringList parsePlaylistFile(const QString &file)
{
    QStringList out;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    const QString text = QString::fromUtf8(f.readAll());
    const QDir base = QFileInfo(file).absoluteDir();
    if (ext(file) == "pls") {
        static const QRegularExpression re(QStringLiteral("^File\\d+=(.+)$"), QRegularExpression::CaseInsensitiveOption);
        for (const auto &line : text.split('\n')) {
            const auto m = re.match(line.trimmed());
            if (m.hasMatch()) {
                const auto e = m.captured(1).trimmed();
                out << (isUrl(e) ? e : QDir::cleanPath(base.absoluteFilePath(e)));
            }
        }
        return out;
    }
    // HLS-Manifest (kein Wiedergabelistenformat im Sinne von M3U) → als Stream zurückgeben
    if (text.contains("#EXT-X-TARGETDURATION") || text.contains("#EXT-X-STREAM-INF")) return {file};
    for (QString line : text.split('\n')) {
        line = line.trimmed();
        if (line.startsWith(QChar(0xFEFF))) line.remove(0, 1);
        if (line.isEmpty() || line.startsWith('#')) continue;
        if (line.startsWith("file://", Qt::CaseInsensitive)) line = QUrl(line).toLocalFile();
        out << (isUrl(line) ? line : QDir::cleanPath(base.absoluteFilePath(line)));
    }
    return out;
}

// Natürliche Sortierung ("Folge 2" vor "Folge 10"), unabhängig davon, ob Qt mit ICU gebaut ist
// (QCollator::setNumericMode wirkt ohne ICU – z. B. unter Linux – nicht).
static int naturalCompare(const QString &a, const QString &b, const QCollator &coll)
{
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            qsizetype si = i, sj = j;
            while (i < a.size() && a[i].isDigit()) ++i;
            while (j < b.size() && b[j].isDigit()) ++j;
            QStringView na = QStringView(a).mid(si, i - si), nb = QStringView(b).mid(sj, j - sj);
            while (na.size() > 1 && na[0] == u'0') na = na.mid(1);
            while (nb.size() > 1 && nb[0] == u'0') nb = nb.mid(1);
            if (na.size() != nb.size()) return na.size() < nb.size() ? -1 : 1;
            if (const int c = na.compare(nb)) return c < 0 ? -1 : 1;
        } else {
            qsizetype si = i, sj = j;
            while (i < a.size() && !a[i].isDigit()) ++i;
            while (j < b.size() && !b[j].isDigit()) ++j;
            if (const int c = coll.compare(QStringView(a).mid(si, i - si), QStringView(b).mid(sj, j - sj))) return c;
        }
    }
    return (a.size() - i) < (b.size() - j) ? -1 : ((a.size() - i) > (b.size() - j) ? 1 : 0);
}

QStringList expand(const QStringList &inputs)
{
    QStringList out;
    QCollator coll;
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    for (const auto &raw : inputs) {
        if (raw.isEmpty()) continue;
        QString in = raw;
        if (in.startsWith("file:", Qt::CaseInsensitive)) in = QUrl(in).toLocalFile();
        if (isUrl(in)) { if (isAllowedUrlScheme(in)) out << in; continue; }
        const QFileInfo fi(in);
        if (!fi.exists()) continue;
        if (fi.isDir()) {
            QStringList files;
            QDirIterator it(fi.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) { const auto f = it.next(); if (isMediaFile(f)) files << f; }
            std::sort(files.begin(), files.end(), [&](const QString &a, const QString &b) { return naturalCompare(a, b, coll) < 0; });
            out << files;
        } else if (isPlaylistFile(in)) {
            for (const auto &e : parsePlaylistFile(in)) {
                if (isUrl(e) || QFileInfo::exists(e)) out << e;
            }
        } else {
            out << QDir::toNativeSeparators(fi.absoluteFilePath());
        }
    }
    out.removeDuplicates();
    return out;
}

bool writeM3U(const QString &file, const QStringList &entries, const QStringList &titles)
{
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream s(&f);
    s.setEncoding(QStringConverter::Utf8);
    s << "#EXTM3U\n";
    for (int i = 0; i < entries.size(); ++i) {
        s << "#EXTINF:-1," << (i < titles.size() ? titles[i] : QFileInfo(entries[i]).fileName()) << "\n";
        s << entries[i] << "\n";
    }
    s.flush();
    return f.commit();
}

} // namespace uve::media
