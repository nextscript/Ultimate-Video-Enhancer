#pragma once
// Drehbarer 3D-Globus (orthografische Projektion) für die Senderkarte. Eigene Zeichnung mit QPainter:
// Ozean, Landflächen und Grenzen (Natural Earth 1:110 Mio., gemeinfrei, im Plugin eingebaut), Gradnetz,
// leuchtende Punkte je Ort. Ziehen dreht, Mausrad zoomt, Klick oder Loslassen über dem Fadenkreuz wählt einen Ort.
// Kein Kartendienst, kein Netz für die Karte selbst.
#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QVariantAnimation>
#include <QVector>

namespace uve::radio {

class GeoIndex;

class GlobeItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(double centerLat READ centerLat WRITE setCenterLat NOTIFY viewChanged)
    Q_PROPERTY(double centerLon READ centerLon WRITE setCenterLon NOTIFY viewChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY viewChanged)
    Q_PROPERTY(int selectedPlace READ selectedPlace WRITE setSelectedPlace NOTIFY selectionChanged)
    Q_PROPERTY(int playingPlace READ playingPlace WRITE setPlayingPlace NOTIFY selectionChanged)
    Q_PROPERTY(int hoveredPlace READ hoveredPlace NOTIFY hoverChanged)
    Q_PROPERTY(int centerPlace READ centerPlace NOTIFY hoverChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY hoverChanged)
    Q_PROPERTY(QPointF hoverPos READ hoverPos NOTIFY hoverChanged)
    Q_PROPERTY(QColor oceanColor MEMBER m_ocean WRITE setOceanColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor landColor MEMBER m_land WRITE setLandColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor borderColor MEMBER m_border WRITE setBorderColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor dotColor MEMBER m_dot WRITE setDotColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor accentColor MEMBER m_accent WRITE setAccentColor NOTIFY colorsChanged)
public:
    explicit GlobeItem(QQuickItem *parent = nullptr);

    void setIndex(GeoIndex *index);

    double centerLat() const { return m_lat; }
    double centerLon() const { return m_lon; }
    double zoom() const { return m_zoom; }
    void setCenterLat(double v);
    void setCenterLon(double v);
    void setZoom(double v);
    int selectedPlace() const { return m_selected; }
    void setSelectedPlace(int p);
    int playingPlace() const { return m_playing; }
    void setPlayingPlace(int p);
    int hoveredPlace() const { return m_hovered; }
    int centerPlace() const { return m_center; }
    bool dragging() const { return m_dragging; }
    QPointF hoverPos() const { return m_hoverPos; }
    void setOceanColor(const QColor &c) { m_ocean = c; emit colorsChanged(); update(); }
    void setLandColor(const QColor &c) { m_land = c; emit colorsChanged(); update(); }
    void setBorderColor(const QColor &c) { m_border = c; emit colorsChanged(); update(); }
    void setDotColor(const QColor &c) { m_dot = c; emit colorsChanged(); update(); }
    void setAccentColor(const QColor &c) { m_accent = c; emit colorsChanged(); update(); }

    // Sanft zu einem Ort fliegen (zoom ≤ 0: Zoom behalten)
    Q_INVOKABLE void flyTo(double lat, double lon, double zoom = 0);
    Q_INVOKABLE void flyToPlace(int place, double zoom = 0);
    Q_INVOKABLE void zoomBy(double factor);
    // Bildschirmpunkt → Ort (−1: keiner in der Nähe); für Tests
    Q_INVOKABLE int placeAt(double x, double y, double maxDistance = 14) const;
    Q_INVOKABLE QPointF placeScreenPos(int place) const;

    void paint(QPainter *painter) override;

    struct Vec { float x, y, z; };

signals:
    void viewChanged();
    void selectionChanged();
    void hoverChanged();
    void colorsChanged();
    void placeClicked(int place);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void hoverMoveEvent(QHoverEvent *e) override;
    void hoverLeaveEvent(QHoverEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    struct Proj { double x, y, z; };   // Bildschirm-x/y und Tiefe (z > 0 = Vorderseite)
    Proj project(double X, double Y, double Z) const;
    double radius() const;
    void updateCenterPlace();
    static const QVector<QVector<Vec>> &land();
    static const QVector<QVector<Vec>> &borders();

    QPointer<GeoIndex> m_index;
    double m_lat = 30, m_lon = 10, m_zoom = 1;
    int m_selected = -1, m_playing = -1, m_hovered = -1, m_center = -1;
    bool m_dragging = false;
    bool m_pressed = false;
    QPointF m_pressPos, m_lastPos, m_hoverPos;
    QVariantAnimation m_fly;
    double m_flyFrom[3]{}, m_flyTo[3]{};
    QColor m_ocean{"#0d1b2a"}, m_land{"#1d3a2c"}, m_border{"#3b5a48"}, m_dot{"#3ecf6e"}, m_accent{"#ff8a1f"};
};

} // namespace uve::radio
