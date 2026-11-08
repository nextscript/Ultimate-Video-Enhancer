#pragma once
#include <QHash>
#include <QObject>
#include <QStringList>

namespace uve {

class MpvController;

// Verwaltet die Benutzer-Shader-Stufen (mpv "glsl-shaders") aller Plugins in fester Reihenfolge.
// Kompilierfehler werden der verursachenden Stufe zugeordnet (über //!DESC-Präfix "<pluginId>:")
// bzw. der zuletzt geänderten Stufe; diese wird deaktiviert, die Wiedergabe läuft weiter.
class ShaderChain : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList activeShaders READ activeShaders NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)
public:
    explicit ShaderChain(MpvController *mpv, QObject *parent = nullptr);

    // owner: Plugin-ID oder "<Plugin-ID>#<Name>" für eine weitere Stufe desselben Plugins.
    // Die Reihenfolge ergibt sich aus dem ersten Aufruf je Besitzer, auch mit leerer Datei.
    void setStage(const QString &owner, const QString &file);
    void removeSubStages(const QString &pluginId);   // alle "<Plugin-ID>#…"-Stufen
    QStringList activeShaders() const;
    QString lastError() const { return m_lastError; }
    QStringList disabledOwners() const { return m_disabled.keys(); }
    Q_INVOKABLE void reenable(const QString &owner);

signals:
    void changed();
    void errorOccurred(const QString &owner, const QString &message);

private:
    void apply();
    void onMpvError(const QString &text);

    MpvController *m_mpv;
    QStringList m_order;              // Reihenfolge der Besitzer
    QHash<QString, QString> m_files;  // owner → Shaderdatei
    QHash<QString, QString> m_disabled;
    QString m_lastChanged;
    QString m_lastError;
};

} // namespace uve
