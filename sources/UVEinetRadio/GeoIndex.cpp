#include "GeoIndex.h"
#include "RadioBrowser.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrlQuery>
#include <cmath>

namespace uve::radio {

GeoIndex::GeoIndex(const QString &dataDir, QObject *parent)
    : QObject(parent), m_dir(QDir(dataDir).filePath(QStringLiteral("cache/geo")))
{
}

QString GeoIndex::cacheFile() const { return QDir(m_dir).filePath(QStringLiteral("places.json")); }

void GeoIndex::setState(const QString &state, const QString &error)
{
    m_state = state;
    m_error = error;
    emit changed();
}

void GeoIndex::clearCache()
{
    QDir(m_dir).removeRecursively();
}

bool GeoIndex::loadCache(bool allowOld)
{
    QFile f(cacheFile());
    if (!f.exists()) return false;
    const qint64 age = QFileInfo(f).lastModified().secsTo(QDateTime::currentDateTime());
    if (!allowOld && age > kMaxAgeSec) return false;
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("uveinetradio-geo")
        || root.value(QStringLiteral("version")).toInt() != kFormatVersion)
        return false;
    build(root.value(QStringLiteral("stations")).toArray(), false);
    return !m_places.isEmpty();
}

void GeoIndex::saveCache(const QJsonArray &compact)
{
    QDir().mkpath(m_dir);
    QSaveFile f(cacheFile());
    if (!f.open(QIODevice::WriteOnly)) return;
    const QJsonObject root{{"format", "uveinetradio-geo"}, {"version", kFormatVersion},
                           {"fetched", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"stations", compact}};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

void GeoIndex::load(RadioBrowser *browser, bool force)
{
    if (m_loading) return;
    if (!force && m_state == QLatin1String("ready")) return;
    if (!force && loadCache(false)) { m_stale = false; setState(QStringLiteral("ready")); return; }
    m_loading = true;
    setState(QStringLiteral("loading"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("has_geo_info"), QStringLiteral("true"));
    q.addQueryItem(QStringLiteral("hidebroken"), QStringLiteral("true"));
    q.addQueryItem(QStringLiteral("order"), QStringLiteral("clickcount"));
    q.addQueryItem(QStringLiteral("reverse"), QStringLiteral("true"));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("100000"));
    browser->get(QStringLiteral("/json/stations/search"), q, 0, [this](const RadioBrowser::Result &r) {
        m_loading = false;
        if (r.ok && r.data.isArray() && !r.data.array().isEmpty()) {
            build(r.data.array(), true);
            // Kompakt speichern: nur die Felder, die Karte und Wiedergabe brauchen
            QJsonArray compact;
            for (int i = 0; i < m_stations.size(); ++i) {
                QJsonObject o = m_stations.at(i).toJson();
                o.remove(QStringLiteral("homepage"));
                compact << o;
            }
            saveCache(compact);
            m_stale = false;
            setState(QStringLiteral("ready"));
            return;
        }
        // Katalog nicht erreichbar: alter Stand ist besser als keine Karte
        if (loadCache(true)) { m_stale = true; setState(QStringLiteral("ready"), r.error); return; }
        setState(QStringLiteral("error"), r.error.isEmpty() ? QStringLiteral("no data") : r.error);
    });
}

void GeoIndex::build(const QJsonArray &stations, bool fromApi)
{
    m_stations.clear();
    m_places.clear();
    m_placeOfStation.clear();
    struct Acc { double lat = 0, lon = 0; QHash<QString, int> labels; QVector<int> idx; };
    QHash<qint64, Acc> cells;
    QList<qint64> order;   // Reihenfolge des ersten Auftretens = Beliebtheit
    for (const auto &v : stations) {
        const QJsonObject o = v.toObject();
        Station s = fromApi ? Station::fromApi(o) : Station::fromJson(o);
        const QJsonValue la = o.value(fromApi ? QStringLiteral("geo_lat") : QStringLiteral("lat"));
        const QJsonValue lo = o.value(fromApi ? QStringLiteral("geo_long") : QStringLiteral("lon"));
        if (!s.isValid() || !la.isDouble() || !lo.isDouble()) continue;
        const double lat = la.toDouble(), lon = lo.toDouble();
        if (!std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat) > 90 || std::abs(lon) > 180) continue;
        if (lat == 0 && lon == 0) continue;   // fehlende Angabe
        s.lat = lat;
        s.lon = lon;
        if (fromApi) s.region = cleanText(o.value(QStringLiteral("state")).toString(), 80);
        else s.region = o.value(QStringLiteral("region")).toString();
        const int idx = int(m_stations.size());
        m_stations << s;
        const qint64 key = (qint64(std::lround(lat * 5)) << 32) ^ qint64(std::lround(lon * 5) & 0xffffffff);
        auto it = cells.find(key);
        if (it == cells.end()) { it = cells.insert(key, {}); order << key; }
        it->lat += lat;
        it->lon += lon;
        it->idx << idx;
        const QString label = s.region.isEmpty() ? s.country : s.region + QStringLiteral(", ") + s.country;
        if (!label.isEmpty()) it->labels[label]++;
    }
    m_places.reserve(order.size());
    for (const qint64 key : std::as_const(order)) {
        const Acc &a = cells.value(key);
        GeoPlace p;
        p.lat = a.lat / a.idx.size();
        p.lon = a.lon / a.idx.size();
        int best = 0;
        for (auto it = a.labels.begin(); it != a.labels.end(); ++it)
            if (it.value() > best || (it.value() == best && it.key().contains(QLatin1Char(',')))) { best = it.value(); p.label = it.key(); }
        p.stations = a.idx;
        const double la = p.lat * 3.14159265358979323846 / 180, lo = p.lon * 3.14159265358979323846 / 180;
        p.x = std::cos(la) * std::cos(lo);
        p.y = std::cos(la) * std::sin(lo);
        p.z = std::sin(la);
        const int pi = int(m_places.size());
        for (const int si : a.idx) m_placeOfStation.insert(m_stations.at(si).id, pi);
        m_places << p;
    }
    if (!m_places.isEmpty()) m_state = QStringLiteral("ready");
    emit dataChanged();
    emit changed();
}

QVariantList GeoIndex::stationsAt(int place) const
{
    QVariantList l;
    if (place < 0 || place >= m_places.size()) return l;
    for (const int i : m_places.at(place).stations) l << m_stations.at(i).toVariant();
    return l;
}

QString GeoIndex::placeLabel(int place) const
{
    return (place >= 0 && place < m_places.size()) ? m_places.at(place).label : QString();
}

int GeoIndex::placeStationCount(int place) const
{
    return (place >= 0 && place < m_places.size()) ? int(m_places.at(place).stations.size()) : 0;
}

int GeoIndex::placeOfStation(const QString &id) const { return m_placeOfStation.value(id, -1); }

QVariantMap GeoIndex::placePosition(int place) const
{
    if (place < 0 || place >= m_places.size()) return {};
    return {{"lat", m_places.at(place).lat}, {"lon", m_places.at(place).lon}};
}

} // namespace uve::radio
