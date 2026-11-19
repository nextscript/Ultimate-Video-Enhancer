#include "TwitchChatPlugin.h"
#include "TwitchChatController.h"
#include "TokenVault.h"

#include <QDir>
#include <QFileInfo>

#include <iterator>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static const char kId[] = "cc.ts3x.twitch-chat";

// Der Host nennt nur den Datenordner; die Wörterbücher liegen im Paket neben dieser DLL.
static QString pluginFolder()
{
#ifdef Q_OS_WIN
    HMODULE self = nullptr;
    wchar_t path[32768];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&pluginFolder), &self)
        && GetModuleFileNameW(self, path, DWORD(std::size(path))))
        return QFileInfo(QString::fromWCharArray(path)).absolutePath();
#else
    // Linux/macOS: Datei des geladenen Moduls (libtwitchchat.so / .dylib im Plugin-Ordner)
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void *>(&pluginFolder), &info) && info.dli_fname)
        return QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absolutePath();
#endif
    return QString();
}

TwitchChatPlugin::~TwitchChatPlugin()
{
    delete m_vault;                       // kein QObject – gehört uns
}

bool TwitchChatPlugin::initialize(uve::IPlayerHost *host, QString *error)
{
    if (!host) {
        if (error) *error = QStringLiteral("Kein Host übergeben.");
        return false;
    }
    m_host = host;
    const QString id = QString::fromLatin1(kId);

    QString dataDir = host->pluginDataDir(id);
    if (dataDir.isEmpty())
        dataDir = QDir::tempPath() + QStringLiteral("/twitchchat");
    QDir().mkpath(dataDir);

    m_ctrl = new TwitchChatController(host, id, this);
    m_ctrl->setSpellDictionaryDir(pluginFolder() + QStringLiteral("/dict"));
    m_ctrl->setNotificationSound(pluginFolder() + QStringLiteral("/pn.mp3"));
    m_vault = new TokenVault(dataDir + QStringLiteral("/tokens.dat"), id.toUtf8());
    m_ctrl->applyVault(m_vault);          // inkl. Migration alter Klartext-Logins

    // Der frühere Kanal-Browser ist bewusst nicht mehr erzeugt: gewollt ist nur
    // noch der Chat. Damit laufen auch kein OAuth-Login, keine Helix-Abfragen
    // und kein Token-Refresh mehr – die Fehlerquelle "Login abgelaufen" existiert
    // in der Benutzeroberfläche nicht mehr.

    m_host->log(id, QStringLiteral("info"),
                QStringLiteral("TwitchChat-Plugin initialisiert (nur Chat)."));
    return true;
}

void TwitchChatPlugin::activate()
{
    if (m_ctrl) m_ctrl->startFollowing();
}

void TwitchChatPlugin::suspend()
{
    if (m_ctrl) m_ctrl->closeChat();
}

void TwitchChatPlugin::deactivate()
{
    if (m_ctrl) m_ctrl->closeChat();
}

QObject *TwitchChatPlugin::controller()
{
    return m_ctrl;
}

void TwitchChatPlugin::onMediaChanged(const QString &path)
{
    if (m_ctrl) m_ctrl->onMediaPath(path);
}

void TwitchChatPlugin::onEvent(const QString &name, const QVariantMap &data)
{
    if (!m_ctrl) return;
    if (name == QLatin1String("media.loading") || name == QLatin1String("media.loaded")) {
        m_ctrl->onMediaEvent(name, data);
    } else if (name == QLatin1String("action.triggered")) {
        if (data.value(QStringLiteral("id")).toString() == QLatin1String("open")) {
            m_ctrl->openChat();
            if (m_host && m_host->hasCommand(QStringLiteral("panel.open"))) {
                QString err;
                m_host->call(QString::fromLatin1(kId), QStringLiteral("panel.open"),
                             QVariantMap{{QStringLiteral("panel"), QStringLiteral("chat")}}, &err);
            }
        }
    }
}
