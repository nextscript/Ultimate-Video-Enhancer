#include "CubeLut.h"
#include <QCoreApplication>

#include <QFile>
#include <QList>
#include <cmath>
#include <cstring>

namespace uve::fx {

LutLoadResult loadCubeFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {false, QCoreApplication::translate("uve", "Datei nicht lesbar: %1").arg(path), {}};
    if (f.size() > 64 * 1024 * 1024) return {false, QCoreApplication::translate("uve", "LUT-Datei zu groß"), {}};
    return parseCube(f.readAll());
}

LutLoadResult parseCube(const QByteArray &text)
{
    LutLoadResult r;
    CubeLut &lut = r.lut;
    int line = 0;
    for (QByteArray raw : text.split('\n')) {
        ++line;
        raw = raw.trimmed();
        if (raw.isEmpty() || raw.startsWith('#')) continue;
        const QList<QByteArray> tok = raw.simplified().split(' ');
        const QByteArray key = tok.first().toUpper();
        if (key == "TITLE") { lut.title = QString::fromUtf8(raw.mid(5)).trimmed().remove('"'); continue; }
        if (key == "LUT_1D_SIZE") { r.error = QCoreApplication::translate("uve", "1D-LUTs werden nicht unterstützt (nur LUT_3D_SIZE)"); return r; }
        if (key == "LUT_3D_SIZE") {
            bool ok = false;
            lut.size = tok.value(1).toInt(&ok);
            if (!ok || lut.size < 2 || lut.size > 129) { r.error = QCoreApplication::translate("uve", "Ungültige LUT_3D_SIZE in Zeile %1").arg(line); return r; }
            lut.rgb.reserve(lut.size * lut.size * lut.size * 3);
            continue;
        }
        if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX") {
            if (tok.size() != 4) { r.error = QCoreApplication::translate("uve", "Ungültiges %1 in Zeile %2").arg(QString::fromLatin1(key)).arg(line); return r; }
            float *dst = key == "DOMAIN_MIN" ? lut.domainMin : lut.domainMax;
            for (int i = 0; i < 3; ++i) dst[i] = tok[i + 1].toFloat();
            continue;
        }
        if (key.at(0) >= 'A' && key.at(0) <= 'Z') continue; // weitere Schlüsselwörter (z. B. LUT_IN_VIDEO_RANGE) ignorieren
        if (tok.size() != 3) { r.error = QCoreApplication::translate("uve", "Zeile %1: erwartet 3 Werte").arg(line); return r; }
        for (const auto &t : tok) {
            bool ok = false;
            const float v = t.toFloat(&ok);
            if (!ok || !std::isfinite(v)) { r.error = QCoreApplication::translate("uve", "Zeile %1: ungültige Zahl '%2'").arg(line).arg(QString::fromLatin1(t)); return r; }
            lut.rgb << v;
        }
    }
    if (lut.size == 0) { r.error = QStringLiteral("LUT_3D_SIZE fehlt"); return r; }
    const int expected = lut.size * lut.size * lut.size * 3;
    if (lut.rgb.size() != expected) {
        r.error = QStringLiteral("Falsche Anzahl Werte: %1 statt %2").arg(lut.rgb.size() / 3).arg(expected / 3);
        return r;
    }
    for (int i = 0; i < 3; ++i) {
        if (!(lut.domainMax[i] > lut.domainMin[i])) { r.error = QCoreApplication::translate("uve", "DOMAIN_MAX muss größer als DOMAIN_MIN sein"); return r; }
    }
    r.ok = true;
    return r;
}

quint16 floatToHalf(float f)
{
    quint32 x;
    std::memcpy(&x, &f, 4);
    const quint32 sign = (x >> 16) & 0x8000;
    int exp = int((x >> 23) & 0xFF) - 127 + 15;
    quint32 mant = x & 0x7FFFFF;
    if (exp <= 0) {
        if (exp < -10) return quint16(sign);
        mant |= 0x800000;
        const int shift = 14 - exp;
        quint32 half = mant >> shift;
        if ((mant >> (shift - 1)) & 1) ++half; // runden
        return quint16(sign | half);
    }
    if (exp >= 31) return quint16(sign | 0x7C00); // Überlauf → ∞
    quint32 half = sign | (quint32(exp) << 10) | (mant >> 13);
    if (mant & 0x1000) ++half; // runden
    return quint16(half);
}

} // namespace uve::fx
