#include "TokenVault.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

TokenVault::TokenVault(const QString &filePath, const QByteArray &entropy)
    : m_path(filePath), m_entropy(entropy)
{
}

void TokenVault::loadNow() const
{
    if (m_loaded)
        return;
    m_loaded = true;

    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return;                      // keine Datei → leer
    const QByteArray blob = f.readAll();
    f.close();
    if (blob.isEmpty())
        return;

    const QByteArray plain = unprotect(blob, m_entropy);
    if (plain.isEmpty()) {
        // Entschlüsselung fehlgeschlagen (andere Maschine? korrupt?) → verwerfen.
        QFile::remove(m_path);
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(plain);
    if (!doc.isObject())
        return;
    m_data = doc.object();
    m_diskOk = true;
}

QJsonObject TokenVault::data() const
{
    loadNow();
    return m_data;
}

bool TokenVault::save(const QJsonObject &data)
{
    loadNow();
    m_data = data;
#ifdef Q_OS_WIN
    const QByteArray blob = protect(QJsonDocument(data).toJson(QJsonDocument::Compact),
                                    m_entropy);
    if (blob.isEmpty()) {
        m_diskOk = false;            // DPAPI fehlgeschlagen: nur im Speicher
        return false;
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly) || !f.write(blob) || !f.commit()) {
        m_diskOk = false;
        return false;
    }
    m_diskOk = true;
    return true;
#else
    m_diskOk = false;
    return false;                    // Tokens nur in dieser Sitzung
#endif
}

bool TokenVault::clear()
{
    m_data = QJsonObject();
    m_loaded = true;
    m_diskOk = false;
    QFile::remove(m_path);
    return true;
}

void TokenVault::migrateLegacyToken(const QString &user, const QString &token)
{
    if (token.isEmpty())
        return;
    QJsonObject d = data();
    if (!d.value("chatToken").toString().isEmpty())
        return;                      // Vault enthält bereits ein Token
    d["chatUser"] = user;
    d["chatToken"] = token;
    save(d);
}

// ---------------------------------------------------------------- DPAPI ---

QByteArray TokenVault::protect(const QByteArray &plain, const QByteArray &entropy)
{
#ifdef Q_OS_WIN
    DATA_BLOB blobIn{static_cast<DWORD>(plain.size()),
                   reinterpret_cast<BYTE *>(const_cast<char *>(plain.data()))};
    DATA_BLOB ent{static_cast<DWORD>(entropy.size()),
                  reinterpret_cast<BYTE *>(const_cast<char *>(entropy.data()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&blobIn, L"uve-twitchchat", &ent, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    QByteArray res(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return res;
#else
    Q_UNUSED(plain) Q_UNUSED(entropy)
    return {};
#endif
}

QByteArray TokenVault::unprotect(const QByteArray &blob, const QByteArray &entropy)
{
#ifdef Q_OS_WIN
    DATA_BLOB in{static_cast<DWORD>(blob.size()),
                 reinterpret_cast<BYTE *>(const_cast<char *>(blob.data()))};
    DATA_BLOB ent{static_cast<DWORD>(entropy.size()),
                  reinterpret_cast<BYTE *>(const_cast<char *>(entropy.data()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    QByteArray res(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return res;
#else
    Q_UNUSED(blob) Q_UNUSED(entropy)
    return {};
#endif
}
