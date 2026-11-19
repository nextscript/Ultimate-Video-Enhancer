// QML-Erweiterung, geladen über oauth/qmldir ("plugin uve_oauth") beim Import des Ordners.
// Registriert die Typen unter der URI, mit der der Ordner importiert wird.
#include <QQmlEngineExtensionPlugin>
#include <QQmlExtensionPlugin>
#include <qqml.h>

#include "OAuthLoopback.h"

class UveOAuthPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)
public:
    void registerTypes(const char *uri) override
    {
        qmlRegisterType<OAuthLoopback>(uri, 1, 0, "OAuthLoopback");
    }
};

#include "plugin.moc"
