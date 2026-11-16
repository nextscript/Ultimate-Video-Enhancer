// C++/WinRT-Header vor den Qt-Headern einbinden (Qt-Makros wie "signals"/"emit" stören sie sonst)
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <systemmediatransportcontrolsinterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#endif

#include "MediaKeys.h"

#include <QMetaObject>
#include <QPointer>

#ifdef _WIN32
using namespace winrt::Windows::Media;

struct MediaKeys::Impl
{
    SystemMediaTransportControls smtc{nullptr};
    winrt::event_token token{};
};

MediaKeys::MediaKeys(QObject *parent) : QObject(parent) {}

MediaKeys::~MediaKeys()
{
    if (d && d->smtc) {
        try {
            d->smtc.ButtonPressed(d->token);
            d->smtc.IsEnabled(false);
        } catch (...) {
        }
    }
}

bool MediaKeys::available() const { return d && d->smtc; }

bool MediaKeys::attach(quintptr nativeWindow)
{
    if (available()) return true;
    try {
        auto interop = winrt::get_activation_factory<SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
        SystemMediaTransportControls smtc{nullptr};
        winrt::check_hresult(interop->GetForWindow(reinterpret_cast<HWND>(nativeWindow), winrt::guid_of<SystemMediaTransportControls>(),
                                                   winrt::put_abi(smtc)));
        smtc.IsEnabled(true);
        smtc.IsPlayEnabled(true);
        smtc.IsPauseEnabled(true);
        smtc.IsStopEnabled(true);
        smtc.IsNextEnabled(true);
        smtc.IsPreviousEnabled(true);
        smtc.PlaybackStatus(MediaPlaybackStatus::Closed);
        // Rückruf kommt auf einem Fremd-Thread → in den Qt-Hauptthread weiterreichen
        QPointer<MediaKeys> self(this);
        auto token = smtc.ButtonPressed([self](const SystemMediaTransportControls &, const SystemMediaTransportControlsButtonPressedEventArgs &args) {
            QString action;
            switch (args.Button()) {
            case SystemMediaTransportControlsButton::Play: action = QStringLiteral("play.play"); break;
            case SystemMediaTransportControlsButton::Pause: action = QStringLiteral("play.pause"); break;
            case SystemMediaTransportControlsButton::Stop: action = QStringLiteral("play.stop"); break;
            case SystemMediaTransportControlsButton::Next: action = QStringLiteral("play.next"); break;
            case SystemMediaTransportControlsButton::Previous: action = QStringLiteral("play.prev"); break;
            default: return;
            }
            QMetaObject::invokeMethod(self.data(), [self, action] { if (self) emit self->pressed(action); }, Qt::QueuedConnection);
        });
        d = std::make_unique<Impl>();
        d->smtc = smtc;
        d->token = token;
        return true;
    } catch (const winrt::hresult_error &e) {
        m_error = QString::fromWCharArray(e.message().c_str()) + QStringLiteral(" (0x%1)").arg(quint32(e.code().value), 8, 16, QLatin1Char('0'));
    } catch (...) {
        m_error = QStringLiteral("unbekannter Fehler");
    }
    return false;
}

void MediaKeys::setStatus(Status status)
{
    if (!available()) return;
    try {
        MediaPlaybackStatus s = MediaPlaybackStatus::Closed;
        switch (status) {
        case Status::Stopped: s = MediaPlaybackStatus::Stopped; break;
        case Status::Playing: s = MediaPlaybackStatus::Playing; break;
        case Status::Paused: s = MediaPlaybackStatus::Paused; break;
        case Status::Closed: s = MediaPlaybackStatus::Closed; break;
        }
        if (d->smtc.PlaybackStatus() != s) d->smtc.PlaybackStatus(s);
    } catch (...) {
    }
}

void MediaKeys::setTitle(const QString &title, const QString &artist, bool video)
{
    if (!available()) return;
    try {
        auto du = d->smtc.DisplayUpdater();
        du.ClearAll();
        if (video) {
            du.Type(MediaPlaybackType::Video);
            du.VideoProperties().Title(winrt::hstring(reinterpret_cast<const wchar_t *>(title.utf16())));
            if (!artist.isEmpty()) du.VideoProperties().Subtitle(winrt::hstring(reinterpret_cast<const wchar_t *>(artist.utf16())));
        } else {
            du.Type(MediaPlaybackType::Music);
            du.MusicProperties().Title(winrt::hstring(reinterpret_cast<const wchar_t *>(title.utf16())));
            if (!artist.isEmpty()) du.MusicProperties().Artist(winrt::hstring(reinterpret_cast<const wchar_t *>(artist.utf16())));
        }
        du.AppMediaId(L"UVE Player");
        du.Update();
    } catch (...) {
    }
}

void MediaKeys::setNavigation(bool canNext, bool canPrevious)
{
    if (!available()) return;
    try {
        d->smtc.IsNextEnabled(canNext);
        d->smtc.IsPreviousEnabled(canPrevious);
    } catch (...) {
    }
}

#else

struct MediaKeys::Impl {};
MediaKeys::MediaKeys(QObject *parent) : QObject(parent) {}
MediaKeys::~MediaKeys() = default;
bool MediaKeys::available() const { return false; }
bool MediaKeys::attach(quintptr) { m_error = QStringLiteral("nur unter Windows"); return false; }
void MediaKeys::setStatus(Status) {}
void MediaKeys::setTitle(const QString &, const QString &, bool) {}
void MediaKeys::setNavigation(bool, bool) {}

#endif
