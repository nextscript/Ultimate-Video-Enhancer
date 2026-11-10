#include "OpticalDriveEnumerator.h"
#include "CdAudioStream.h"
#include "DiscSourceResolver.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <linux/cdrom.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace uve::optical {

QVariantMap DriveInfo::toMap() const
{
    return {{"device", device}, {"name", name}, {"label", label}, {"kind", kindId(kind)}, {"error", int(error)},
            {"encrypted", encrypted}, {"audioTracks", audioTracks}};
}

namespace {

// Disc-Struktur im eingebundenen Dateisystem; sonst Audio-CD über den eigenen Leser (falls vorhanden)
void classifyMounted(DriveInfo &d, const QString &root, const QString &cdDevice)
{
    const auto det = resolver::detectFolder(root);
    if (det.kind != DiscKind::Unknown) { d.kind = det.kind; d.encrypted = det.encrypted; return; }
    if (!cdDevice.isEmpty() && systemCdReaderAvailable()) {
        QString err;
        if (auto dev = openSystemCdDevice(cdDevice, &err)) {
            for (const auto &t : dev->tracks(&err)) if (t.audio) ++d.audioTracks;
            if (d.audioTracks > 0) { d.kind = DiscKind::AudioCd; return; }
        } else if (err == QLatin1String("access")) {
            d.error = DiscError::NoAccess;
            return;
        }
    }
    d.error = DiscError::Incompatible;
}

} // namespace

QList<DriveInfo> enumerateOpticalDrives()
{
    QList<DriveInfo> out;
#ifdef Q_OS_WIN
    // Keine Systemmeldung „Legen Sie einen Datenträger ein“ bei leeren Laufwerken
    const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        const QString letter = QString(QChar(u'A' + i)) + QLatin1Char(':');
        const std::wstring root = (letter + QLatin1Char('\\')).toStdWString();
        if (GetDriveTypeW(root.c_str()) != DRIVE_CDROM) continue;
        DriveInfo d;
        d.device = letter;
        d.name = letter;
        wchar_t label[MAX_PATH + 1] = {};
        if (!GetVolumeInformationW(root.c_str(), label, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0)) {
            const DWORD e = GetLastError();
            // Audio-CDs haben unter Windows ein CDFS-Volume; ohne Volume ist meist kein Medium eingelegt
            d.error = (e == ERROR_NOT_READY || e == ERROR_UNRECOGNIZED_MEDIA || e == ERROR_NO_MEDIA_IN_DRIVE)
                          ? DiscError::NoMedium : DiscError::NoAccess;
        } else {
            d.label = QString::fromWCharArray(label);
            classifyMounted(d, letter + QLatin1Char('\\'), letter);
        }
        out << d;
    }
    SetErrorMode(oldMode);
#elif defined(Q_OS_LINUX)
    const auto volumes = QStorageInfo::mountedVolumes();
    for (const QString &node : QDir(QStringLiteral("/dev")).entryList({QStringLiteral("sr*")}, QDir::System | QDir::Files)) {
        DriveInfo d;
        d.name = QStringLiteral("/dev/") + node;
        d.device = d.name;
        const int fd = ::open(d.name.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) { d.error = errno == EACCES ? DiscError::NoAccess : DiscError::NoMedium; out << d; continue; }
        const int drive = ::ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);
        const int disc = drive == CDS_DISC_OK ? ::ioctl(fd, CDROM_DISC_STATUS, 0) : -1;
        ::close(fd);
        if (drive != CDS_DISC_OK) { d.error = DiscError::NoMedium; out << d; continue; }
        QString mount;
        for (const auto &v : volumes) if (QString::fromLocal8Bit(v.device()) == d.name) { mount = v.rootPath(); d.label = v.name(); }
        if (!mount.isEmpty()) {
            const auto det = resolver::detectFolder(mount);
            if (det.kind != DiscKind::Unknown) { d.kind = det.kind; d.encrypted = det.encrypted; out << d; continue; }
        }
        if (disc == CDS_AUDIO || disc == CDS_MIXED) d.kind = DiscKind::AudioCd;
        else d.error = DiscError::Incompatible;
        out << d;
    }
#elif defined(Q_OS_MACOS)
    // Disk Arbitration bindet optische Medien automatisch ein; Audio-CDs erscheinen als cddafs-Volume
    for (const auto &v : QStorageInfo::mountedVolumes()) {
        const QByteArray fs = v.fileSystemType().toLower();
        if (fs != "cd9660" && fs != "udf" && fs != "cddafs") continue;
        DriveInfo d;
        d.device = v.rootPath();
        d.name = QString::fromLocal8Bit(v.device());
        d.label = v.name();
        if (fs == "cddafs") {
            d.kind = DiscKind::AudioCd;
            d.audioTracks = int(QDir(v.rootPath()).entryList({QStringLiteral("*.aiff")}, QDir::Files).size());
            d.device = QString::fromLocal8Bit(v.device());
        } else {
            classifyMounted(d, v.rootPath(), QString());
        }
        out << d;
    }
#endif
    if (qEnvironmentVariableIsSet("UVE_TEST_FAKE_CD")) {
        // Selbsttest ohne Hardware: synthetische Audio-CD (deutlich als Test gekennzeichnet)
        DriveInfo d;
        d.device = syntheticCdDevice();
        d.name = QStringLiteral("TEST");
        d.label = QStringLiteral("UVE Test-CD (synthetisch)");
        d.kind = DiscKind::AudioCd;
        d.audioTracks = 3;
        out << d;
    }
    return out;
}

} // namespace uve::optical
