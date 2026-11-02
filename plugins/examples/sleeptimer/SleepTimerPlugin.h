#pragma once
#include "PluginApi.h"

#include <QObject>
#include <QTimer>

namespace uve::example {

class SleepTimerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int minutes READ minutes WRITE setMinutes NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(int remainingSeconds READ remainingSeconds NOTIFY tick)
public:
    explicit SleepTimerController(uve::IPlayerHost *host, QObject *parent = nullptr);
    int minutes() const { return m_minutes; }
    void setMinutes(int m);
    bool running() const { return m_timer.isActive(); }
    int remainingSeconds() const { return running() ? m_timer.remainingTime() / 1000 : 0; }
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    void setSuspended(bool s);
signals:
    void changed();
    void tick();
private:
    uve::IPlayerHost *m_host;
    int m_minutes = 30;
    QTimer m_timer, m_tick;
};

class SleepTimerPlugin : public QObject, public uve::IPlayerPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID UVE_PLAYER_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(uve::IPlayerPlugin)
public:
    bool initialize(uve::IPlayerHost *host, QString *error) override;
    void activate() override {}
    void suspend() override { if (m_ctrl) m_ctrl->stop(); }
    void deactivate() override { if (m_ctrl) m_ctrl->stop(); }
    QObject *controller() override { return m_ctrl; }
    QUrl panelQml() const override { return QUrl(QStringLiteral("qrc:/sleeptimer/SleepTimerPanel.qml")); }
    QString panelTitle() const override { return QStringLiteral("Timer"); }
private:
    SleepTimerController *m_ctrl = nullptr;
};

} // namespace uve::example
