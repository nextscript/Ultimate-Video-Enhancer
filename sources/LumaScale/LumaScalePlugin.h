#pragma once
#include "PluginApi.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace lumascale {

class LumaScaleController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList parameters READ parameters CONSTANT)   // key, label, note, from, to, step, def
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit LumaScaleController(uve::IPlayerHost *host, QObject *parent = nullptr);

    QVariantList parameters() const;
    QVariantMap values() const { return m_values; }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString error() const { return m_error; }

    Q_INVOKABLE void setParameter(const QString &key, double value);
    Q_INVOKABLE void resetDefaults();

    void setActive(bool active);
    void onShaderError(const QString &message);

signals:
    void valuesChanged();
    void enabledChanged();
    void errorChanged();

private:
    void load();
    void flush();          // save settings and apply the shader now
    void scheduleFlush();
    void applyShader();
    void setError(const QString &message);
    QString buildShader() const;

    uve::IPlayerHost *m_host;
    QVariantMap m_values;
    QString m_template;
    QString m_currentFile;
    QString m_error;
    QTimer m_debounce;
    bool m_enabled = true;
    bool m_active = false;
};

class LumaScalePlugin : public QObject, public uve::IPlayerPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID UVE_PLAYER_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(uve::IPlayerPlugin)
public:
    bool initialize(uve::IPlayerHost *host, QString *error) override;
    void activate() override;
    void suspend() override;
    void deactivate() override;
    QObject *controller() override { return m_controller; }
    QUrl panelQml() const override { return QUrl(QStringLiteral("qrc:/lumascale/LumaScalePanel.qml")); }
    QString panelTitle() const override { return QStringLiteral("LumaScale"); }
    void onShaderError(const QString &message) override;

private:
    LumaScaleController *m_controller = nullptr;
};

} // namespace lumascale
