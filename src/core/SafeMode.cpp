#include "SafeMode.h"
#include <QCoreApplication>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace uve {

SafeMode::SafeMode(const QString &dataDir) : m_file(QDir(dataDir).filePath("crash-guard.json")) {}

bool SafeMode::evaluateOnStartup(bool forcedByUser)
{
    QFile f(m_file);
    QJsonObject last;
    if (f.open(QIODevice::ReadOnly)) last = QJsonDocument::fromJson(f.readAll()).object();
    m_crashes = last.value("pluginCrashes").toInt(0);
    const bool unclean = last.value("running").toBool(false);
    const QString phase = last.value("phase").toString();
    if (unclean && phase == QLatin1String("plugins-loading")) {
        ++m_crashes;
    } else if (!unclean) {
        m_crashes = 0;
    }
    if (forcedByUser) {
        m_active = true;
        m_reason = QStringLiteral("Safe Mode per Startparameter --safe-mode");
    } else if (m_crashes >= 2) {
        m_active = true;
        m_reason = QCoreApplication::translate("SafeMode", "Der Player ist %1-mal beim Laden der Plugins abgestürzt").arg(m_crashes);
    }
    write(true, QStringLiteral("startup"));
    return m_active;
}

void SafeMode::markPluginsLoading() { write(true, QStringLiteral("plugins-loading")); }
void SafeMode::markPluginsStable() { m_crashes = 0; write(true, QStringLiteral("running")); }
void SafeMode::markCleanExit() { write(false, QStringLiteral("exited")); }
void SafeMode::reset() { m_crashes = 0; m_active = false; write(true, QStringLiteral("running")); }

void SafeMode::write(bool running, const QString &phase)
{
    QSaveFile f(m_file);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(QJsonObject{{"running", running}, {"phase", phase}, {"pluginCrashes", m_crashes}}).toJson());
    f.commit();
}

} // namespace uve
