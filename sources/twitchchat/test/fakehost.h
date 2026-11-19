#pragma once
// Minimal-Implementierung von uve::IPlayerHost für die Konsolentests
// (ersetzt den echten Player – alle rein virtuellen Methoden, keine Effekte).
// v2: Einstellungen werden pro Plugin gespeichert (load-modify-write testbar),
//     Aufrufe von call() protokolliert, hasCommand gibt definierte Befehle zurück.

#include "PluginApi.h"

#include <QDir>
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QVariantMap>
#include <QVector>
#include <cstdio>

class FakeHost : public uve::IPlayerHost
{
public:
    QString hostVersion() const override { return QStringLiteral("1.0.6-testhost"); }
    int apiVersion() const override { return 1; }

    QJsonObject loadPluginSettings(const QString &id) const override { return m_settings.value(id); }
    void savePluginSettings(const QString &id, const QJsonObject &s) override { m_settings[id] = s; }
    QString pluginDataDir(const QString &id) const override
    {
        const QString dir = QDir::currentPath() + QStringLiteral("/testdata_") + id;
        QDir().mkpath(dir);
        return dir;
    }

    void preloadSettings(const QString &id, const QJsonObject &s) { m_settings[id] = s; }
    QJsonObject storedSettings(const QString &id) const { return m_settings.value(id); }

    // Befehle, die dieser Fake-Host "kann" (wie UVE Player 1.0.6+)
    bool hasCommand(const QString &command) const override { return m_commands.contains(command); }
    void disableCommand(const QString &command) { m_commands.remove(command); }

    QVariant call(const QString &pluginId, const QString &command,
                  const QVariantMap &args = QVariantMap(), QString *error = nullptr) override
    {
        if (!m_commands.contains(command)) {
            if (error) *error = QStringLiteral("unknown command");
            return QVariant();
        }
        m_calls.append(qMakePair(command, args));
        Q_UNUSED(pluginId)
        if (command == QLatin1String("mpv.get")) return m_mpvProps.value(args.value(QStringLiteral("name")).toString());
        return QVariant(true);
    }

    const QVector<QPair<QString, QVariantMap>> &calls() const { return m_calls; }
    void clearCalls() { m_calls.clear(); }

    void setShaderStage(const QString &, const QString &) override {}
    void showOsd(const QString &text, int) override { std::printf("OSD: %s\n", text.toUtf8().constData()); }
    void log(const QString &, const QString &level, const QString &message) override
    {
        std::printf("LOG[%s]: %s\n", level.toUtf8().constData(), message.toUtf8().constData());
    }

    QString currentMediaPath() const override { return m_currentMedia; }
    double currentPosition() const override { return 0.0; }
    bool isPaused() const override { return false; }
    void setPaused(bool) override {}
    QImage grabVideoFrame(int) override { return QImage(); }
    void setCurrentMediaPath(const QString &p) { m_currentMedia = p; }
    void setMpvProperty(const QString &name, const QVariant &v) { m_mpvProps[name] = v; }

private:
    mutable QMap<QString, QJsonObject> m_settings;
    QSet<QString> m_commands{QStringLiteral("player.load"), QStringLiteral("panel.open"),
                             QStringLiteral("osd.show"), QStringLiteral("webview.available"),
                             QStringLiteral("mpv.get")};
    QVector<QPair<QString, QVariantMap>> m_calls;
    QString m_currentMedia;
    QVariantMap m_mpvProps;
};
