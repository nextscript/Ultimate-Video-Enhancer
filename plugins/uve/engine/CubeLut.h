#pragma once
#include <QString>
#include <QVector>

namespace uve::fx {

// Adobe/IRIDAS .cube-3D-LUT (LUT_3D_SIZE, DOMAIN_MIN/MAX, Werte rot-schnell geordnet)
struct CubeLut
{
    QString title;
    int size = 0;
    float domainMin[3]{0, 0, 0};
    float domainMax[3]{1, 1, 1};
    QVector<float> rgb; // size³ × 3

    bool isValid() const { return size >= 2 && rgb.size() == size * size * size * 3; }
};

struct LutLoadResult { bool ok = false; QString error; CubeLut lut; };

LutLoadResult loadCubeFile(const QString &path);
LutLoadResult parseCube(const QByteArray &text);

// Halbgenaue Gleitkommazahl (IEEE 754 binary16), für //!TEXTURE … rgba16f
quint16 floatToHalf(float f);

} // namespace uve::fx
