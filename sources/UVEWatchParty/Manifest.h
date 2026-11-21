#pragma once
// Manifest of a shared file for the private swarm: file size, block size and the SHA-256 hash of every block.
// Its own SHA-256 ("manifest identity") is confirmed through the room control for the current media generation;
// every received block is checked against it before it is played or passed on.
//   binary: "UVEWPM1\0" | u64 size | u32 blockSize | u32 count | count × 32-byte SHA-256   (big endian)
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include <optional>

class QThread;

namespace uve::watchparty {

struct Manifest {
    quint64 size = 0;
    quint32 blockSize = 0;
    QList<QByteArray> hashes;
    QByteArray bytes;     // serialized form
    QByteArray digest;    // SHA-256 of `bytes` (32 bytes)

    int blockCount() const { return int(hashes.size()); }
    qint64 blockLength(int index) const;
    bool verifyBlock(int index, const QByteArray &data) const;
    QString digestHex() const { return QString::fromLatin1(digest.toHex()); }

    // 1 MiB blocks; larger for very big files so a manifest stays ≤ 32768 blocks (≤ 1 MiB)
    static quint32 chooseBlockSize(quint64 size);
    static std::optional<Manifest> parse(const QByteArray &bytes);
    static Manifest build(quint64 size, quint32 blockSize, const QList<QByteArray> &hashes);
};

// Hashes a local file in a background thread (progress 0–100), cancelable
class ManifestBuilder : public QObject
{
    Q_OBJECT
public:
    explicit ManifestBuilder(QObject *parent = nullptr);
    ~ManifestBuilder() override;
    void start(const QString &path);
    void cancel();
    bool running() const { return m_thread != nullptr; }

signals:
    void progress(int percent);
    void finished(const QString &path, const uve::watchparty::Manifest &manifest);
    void failed(const QString &path, const QString &reason);

private:
    QThread *m_thread = nullptr;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};

} // namespace uve::watchparty

Q_DECLARE_METATYPE(uve::watchparty::Manifest)
