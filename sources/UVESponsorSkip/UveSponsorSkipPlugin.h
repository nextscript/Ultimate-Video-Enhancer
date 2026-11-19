#pragma once
// UVESponsorSkip: holt für YouTube-Videos die Segmente aus der SponsorBlock-Datenbank (sponsor.ajay.app) und
// überspringt Sponsoren, Intros, Outros usw. je nach Einstellung automatisch oder per Taste – mit "Sprung rückgängig".
#include "PluginApi.h"
#include "SponsorCore.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;
class QQuickItem;
class QTimer;

namespace uve::sponsor {

class UveSponsorSkipController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool countViews READ countViews WRITE setCountViews NOTIFY changed)
    Q_PROPERTY(QVariantList categories READ categoryList NOTIFY changed)
    Q_PROPERTY(QVariantList segments READ segments NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(QString language READ language NOTIFY changed)
public:
    UveSponsorSkipController(uve::IPlayerHost *host, SkipEngine *engine, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool countViews() const { return m_countViews; }
    void setCountViews(bool count);
    QString server() const { return m_server; }
    QVariantList categoryList() const;
    QVariantList segments() const { return m_segments; }
    void setSegments(const QVariantList &segments) { m_segments = segments; emit changed(); }
    QString status() const { return m_status; }
    void setStatus(const QString &status);
    bool canUndo() const { return m_canUndo; }
    void setCanUndo(bool can);
    QString language() const { return m_language; }
    void setLanguage(const QString &language);
    QString text(const char *de, const char *en) const;
    QString categoryLabel(const QString &id) const;

    Q_INVOKABLE void setMode(const QString &category, int mode);
    Q_INVOKABLE void skip() { emit skipRequested(); }
    Q_INVOKABLE void undo() { emit undoRequested(); }
    Q_INVOKABLE void seekTo(double seconds) { emit seekRequested(seconds); }

signals:
    void changed();
    void skipRequested();
    void undoRequested();
    void seekRequested(double seconds);
    void enabledChanged();

private:
    void save();

    uve::IPlayerHost *m_host;
    SkipEngine *m_engine;
    bool m_enabled = true;
    bool m_countViews = true;
    bool m_canUndo = false;
    QString m_server = QStringLiteral("https://sponsor.ajay.app");
    QVariantList m_segments;
    QString m_status;
    QString m_language = QStringLiteral("de");
};

class UveSponsorSkipPlugin : public QObject, public uve::IPlayerPlugin, public uve::IPluginEvents
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID UVE_PLAYER_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(uve::IPlayerPlugin uve::IPluginEvents)
public:
    bool initialize(uve::IPlayerHost *host, QString *error) override;
    void activate() override;
    void suspend() override;
    void deactivate() override;
    QObject *controller() override { return m_controller; }
    QUrl panelQml() const override { return {}; }
    QString panelTitle() const override { return {}; }
    void onEvent(const QString &name, const QVariantMap &data) override;

private:
    void mediaLoaded(const QVariantMap &data);
    void reset();
    void fetch(const QString &videoId);
    void publishSegments();
    void updateTimer();
    void tick();
    void jump(const SkipEngine::Jump &jump, bool automatic);
    void skipNow();
    void undo();
    void seek(double seconds);
    void setMuted(bool mute);
    void reportViews(const QStringList &uuids);
    double duration() const;
    // Farbige Markierungen der Segmente in der Zeitleiste des Players (eigene Ebene in der SeekBar, kein Player-Update nötig)
    void updateMarkers();
    QVariantList markerData() const;
    void attachMarkers();
    void removeMarkers();

    uve::IPlayerHost *m_host = nullptr;
    QList<QPointer<QQuickItem>> m_markerLayers;
    int m_attachRetry = 0;
    UveSponsorSkipController *m_controller = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QTimer *m_timer = nullptr;
    QPointer<QNetworkReply> m_reply;
    SkipEngine m_engine;
    QString m_videoId;
    bool m_active = true;
    bool m_muting = false;

    struct LastJump { bool valid = false; double from = 0; QStringList uuids; QString category; };
    LastJump m_last;
    qint64 m_guardUntil = 0;   // nach einem eigenen Sprung: alte Position ignorieren, bis mpv dort ist
    double m_guardTarget = 0;
};

} // namespace uve::sponsor
