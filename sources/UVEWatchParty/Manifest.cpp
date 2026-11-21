#include "Manifest.h"

#include <QCryptographicHash>
#include <QFile>
#include <QPointer>
#include <QThread>
#include <QtEndian>

namespace uve::watchparty {

static const QByteArray kMagic("UVEWPM1\0", 8);
static constexpr int kMaxBlocks = 32768;

qint64 Manifest::blockLength(int index) const
{
    if (index < 0 || index >= blockCount()) return 0;
    const quint64 start = quint64(index) * blockSize;
    return qint64(qMin<quint64>(blockSize, size - start));
}

bool Manifest::verifyBlock(int index, const QByteArray &data) const
{
    if (index < 0 || index >= blockCount() || data.size() != blockLength(index)) return false;
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256) == hashes.at(index);
}

quint32 Manifest::chooseBlockSize(quint64 size)
{
    quint32 bs = 1u << 20;
    while (size / bs >= quint64(kMaxBlocks) && bs < (16u << 20)) bs <<= 1;
    return bs;
}

Manifest Manifest::build(quint64 size, quint32 blockSize, const QList<QByteArray> &hashes)
{
    Manifest m;
    m.size = size;
    m.blockSize = blockSize;
    m.hashes = hashes;
    QByteArray b = kMagic;
    char buf[8];
    qToBigEndian<quint64>(size, buf); b.append(buf, 8);
    qToBigEndian<quint32>(blockSize, buf); b.append(buf, 4);
    qToBigEndian<quint32>(quint32(hashes.size()), buf); b.append(buf, 4);
    for (const auto &h : hashes) b += h;
    m.bytes = b;
    m.digest = QCryptographicHash::hash(b, QCryptographicHash::Sha256);
    return m;
}

std::optional<Manifest> Manifest::parse(const QByteArray &bytes)
{
    if (bytes.size() < 24 || !bytes.startsWith(kMagic)) return std::nullopt;
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData());
    const quint64 size = qFromBigEndian<quint64>(p + 8);
    const quint32 bs = qFromBigEndian<quint32>(p + 16);
    const quint32 count = qFromBigEndian<quint32>(p + 20);
    if (size == 0 || bs < 16384 || count > quint32(kMaxBlocks) * 2 || count != quint32((size + bs - 1) / bs)) return std::nullopt;
    if (bytes.size() != 24 + qsizetype(count) * 32) return std::nullopt;
    QList<QByteArray> hashes;
    hashes.reserve(count);
    for (quint32 i = 0; i < count; ++i) hashes << bytes.mid(24 + qsizetype(i) * 32, 32);
    return build(size, bs, hashes);
}

ManifestBuilder::ManifestBuilder(QObject *parent) : QObject(parent) {}

ManifestBuilder::~ManifestBuilder()
{
    cancel();
    if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}

void ManifestBuilder::cancel()
{
    if (m_cancel) m_cancel->store(true);
}

void ManifestBuilder::start(const QString &path)
{
    cancel();
    if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; m_thread = nullptr; }
    auto flag = std::make_shared<std::atomic<bool>>(false);
    m_cancel = flag;
    QPointer<ManifestBuilder> self(this);
    m_thread = QThread::create([self, flag, path] {
        auto post = [self](auto fn) { if (self) QMetaObject::invokeMethod(self.data(), fn, Qt::QueuedConnection); };
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) { post([self, path, e = f.errorString()] { if (self) emit self->failed(path, e); }); return; }
        const quint64 size = quint64(f.size());
        if (size == 0) { post([self, path] { if (self) emit self->failed(path, QStringLiteral("empty file")); }); return; }
        const quint32 bs = Manifest::chooseBlockSize(size);
        QList<QByteArray> hashes;
        int lastPct = -1;
        for (quint64 off = 0; off < size; off += bs) {
            if (flag->load()) return;
            const QByteArray data = f.read(qint64(qMin<quint64>(bs, size - off)));
            if (quint64(data.size()) != qMin<quint64>(bs, size - off)) {
                post([self, path, e = f.errorString()] { if (self) emit self->failed(path, e); });
                return;
            }
            hashes << QCryptographicHash::hash(data, QCryptographicHash::Sha256);
            const int pct = int((off + data.size()) * 100 / size);
            if (pct != lastPct) { lastPct = pct; post([self, pct] { if (self) emit self->progress(pct); }); }
        }
        const Manifest m = Manifest::build(size, bs, hashes);
        post([self, path, m, flag] { if (self && !flag->load()) emit self->finished(path, m); });
    });
    QThread *t = m_thread;
    connect(t, &QThread::finished, this, [this, t] {
        if (m_thread == t) m_thread = nullptr;
        t->deleteLater();
    });
    m_thread->start(QThread::LowPriority);
}

} // namespace uve::watchparty
