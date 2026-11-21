#pragma once
// Sender mit Koordinaten für die Senderkarte (Globus): einmal aus Radio Browser geladen
// (/json/stations/search?has_geo_info=true), zu Orten zusammengefasst (Raster 0,2°) und kompakt im Datenordner
// zwischengespeichert (cache/geo/places.json, 7 Tage gültig, bei Ausfall auch älter).
#include "RadioTypes.h"

#include <QJsonArray>
#include <QObject>
#include <QVariantList>
#include <QVector>

namespace uve::radio {

class RadioBrowser;

struct GeoPlace
{
    double lat = 0, lon = 0;
    QString label;            // Region (state) und Land, sonst nur Land
    QVector<int> stations;    // Indizes in GeoIndex::m_stations, beliebteste zuerst
    // Einheitsvektor (für die Projektion vorberechnet)
    double x = 0, y = 0, z = 0;
};

class GeoIndex : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)          // "", "loading", "ready", "error"
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool stale READ stale NOTIFY changed)             // Daten aus altem Cache (Katalog nicht erreichbar)
    Q_PROPERTY(int placeCount READ placeCount NOTIFY changed)
    Q_PROPERTY(int stationCount READ stationCount NOTIFY changed)
public:
    static constexpr int kFormatVersion = 1;
    static constexpr int kMaxAgeSec = 7 * 24 * 3600;

    explicit GeoIndex(const QString &dataDir, QObject *parent = nullptr);

    QString state() const { return m_state; }
    QString error() const { return m_error; }
    bool stale() const { return m_stale; }
    int placeCount() const { return int(m_places.size()); }
    int stationCount() const { return int(m_stations.size()); }
    const QVector<GeoPlace> &places() const { return m_places; }

    void load(RadioBrowser *browser, bool force = false);
    void clearCache();
    // Aus API-Antwort bzw. gespeicherter Liste aufbauen (öffentlich für Tests)
    void build(const QJsonArray &stations, bool fromApi);

    Q_INVOKABLE QVariantList stationsAt(int place) const;
    Q_INVOKABLE QString placeLabel(int place) const;
    Q_INVOKABLE int placeStationCount(int place) const;
    Q_INVOKABLE int placeOfStation(const QString &id) const;
    Q_INVOKABLE QVariantMap placePosition(int place) const;

signals:
    void changed();
    void dataChanged();   // neue Orte (Globus neu zeichnen)

private:
    QString cacheFile() const;
    bool loadCache(bool allowOld);
    void saveCache(const QJsonArray &compact);
    void setState(const QString &state, const QString &error = {});

    QString m_dir;
    QString m_state;
    QString m_error;
    bool m_stale = false;
    bool m_loading = false;
    QVector<Station> m_stations;
    QVector<GeoPlace> m_places;
    QHash<QString, int> m_placeOfStation;
};

} // namespace uve::radio
