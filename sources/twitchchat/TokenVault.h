#pragma once
// Sichere, lokal verschlüsselte Ablage für Twitch-Tokens.
//
// Windows: DPAPI (CryptProtectData, benutzerbezogener Schlüssel) – die Datei
// dataDir/tokens.dat enthält niemals Klartext-Tokens.
// macOS: Schlüsselbund des Benutzers (Security.framework), Linux: Secret Service über libsecret
// (GNOME-Schlüsselbund, KWallet …), zur Laufzeit geladen; Konto = Pfad von tokens.dat.
// Ist nichts davon nutzbar, bleiben Tokens nur im Speicher dieser Sitzung (kein Klartext auf der Platte).
//
// Inhalt (vor der Verschlüsselung als JSON):
//   { "chatUser": "...", "chatToken": "...", "session": { ... } | null }
// session = Browser-/Helix-Sitzung: accessToken, refreshToken, expiresAt(ms),
//           scope[], userId, login, displayName, profileImage, obtainedAt(ms)

#include <QByteArray>
#include <QJsonObject>
#include <QString>

class TokenVault
{
public:
    TokenVault(const QString &filePath, const QByteArray &entropy);

    // Beim ersten Zugriff wird die Datei gelesen und entschlüsselt.
    QJsonObject data() const;

    bool save(const QJsonObject &data);   // false = nur Sitzungsspeicher (kein DPAPI/Schlüsselbund)
    bool clear();                         // Session und Datei entfernen

    bool usingDisk() const { return m_diskOk; }
    QString path() const { return m_path; }

    // Einmalige Migration: altes Klartext-Token aus den Plugin-Einstellungen übernehmen.
    void migrateLegacyToken(const QString &user, const QString &token);

private:
    void loadNow() const;
    static QByteArray protect(const QByteArray &plain, const QByteArray &entropy);
    static QByteArray unprotect(const QByteArray &blob, const QByteArray &entropy);

    QString m_path;
    QByteArray m_entropy;
    mutable bool m_loaded = false;
    mutable bool m_diskOk = false;
    mutable QJsonObject m_data;
};
