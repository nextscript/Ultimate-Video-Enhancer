#include "NotifySound.h"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
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
#endif
}

NotifySound::NotifySound()
    : m_alias(QStringLiteral("kickchat_pn_%1").arg(++g_counter))
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
#endif
    m_open = false;
}

void NotifySound::setVolumePercent(int percent)
{
    m_volume = qBound(0, percent, 100);
#ifdef Q_OS_WIN
    if (m_open) mci(QStringLiteral("setaudio %1 volume to %2").arg(m_alias).arg(m_volume * 10));
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
    m_last.start();
    ++m_plays;
    return true;
#else
    return false;
#endif
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
