#pragma once
// Cryptography for the serverless room channel (Mbed TLS, already linked for WebRTC):
//   • secp256k1 Schnorr signatures (BIP340) for Nostr events – a fresh key pair per session, no account
//   • room key = PBKDF2-HMAC-SHA-256(password, "uvewp/v1/" + room id, 100 000) – only members can read the room
//   • AES-256-GCM for every room message (confidential and authenticated)
#include <QByteArray>
#include <QString>

namespace uve::watchparty::crypto {

QByteArray sha256(const QByteArray &data);
QByteArray randomBytes(int n);

struct KeyPair {
    QByteArray secret;   // 32 bytes
    QByteArray pub;      // 32-byte x-only public key
};
KeyPair generateKeyPair();
QByteArray xOnlyPublicKey(const QByteArray &secret);
// BIP340 signature (64 bytes) of a 32-byte message; aux = 32 random bytes (empty → random)
QByteArray schnorrSign(const QByteArray &secret, const QByteArray &msg32, const QByteArray &aux = {});
bool schnorrVerify(const QByteArray &pub, const QByteArray &msg32, const QByteArray &sig);

QByteArray deriveRoomKey(const QString &roomId, const QString &password);
QString roomTopic(const QString &roomId);   // public lookup tag (hash of the id, not the id itself)
// base64(nonce 12 | ciphertext | tag 16)
QByteArray encrypt(const QByteArray &key, const QByteArray &plain);
bool decrypt(const QByteArray &key, const QByteArray &b64, QByteArray *plain);

} // namespace uve::watchparty::crypto
