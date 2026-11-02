#include "LutLibrary.h"
#include "engine/MiniZip.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>
#include <memory>

namespace uve::fx {

namespace {
constexpr qint64 kMaxPackage = 20 << 20;  // 20 MiB
constexpr qint64 kMaxJson = 256 << 10;    // 256 KiB
constexpr qint64 kMaxListing = 4 << 20;
constexpr int kMaxProfiles = 5000;

QList<int> versionOf(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^LUTsProfiles_v(\\d+(?:\\.\\d+)*)\\.zip$"), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(name);
    QList<int> v;
    if (m.hasMatch()) for (const auto &part : m.captured(1).split('.')) v << part.toInt();
    return v;
}

bool versionLess(const QList<int> &a, const QList<int> &b)
{
    for (int i = 0; i < std::max(a.size(), b.size()); ++i) {
        const int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y) return x < y;
    }
    return false;
}

bool safeName(const QString &n)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_.\\-]+$"));
    return re.match(n).hasMatch() && !n.contains(QLatin1String(".."));
}
} // namespace

LutLibrary::LutLibrary(const QString &dataDir, QObject *parent)
    : QObject(parent),
      m_file(QDir(dataDir).filePath(QStringLiteral("lut-profiles.json"))),
      m_cacheFile(QDir(dataDir).filePath(QStringLiteral("github-cache.json")))
{
    load();
}

const LutProfile *LutLibrary::find(const QString &key) const
{
    for (const auto &p : m_profiles) if (p.key() == key) return &p;
    return nullptr;
}

QVariantList LutLibrary::list() const
{
    QVariantList out;
    for (const auto &p : m_profiles)
        out << QVariantMap{{"key", p.key()}, {"name", p.name}, {"group", p.group},
                           {"text", p.group.isEmpty() ? p.name : p.group + QStringLiteral(" · ") + p.name}};
    return out;
}

void LutLibrary::load()
{
    m_profiles.clear();
    QFile f(m_file);
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto o = QJsonDocument::fromJson(f.readAll()).object();
    m_source = o.value("source").toString();
    m_sourceHash = o.value("sourceHash").toString();
    for (const auto &v : o.value("profiles").toArray()) {
        const auto r = parseLutProfile(v.toObject());
        if (r.ok) m_profiles << r.profile;
    }
}

bool LutLibrary::save()
{
    QJsonArray arr;
    for (const auto &p : m_profiles) arr.append(lutProfileToJson(p));
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile f(m_file);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject{{"schema", "uve-player-lut-library"}, {"ver", 1}, {"source", m_source},
                                      {"sourceHash", m_sourceHash}, {"profiles", arr}}).toJson());
    return f.commit();
}

QJsonObject LutLibrary::readCache() const
{
    QFile f(m_cacheFile);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

void LutLibrary::writeCache(const QJsonObject &o) const
{
    QDir().mkpath(QFileInfo(m_cacheFile).absolutePath());
    QSaveFile f(m_cacheFile);
    if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)); f.commit(); }
}

void LutLibrary::upsert(const LutProfile &p)
{
    // Wie UVE: gleiche Gruppe+Name wird überschrieben
    for (auto &e : m_profiles) if (e.key() == p.key()) { e = p; return; }
    m_profiles << p;
}

QByteArray LutLibrary::gitBlobSha1(const QByteArray &data)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData("blob " + QByteArray::number(data.size()));
    h.addData(QByteArrayView("\0", 1));
    h.addData(data);
    return h.result().toHex();
}

QList<LutLibrary::RemoteFile> LutLibrary::parseGithubListing(const QByteArray &json)
{
    QList<RemoteFile> out;
    for (const auto &v : QJsonDocument::fromJson(json).array()) {
        const auto o = v.toObject();
        if (o.value("type").toString() != QLatin1String("file")) continue;
        out << RemoteFile{o.value("name").toString(), o.value("sha").toString(), QStringLiteral("git-sha1"), qint64(o.value("size").toDouble())};
    }
    return out;
}

QList<LutLibrary::RemoteFile> LutLibrary::parseJsdelivrListing(const QByteArray &json)
{
    QList<RemoteFile> out;
    for (const auto &v : QJsonDocument::fromJson(json).object().value("files").toArray()) {
        const auto o = v.toObject();
        QString name = o.value("name").toString();
        if (!name.startsWith('/') || name.indexOf('/', 1) >= 0) continue; // nur Dateien im Wurzelordner
        out << RemoteFile{name.mid(1), o.value("hash").toString(), QStringLiteral("sha256-b64"), qint64(o.value("size").toDouble())};
    }
    return out;
}

int LutLibrary::pickLatestPackage(const QList<RemoteFile> &files)
{
    int best = -1;
    QList<int> bestV;
    for (int i = 0; i < files.size(); ++i) {
        const auto v = versionOf(files[i].name);
        if (v.isEmpty()) continue;
        if (best < 0 || versionLess(bestV, v)) { best = i; bestV = v; }
    }
    return best;
}

bool LutLibrary::verify(const QByteArray &data, const RemoteFile &f)
{
    if (f.size > 0 && data.size() != f.size) return false;
    if (f.algo == QLatin1String("git-sha1")) return gitBlobSha1(data) == f.checksum.toLatin1();
    if (f.algo == QLatin1String("sha256-b64"))
        return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toBase64() == f.checksum.toLatin1();
    return false;
}

int LutLibrary::importPackage(const QByteArray &zip, const QString &sourceName, QString *message, const QString &sourceHash)
{
    const auto z = readZip(zip, [](const QString &n) { return n.endsWith(QLatin1String(".json"), Qt::CaseInsensitive); }, kMaxJson);
    if (!z.ok) { if (message) *message = tr("Paket ungültig: ") + z.error; return 0; }
    int ok = 0, bad = 0;
    for (const auto &e : z.entries) {
        if (m_profiles.size() >= kMaxProfiles) break;
        // Gruppen-Metadaten des Pakets (_lut_groups.json, schema "gvf-lut-groups") sind kein Profil
        if (QJsonDocument::fromJson(e.data).object().value("schema").toString() == QLatin1String("gvf-lut-groups")) continue;
        const auto r = parseLutProfileFile(e.data, QFileInfo(e.name).fileName());
        if (r.ok) { upsert(r.profile); ++ok; } else ++bad;
    }
    if (ok == 0) { if (message) *message = tr("Keine gültigen LUT-Profile im Paket"); return 0; }
    std::sort(m_profiles.begin(), m_profiles.end(), [](const LutProfile &a, const LutProfile &b) {
        return QString::compare(a.group + QChar(1) + a.name, b.group + QChar(1) + b.name, Qt::CaseInsensitive) < 0; });
    m_source = sourceName;
    m_sourceHash = sourceHash.isEmpty() ? QString::fromLatin1(QCryptographicHash::hash(zip, QCryptographicHash::Sha256).toBase64()) : sourceHash;
    save();
    emit changed();
    if (message) *message = tr("%1 LUT-Profile aus %2 geladen%3").arg(ok).arg(sourceName)
                                .arg(bad ? tr(" (%1 ungültige übersprungen)").arg(bad) : QString());
    return ok;
}

bool LutLibrary::importFile(const QString &path, QString *message)
{
    QFile f(path);
    if (f.size() > kMaxPackage || !f.open(QIODevice::ReadOnly)) { if (message) *message = tr("Datei nicht lesbar oder zu groß"); return false; }
    const QByteArray data = f.readAll();
    if (path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
        return importPackage(data, QFileInfo(path).fileName(), message) > 0;
    const auto r = parseLutProfileFile(data, QFileInfo(path).fileName());
    if (!r.ok) { if (message) *message = tr("LUT-Profil ungültig: ") + r.error; return false; }
    upsert(r.profile);
    save();
    emit changed();
    if (message) *message = QStringLiteral("LUT-Profil \"%1\" importiert").arg(r.profile.name);
    return true;
}

void LutLibrary::releaseNetwork()
{
    if (!m_net) return;
    for (auto *r : m_net->findChildren<QNetworkReply *>()) { r->disconnect(this); r->abort(); }
    delete m_net;               // löscht auch alle Replies (Kinder)
    m_net = nullptr;
    setBusy(false, m_status);
}

void LutLibrary::setBusy(bool b, const QString &status)
{
    if (m_busy != b) { m_busy = b; emit busyChanged(); }
    if (m_status != status) { m_status = status; emit statusChanged(); }
}

QNetworkReply *LutLibrary::get(const QString &url, qint64 maxBytes, const QByteArray &etag)
{
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("UVE-Player/1.0.0"));
    if (url.contains(QLatin1String("api.github.com"))) {
        req.setRawHeader("Accept", "application/vnd.github+json");
        req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    }
    if (!etag.isEmpty()) req.setRawHeader("If-None-Match", etag);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(20000);
    auto *reply = m_net->get(req);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 got, qint64) {
        if (got > maxBytes) reply->abort();
    });
    return reply;
}

void LutLibrary::fetchListing(const Listing &cb)
{
    const QJsonObject cache = readCache();
    auto *api = get(QString::fromLatin1(kRepoApi), kMaxListing, cache.value("etag").toString().toLatin1());
    connect(api, &QNetworkReply::finished, this, [this, api, cache, cb] {
        api->deleteLater();
        const int http = api->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (http == 304 && cache.contains("github")) {                      // unverändert, zählt nicht gegen das Limit
            cb(parseGithubListing(cache.value("github").toString().toUtf8()), QStringLiteral("GitHub (zwischengespeichert)"));
            return;
        }
        if (api->error() == QNetworkReply::NoError && http == 200) {
            const QByteArray body = api->readAll();
            const auto files = parseGithubListing(body);
            if (!files.isEmpty()) {
                QJsonObject c = cache;
                c.insert("etag", QString::fromLatin1(api->rawHeader("ETag")));
                c.insert("github", QString::fromUtf8(body));
                writeCache(c);
                cb(files, tr("GitHub"));
                return;
            }
        }
        const QString reason = (http == 403 || http == 429) ? tr("GitHub-Limit erreicht") : tr("GitHub-API nicht erreichbar");
        setBusy(true, reason + tr(" – nutze jsDelivr-Spiegel …"));
        auto *js = get(QString::fromLatin1(kJsdelivrList), kMaxListing);
        connect(js, &QNetworkReply::finished, this, [this, js, cache, cb, reason] {
            js->deleteLater();
            if (js->error() == QNetworkReply::NoError) {
                const QByteArray body = js->readAll();
                const auto files = parseJsdelivrListing(body);
                if (!files.isEmpty()) {
                    QJsonObject c = cache;
                    c.insert("jsdelivr", QString::fromUtf8(body));
                    writeCache(c);
                    cb(files, tr("jsDelivr (%1)").arg(reason));
                    return;
                }
            }
            // Letzte Rückfallebene: zuletzt bekannte Liste
            if (cache.contains("github")) { cb(parseGithubListing(cache.value("github").toString().toUtf8()), QStringLiteral("letzte bekannte Liste")); return; }
            if (cache.contains("jsdelivr")) { cb(parseJsdelivrListing(cache.value("jsdelivr").toString().toUtf8()), QStringLiteral("letzte bekannte Liste")); return; }
            cb({}, reason);
        });
    });
}

void LutLibrary::fetchFile(const RemoteFile &f, qint64 maxBytes, std::function<void(bool, const QByteArray &, const QString &)> cb)
{
    if (!safeName(f.name)) { cb(false, {}, tr("unzulässiger Dateiname")); return; }
    // Erst raw.githubusercontent.com (kein API-Limit), dann jsDelivr-CDN; Prüfsumme muss jeweils passen
    const auto urls = std::make_shared<QStringList>(QStringList{QString::fromLatin1(kRawBase) + f.name, QString::fromLatin1(kCdnBase) + f.name});
    auto attempt = std::make_shared<std::function<void(QString)>>();
    *attempt = [this, f, maxBytes, cb, urls, weak = std::weak_ptr(attempt)](QString lastError) {
        auto self = weak.lock();
        if (urls->isEmpty()) { cb(false, {}, lastError); return; }
        auto *r = get(urls->takeFirst(), maxBytes);
        connect(r, &QNetworkReply::finished, this, [r, f, cb, self] {
            r->deleteLater();
            QString err;
            if (r->error() == QNetworkReply::NoError) {
                const QByteArray data = r->readAll();
                if (verify(data, f)) { cb(true, data, {}); return; }
                err = tr("Prüfsumme von %1 stimmt nicht").arg(f.name);
            } else {
                err = r->errorString();
            }
            (*self)(err);
        });
    };
    (*attempt)(tr("kein Server erreichbar"));
}

void LutLibrary::downloadLatestPackage(std::function<void(bool, const QString &)> done)
{
    if (m_busy) { if (done) done(false, tr("Download läuft bereits")); return; }
    setBusy(true, tr("Suche LUT-Pakete …"));
    auto finish = [this, done](bool ok, const QString &msg) { setBusy(false, msg); if (done) done(ok, msg); };
    fetchListing([this, finish](const QList<RemoteFile> &files, const QString &via) {
        const int i = pickLatestPackage(files);
        if (i < 0) { finish(false, tr("Kein LUTsProfiles_v*.zip gefunden (%1)").arg(via)); return; }
        const RemoteFile f = files[i];
        if (f.size <= 0 || f.size > kMaxPackage) { finish(false, tr("Unerwartete Paketgröße: ") + f.name); return; }
        if (m_source == f.name && !m_profiles.isEmpty()) {           // schon vorhanden → kein erneuter Download
            finish(true, tr("%1 ist bereits aktuell (%2 LUT-Profile)").arg(f.name).arg(m_profiles.size()));
            return;
        }
        setBusy(true, tr("Lade %1 (%2 KB, Liste: %3) …").arg(f.name).arg(f.size / 1024).arg(via));
        fetchFile(f, kMaxPackage, [this, f, finish](bool ok, const QByteArray &data, const QString &err) {
            if (!ok) { finish(false, tr("Download von %1 fehlgeschlagen: %2").arg(f.name, err)); return; }
            QString msg;
            const int n = importPackage(data, f.name, &msg, f.checksum);
            finish(n > 0, msg);
        });
    });
}

void LutLibrary::fetchExampleProfile(std::function<void(bool, const QByteArray &, const QString &)> done)
{
    if (m_busy) { done(false, {}, tr("Download läuft bereits")); return; }
    setBusy(true, tr("Lade Beispielprofil My_Profile.json …"));
    // raw.githubusercontent.com unterliegt nicht dem API-Limit; Ersatz: jsDelivr-CDN. Prüfung: gültiges UVE-Profil (Aufrufer)
    const auto urls = std::make_shared<QStringList>(QStringList{QString::fromLatin1(kRawBase) + "My_Profile.json",
                                                                 QString::fromLatin1(kCdnBase) + "My_Profile.json"});
    auto attempt = std::make_shared<std::function<void(QString)>>();
    *attempt = [this, done, urls, weak = std::weak_ptr(attempt)](QString lastError) {
        auto self = weak.lock();
        if (urls->isEmpty()) {
            const QString msg = tr("Beispielprofil nicht ladbar: ") + lastError;
            setBusy(false, msg);
            done(false, {}, msg);
           
            return;
        }
        auto *r = get(urls->takeFirst(), kMaxJson);
        connect(r, &QNetworkReply::finished, this, [this, r, done, self] {
            r->deleteLater();
            if (r->error() == QNetworkReply::NoError) {
                const QByteArray json = r->readAll();
                if (QJsonDocument::fromJson(json).isObject()) {
                    setBusy(false, tr("Beispielprofil geladen"));
                    done(true, json, {});
                   
                    return;
                }
                (*self)(tr("Antwort ist kein JSON"));
                return;
            }
            (*self)(r->errorString());
        });
    };
    (*attempt)(QString());
}

} // namespace uve::fx
