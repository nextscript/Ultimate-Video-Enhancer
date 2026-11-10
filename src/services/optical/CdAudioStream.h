#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>
#include <memory>

struct mpv_handle;

namespace uve::optical {

// Audio-CD (CD-DA) über die libmpv-Stream-Schnittstelle (mpv_stream_cb_add_ro, Protokoll "uvecdda://"):
// Für libmpv-Builds ohne cdda:// (z. B. die Windows-Builds von shinchiro) liest UVE die Spuren selbst
// (Windows: IOCTL_CDROM_READ_TOC / IOCTL_CDROM_RAW_READ, ohne Administratorrechte) und reicht jede Spur als
// WAV-Datenstrom an libmpv weiter. Dekodiert und ausgegeben wird weiter ausschließlich von libmpv.

constexpr int kCdSectorBytes = 2352;     // 1/75 s Stereo, 16 Bit, 44,1 kHz
constexpr int kCdSectorsPerSecond = 75;

struct CdTrack
{
    int number = 0;
    qint64 startLba = 0;
    qint64 sectors = 0;
    bool audio = true;
    double seconds() const { return double(sectors) / kCdSectorsPerSecond; }
};

// Zugriff auf ein Laufwerk (oder einen synthetischen Ersatz in Tests)
class CdAudioDevice
{
public:
    virtual ~CdAudioDevice() = default;
    virtual QList<CdTrack> tracks(QString *error) = 0;
    // count Sektoren ab lba nach buf (count × 2352 Bytes); false bei Lesefehler/entferntem Medium
    virtual bool read(qint64 lba, int count, char *buf) = 0;
};

using CdDeviceFactory = std::function<std::unique_ptr<CdAudioDevice>(const QString &device)>;

// Laufwerk des Betriebssystems öffnen (Windows: "D:" / "D:\"); nullptr, wenn nicht möglich oder nicht unterstützt
std::unique_ptr<CdAudioDevice> openSystemCdDevice(const QString &device, QString *error = nullptr);
bool systemCdReaderAvailable();
// Synthetische Test-CD (Sinustöne, 3 Spuren) – nur für Tests und Selbsttests (Gerät "uve-test-cd")
std::unique_ptr<CdAudioDevice> createSyntheticCd(int tracks = 3, double secondsPerTrack = 8.0);
QString syntheticCdDevice();
// Standard-Fabrik: Laufwerk des Systems; die synthetische Test-CD nur, wenn UVE_TEST_FAKE_CD gesetzt ist
std::unique_ptr<CdAudioDevice> openCdDevice(const QString &device, QString *error = nullptr);

// Eine Spur als WAV-Datenstrom (44-Byte-Kopf + PCM)
class CdTrackStream
{
public:
    static std::unique_ptr<CdTrackStream> open(std::unique_ptr<CdAudioDevice> device, int track, QString *error);
    static QByteArray wavHeader(qint64 dataBytes);

    qint64 size() const { return m_header.size() + m_dataBytes; }
    qint64 read(char *buf, qint64 n);     // 0 = Ende, -1 = Lesefehler
    qint64 seek(qint64 offset);           // neue Position oder -1
    const CdTrack &track() const { return m_track; }

private:
    std::unique_ptr<CdAudioDevice> m_device;
    CdTrack m_track;
    QByteArray m_header;
    qint64 m_dataBytes = 0;
    qint64 m_pos = 0;
    qint64 m_cacheLba = -1;
    int m_cacheSectors = 0;
    QByteArray m_cache;
};

// Protokoll "uvecdda" an einem mpv-Handle anmelden. factory muss so lange leben wie das Handle
// (wird aus mpv-Threads aufgerufen und darf keinen Zustand teilen).
bool registerCdAudioProtocol(mpv_handle *mpv, const CdDeviceFactory *factory);

} // namespace uve::optical
