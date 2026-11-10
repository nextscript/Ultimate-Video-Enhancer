#include "CdAudioStream.h"
#include "DiscSourceResolver.h"

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include <QtEndian>
#include <cmath>
#include <cstring>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <ntddcdrm.h>
#endif

namespace uve::optical {

namespace {

constexpr int kReadSectors = 24;   // 24 × 2352 = 56 KiB je Lesebefehl (unter der üblichen 64-KiB-Grenze)

// ---------------------------------------------------------------- synthetische Test-CD
class SyntheticCd : public CdAudioDevice
{
public:
    SyntheticCd(int count, double seconds)
    {
        qint64 lba = 0;
        const qint64 sectors = qint64(seconds * kCdSectorsPerSecond);
        for (int i = 1; i <= count; ++i) {
            m_tracks << CdTrack{i, lba, sectors + i * kCdSectorsPerSecond, true};   // Spuren verschieden lang
            lba += m_tracks.last().sectors + 2 * kCdSectorsPerSecond;               // Pause zwischen Spuren
        }
    }
    QList<CdTrack> tracks(QString *) override { return m_tracks; }
    bool read(qint64 lba, int count, char *buf) override
    {
        // Sinuston je Spur (220 Hz × Spurnummer), Stereo, 16 Bit
        for (int s = 0; s < count; ++s) {
            const qint64 sector = lba + s;
            int number = 0;
            for (const auto &t : m_tracks) if (sector >= t.startLba && sector < t.startLba + t.sectors) number = t.number;
            auto *out = reinterpret_cast<qint16 *>(buf + qint64(s) * kCdSectorBytes);
            for (int i = 0; i < kCdSectorBytes / 4; ++i) {
                const double t = double(sector * (kCdSectorBytes / 4) + i) / 44100.0;
                const qint16 v = number ? qint16(8000 * std::sin(6.283185307179586 * 220.0 * number * t)) : 0;
                out[2 * i] = qToLittleEndian(v);
                out[2 * i + 1] = qToLittleEndian(v);
            }
        }
        return true;
    }
private:
    QList<CdTrack> m_tracks;
};

#ifdef Q_OS_WIN
// ---------------------------------------------------------------- Windows: IOCTL_CDROM_*
class WindowsCd : public CdAudioDevice
{
public:
    explicit WindowsCd(HANDLE h) : m_handle(h) {}
    ~WindowsCd() override { CloseHandle(m_handle); }

    QList<CdTrack> tracks(QString *error) override
    {
        CDROM_TOC toc{};
        DWORD got = 0;
        if (!DeviceIoControl(m_handle, IOCTL_CDROM_READ_TOC, nullptr, 0, &toc, sizeof(toc), &got, nullptr)) {
            if (error) *error = QStringLiteral("toc:%1").arg(GetLastError());
            return {};
        }
        QList<CdTrack> out;
        const int n = toc.LastTrack - toc.FirstTrack + 1;
        if (n <= 0 || n > 99) return out;
        auto lbaOf = [](const TRACK_DATA &t) { return (qint64(t.Address[1]) * 60 + t.Address[2]) * 75 + t.Address[3] - 150; };
        for (int i = 0; i < n; ++i) {
            const TRACK_DATA &t = toc.TrackData[i];
            const TRACK_DATA &next = toc.TrackData[i + 1];   // nach der letzten Spur: Lead-out (0xAA)
            CdTrack track{t.TrackNumber, lbaOf(t), lbaOf(next) - lbaOf(t), (t.Control & 0x04) == 0};
            if (track.sectors > 0) out << track;
        }
        return out;
    }

    bool read(qint64 lba, int count, char *buf) override
    {
        if (rawRead(lba, count, buf)) return true;
        // Manche Laufwerke lesen nur kleinere Blöcke: einzeln wiederholen
        for (int i = 0; i < count; ++i)
            if (!rawRead(lba + i, 1, buf + qint64(i) * kCdSectorBytes)) return false;
        return true;
    }

private:
    bool rawRead(qint64 lba, int count, char *buf)
    {
        RAW_READ_INFO info{};
        info.DiskOffset.QuadPart = lba * 2048;
        info.SectorCount = DWORD(count);
        info.TrackMode = CDDA;
        DWORD got = 0;
        return DeviceIoControl(m_handle, IOCTL_CDROM_RAW_READ, &info, sizeof(info), buf, DWORD(count * kCdSectorBytes), &got, nullptr)
               && got == DWORD(count * kCdSectorBytes);
    }
    HANDLE m_handle;
};
#endif

// ---------------------------------------------------------------- libmpv-Stream-Rückrufe
int64_t cbRead(void *cookie, char *buf, uint64_t n) { return static_cast<CdTrackStream *>(cookie)->read(buf, qint64(n)); }
int64_t cbSeek(void *cookie, int64_t offset)
{
    const qint64 r = static_cast<CdTrackStream *>(cookie)->seek(offset);
    return r < 0 ? MPV_ERROR_GENERIC : r;
}
int64_t cbSize(void *cookie) { return static_cast<CdTrackStream *>(cookie)->size(); }
void cbClose(void *cookie) { delete static_cast<CdTrackStream *>(cookie); }

int cbOpen(void *userData, char *uri, mpv_stream_cb_info *info)
{
    const auto *factory = static_cast<const CdDeviceFactory *>(userData);
    QString device;
    int track = 0;
    if (!factory || !resolver::parseCdReaderUrl(QString::fromUtf8(uri), &device, &track)) return MPV_ERROR_LOADING_FAILED;
    auto dev = (*factory)(device);
    if (!dev) return MPV_ERROR_LOADING_FAILED;
    QString error;
    auto stream = CdTrackStream::open(std::move(dev), track, &error);
    if (!stream) return MPV_ERROR_LOADING_FAILED;
    info->cookie = stream.release();
    info->read_fn = cbRead;
    info->seek_fn = cbSeek;
    info->size_fn = cbSize;
    info->close_fn = cbClose;
    return 0;
}

} // namespace

QString syntheticCdDevice() { return QStringLiteral("uve-test-cd"); }

std::unique_ptr<CdAudioDevice> createSyntheticCd(int tracks, double secondsPerTrack)
{
    return std::make_unique<SyntheticCd>(qBound(1, tracks, 99), qMax(1.0, secondsPerTrack));
}

bool systemCdReaderAvailable()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

std::unique_ptr<CdAudioDevice> openSystemCdDevice(const QString &device, QString *error)
{
#ifdef Q_OS_WIN
    // Nur Laufwerksbuchstaben ("D:", "D:\") – keine beliebigen Gerätepfade
    const QString d = device.trimmed();
    if (d.size() < 2 || !d[0].isLetter() || d[1] != QLatin1Char(':') || d.size() > 3) { if (error) *error = QStringLiteral("device"); return {}; }
    const std::wstring path = L"\\\\.\\" + d.left(2).toUpper().toStdWString();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (error) *error = GetLastError() == ERROR_ACCESS_DENIED ? QStringLiteral("access") : QStringLiteral("open");
        return {};
    }
    return std::make_unique<WindowsCd>(h);
#else
    Q_UNUSED(device);
    if (error) *error = QStringLiteral("unsupported");
    return {};
#endif
}

std::unique_ptr<CdAudioDevice> openCdDevice(const QString &device, QString *error)
{
    if (device == syntheticCdDevice()) {
        if (qEnvironmentVariableIsSet("UVE_TEST_FAKE_CD")) return createSyntheticCd();
        if (error) *error = QStringLiteral("device");
        return {};
    }
    return openSystemCdDevice(device, error);
}

QByteArray CdTrackStream::wavHeader(qint64 dataBytes)
{
    const quint32 data = quint32(qMin<qint64>(dataBytes, 0xFFFFFFFFLL - 36));
    QByteArray h(44, '\0');
    auto put32 = [&h](int at, quint32 v) { qToLittleEndian(v, h.data() + at); };
    auto put16 = [&h](int at, quint16 v) { qToLittleEndian(v, h.data() + at); };
    std::memcpy(h.data(), "RIFF", 4);
    put32(4, 36 + data);
    std::memcpy(h.data() + 8, "WAVEfmt ", 8);
    put32(16, 16);                 // Länge des fmt-Blocks
    put16(20, 1);                  // PCM
    put16(22, 2);                  // Stereo
    put32(24, 44100);
    put32(28, 44100 * 4);          // Bytes je Sekunde
    put16(32, 4);                  // Blockgröße
    put16(34, 16);                 // Bits je Abtastwert
    std::memcpy(h.data() + 36, "data", 4);
    put32(40, data);
    return h;
}

std::unique_ptr<CdTrackStream> CdTrackStream::open(std::unique_ptr<CdAudioDevice> device, int track, QString *error)
{
    if (!device) { if (error) *error = QStringLiteral("device"); return {}; }
    const QList<CdTrack> tracks = device->tracks(error);
    for (const auto &t : tracks) {
        if (t.number != track) continue;
        if (!t.audio) { if (error) *error = QStringLiteral("data-track"); return {}; }
        auto s = std::unique_ptr<CdTrackStream>(new CdTrackStream);
        s->m_device = std::move(device);
        s->m_track = t;
        s->m_dataBytes = t.sectors * kCdSectorBytes;
        s->m_header = wavHeader(s->m_dataBytes);
        return s;
    }
    if (error && error->isEmpty()) *error = tracks.isEmpty() ? QStringLiteral("no-tracks") : QStringLiteral("no-such-track");
    return {};
}

qint64 CdTrackStream::seek(qint64 offset)
{
    if (offset < 0 || offset > size()) return -1;
    m_pos = offset;
    return m_pos;
}

qint64 CdTrackStream::read(char *buf, qint64 n)
{
    qint64 done = 0;
    while (done < n && m_pos < size()) {
        if (m_pos < m_header.size()) {
            const qint64 k = qMin(n - done, qint64(m_header.size()) - m_pos);
            std::memcpy(buf + done, m_header.constData() + m_pos, size_t(k));
            done += k; m_pos += k;
            continue;
        }
        const qint64 dataPos = m_pos - m_header.size();
        const qint64 sector = dataPos / kCdSectorBytes;
        if (m_cacheLba < 0 || sector < m_cacheLba || sector >= m_cacheLba + m_cacheSectors) {
            const int count = int(qMin<qint64>(kReadSectors, m_track.sectors - sector));
            m_cache.resize(qint64(count) * kCdSectorBytes);
            if (!m_device->read(m_track.startLba + sector, count, m_cache.data())) {
                m_cacheLba = -1;
                return done > 0 ? done : -1;   // Medium entfernt oder unlesbar
            }
            m_cacheLba = sector;
            m_cacheSectors = count;
        }
        const qint64 inCache = dataPos - m_cacheLba * kCdSectorBytes;
        const qint64 k = qMin(n - done, qint64(m_cache.size()) - inCache);
        std::memcpy(buf + done, m_cache.constData() + inCache, size_t(k));
        done += k; m_pos += k;
    }
    return done;
}

bool registerCdAudioProtocol(mpv_handle *mpv, const CdDeviceFactory *factory)
{
    if (!mpv || !factory) return false;
    return mpv_stream_cb_add_ro(mpv, "uvecdda", const_cast<CdDeviceFactory *>(factory), cbOpen) >= 0;
}

} // namespace uve::optical
