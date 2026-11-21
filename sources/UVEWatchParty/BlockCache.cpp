#include "BlockCache.h"

#include <QDir>
#include <QTemporaryDir>

namespace uve::watchparty {

BlockCache::BlockCache() = default;

BlockCache::~BlockCache() { clear(); }

bool BlockCache::open(const QString &baseDir, qint64 capacityBytes, quint32 blockSize, qint64 maxAgeMs)
{
    clear();
    QDir().mkpath(baseDir);
    m_dir = std::make_unique<QTemporaryDir>(QDir(baseDir).filePath(QStringLiteral("swarm-XXXXXX")));
    if (!m_dir->isValid()) { m_dir.reset(); return false; }
    m_file.setFileName(QDir(m_dir->path()).filePath(QStringLiteral("blocks.bin")));
    if (!m_file.open(QIODevice::ReadWrite | QIODevice::Truncate)) { m_dir.reset(); return false; }
    m_blockSize = blockSize;
    m_slots = int(qMax<qint64>(8, capacityBytes / blockSize));
    m_maxAge = maxAgeMs;
    m_free.clear();
    for (int i = m_slots - 1; i >= 0; --i) m_free << i;
    return true;
}

void BlockCache::clear()
{
    m_index.clear();
    m_free.clear();
    m_slots = 0;
    if (m_file.isOpen()) m_file.close();
    if (m_dir) { m_dir->remove(); m_dir.reset(); }
}

int BlockCache::evictOne(QList<int> *evicted)
{
    int victim = -1;
    qint64 oldest = 0;
    for (auto it = m_index.cbegin(); it != m_index.cend(); ++it) {
        if (victim < 0 || it->used < oldest) { victim = it.key(); oldest = it->used; }
    }
    if (victim < 0) return -1;
    const int slot = m_index.take(victim).slot;
    if (evicted) *evicted << victim;
    return slot;
}

bool BlockCache::put(int index, const QByteArray &data, qint64 nowMs, QList<int> *evicted)
{
    if (!m_file.isOpen() || data.size() > qsizetype(m_blockSize)) return false;
    if (m_index.contains(index)) { m_index[index].used = nowMs; return true; }
    int slot = m_free.isEmpty() ? evictOne(evicted) : m_free.takeLast();
    if (slot < 0) return false;
    if (!m_file.seek(qint64(slot) * m_blockSize) || m_file.write(data) != data.size()) { m_free << slot; return false; }
    m_index.insert(index, {slot, data.size(), nowMs, nowMs});
    return true;
}

QByteArray BlockCache::get(int index, qint64 nowMs)
{
    auto it = m_index.find(index);
    if (it == m_index.end() || !m_file.seek(qint64(it->slot) * m_blockSize)) return {};
    it->used = nowMs;
    QByteArray d = m_file.read(it->length);
    return d.size() == it->length ? d : QByteArray();
}

QList<int> BlockCache::expire(qint64 nowMs)
{
    QList<int> out;
    if (m_maxAge <= 0) return out;
    for (auto it = m_index.begin(); it != m_index.end();) {
        if (nowMs - it->used > m_maxAge) { out << it.key(); m_free << it->slot; it = m_index.erase(it); }
        else ++it;
    }
    return out;
}

} // namespace uve::watchparty
