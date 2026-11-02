#include "LutProfile.h"
#include <QCoreApplication>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

namespace uve::fx {

void splitPackageFileName(const QString &fileName, QString *group, QString *name)
{
    const QString base = QFileInfo(fileName).completeBaseName();
    const int sep = base.indexOf(QStringLiteral("__"));
    if (group) *group = sep > 0 ? base.left(sep) : QString();
    if (name) *name = sep > 0 ? base.mid(sep + 2) : base;
}

LutProfileResult parseLutProfile(const QJsonObject &o, const QString &fallbackName)
{
    LutProfileResult r;
    const QString schema = o.value("schema").toString();
    if (!schema.isEmpty() && schema != QLatin1String("gvf-lut-profile")) {
        r.error = QStringLiteral("Unbekanntes Schema \"%1\"").arg(schema);
        return r;
    }
    const auto arr = o.value("matrix4x5").toArray();
    if (arr.size() != 20) { r.error = QCoreApplication::translate("uve", "matrix4x5 muss 20 Zahlen enthalten"); return r; }
    for (const auto &v : arr) {
        if (!v.isDouble() || !std::isfinite(v.toDouble())) { r.error = QCoreApplication::translate("uve", "matrix4x5 enthält ungültige Werte"); return r; }
        // Plausibilitätsgrenze: Farbmatrizen liegen in einem kleinen Wertebereich
        if (std::fabs(v.toDouble()) > 16) { r.error = QCoreApplication::translate("uve", "matrix4x5-Wert außerhalb des zulässigen Bereichs"); return r; }
        r.profile.matrix << v.toDouble();
    }
    r.profile.name = o.value("name").toString(fallbackName).trimmed();
    r.profile.group = o.value("group").toString().trimmed();
    if (r.profile.name.isEmpty()) { r.error = QStringLiteral("Name fehlt"); return r; }
    r.ok = true;
    return r;
}

LutProfileResult parseLutProfileFile(const QByteArray &json, const QString &fileName)
{
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {false, QCoreApplication::translate("uve", "kein gültiges JSON"), {}};
    QString group, name;
    splitPackageFileName(fileName, &group, &name);
    auto r = parseLutProfile(doc.object(), name);
    if (r.ok && r.profile.group.isEmpty()) r.profile.group = group;
    return r;
}

QImage applyLutMatrix(const QImage &src, const QVector<double> &m, double strength)
{
    if (src.isNull() || m.size() != 20) return src;
    QImage img = src.convertToFormat(QImage::Format_RGB32);
    const double st = std::fmin(std::fmax(strength, 0.0), 1.0);
    auto cl = [](double v) { return v < 0 ? 0.0 : (v > 1 ? 1.0 : v); };
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const double r = qRed(line[x]) / 255.0, g = qGreen(line[x]) / 255.0, b = qBlue(line[x]) / 255.0;
            const double nr = cl(m[0] * r + m[1] * g + m[2] * b + m[4]);
            const double ng = cl(m[5] * r + m[6] * g + m[7] * b + m[9]);
            const double nb = cl(m[10] * r + m[11] * g + m[12] * b + m[14]);
            line[x] = qRgb(int(std::lround((r + (nr - r) * st) * 255)), int(std::lround((g + (ng - g) * st) * 255)),
                           int(std::lround((b + (nb - b) * st) * 255)));
        }
    }
    return img;
}

QJsonObject lutProfileToJson(const LutProfile &p)
{
    QJsonArray m;
    for (double v : p.matrix) m.append(v);
    return {{"schema", "gvf-lut-profile"}, {"ver", 1}, {"name", p.name}, {"group", p.group}, {"matrix4x5", m}};
}

} // namespace uve::fx
