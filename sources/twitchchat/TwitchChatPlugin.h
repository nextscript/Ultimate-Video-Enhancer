#pragma once
// UVE-Player-Plugin-Einstieg: meldet Manifest, erzeugt Chat-Controller und
// TokenVault, reicht Host-Ereignisse weiter. Der Kanal-Browser ist entfernt –
// nur der Chat ist noch Teil des Plugins.

#include <QObject>
#include <QUrl>

#include "PluginApi.h"

class TwitchChatController;
class TokenVault;

class TwitchChatPlugin : public QObject, public uve::IPlayerPlugin, public uve::IPluginEvents
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID UVE_PLAYER_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(uve::IPlayerPlugin uve::IPluginEvents)

public:
    ~TwitchChatPlugin() override;

    bool initialize(uve::IPlayerHost *host, QString *error) override;
    void activate() override;
    void suspend() override;
    void deactivate() override;

    QObject *controller() override;
    QUrl panelQml() const override { return QUrl(); }      // Panel kommt über contributes.panels
    QString panelTitle() const override { return QStringLiteral("Twitch Chat"); }

    void onMediaChanged(const QString &path) override;
    void onEvent(const QString &name, const QVariantMap &data) override;

private:
    uve::IPlayerHost *m_host = nullptr;
    TwitchChatController *m_ctrl = nullptr;
    TokenVault *m_vault = nullptr;
};
