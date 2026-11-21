#pragma once
// Size- and time-bounded cache for received, verified blocks: one slot file in a temporary folder of the plugin
// data directory. Least recently used blocks and blocks older than the maximum age are dropped; clear() deletes
// the file (end of a share, leaving the room, lock of the session).
#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QList>
#include <QString>
#include <memory>

class QTemporaryDir;

namespace uve::watchparty {

class BlockCache
{
public:
    BlockCache();
    ~BlockCache();

    bool open(const QString &baseDir, qint64 capacityBytes, quint32 blockSize, qint64 maxAgeMs);
    void clear();
    bool isOpen() const { return m_file.isOpen(); }

    bool has(int index) const { return m_index.contains(index); }
    // Stores a block; blocks that had to make room are appended to *evicted
    bool put(int index, const QByteArray &data, qint64 nowMs, QList<int> *evicted);
    QByteArray get(int index, qint64 nowMs);
    QList<int> expire(qint64 nowMs);
    int count() const { return int(m_index.size()); }
    int capacityBlocks() const { return m_slots; }
    QString path() const { return m_file.fileName(); }

private:
    struct Entry { int slot = 0; qint64 length = 0; qint64 stored = 0; qint64 used = 0; };
    int evictOne(QList<int> *evicted);

    std::unique_ptr<QTemporaryDir> m_dir;
    QFile m_file;
    QHash<int, Entry> m_index;
    QList<int> m_free;
    int m_slots = 0;
    quint32 m_blockSize = 0;
    qint64 m_maxAge = 0;
};

} // namespace uve::watchparty
