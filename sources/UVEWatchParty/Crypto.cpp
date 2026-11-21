#include "Crypto.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

#include <mbedtls/bignum.h>
#include <mbedtls/ecp.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

namespace uve::watchparty::crypto {

namespace {

int rng(void *, unsigned char *out, size_t len)
{
    for (size_t i = 0; i < len; ++i) out[i] = uchar(QRandomGenerator::system()->bounded(256));
    return 0;
}

QByteArray taggedHash(const QByteArray &tag, const QByteArray &msg)
{
    const QByteArray t = sha256(tag);
    return sha256(t + t + msg);
}

// RAII helpers for Mbed TLS structures
struct Mpi {
    mbedtls_mpi v;
    Mpi() { mbedtls_mpi_init(&v); }
    ~Mpi() { mbedtls_mpi_free(&v); }
    bool read(const QByteArray &b) { return mbedtls_mpi_read_binary(&v, reinterpret_cast<const uchar *>(b.constData()), size_t(b.size())) == 0; }
    QByteArray bytes32() const { QByteArray o(32, '\0'); mbedtls_mpi_write_binary(&v, reinterpret_cast<uchar *>(o.data()), 32); return o; }
};
struct Point {
    mbedtls_ecp_point p;
    Point() { mbedtls_ecp_point_init(&p); }
    ~Point() { mbedtls_ecp_point_free(&p); }
    bool evenY() const { return mbedtls_mpi_get_bit(&p.private_Y, 0) == 0; }
    QByteArray x() const { QByteArray o(32, '\0'); mbedtls_mpi_write_binary(&p.private_X, reinterpret_cast<uchar *>(o.data()), 32); return o; }
};
struct Group {
    mbedtls_ecp_group g;
    Group() { mbedtls_ecp_group_init(&g); mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256K1); }
    ~Group() { mbedtls_ecp_group_free(&g); }
};

bool mulG(Group &grp, Point &out, const Mpi &k)
{
    return mbedtls_ecp_mul(&grp.g, &out.p, &k.v, &grp.g.G, rng, nullptr) == 0;
}

// y² = x³ + 7 over p; returns the point with even y (BIP340 lift_x)
bool liftX(Group &grp, const QByteArray &x32, Point &out)
{
    Mpi x, c, y, e, t;
    if (!x.read(x32) || mbedtls_mpi_cmp_mpi(&x.v, &grp.g.P) >= 0) return false;
    mbedtls_mpi_mul_mpi(&c.v, &x.v, &x.v);
    mbedtls_mpi_mod_mpi(&c.v, &c.v, &grp.g.P);
    mbedtls_mpi_mul_mpi(&c.v, &c.v, &x.v);
    mbedtls_mpi_add_int(&c.v, &c.v, 7);
    mbedtls_mpi_mod_mpi(&c.v, &c.v, &grp.g.P);
    // y = c^((p+1)/4) mod p
    mbedtls_mpi_add_int(&e.v, &grp.g.P, 1);
    mbedtls_mpi_shift_r(&e.v, 2);
    if (mbedtls_mpi_exp_mod(&y.v, &c.v, &e.v, &grp.g.P, nullptr) != 0) return false;
    mbedtls_mpi_mul_mpi(&t.v, &y.v, &y.v);
    mbedtls_mpi_mod_mpi(&t.v, &t.v, &grp.g.P);
    if (mbedtls_mpi_cmp_mpi(&t.v, &c.v) != 0) return false;
    if (mbedtls_mpi_get_bit(&y.v, 0) == 1) mbedtls_mpi_sub_mpi(&y.v, &grp.g.P, &y.v);
    mbedtls_mpi_copy(&out.p.private_X, &x.v);
    mbedtls_mpi_copy(&out.p.private_Y, &y.v);
    mbedtls_mpi_lset(&out.p.private_Z, 1);
    return true;
}

} // namespace

QByteArray sha256(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QByteArray randomBytes(int n)
{
    QByteArray b(n, Qt::Uninitialized);
    rng(nullptr, reinterpret_cast<uchar *>(b.data()), size_t(n));
    return b;
}

QByteArray xOnlyPublicKey(const QByteArray &secret)
{
    Group grp;
    Mpi d;
    Point P;
    if (!d.read(secret) || !mulG(grp, P, d)) return {};
    return P.x();
}

KeyPair generateKeyPair()
{
    Group grp;
    for (;;) {
        KeyPair kp;
        kp.secret = randomBytes(32);
        Mpi d;
        d.read(kp.secret);
        if (mbedtls_mpi_cmp_int(&d.v, 0) == 0 || mbedtls_mpi_cmp_mpi(&d.v, &grp.g.N) >= 0) continue;
        kp.pub = xOnlyPublicKey(kp.secret);
        if (kp.pub.size() == 32) return kp;
    }
}

QByteArray schnorrSign(const QByteArray &secret, const QByteArray &msg32, const QByteArray &auxIn)
{
    if (secret.size() != 32 || msg32.size() != 32) return {};
    const QByteArray aux = auxIn.size() == 32 ? auxIn : randomBytes(32);
    Group grp;
    Mpi d;
    Point P;
    if (!d.read(secret) || !mulG(grp, P, d)) return {};
    if (!P.evenY()) mbedtls_mpi_sub_mpi(&d.v, &grp.g.N, &d.v);
    const QByteArray db = d.bytes32();
    const QByteArray ah = taggedHash("BIP0340/aux", aux);
    QByteArray t(32, '\0');
    for (int i = 0; i < 32; ++i) t[i] = char(db[i] ^ ah[i]);
    const QByteArray px = P.x();
    Mpi k;
    k.read(taggedHash("BIP0340/nonce", t + px + msg32));
    mbedtls_mpi_mod_mpi(&k.v, &k.v, &grp.g.N);
    if (mbedtls_mpi_cmp_int(&k.v, 0) == 0) return {};
    Point R;
    if (!mulG(grp, R, k)) return {};
    if (!R.evenY()) mbedtls_mpi_sub_mpi(&k.v, &grp.g.N, &k.v);
    const QByteArray rx = R.x();
    Mpi e, s;
    e.read(taggedHash("BIP0340/challenge", rx + px + msg32));
    mbedtls_mpi_mod_mpi(&e.v, &e.v, &grp.g.N);
    mbedtls_mpi_mul_mpi(&s.v, &e.v, &d.v);
    mbedtls_mpi_add_mpi(&s.v, &s.v, &k.v);
    mbedtls_mpi_mod_mpi(&s.v, &s.v, &grp.g.N);
    return rx + s.bytes32();
}

bool schnorrVerify(const QByteArray &pub, const QByteArray &msg32, const QByteArray &sig)
{
    if (pub.size() != 32 || msg32.size() != 32 || sig.size() != 64) return false;
    Group grp;
    Point P;
    if (!liftX(grp, pub, P)) return false;
    Mpi r, s, e, negE;
    r.read(sig.left(32));
    s.read(sig.mid(32));
    if (mbedtls_mpi_cmp_mpi(&r.v, &grp.g.P) >= 0 || mbedtls_mpi_cmp_mpi(&s.v, &grp.g.N) >= 0) return false;
    e.read(taggedHash("BIP0340/challenge", sig.left(32) + pub + msg32));
    mbedtls_mpi_mod_mpi(&e.v, &e.v, &grp.g.N);
    mbedtls_mpi_sub_mpi(&negE.v, &grp.g.N, &e.v);
    mbedtls_mpi_mod_mpi(&negE.v, &negE.v, &grp.g.N);
    // R = s·G − e·P
    Point R;
    if (mbedtls_ecp_muladd(&grp.g, &R.p, &s.v, &grp.g.G, &negE.v, &P.p) != 0) return false;
    if (mbedtls_ecp_is_zero(&R.p)) return false;
    return R.evenY() && R.x() == sig.left(32);
}

QByteArray deriveRoomKey(const QString &roomId, const QString &password)
{
    const QByteArray pw = password.normalized(QString::NormalizationForm_C).toUtf8();
    const QByteArray salt = "uvewp/v1/" + roomId.toUtf8();
    QByteArray key(32, '\0');
    mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, reinterpret_cast<const uchar *>(pw.constData()), size_t(pw.size()),
                                  reinterpret_cast<const uchar *>(salt.constData()), size_t(salt.size()), 100000, 32,
                                  reinterpret_cast<uchar *>(key.data()));
    return key;
}

QString roomTopic(const QString &roomId)
{
    return QString::fromLatin1(sha256("uvewp/v1/topic/" + roomId.toUtf8()).toHex());
}

QByteArray encrypt(const QByteArray &key, const QByteArray &plain)
{
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    QByteArray out;
    if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, reinterpret_cast<const uchar *>(key.constData()), 256) == 0) {
        const QByteArray nonce = randomBytes(12);
        QByteArray ct(plain.size(), Qt::Uninitialized), tag(16, Qt::Uninitialized);
        if (mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, size_t(plain.size()), reinterpret_cast<const uchar *>(nonce.constData()), 12,
                                      nullptr, 0, reinterpret_cast<const uchar *>(plain.constData()), reinterpret_cast<uchar *>(ct.data()),
                                      16, reinterpret_cast<uchar *>(tag.data())) == 0)
            out = (nonce + ct + tag).toBase64();
    }
    mbedtls_gcm_free(&ctx);
    return out;
}

bool decrypt(const QByteArray &key, const QByteArray &b64, QByteArray *plain)
{
    const QByteArray raw = QByteArray::fromBase64(b64);
    if (raw.size() < 28) return false;
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    bool ok = false;
    if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, reinterpret_cast<const uchar *>(key.constData()), 256) == 0) {
        const qsizetype n = raw.size() - 28;
        QByteArray pt(n, Qt::Uninitialized);
        ok = mbedtls_gcm_auth_decrypt(&ctx, size_t(n), reinterpret_cast<const uchar *>(raw.constData()), 12, nullptr, 0,
                                      reinterpret_cast<const uchar *>(raw.constData() + 12 + n), 16,
                                      reinterpret_cast<const uchar *>(raw.constData() + 12), reinterpret_cast<uchar *>(pt.data())) == 0;
        if (ok) *plain = pt;
    }
    mbedtls_gcm_free(&ctx);
    return ok;
}

} // namespace uve::watchparty::crypto
