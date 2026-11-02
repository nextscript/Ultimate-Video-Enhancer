#pragma once
// UVE-LUT-Profile (Schema "gvf-lut-profile", ver 1): 4×5-Farbmatrix wie SVG feColorMatrix.
// Quelle: uve.user.js Z. 7789 ff. (Import), Z. 10809 ff. (Anwendung im WebGL-Shader):
//   R' = m0·R + m1·G + m2·B + m4,  G' = m5·R + m6·G + m7·B + m9,  B' = m10·R + m11·G + m12·B + m14
// (Alpha-Spalte m3/m8/m13 und Alpha-Zeile werden im GPU-Pfad nicht verwendet), angewendet zuletzt im sRGB-Raum.
#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace uve::fx {

struct LutProfile
{
    QString name;
    QString group;
    QVector<double> matrix; // 20 Werte
    QString key() const { return group + QStringLiteral("||") + name; }
    bool isValid() const { return !name.isEmpty() && matrix.size() == 20; }
};

struct LutProfileResult { bool ok = false; QString error; LutProfile profile; };

// Akzeptiert {schema:"gvf-lut-profile", name, group?, matrix4x5} oder {name, matrix4x5, group?} (wie UVE)
LutProfileResult parseLutProfile(const QJsonObject &o, const QString &fallbackName = {});
LutProfileResult parseLutProfileFile(const QByteArray &json, const QString &fileName);
QJsonObject lutProfileToJson(const LutProfile &p);

// Vorschau für die LUT-Galerie: identische Formel wie im Shader (Matrix, Begrenzung, Mischung mit strength)
QImage applyLutMatrix(const QImage &src, const QVector<double> &matrix, double strength = 1.0);

// Gruppen-/Namensaufteilung aus Dateinamen der UVE-Pakete: "Gruppe__Name.json"
void splitPackageFileName(const QString &fileName, QString *group, QString *name);

} // namespace uve::fx
