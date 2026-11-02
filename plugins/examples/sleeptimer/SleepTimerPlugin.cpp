#include "SleepTimerPlugin.h"

#include <QCoreApplication>
#include <QJsonObject>

namespace uve::example {

static const char *kId = "org.uveplayer.example.sleep-timer";

SleepTimerController::SleepTimerController(uve::IPlayerHost *host, QObject *parent) : QObject(parent), m_host(host)
{
    m_minutes = qBound(1, host->loadPluginSettings(QString::fromLatin1(kId)).value("minutes").toInt(30), 600);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_host->setPaused(true);
        m_host->showOsd(QCoreApplication::translate("SleepTimerPlugin", "Einschlaf-Timer: Wiedergabe pausiert"), 3000);
        m_tick.stop();
        emit changed();
    });
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &SleepTimerController::tick);
}

void SleepTimerController::setMinutes(int m)
{
    m = qBound(1, m, 600);
    if (m == m_minutes) return;
    m_minutes = m;
    m_host->savePluginSettings(QString::fromLatin1(kId), QJsonObject{{"minutes", m}});
    emit changed();
}

void SleepTimerController::start()
{
    m_timer.start(m_minutes * 60 * 1000);
    m_tick.start();
    m_host->showOsd(QCoreApplication::translate("SleepTimerPlugin", "Einschlaf-Timer: %1 min").arg(m_minutes), 1500);
    emit changed();
}

void SleepTimerController::stop()
{
    m_timer.stop();
    m_tick.stop();
    emit changed();
}

bool SleepTimerPlugin::initialize(uve::IPlayerHost *host, QString *error)
{
    if (!host) { if (error) *error = QCoreApplication::translate("SleepTimerPlugin", "kein Host"); return false; }
    m_ctrl = new SleepTimerController(host, this);
    return true;
}

} // namespace uve::example
