#include "NotifySound.h"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#else
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <dlfcn.h>
#endif

namespace {
constexpr qint64 kMinGapMs = 1000;
int g_counter = 0;

#ifdef Q_OS_WIN
bool mci(const QString &command, QString *reply = nullptr)
{
    wchar_t buffer[128] = {};
    const MCIERROR error = mciSendStringW(reinterpret_cast<LPCWSTR>(command.utf16()), buffer,
                                          DWORD(std::size(buffer)), nullptr);
    if (reply) *reply = QString::fromWCharArray(buffer);
    return error == 0;
}
#else
// Linux/macOS: libmpv, die der Player ohnehin geladen hat – eine eigene, unsichtbare Instanz nur für den Ton,
// die laufende Wiedergabe bleibt unberührt. Die Funktionen kommen aus dem Prozess (kein eigenes Linken).
struct MpvEvent { int event_id; };
struct Mpv
{
    void *(*create)() = nullptr;
    int (*initialize)(void *) = nullptr;
    int (*setOption)(void *, const char *, const char *) = nullptr;
    int (*setProperty)(void *, const char *, const char *) = nullptr;
    int (*command)(void *, const char **) = nullptr;
    MpvEvent *(*waitEvent)(void *, double) = nullptr;
    void (*destroy)(void *) = nullptr;
    bool ok() const { return create && initialize && setOption && setProperty && command && waitEvent && destroy; }
};

const Mpv &mpv()
{
    static const Mpv m = [] {
        Mpv r;
        void *self = dlopen(nullptr, RTLD_NOW);   // Hauptprogramm samt geladener Bibliotheken (libmpv des Players)
        auto sym = [self](const char *name) {
            void *p = self ? dlsym(self, name) : nullptr;
            return p ? p : dlsym(RTLD_DEFAULT, name);
        };
        r.create = reinterpret_cast<void *(*)()>(sym("mpv_create"));
        r.initialize = reinterpret_cast<int (*)(void *)>(sym("mpv_initialize"));
        r.setOption = reinterpret_cast<int (*)(void *, const char *, const char *)>(sym("mpv_set_option_string"));
        r.setProperty = reinterpret_cast<int (*)(void *, const char *, const char *)>(sym("mpv_set_property_string"));
        r.command = reinterpret_cast<int (*)(void *, const char **)>(sym("mpv_command"));
        r.waitEvent = reinterpret_cast<MpvEvent *(*)(void *, double)>(sym("mpv_wait_event"));
        r.destroy = reinterpret_cast<void (*)(void *)>(sym("mpv_terminate_destroy"));
        return r;
    }();
    return m;
}

// Ohne libmpv (z. B. Tests): Abspielprogramm des Systems, Lautstärke je Programm
bool playExternal(const QString &file, int volume)
{
#ifdef Q_OS_MACOS
    return QProcess::startDetached(QStringLiteral("/usr/bin/afplay"),
                                   {QStringLiteral("-v"), QString::number(volume / 100.0, 'f', 2), file});
#else
    const struct { const char *exe; QStringList args; } players[] = {
        {"pw-play", {QStringLiteral("--volume=%1").arg(volume / 100.0, 0, 'f', 2), file}},
        {"paplay", {QStringLiteral("--volume=%1").arg(volume * 65536 / 100), file}},
        {"ffplay", {QStringLiteral("-nodisp"), QStringLiteral("-autoexit"), QStringLiteral("-loglevel"), QStringLiteral("quiet"),
                    QStringLiteral("-volume"), QString::number(volume), file}},
        {"mpg123", {QStringLiteral("-q"), QStringLiteral("-f"), QString::number(volume * 32768 / 100), file}},
    };
    for (const auto &p : players) {
        const QString exe = QStandardPaths::findExecutable(QString::fromLatin1(p.exe));
        if (!exe.isEmpty() && QProcess::startDetached(exe, p.args)) return true;
    }
    return false;
#endif
}
#endif
}

NotifySound::NotifySound()
    : m_alias(QStringLiteral("twitchchat_pn_%1").arg(++g_counter))
{
}

NotifySound::~NotifySound()
{
    closeDevice();
}

void NotifySound::closeDevice()
{
#ifdef Q_OS_WIN
    if (m_open) mci(QStringLiteral("close %1").arg(m_alias));
#else
    if (m_mpv) mpv().destroy(m_mpv);
    m_mpv = nullptr;
#endif
    m_open = false;
}

void NotifySound::setVolumePercent(int percent)
{
    m_volume = qBound(0, percent, 100);
#ifdef Q_OS_WIN
    if (m_open) mci(QStringLiteral("setaudio %1 volume to %2").arg(m_alias).arg(m_volume * 10));
#else
    if (m_mpv) mpv().setProperty(m_mpv, "volume", QByteArray::number(m_volume).constData());
#endif
}

void NotifySound::setFile(const QString &path)
{
    const QString file = path.isEmpty() || !QFileInfo::exists(path) ? QString() : QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if (file == m_file) return;
    closeDevice();
    m_file = file;
}

bool NotifySound::play()
{
    if (m_file.isEmpty()) return false;
    if (m_last.isValid() && m_last.elapsed() < kMinGapMs) return true;
#ifdef Q_OS_WIN
    if (!m_open) {
        // mpegvideo = MP3-Decoder von Windows; Anführungszeichen erlauben Leerzeichen im Pfad.
        m_open = mci(QStringLiteral("open \"%1\" type mpegvideo alias %2").arg(m_file, m_alias));
        if (!m_open) return false;
        // MCI-Lautstärke 0–1000 gilt nur für dieses Gerät, nicht für Windows insgesamt.
        mci(QStringLiteral("setaudio %1 volume to %2").arg(m_alias).arg(m_volume * 10));
    }
    if (!mci(QStringLiteral("play %1 from 0").arg(m_alias))) {
        closeDevice();                       // Gerät weg (z. B. Kopfhörer gezogen): beim nächsten Mal neu öffnen
        return false;
    }
#else
    const Mpv &m = mpv();
    if (m.ok() && !m_mpv) {
        m_mpv = m.create();
        if (m_mpv) {
            // nur Ton, keine Konfiguration oder Tasten des Benutzers, eigene Lautstärke (0–100)
            static const char *const options[][2] = {{"config", "no"}, {"terminal", "no"}, {"video", "no"}, {"vo", "null"},
                                                     {"input-default-bindings", "no"}, {"idle", "yes"}, {"ytdl", "no"},
                                                     {"audio-display", "no"}, {"msg-level", "all=no"}};
            for (const auto &o : options)
                m.setOption(m_mpv, o[0], o[1]);
            m.setOption(m_mpv, "volume", QByteArray::number(m_volume).constData());
            if (m.initialize(m_mpv) < 0) { m.destroy(m_mpv); m_mpv = nullptr; }
        }
    }
    if (m_mpv) {
        while (m.waitEvent(m_mpv, 0)->event_id != 0) {}   // Ereignisse verwerfen, die Warteschlange bleibt leer
        const QByteArray file = m_file.toUtf8();
        const char *cmd[] = {"loadfile", file.constData(), "replace", nullptr};
        m_open = m.command(m_mpv, cmd) >= 0;
    } else {
        m_open = false;
    }
    if (!m_open && !playExternal(m_file, m_volume)) return false;
#endif
    m_last.start();
    ++m_plays;
    return true;
}

int NotifySound::deviceVolume() const
{
#ifdef Q_OS_WIN
    QString reply;
    if (m_open && mci(QStringLiteral("status %1 volume").arg(m_alias), &reply)) return reply.toInt();
#endif
    return -1;
}

QString NotifySound::mode() const
{
#ifdef Q_OS_WIN
    QString reply;
    if (m_open && mci(QStringLiteral("status %1 mode").arg(m_alias), &reply)) return reply;
#endif
    return QString();
}
