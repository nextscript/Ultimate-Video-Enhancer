#pragma once
// Minimaler, defensiver ZIP-Leser für UVE-LUT-Pakete (LUTsProfiles_v*.zip).
// - liest nur in den Speicher (kein Entpacken auf die Platte → kein Pfad-Traversal)
// - Methoden "stored" (0) und "deflate" (8), kein ZIP64, keine Verschlüsselung
// - CRC-32 jedes Eintrags wird geprüft, Größengrenzen pro Eintrag und gesamt
#include <QByteArray>
#include <QList>
#include <QString>
#include <functional>

namespace uve::fx {

struct ZipEntry { QString name; QByteArray data; };
struct ZipResult { bool ok = false; QString error; QList<ZipEntry> entries; };

ZipResult readZip(const QByteArray &zip, const std::function<bool(const QString &name)> &want,
                  qint64 maxEntrySize = 1 << 20, qint64 maxTotal = 64 << 20, int maxEntries = 4096);

// Roh-Deflate (RFC 1951) dekodieren; false bei fehlerhaften Daten oder Überschreitung von maxOut
bool inflateRaw(const QByteArray &in, qint64 maxOut, QByteArray *out);
quint32 crc32(const QByteArray &data);

} // namespace uve::fx
