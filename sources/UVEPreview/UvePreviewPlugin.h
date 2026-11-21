#pragma once
// UVEPreview: Vorschaubild über der Zeitleiste. Beim Überfahren oder Ziehen des Fortschrittsbalkens erscheint ein
// kleines Bild der Stelle mit Zeitstempel. Die Ebene wird (wie Markierungen anderer Plugins) als reines QML in die
// SeekBar des Players eingehängt – kein Player-Update nötig. Klicken und Ziehen bleiben Sache der Zeitleiste.
#include "PluginApi.h"
#include "PreviewWorker.h"

#include <QHash>
#include <QSet>
#include <QList>
#include <QObject>
#include <QPointer>

class QQuickItem;
class QTimer;

namespace uve::preview {

class UvePreviewController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)       // aktuelles Medium hat eine Bildvorschau
    Q_PROPERTY(QString image READ image NOTIFY imageChanged)               // data:-URL (PNG) oder leer
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY imageChanged)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY imageChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
public:
    UvePreviewController(uve::IPlayerHost *host, QObject *parent = nullptr);
    ~UvePreviewController() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    bool available() const { return m_available; }
    QString image() const { return m_image; }
    int imageWidth() const { return m_imageW; }
    int imageHeight() const { return m_imageH; }
    QString status() const { return m_status; }
    QString language() const { return m_language; }
    void setLanguage(const QString &code);

    // Von der Zeitleisten-Ebene: Zeiger auf Zeit `seconds` (gleiche Berechnung wie Tooltip und Sprung)
    Q_INVOKABLE void hover(double seconds);
    // Zeiger hat die Leiste verlassen (ohne Ziehen) bzw. Ziehen abgebrochen: Auftrag invalidieren, Bild weg
    Q_INVOKABLE void leave();

    // Vom Plugin
    void mediaChanged(const QString &path, double duration);   // leerer Pfad = kein Medium
    void setActive(bool active);                               // Plugin aktiv (nicht suspendiert/deaktiviert)
    void shutdown();
    QString text(const char *de, const char *en) const;

signals:
    void enabledChanged();
    void availableChanged();
    void imageChanged();
    void statusChanged();
    void languageChanged();

private:
    void dispatch();
    void onFrame(quint64 id, quint64 media, int index, const QImage &img, const QString &url);
    void onFailed(quint64 id, quint64 media, int index, const QString &reason);
    void onUnsupported(quint64 media, const QString &reason);
    void publish(int index);
    void clearImage();
    void updateAvailable();
    void setStatus(const QString &s);
    void invalidate();

    struct Frame { QString url; int w = 0; int h = 0; quint64 used = 0; };

    uve::IPlayerHost *m_host;
    PreviewWorker *m_worker;
    QTimer *m_throttle;
    bool m_enabled = true;
    bool m_active = false;
    bool m_available = false;
    bool m_unsupported = false;
    QString m_language = QStringLiteral("de");
    QString m_status;

    quint64 m_media = 0;          // Wiedergabesitzung (wechselt mit jedem Medium)
    QString m_path;               // lokaler Pfad oder leer
    double m_duration = 0;
    double m_step = 1;

    quint64 m_seq = 0;            // Anfragekennung
    quint64 m_lastSent = 0;
    int m_wanted = -1;            // Rasterplatz unter dem Zeiger
    int m_sentIndex = -1;         // zuletzt an den Decoder gegeben
    int m_shownIndex = -1;
    QHash<int, Frame> m_cache;    // kleiner Speicher-Cache, nur für das aktuelle Medium
    QSet<int> m_failed;           // Stellen ohne Bild (nicht ständig neu anfragen; Tooltip bleibt)
    quint64 m_useClock = 0;

    QString m_image;
    int m_imageW = 0;
    int m_imageH = 0;
};

class UvePreviewPlugin : public QObject, public uve::IPlayerPlugin, public uve::IPluginEvents
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
    void ensureLayers();
    void attachLayers();
    void removeLayers();

    uve::IPlayerHost *m_host = nullptr;
    UvePreviewController *m_controller = nullptr;
    QTimer *m_attachTimer = nullptr;
    QList<QPointer<QQuickItem>> m_layers;
};

} // namespace uve::preview
