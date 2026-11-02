#include "MiniZip.h"
#include <QCoreApplication>

#include <array>

namespace uve::fx {

namespace {

struct InflateError {};

struct Huffman
{
    std::array<short, 16> count{};
    std::array<short, 320> symbol{};
};

class Inflater
{
public:
    Inflater(const QByteArray &in, qint64 maxOut) : m_in(reinterpret_cast<const uchar *>(in.constData())), m_len(in.size()), m_max(maxOut) {}

    QByteArray run()
    {
        int last = 0;
        do {
            last = bits(1);
            const int type = bits(2);
            if (type == 0) stored();
            else if (type == 1) fixed();
            else if (type == 2) dynamic();
            else throw InflateError{};
        } while (!last);
        return m_out;
    }

private:
    int bits(int need)
    {
        quint32 val = m_bitbuf;
        while (m_bitcnt < need) {
            if (m_pos >= m_len) throw InflateError{};
            val |= quint32(m_in[m_pos++]) << m_bitcnt;
            m_bitcnt += 8;
        }
        m_bitbuf = val >> need;
        m_bitcnt -= need;
        return int(val & ((1u << need) - 1));
    }

    void put(char c)
    {
        if (m_out.size() >= m_max) throw InflateError{};
        m_out.append(c);
    }

    void stored()
    {
        m_bitbuf = 0; m_bitcnt = 0;
        if (m_pos + 4 > m_len) throw InflateError{};
        const int len = m_in[m_pos] | (m_in[m_pos + 1] << 8);
        const int nlen = m_in[m_pos + 2] | (m_in[m_pos + 3] << 8);
        m_pos += 4;
        if (len != (~nlen & 0xffff) || m_pos + len > m_len) throw InflateError{};
        for (int i = 0; i < len; ++i) put(char(m_in[m_pos++]));
    }

    // Kanonische Huffman-Tabelle; Rückgabe <0 = überbelegt (Fehler), >0 = unvollständig, 0 = vollständig
    static int construct(Huffman &h, const short *lengths, int n)
    {
        h.count.fill(0);
        for (int s = 0; s < n; ++s) h.count[lengths[s]]++;
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) return left;
        }
        std::array<short, 16> offs{};
        for (int len = 1; len < 15; ++len) offs[len + 1] = short(offs[len] + h.count[len]);
        for (int s = 0; s < n; ++s)
            if (lengths[s] != 0) h.symbol[offs[lengths[s]]++] = short(s);
        return left;
    }

    int decode(const Huffman &h)
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= bits(1);
            const int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        throw InflateError{};
    }

    void codes(const Huffman &lencode, const Huffman &distcode)
    {
        static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = decode(lencode);
            if (sym < 256) { put(char(sym)); continue; }
            if (sym == 256) return;
            sym -= 257;
            if (sym >= 29) throw InflateError{};
            const int len = lbase[sym] + bits(lext[sym]);
            sym = decode(distcode);
            if (sym >= 30) throw InflateError{};
            const qint64 dist = dbase[sym] + bits(dext[sym]);
            if (dist > m_out.size()) throw InflateError{};
            for (int i = 0; i < len; ++i) put(m_out.at(m_out.size() - dist));
        }
    }

    void fixed()
    {
        static Huffman lencode, distcode;
        static bool built = false;
        if (!built) {
            short lengths[288];
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < 288; ++s) lengths[s] = 8;
            construct(lencode, lengths, 288);
            for (s = 0; s < 30; ++s) lengths[s] = 5;
            construct(distcode, lengths, 30);
            built = true;
        }
        codes(lencode, distcode);
    }

    void dynamic()
    {
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30) throw InflateError{};
        short lengths[320] = {};
        int index = 0;
        for (; index < ncode; ++index) lengths[order[index]] = short(bits(3));
        for (; index < 19; ++index) lengths[order[index]] = 0;
        Huffman lencode, distcode;
        if (construct(lencode, lengths, 19) != 0) throw InflateError{};
        index = 0;
        while (index < nlen + ndist) {
            int symbol = decode(lencode);
            if (symbol < 16) { lengths[index++] = short(symbol); continue; }
            short len = 0;
            if (symbol == 16) {
                if (index == 0) throw InflateError{};
                len = lengths[index - 1];
                symbol = 3 + bits(2);
            } else if (symbol == 17) {
                symbol = 3 + bits(3);
            } else {
                symbol = 11 + bits(7);
            }
            if (index + symbol > nlen + ndist) throw InflateError{};
            while (symbol--) lengths[index++] = len;
        }
        if (lengths[256] == 0) throw InflateError{};
        int err = construct(lencode, lengths, nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) throw InflateError{};
        err = construct(distcode, lengths + nlen, ndist);
        if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) throw InflateError{};
        codes(lencode, distcode);
    }

    const uchar *m_in;
    qint64 m_len;
    qint64 m_pos = 0;
    qint64 m_max;
    quint32 m_bitbuf = 0;
    int m_bitcnt = 0;
    QByteArray m_out;
};

quint32 u16(const QByteArray &b, qint64 o) { return quint8(b[o]) | (quint8(b[o + 1]) << 8); }
quint32 u32(const QByteArray &b, qint64 o) { return u16(b, o) | (u16(b, o + 2) << 16); }

} // namespace

bool inflateRaw(const QByteArray &in, qint64 maxOut, QByteArray *out)
{
    try {
        Inflater inf(in, maxOut);
        *out = inf.run();
        return true;
    } catch (const InflateError &) {
        return false;
    }
}

quint32 crc32(const QByteArray &data)
{
    static const auto table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    quint32 c = 0xFFFFFFFFu;
    for (const char ch : data) c = table[(c ^ quint8(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

ZipResult readZip(const QByteArray &zip, const std::function<bool(const QString &)> &want, qint64 maxEntrySize, qint64 maxTotal, int maxEntries)
{
    ZipResult r;
    const qint64 n = zip.size();
    // End of Central Directory suchen (max. 64 KiB Kommentar)
    qint64 eocd = -1;
    for (qint64 i = n - 22; i >= 0 && i >= n - 22 - 65535; --i)
        if (u32(zip, i) == 0x06054b50u) { eocd = i; break; }
    if (eocd < 0) { r.error = QCoreApplication::translate("uve", "keine ZIP-Datei (Verzeichnisende fehlt)"); return r; }
    const int count = int(u16(zip, eocd + 10));
    const qint64 cdSize = u32(zip, eocd + 12), cdOff = u32(zip, eocd + 16);
    if (count > maxEntries) { r.error = QCoreApplication::translate("uve", "zu viele Einträge"); return r; }
    if (cdOff + cdSize > eocd) { r.error = QCoreApplication::translate("uve", "ZIP-Verzeichnis beschädigt"); return r; }
    qint64 p = cdOff, total = 0;
    for (int i = 0; i < count; ++i) {
        if (p + 46 > eocd || u32(zip, p) != 0x02014b50u) { r.error = QCoreApplication::translate("uve", "ZIP-Verzeichnis beschädigt"); return r; }
        const quint32 flags = u16(zip, p + 8), method = u16(zip, p + 10), crc = u32(zip, p + 16);
        const qint64 csize = u32(zip, p + 20), usize = u32(zip, p + 24);
        const int nameLen = int(u16(zip, p + 28)), extraLen = int(u16(zip, p + 30)), commentLen = int(u16(zip, p + 32));
        const qint64 local = u32(zip, p + 42);
        if (p + 46 + nameLen > eocd) { r.error = QCoreApplication::translate("uve", "ZIP-Verzeichnis beschädigt"); return r; }
        const QByteArray rawName = zip.mid(p + 46, nameLen);
        const QString name = (flags & 0x800) ? QString::fromUtf8(rawName) : QString::fromLatin1(rawName);
        p += 46 + nameLen + extraLen + commentLen;
        if (name.endsWith('/') || !want(name)) continue;
        if (flags & 1) continue;                                           // verschlüsselt → überspringen
        if (csize == 0xFFFFFFFF || usize == 0xFFFFFFFF) { r.error = QCoreApplication::translate("uve", "ZIP64 wird nicht unterstützt"); return r; }
        if (usize > maxEntrySize) { r.error = QCoreApplication::translate("uve", "Eintrag zu groß: ") + name; return r; }
        total += usize;
        if (total > maxTotal) { r.error = QCoreApplication::translate("uve", "Paket entpackt zu groß"); return r; }
        if (local + 30 > n || u32(zip, local) != 0x04034b50u) { r.error = QCoreApplication::translate("uve", "lokaler Header beschädigt: ") + name; return r; }
        const qint64 dataOff = local + 30 + u16(zip, local + 26) + u16(zip, local + 28);
        if (dataOff + csize > n) { r.error = QStringLiteral("Daten abgeschnitten: ") + name; return r; }
        const QByteArray comp = zip.mid(dataOff, csize);
        QByteArray data;
        if (method == 0) data = comp;
        else if (method == 8) { if (!inflateRaw(comp, maxEntrySize, &data)) { r.error = QStringLiteral("Deflate-Daten fehlerhaft: ") + name; return r; } }
        else continue;                                                     // andere Methoden ignorieren
        if (data.size() != usize || crc32(data) != crc) { r.error = QCoreApplication::translate("uve", "Prüfsumme falsch: ") + name; return r; }
        r.entries << ZipEntry{name, data};
    }
    r.ok = true;
    return r;
}

} // namespace uve::fx
