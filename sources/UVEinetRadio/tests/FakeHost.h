#pragma once
// Nachbildung des Player-Hosts für Tests: zeichnet Befehle auf, hält eine Wiedergabeliste und liefert mpv-Werte.
// Ereignisse (media.loaded …) schickt der Test selbst an den Controller – so lassen sich verspätete Ergebnisse,
// Fehler und fehlende Metadaten gezielt nachstellen.
#include "PluginApi.h"

#include <QJsonObject>
#include <QStringList>
#include <QVariantMap>
#include <functional>

class FakeHost : public uve::IPlayerHost
{
public:
    struct Call { QString name; QVariantMap args; };
    QList<Call> calls;
    QStringList playlist;
    int current = -1;
    QString loadedSource;
    QVariantMap mpvValues;      // Name → Wert für mpv.get
    QJsonObject settings;
    QString dataDir;
    // Optional: eigener Umgang mit Befehlen (Live-Test mit echtem mpv)
    std::function<QVariant(const QString &, const QVariantMap &)> override;

    QString hostVersion() const override { return QStringLiteral("1.3.2"); }
    int apiVersion() const override { return 1; }
    QJsonObject loadPluginSettings(const QString &) const override { return settings; }
    void savePluginSettings(const QString &, const QJsonObject &s) override { settings = s; }
    QString pluginDataDir(const QString &) const override { return dataDir; }
    void setShaderStage(const QString &, const QString &) override {}
    void showOsd(const QString &, int) override {}
    void log(const QString &, const QString &, const QString &) override {}
    QString currentMediaPath() const override { return loadedSource; }
    double currentPosition() const override { return 0; }
    bool isPaused() const override { return false; }
    void setPaused(bool) override {}
    QImage grabVideoFrame(int) override { return {}; }

    bool hasCommand(const QString &c) const override
    {
        static const QStringList known{"playlist.add", "playlist.items", "playlist.play", "player.load", "player.stop",
                                       "player.state", "mpv.get", "osd.show", "panel.open"};
        return known.contains(c);
    }
    QStringList commands() const override { return {}; }

    QVariant call(const QString &, const QString &command, const QVariantMap &args, QString *) override
    {
        calls << Call{command, args};
        if (override) {
            const QVariant v = override(command, args);
            if (v.isValid()) return v;
        }
        if (command == QLatin1String("playlist.items")) {
            QVariantList l;
            for (int i = 0; i < playlist.size(); ++i) l << QVariantMap{{"path", playlist[i]}, {"title", QString()}, {"current", i == current}};
            return l;
        }
        if (command == QLatin1String("playlist.add")) {
            playlist << args.value("urls").toStringList();
            if (args.value("play").toBool()) current = int(playlist.size()) - 1;
            return int(playlist.size());
        }
        if (command == QLatin1String("playlist.play")) { current = args.value("index").toInt(); return true; }
        if (command == QLatin1String("player.load")) { playlist << args.value("url").toString(); current = int(playlist.size()) - 1; return true; }
        if (command == QLatin1String("player.state"))
            return QVariantMap{{"source", loadedSource}, {"path", loadedSource}};
        if (command == QLatin1String("mpv.get")) return mpvValues.value(args.value("name").toString());
        return true;
    }

    int count(const QString &command) const
    {
        int n = 0;
        for (const auto &c : calls) if (c.name == command) ++n;
        return n;
    }
    QStringList loadedUrls() const
    {
        QStringList out;
        for (const auto &c : calls) {
            if (c.name == QLatin1String("playlist.add")) out << c.args.value("urls").toStringList();
            else if (c.name == QLatin1String("player.load")) out << c.args.value("url").toString();
            else if (c.name == QLatin1String("playlist.play")) out << playlist.value(c.args.value("index").toInt());
        }
        return out;
    }
};
