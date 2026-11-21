#include "GlobeItem.h"
#include "GeoIndex.h"

#include <QFile>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <functional>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QtEndian>
#include <cmath>

namespace uve::radio {

static constexpr double kDeg = 3.14159265358979323846 / 180.0;

// ---------------------------------------------------------------- eingebaute Weltkarte (qrc)
static QVector<QVector<GlobeItem::Vec>> readLines(const QString &path)
{
    QVector<QVector<GlobeItem::Vec>> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QByteArray d = f.readAll();
    if (d.size() < 4) return out;
    const auto *p = reinterpret_cast<const uchar *>(d.constData());
    const auto *end = p + d.size();
    const quint32 n = qFromLittleEndian<quint32>(p);
    p += 4;
    for (quint32 i = 0; i < n && p + 2 <= end; ++i) {
        const quint16 cnt = qFromLittleEndian<quint16>(p);
        p += 2;
        if (p + cnt * 4 > end) break;
        QVector<GlobeItem::Vec> line;
        line.reserve(cnt);
        for (quint16 k = 0; k < cnt; ++k, p += 4) {
            const double lon = qFromLittleEndian<qint16>(p) / 100.0 * kDeg;
            const double lat = qFromLittleEndian<qint16>(p + 2) / 100.0 * kDeg;
            line.append({float(std::cos(lat) * std::cos(lon)), float(std::cos(lat) * std::sin(lon)), float(std::sin(lat))});
        }
        out.append(line);
    }
    return out;
}

const QVector<QVector<GlobeItem::Vec>> &GlobeItem::land()
{
    static const auto v = readLines(QStringLiteral(":/uveinetradio/world/land.bin"));
    return v;
}

const QVector<QVector<GlobeItem::Vec>> &GlobeItem::borders()
{
    static const auto v = readLines(QStringLiteral(":/uveinetradio/world/borders.bin"));
    return v;
}

// ---------------------------------------------------------------- Grundlagen
GlobeItem::GlobeItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setFlag(ItemAcceptsInputMethod, false);
    setOpaquePainting(false);
    m_fly.setDuration(650);
    m_fly.setEasingCurve(QEasingCurve::InOutCubic);
    m_fly.setStartValue(0.0);
    m_fly.setEndValue(1.0);
    connect(&m_fly, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        const double t = v.toDouble();
        m_lat = m_flyFrom[0] + (m_flyTo[0] - m_flyFrom[0]) * t;
        m_lon = m_flyFrom[1] + (m_flyTo[1] - m_flyFrom[1]) * t;
        if (m_lon > 180) m_lon -= 360;
        if (m_lon < -180) m_lon += 360;
        m_zoom = t >= 1.0 ? m_flyTo[2] : m_flyFrom[2] * std::pow(m_flyTo[2] / m_flyFrom[2], t);
        updateCenterPlace();
        emit viewChanged();
        update();
    });
}

void GlobeItem::setIndex(GeoIndex *index)
{
    if (m_index) disconnect(m_index, nullptr, this, nullptr);
    m_index = index;
    if (m_index) connect(m_index, &GeoIndex::dataChanged, this, [this] {
        m_selected = m_hovered = m_center = -1;
        updateCenterPlace();
        emit selectionChanged();
        update();
    });
    update();
}

void GlobeItem::setCenterLat(double v) { m_lat = qBound(-85.0, v, 85.0); updateCenterPlace(); emit viewChanged(); update(); }
void GlobeItem::setCenterLon(double v)
{
    m_lon = std::fmod(v + 540.0, 360.0) - 180.0;
    updateCenterPlace();
    emit viewChanged();
    update();
}
void GlobeItem::setZoom(double v) { m_zoom = qBound(0.8, v, 60.0); updateCenterPlace(); emit viewChanged(); update(); }
void GlobeItem::setSelectedPlace(int p) { if (p == m_selected) return; m_selected = p; emit selectionChanged(); update(); }
void GlobeItem::setPlayingPlace(int p) { if (p == m_playing) return; m_playing = p; emit selectionChanged(); update(); }

double GlobeItem::radius() const { return std::min(width(), height()) * 0.42 * m_zoom; }

GlobeItem::Proj GlobeItem::project(double X, double Y, double Z) const
{
    const double l0 = m_lon * kDeg, p0 = m_lat * kDeg;
    const double cl = std::cos(l0), sl = std::sin(l0), cp = std::cos(p0), sp = std::sin(p0);
    const double c = X * cl + Y * sl;           // cosφ·cos(λ−λ0)
    const double xs = Y * cl - X * sl;          // cosφ·sin(λ−λ0)
    const double ys = cp * Z - sp * c;
    const double zs = sp * Z + cp * c;
    const double R = radius();
    return {width() / 2 + R * xs, height() / 2 - R * ys, zs};
}

void GlobeItem::zoomBy(double factor) { setZoom(m_zoom * factor); }

void GlobeItem::flyTo(double lat, double lon, double zoom)
{
    m_fly.stop();
    m_flyFrom[0] = m_lat; m_flyFrom[1] = m_lon; m_flyFrom[2] = m_zoom;
    double target = lon;
    // kürzester Weg um die Erde
    while (target - m_lon > 180) target -= 360;
    while (target - m_lon < -180) target += 360;
    m_flyTo[0] = qBound(-85.0, lat, 85.0); m_flyTo[1] = target; m_flyTo[2] = zoom > 0 ? qBound(0.8, zoom, 60.0) : m_zoom;
    m_fly.start();
}

void GlobeItem::flyToPlace(int place, double zoom)
{
    if (!m_index || place < 0 || place >= m_index->places().size()) return;
    const auto &p = m_index->places().at(place);
    flyTo(p.lat, p.lon, zoom);
}

int GlobeItem::placeAt(double x, double y, double maxDistance) const
{
    if (!m_index) return -1;
    int best = -1;
    double bestD = maxDistance * maxDistance;
    const auto &places = m_index->places();
    for (int i = 0; i < places.size(); ++i) {
        const auto &p = places.at(i);
        const Proj s = project(p.x, p.y, p.z);
        if (s.z < 0.05) continue;
        const double d = (s.x - x) * (s.x - x) + (s.y - y) * (s.y - y);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

QPointF GlobeItem::placeScreenPos(int place) const
{
    if (!m_index || place < 0 || place >= m_index->places().size()) return {-1, -1};
    const auto &p = m_index->places().at(place);
    const Proj s = project(p.x, p.y, p.z);
    return s.z > 0 ? QPointF(s.x, s.y) : QPointF(-1, -1);
}

void GlobeItem::updateCenterPlace()
{
    const int c = placeAt(width() / 2, height() / 2, 28);
    if (c != m_center) { m_center = c; emit hoverChanged(); }
}

// ---------------------------------------------------------------- Zeichnen
void GlobeItem::paint(QPainter *p)
{
    const double R = radius();
    const QPointF c(width() / 2, height() / 2);
    p->setRenderHint(QPainter::Antialiasing, true);

    // Atmosphäre
    QRadialGradient glow(c, R * 1.12);
    QColor g = m_dot;
    g.setAlphaF(0.0);
    glow.setColorAt(0.86, g);
    g.setAlphaF(0.22);
    glow.setColorAt(0.9, g);
    g.setAlphaF(0.0);
    glow.setColorAt(1.0, g);
    p->setPen(Qt::NoPen);
    p->setBrush(glow);
    p->drawEllipse(c, R * 1.12, R * 1.12);

    // Ozean mit leichter Kugelschattierung
    QRadialGradient sea(c - QPointF(R * 0.35, R * 0.35), R * 1.4);
    sea.setColorAt(0, m_ocean.lighter(135));
    sea.setColorAt(1, m_ocean.darker(130));
    p->setBrush(sea);
    p->drawEllipse(c, R, R);

    QPainterPath disc;
    disc.addEllipse(c, R, R);
    p->save();
    p->setClipPath(disc);

    // Gradnetz
    QColor grid = m_border;
    grid.setAlphaF(0.25);
    p->setPen(QPen(grid, 0.6));
    p->setBrush(Qt::NoBrush);
    auto drawLine = [&](const std::function<void(int, double &, double &, double &)> &pt, int steps) {
        QPolygonF line;
        for (int i = 0; i <= steps; ++i) {
            double X, Y, Z;
            pt(i, X, Y, Z);
            const Proj s = project(X, Y, Z);
            if (s.z > 0) line << QPointF(s.x, s.y);
            else if (!line.isEmpty()) { p->drawPolyline(line); line.clear(); }
        }
        if (line.size() > 1) p->drawPolyline(line);
    };
    for (int lat = -60; lat <= 60; lat += 30)
        drawLine([lat](int i, double &X, double &Y, double &Z) {
            const double la = lat * kDeg, lo = (i * 5 - 180) * kDeg;
            X = std::cos(la) * std::cos(lo); Y = std::cos(la) * std::sin(lo); Z = std::sin(la);
        }, 72);
    for (int lon = -180; lon < 180; lon += 30)
        drawLine([lon](int i, double &X, double &Y, double &Z) {
            const double la = (i * 5 - 90) * kDeg, lo = lon * kDeg;
            X = std::cos(la) * std::cos(lo); Y = std::cos(la) * std::sin(lo); Z = std::sin(la);
        }, 36);

    // Landflächen: Punkte hinter dem Horizont werden auf den Rand gelegt
    p->setPen(QPen(m_land.lighter(125), 0.8));
    p->setBrush(m_land);
    for (const auto &ring : land()) {
        QPolygonF poly;
        poly.reserve(ring.size());
        bool anyFront = false;
        for (const Vec &v : ring) {
            Proj s = project(v.x, v.y, v.z);
            if (s.z > 0) anyFront = true;
            else {
                const double dx = s.x - c.x(), dy = s.y - c.y();
                const double len = std::hypot(dx, dy);
                if (len > 1e-6) { s.x = c.x() + dx / len * R; s.y = c.y() + dy / len * R; }
            }
            poly << QPointF(s.x, s.y);
        }
        if (anyFront) p->drawPolygon(poly, Qt::WindingFill);
    }
    // Grenzen
    p->setPen(QPen(m_border, 0.7));
    p->setBrush(Qt::NoBrush);
    for (const auto &line : borders()) {
        QPolygonF seg;
        for (const Vec &v : line) {
            const Proj s = project(v.x, v.y, v.z);
            if (s.z > 0) seg << QPointF(s.x, s.y);
            else if (!seg.isEmpty()) { if (seg.size() > 1) p->drawPolyline(seg); seg.clear(); }
        }
        if (seg.size() > 1) p->drawPolyline(seg);
    }
    p->restore();

    // Orte
    if (m_index) {
        const auto &places = m_index->places();
        const QRectF view = boundingRect().adjusted(-10, -10, 10, 10);
        const double scale = 0.7 + 0.2 * std::sqrt(m_zoom);
        p->setPen(Qt::NoPen);
        for (int i = 0; i < places.size(); ++i) {
            const auto &pl = places.at(i);
            const Proj s = project(pl.x, pl.y, pl.z);
            if (s.z <= 0.02 || !view.contains(s.x, s.y)) continue;
            const double r = std::min(1.0 + 0.55 * std::log2(1.0 + pl.stations.size()), 5.0) * scale;
            QColor col = (i == m_playing) ? m_accent : m_dot;
            col.setAlphaF(std::min(1.0, 0.35 + s.z * 0.75));
            p->setBrush(col);
            p->drawEllipse(QPointF(s.x, s.y), r, r);
        }
        // Hervorhebungen obenauf: gewählt, laufend, unter der Maus
        auto ring = [&](int i, const QColor &col, double extra) {
            if (i < 0 || i >= places.size()) return;
            const auto &pl = places.at(i);
            const Proj s = project(pl.x, pl.y, pl.z);
            if (s.z <= 0) return;
            const double r = std::min(1.0 + 0.55 * std::log2(1.0 + pl.stations.size()), 5.0) * scale + extra;
            p->setPen(QPen(col, 2));
            p->setBrush(Qt::NoBrush);
            p->drawEllipse(QPointF(s.x, s.y), r, r);
        };
        ring(m_hovered, Qt::white, 3);
        ring(m_playing, m_accent, 5);
        ring(m_selected, Qt::white, 6);
    }

    // Fadenkreuz
    QColor cross = Qt::white;
    cross.setAlphaF(0.55);
    p->setPen(QPen(cross, 1.2));
    p->setBrush(Qt::NoBrush);
    p->drawEllipse(c, 16, 16);
    p->drawLine(QPointF(c.x() - 24, c.y()), QPointF(c.x() - 19, c.y()));
    p->drawLine(QPointF(c.x() + 19, c.y()), QPointF(c.x() + 24, c.y()));
    p->drawLine(QPointF(c.x(), c.y() - 24), QPointF(c.x(), c.y() - 19));
    p->drawLine(QPointF(c.x(), c.y() + 19), QPointF(c.x(), c.y() + 24));
}

// ---------------------------------------------------------------- Bedienung
void GlobeItem::mousePressEvent(QMouseEvent *e)
{
    m_fly.stop();
    m_pressed = true;
    m_pressPos = m_lastPos = e->position();
    e->accept();
}

void GlobeItem::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_pressed) return;
    const QPointF d = e->position() - m_lastPos;
    m_lastPos = e->position();
    if (!m_dragging && (e->position() - m_pressPos).manhattanLength() > 4) { m_dragging = true; emit hoverChanged(); }
    if (!m_dragging) return;
    const double R = std::max(radius(), 1.0);
    // Winkel pro Pixel; bei hohen Breiten den Längengrad stärker drehen, damit es sich gleichmäßig anfühlt
    m_lon -= d.x() / R / kDeg / std::max(0.3, std::cos(m_lat * kDeg));
    m_lat = qBound(-85.0, m_lat + d.y() / R / kDeg, 85.0);
    m_lon = std::fmod(m_lon + 540.0, 360.0) - 180.0;
    updateCenterPlace();
    emit viewChanged();
    update();
}

void GlobeItem::mouseReleaseEvent(QMouseEvent *e)
{
    const bool wasDrag = m_dragging;
    m_pressed = m_dragging = false;
    emit hoverChanged();
    // Klick: Ort unter der Maus; Ziehen: Ort unter dem Fadenkreuz (wie radio.garden)
    const int p = wasDrag ? m_center : placeAt(e->position().x(), e->position().y());
    if (p >= 0) {
        setSelectedPlace(p);
        emit placeClicked(p);
        if (!wasDrag) flyToPlace(p);
    }
}

void GlobeItem::hoverMoveEvent(QHoverEvent *e)
{
    m_hoverPos = e->position();
    const int h = placeAt(m_hoverPos.x(), m_hoverPos.y());
    if (h != m_hovered) { m_hovered = h; update(); }
    emit hoverChanged();
}

void GlobeItem::hoverLeaveEvent(QHoverEvent *)
{
    if (m_hovered >= 0) { m_hovered = -1; update(); emit hoverChanged(); }
}

void GlobeItem::wheelEvent(QWheelEvent *e)
{
    m_fly.stop();
    const double steps = e->angleDelta().y() / 120.0;
    if (steps != 0) setZoom(m_zoom * std::pow(1.25, steps));
    e->accept();
}

} // namespace uve::radio
