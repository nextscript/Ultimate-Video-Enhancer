#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>

namespace uve {

static QString g_file;
static QMutex g_mutex;

void Logger::init(const QString &dataDir)
{
    const QString dir = QDir(dataDir).filePath("logs");
    QDir().mkpath(dir);
    g_file = QDir(dir).filePath("uve-player.log");
    QFileInfo fi(g_file);
    if (fi.exists() && fi.size() > 2 * 1024 * 1024) {
        QFile::remove(g_file + ".old");
        QFile::rename(g_file, g_file + ".old");
    }
}

QString Logger::filePath() { return g_file; }

void Logger::write(const QString &category, const QString &message)
{
    if (g_file.isEmpty()) return;
    QMutexLocker lock(&g_mutex);
    QFile f(g_file);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    const auto line = QStringLiteral("[%1] %2 %3\n")
                          .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), category, message);
    f.write(line.toUtf8());
}

static void qtHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    static const char *names[] = {"debug", "warning", "critical", "fatal", "info"};
    const int i = type == QtInfoMsg ? 4 : int(type);
    Logger::write(QStringLiteral("qt.%1").arg(QLatin1String(names[qBound(0, i, 4)])), msg);
}

void Logger::installQtMessageHandler() { qInstallMessageHandler(qtHandler); }

} // namespace uve
