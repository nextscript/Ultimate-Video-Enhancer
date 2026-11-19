#include "KickChatPlugin.h"
#include "KickChatController.h"
#include "TokenVault.h"

#include <QDir>
#include <QFileInfo>

#include <iterator>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static const char kId[] = "cc.ts3x.kick-chat";

// Der Host nennt nur den Datenordner; der Ton liegt im Paket neben dieser DLL.
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
    // Linux/macOS: Datei des geladenen Moduls (libkickchat.so / .dylib im Plugin-Ordner)
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void *>(&pluginFolder), &info) && info.dli_fname)
        return QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absolutePath();
#endif
    return QString();
}

KickChatPlugin::~KickChatPlugin()
{
    delete m_vault;                       // kein QObject – gehört uns
}

bool KickChatPlugin::initialize(uve::IPlayerHost *host, QString *error)
{
    if (!host) {
        if (error) *error = QStringLiteral("Kein Host übergeben.");
        return false;
    }
    m_host = host;
    const QString id = QString::fromLatin1(kId);

    QString dataDir = host->pluginDataDir(id);
    if (dataDir.isEmpty())
        dataDir = QDir::tempPath() + QStringLiteral("/kickchat");
    QDir().mkpath(dataDir);

    m_vault = new TokenVault(dataDir + QStringLiteral("/tokens.dat"), id.toUtf8());
    m_ctrl = new KickChatController(host, id, dataDir, this);
    m_ctrl->applyVault(m_vault);
    m_ctrl->setNotificationSound(pluginFolder() + QStringLiteral("/pn.mp3"));
    m_ctrl->setSpellDictionaryDir(pluginFolder() + QStringLiteral("/dict"));

    m_host->log(id, QStringLiteral("info"), QStringLiteral("KickChat-Plugin initialisiert."));
    return true;
}

void KickChatPlugin::activate()
{
    if (m_ctrl) m_ctrl->startFollowing();
}

void KickChatPlugin::suspend()
{
    if (m_ctrl) m_ctrl->closeChat();
}

void KickChatPlugin::deactivate()
{
    if (m_ctrl) m_ctrl->closeChat();
}

QObject *KickChatPlugin::controller()
{
    return m_ctrl;
}

void KickChatPlugin::onMediaChanged(const QString &path)
{
    if (m_ctrl) m_ctrl->onMediaPath(path);
}

void KickChatPlugin::onEvent(const QString &name, const QVariantMap &data)
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
    } else if (name == QLatin1String("app.quit")) {
        m_ctrl->closeChat();
    }
}
