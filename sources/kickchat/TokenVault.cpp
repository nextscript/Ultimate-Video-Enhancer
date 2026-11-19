#include "TokenVault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <wincrypt.h>
#elif defined(Q_OS_MACOS)
#include <Security/Security.h>
#else
#include <dlfcn.h>
#endif

#ifndef Q_OS_WIN
// Name des Eintrags im Schlüsselbund (macOS) bzw. im Secret Service (Linux)
static const char kService[] = "uve-kickchat";
#endif

// ---------------------------------------------------------------- Schlüsselbund (macOS / Linux) ---
// Statt einer Datei liegt der Inhalt im Schlüsselbund des Systems; Konto = Pfad der Datei (je Profil eindeutig).
#if defined(Q_OS_MACOS)
namespace {
CFDictionaryRef keychainQuery(const QString &account, CFTypeRef extraKey = nullptr, CFTypeRef extraValue = nullptr,
                              CFTypeRef extraKey2 = nullptr, CFTypeRef extraValue2 = nullptr)
{
    CFStringRef svc = QString::fromLatin1(kService).toCFString();
    CFStringRef acc = account.toCFString();
    const void *keys[5] = {kSecClass, kSecAttrService, kSecAttrAccount, extraKey, extraKey2};
    const void *vals[5] = {kSecClassGenericPassword, svc, acc, extraValue, extraValue2};
    const CFIndex n = extraKey2 ? 5 : extraKey ? 4 : 3;
    CFDictionaryRef q = CFDictionaryCreate(nullptr, keys, vals, n, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(svc);
    CFRelease(acc);
    return q;
}

QByteArray secureRead(const QString &account)
{
    CFDictionaryRef q = keychainQuery(account, kSecReturnData, kCFBooleanTrue, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    QByteArray out;
    if (SecItemCopyMatching(q, &result) == errSecSuccess && result) {
        const auto data = static_cast<CFDataRef>(result);
        out = QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), int(CFDataGetLength(data)));
        CFRelease(result);
    }
    CFRelease(q);
    return out;
}

void secureRemove(const QString &account)
{
    CFDictionaryRef q = keychainQuery(account);
    SecItemDelete(q);
    CFRelease(q);
}

bool secureWrite(const QString &account, const QByteArray &data)
{
    secureRemove(account);
    CFDataRef value = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(data.constData()), data.size());
    CFDictionaryRef q = keychainQuery(account, kSecValueData, value, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlock);
    const OSStatus status = SecItemAdd(q, nullptr);
    CFRelease(q);
    CFRelease(value);
    return status == errSecSuccess;
}
} // namespace
#elif !defined(Q_OS_WIN)
// libsecret (Secret Service: GNOME-Schlüsselbund, KWallet …) erst zur Laufzeit laden – ohne libsecret bleibt
// es bei "nur diese Sitzung". Strukturen wie in <libsecret/secret.h> (stabile ABI).
namespace {
struct SecretSchemaAttribute { const char *name; int type; };
struct SecretSchema {
    const char *name;
    int flags;
    SecretSchemaAttribute attributes[32];
    int reserved;
    void *reserved1, *reserved2, *reserved3, *reserved4, *reserved5, *reserved6, *reserved7;
};
using StoreFn = int (*)(const SecretSchema *, const char *, const char *, const char *, void *, void **, ...);
using LookupFn = char *(*)(const SecretSchema *, void *, void **, ...);
using ClearFn = int (*)(const SecretSchema *, void *, void **, ...);
using FreeFn = void (*)(char *);

struct Secret
{
    StoreFn store = nullptr;
    LookupFn lookup = nullptr;
    ClearFn clear = nullptr;
    FreeFn free = nullptr;
    SecretSchema schema{};
    bool ok() const { return store && lookup && clear && free; }
};

const Secret &secret()
{
    static const Secret s = [] {
        Secret r;
        void *lib = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return r;
        r.store = reinterpret_cast<StoreFn>(dlsym(lib, "secret_password_store_sync"));
        r.lookup = reinterpret_cast<LookupFn>(dlsym(lib, "secret_password_lookup_sync"));
        r.clear = reinterpret_cast<ClearFn>(dlsym(lib, "secret_password_clear_sync"));
        r.free = reinterpret_cast<FreeFn>(dlsym(lib, "secret_password_free"));
        r.schema.name = "org.uveplayer.plugin-token";
        r.schema.attributes[0] = {"service", 0};   // SECRET_SCHEMA_ATTRIBUTE_STRING
        r.schema.attributes[1] = {"account", 0};
        return r;
    }();
    return s;
}

QByteArray secureRead(const QString &account)
{
    const Secret &s = secret();
    if (!s.ok()) return {};
    const QByteArray acc = account.toUtf8();
    char *value = s.lookup(&s.schema, nullptr, nullptr, "service", kService, "account", acc.constData(), nullptr);
    if (!value) return {};
    const QByteArray out = QByteArray::fromBase64(QByteArray(value));
    s.free(value);
    return out;
}

bool secureWrite(const QString &account, const QByteArray &data)
{
    const Secret &s = secret();
    if (!s.ok()) return false;
    const QByteArray acc = account.toUtf8();
    const QByteArray label = QByteArray("UVE Player ") + kService;
    return s.store(&s.schema, "default", label.constData(), data.toBase64().constData(), nullptr, nullptr,
                   "service", kService, "account", acc.constData(), nullptr) != 0;
}

void secureRemove(const QString &account)
{
    const Secret &s = secret();
    if (!s.ok()) return;
    const QByteArray acc = account.toUtf8();
    s.clear(&s.schema, nullptr, nullptr, "service", kService, "account", acc.constData(), nullptr);
}
} // namespace
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

#ifdef Q_OS_WIN
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
#else
    const QByteArray plain = secureRead(QFileInfo(m_path).absoluteFilePath());
    if (plain.isEmpty())
        return;                      // kein Eintrag im Schlüsselbund → leer
#endif
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
    // Schlüsselbund des Systems; ohne Schlüsselbund nur in dieser Sitzung (nie Klartext auf der Platte)
    m_diskOk = secureWrite(QFileInfo(m_path).absoluteFilePath(), QJsonDocument(data).toJson(QJsonDocument::Compact));
    return m_diskOk;
#endif
}

bool TokenVault::clear()
{
    m_data = QJsonObject();
    m_loaded = true;
    m_diskOk = false;
    QFile::remove(m_path);
#ifndef Q_OS_WIN
    secureRemove(QFileInfo(m_path).absoluteFilePath());
#endif
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
    if (!CryptProtectData(&blobIn, L"uve-kickchat", &ent, nullptr, nullptr,
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
