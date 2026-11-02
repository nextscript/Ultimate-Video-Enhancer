#include "AppSettings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace uve {

static QJsonObject defaults()
{
    return {
        {"schemaVersion", AppSettings::kSchemaVersion},
        {"volume", 100},
        {"muted", false},
        {"theme", "dark"},
        {"resumePlayback", true},
        {"rememberHistory", true},
        {"restoreSession", true},
        {"hwdec", "auto-safe"},
        {"seekStep", 10},
        {"repeat", "none"},
        {"shuffle", false},
        {"showSidebar", true},
        {"checkToolUpdates", true},
        {"streamQualityLimit", 0},   // 0 = immer beste Qualität
        {"subAuto", true},
        {"keepAspect", "fit"},
        {"plugins", QJsonObject()},
        {"pluginSettings", QJsonObject()},
        {"hotkeys", QJsonObject()},
        {"resume", QJsonObject()},
        {"recent", QJsonArray()},
        {"session", QJsonArray()},
        {"skippedToolVersions", QJsonObject()},
    };
}

AppSettings::AppSettings(const QString &dataDir, QObject *parent)
    : QObject(parent), m_dataDir(dataDir)
{
    QDir().mkpath(m_dataDir);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &AppSettings::flush);
    load();
}

AppSettings::~AppSettings() { flush(); }

QString AppSettings::filePath() const { return QDir(m_dataDir).filePath("settings.json"); }

void AppSettings::load()
{
    m_data = defaults();
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // Beschädigte Datei sichern statt überschreiben (keine Datenkorruption)
        f.close();
        QFile::copy(filePath(), filePath() + ".corrupt-" + QString::number(QDateTime::currentSecsSinceEpoch()));
        return;
    }
    const auto stored = doc.object();
    for (auto it = stored.begin(); it != stored.end(); ++it) m_data.insert(it.key(), it.value());
    const int ver = stored.value("schemaVersion").toInt(0);
    if (ver < kSchemaVersion) migrate(ver);
}

void AppSettings::migrate(int fromVersion)
{
    Q_UNUSED(fromVersion);
    // Schema 0 → 1: noch keine Felder umbenannt; Version nur anheben.
    m_data.insert("schemaVersion", kSchemaVersion);
    scheduleSave();
}

QVariant AppSettings::value(const QString &key, const QVariant &def) const
{
    const auto v = m_data.value(key);
    return v.isUndefined() ? def : v.toVariant();
}

void AppSettings::setValue(const QString &key, const QVariant &value)
{
    const auto jv = QJsonValue::fromVariant(value);
    if (m_data.value(key) == jv) return;
    m_data.insert(key, jv);
    scheduleSave();
    emit changed(key);
}

void AppSettings::setObject(const QString &key, const QJsonObject &o)
{
    m_data.insert(key, o);
    scheduleSave();
    emit changed(key);
}

void AppSettings::scheduleSave() { m_saveTimer.start(); }

void AppSettings::flush()
{
    m_saveTimer.stop();
    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(m_data).toJson(QJsonDocument::Indented));
    f.commit();
}

static QString resumeKey(const QString &path) { return path.toLower(); }

double AppSettings::resumePosition(const QString &path) const
{
    return m_data.value("resume").toObject().value(resumeKey(path)).toObject().value("t").toDouble(0);
}

void AppSettings::setResumePosition(const QString &path, double seconds, double duration)
{
    auto r = m_data.value("resume").toObject();
    r.insert(resumeKey(path), QJsonObject{{"t", qRound(seconds)}, {"d", qRound(duration)},
                                          {"at", QDateTime::currentSecsSinceEpoch()}});
    // höchstens 300 Einträge, älteste zuerst entfernen
    while (r.size() > 300) {
        QString oldest; qint64 oldestAt = std::numeric_limits<qint64>::max();
        for (auto it = r.begin(); it != r.end(); ++it) {
            const auto at = it.value().toObject().value("at").toInteger();
            if (at < oldestAt) { oldestAt = at; oldest = it.key(); }
        }
        r.remove(oldest);
    }
    m_data.insert("resume", r);
    scheduleSave();
}

void AppSettings::clearResumePosition(const QString &path)
{
    auto r = m_data.value("resume").toObject();
    if (!r.contains(resumeKey(path))) return;
    r.remove(resumeKey(path));
    m_data.insert("resume", r);
    scheduleSave();
}

QStringList AppSettings::recentFiles() const
{
    QStringList out;
    for (const auto &v : m_data.value("recent").toArray()) out << v.toString();
    return out;
}

void AppSettings::addRecentFile(const QString &path)
{
    if (!m_data.value("rememberHistory").toBool(true)) return;
    auto list = recentFiles();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 15) list.removeLast();
    m_data.insert("recent", QJsonArray::fromStringList(list));
    scheduleSave();
    emit changed("recent");
}

void AppSettings::clearRecentFiles()
{
    m_data.insert("recent", QJsonArray());
    scheduleSave();
    emit changed("recent");
}

} // namespace uve
