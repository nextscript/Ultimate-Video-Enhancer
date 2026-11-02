#pragma once
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariant>

namespace uve {

// Einstellungen als JSON mit Schemaversion (settings.json im Datenordner).
// Schreibzugriffe werden gebündelt (Debounce) und atomar über eine Temp-Datei geschrieben.
class AppSettings : public QObject
{
    Q_OBJECT
public:
    static constexpr int kSchemaVersion = 1;

    explicit AppSettings(const QString &dataDir, QObject *parent = nullptr);
    ~AppSettings() override;

    QString dataDir() const { return m_dataDir; }
    QString filePath() const;

    Q_INVOKABLE QVariant value(const QString &key, const QVariant &def = {}) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    QJsonObject object(const QString &key) const { return m_data.value(key).toObject(); }
    void setObject(const QString &key, const QJsonObject &o);

    void flush();

    // Fortsetzen-Positionen (pro Datei)
    double resumePosition(const QString &path) const;
    void setResumePosition(const QString &path, double seconds, double duration);
    void clearResumePosition(const QString &path);

    // Zuletzt geöffnet
    QStringList recentFiles() const;
    void addRecentFile(const QString &path);
    void clearRecentFiles();

signals:
    void changed(const QString &key);

private:
    void load();
    void migrate(int fromVersion);
    void scheduleSave();

    QString m_dataDir;
    QJsonObject m_data;
    QTimer m_saveTimer;
};

} // namespace uve
