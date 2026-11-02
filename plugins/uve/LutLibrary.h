#pragma once
// LUT-Profil-Bibliothek (UVE "gvf-lut-profile", 4×5-Matrix) mit Download der Pakete aus dem
// UVE-GitHub-Repository (LUTsProfiles_v*.zip, höchste Version) und des Beispielprofils My_Profile.json.
//
// Netzwerk nur auf ausdrückliche Benutzeraktion. Robust gegen das GitHub-API-Limit (60 Anfragen/h ohne Anmeldung):
//  1. GitHub-Contents-API mit If-None-Match (ETag) – eine 304-Antwort zählt nicht gegen das Limit
//  2. bei Limit/Fehler: jsDelivr-Dateiliste (data.jsdelivr.com) – eigene Prüfsummen (SHA-256)
//  3. danach: zuletzt erfolgreich abgerufene Liste aus dem Cache
// Dateien kommen von raw.githubusercontent.com, ersatzweise vom jsDelivr-CDN; jede Datei wird gegen die
// Prüfsumme der Liste geprüft (Git-Blob-SHA-1 bzw. SHA-256). Bereits importierte Pakete werden nicht erneut geladen.
#include "engine/LutProfile.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace uve::fx {

class LutLibrary : public QObject
{
    Q_OBJECT
public:
    static constexpr const char *kRepoApi = "https://api.github.com/repos/nextscript/Ultimate-Video-Enhancer/contents";
    static constexpr const char *kJsdelivrList = "https://data.jsdelivr.com/v1/packages/gh/nextscript/Ultimate-Video-Enhancer@main?structure=flat";
    static constexpr const char *kRawBase = "https://raw.githubusercontent.com/nextscript/Ultimate-Video-Enhancer/main/";
    static constexpr const char *kCdnBase = "https://cdn.jsdelivr.net/gh/nextscript/Ultimate-Video-Enhancer@main/";

    explicit LutLibrary(const QString &dataDir, QObject *parent = nullptr);

    const LutProfile *find(const QString &key) const;
    QVariantList list() const;
    int count() const { return int(m_profiles.size()); }
    QString source() const { return m_source; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }

    bool importFile(const QString &path, QString *message);                              // lokales .json / .zip
    void downloadLatestPackage(std::function<void(bool ok, const QString &message)> done = {});
    void releaseNetwork();                                                             // laufende Downloads abbrechen, Netzwerk freigeben
    void fetchExampleProfile(std::function<void(bool ok, const QByteArray &json, const QString &message)> done);

    // öffentlich für Tests
    int importPackage(const QByteArray &zip, const QString &sourceName, QString *message, const QString &sourceHash = {});
    static QByteArray gitBlobSha1(const QByteArray &data);
    struct RemoteFile { QString name; QString checksum; QString algo; qint64 size = 0; }; // algo: "git-sha1" | "sha256-b64"
    static QList<RemoteFile> parseGithubListing(const QByteArray &json);
    static QList<RemoteFile> parseJsdelivrListing(const QByteArray &json);
    static int pickLatestPackage(const QList<RemoteFile> &files); // Index oder -1
    static bool verify(const QByteArray &data, const RemoteFile &f);

signals:
    void changed();
    void busyChanged();
    void statusChanged();

private:
    using Listing = std::function<void(const QList<RemoteFile> &files, const QString &via)>;
    void load();
    bool save();
    void upsert(const LutProfile &p);
    void setBusy(bool b, const QString &status);
    QNetworkReply *get(const QString &url, qint64 maxBytes, const QByteArray &etag = {});
    void fetchListing(const Listing &cb);
    void fetchFile(const RemoteFile &f, qint64 maxBytes, std::function<void(bool, const QByteArray &, const QString &)> cb);
    QJsonObject readCache() const;
    void writeCache(const QJsonObject &o) const;

    QString m_file, m_cacheFile;
    QList<LutProfile> m_profiles;
    QString m_source, m_sourceHash;
    bool m_busy = false;
    QString m_status;
    QNetworkAccessManager *m_net = nullptr;
};

} // namespace uve::fx
