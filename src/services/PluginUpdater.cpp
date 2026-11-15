#include "PluginUpdater.h"
#include "AppUpdater.h"
#include "ResumableDownload.h"
#include "../core/Logger.h"
#include "../plugins/PluginManager.h"
#include "../plugins/PluginManifest.h"
#include "../../plugins/uve/engine/MiniZip.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

namespace uve {

static const int kFreshSeconds = 30 * 60;   // so lange gilt eine Liste ohne neue Anfrage

PluginUpdater::PluginUpdater(PluginManager *plugins, const QString &dataDir, QObject *parent)
    : QObject(parent), m_plugins(plugins), m_dataDir(dataDir), m_net(new QNetworkAccessManager(this))
{
    setRepository(QStringLiteral("nextscript"), QStringLiteral("Ultimate-Video-Enhancer"), QStringLiteral("uve-player-plugins"));
    loadCache();
    if (m_plugins) connect(m_plugins, &PluginManager::pluginsChanged, this, &PluginUpdater::recompute);
    recompute();
}

void PluginUpdater::setRepository(const QString &owner, const QString &repo, const QString &branch)
{
    m_owner = owner; m_repo = repo; m_branch = branch;
    const QString gh = QStringLiteral("https://github.com/%1/%2").arg(owner, repo);
    const QString api = QStringLiteral("https://api.github.com/repos/%1/%2").arg(owner, repo);
    m_refsUrls = {gh + QStringLiteral(".git/info/refs?service=git-upload-pack"),
                  api + QStringLiteral("/git/refs/heads/") + branch};
    // {ref} = Commit (oder Zweigname, wenn der Commit nicht ermittelt werden konnte), {file} = Dateiname
    m_listTemplates = {QStringLiteral("https://data.jsdelivr.com/v1/packages/gh/%1/%2@{ref}?structure=flat").arg(owner, repo),
                       api + QStringLiteral("/git/trees/{ref}")};
    m_fileTemplates = {QStringLiteral("https://raw.githubusercontent.com/%1/%2/{ref}/{file}").arg(owner, repo),
                       QStringLiteral("https://cdn.jsdelivr.net/gh/%1/%2@{ref}/{file}").arg(owner, repo),
                       gh + QStringLiteral("/raw/{ref}/{file}")};
}

void PluginUpdater::setEndpoints(const QStringList &refsUrls, const QStringList &listUrlTemplates, const QStringList &fileUrlTemplates)
{
    m_refsUrls = refsUrls; m_listTemplates = listUrlTemplates; m_fileTemplates = fileUrlTemplates;
}

QString PluginUpdater::expand(const QString &tmpl, const QString &ref, const QString &file) const
{
    QString s = tmpl;
    s.replace(QLatin1String("{ref}"), QString::fromLatin1(QUrl::toPercentEncoding(ref)));
    s.replace(QLatin1String("{file}"), QString::fromLatin1(QUrl::toPercentEncoding(file)));
    return s;
}

QString PluginUpdater::lastCheck() const
{
    return m_fetchedAt.isValid() ? QLocale().toString(m_fetchedAt.toLocalTime(), QLocale::ShortFormat) : QString();
}

// ---------------------------------------------------------------- Reine Hilfsfunktionen
QString PluginUpdater::normalizeKey(const QString &s)
{
    QString out;
    for (const QChar c : s)
        if (c.isLetterOrNumber()) out += c.toLower();
    return out;
}

bool PluginUpdater::splitPackageName(const QString &file, QString *key, QString *version)
{
    static const QRegularExpression re(QStringLiteral("^(.+?)[-_ ]v?(\\d+(?:\\.\\d+)*)\\.zip$"), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(QFileInfo(file).fileName());
    if (!m.hasMatch()) return false;
    if (key) *key = normalizeKey(m.captured(1));
    if (version) *version = m.captured(2);
    return !normalizeKey(m.captured(1)).isEmpty();
}

QList<PluginUpdater::CatalogEntry> PluginUpdater::parseCatalog(const QByteArray &json)
{
    QList<CatalogEntry> out;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    auto add = [&out](const QString &path, qint64 size, const QString &sha256, const QString &gitSha) {
        if (path.contains(QLatin1Char('/'))) return;   // nur Dateien im Wurzelverzeichnis des Zweigs
        CatalogEntry e;
        if (!splitPackageName(path, &e.key, &e.version)) return;
        e.file = path; e.size = size; e.sha256 = sha256; e.gitSha = gitSha;
        out << e;
    };
    if (root.value("files").isArray()) {   // jsDelivr
        for (const auto &v : root.value("files").toArray()) {
            const auto o = v.toObject();
            QString name = o.value("name").toString();
            if (name.startsWith(QLatin1Char('/'))) name.remove(0, 1);
            const QByteArray hash = QByteArray::fromBase64(o.value("hash").toString().toLatin1());
            add(name, o.value("size").toInteger(-1), hash.size() == 32 ? QString::fromLatin1(hash.toHex()) : QString(), {});
        }
    } else if (root.value("tree").isArray()) {   // REST-API
        for (const auto &v : root.value("tree").toArray()) {
            const auto o = v.toObject();
            if (o.value("type").toString() != QLatin1String("blob")) continue;
            add(o.value("path").toString(), o.value("size").toInteger(-1), {}, o.value("sha").toString());
        }
    }
    return out;
}

int PluginUpdater::findEntry(const QList<CatalogEntry> &catalog, const QString &id, const QString &name)
{
    QStringList keys{normalizeKey(name), normalizeKey(id.section(QLatin1Char('.'), -1)), normalizeKey(id)};
    keys.removeAll(QString());
    int best = -1;
    for (int i = 0; i < catalog.size(); ++i) {
        if (!keys.contains(catalog[i].key)) continue;
        if (best < 0 || compareVersions(catalog[i].version, catalog[best].version) > 0) best = i;
    }
    return best;
}

QString PluginUpdater::gitBlobSha(const QByteArray &data)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData("blob " + QByteArray::number(data.size()) + '\0');
    h.addData(data);
    return QString::fromLatin1(h.result().toHex());
}

QString PluginUpdater::packageId(const QByteArray &zipData)
{
    const auto zip = fx::readZip(zipData, [](const QString &n) {
        const QString p = QString(n).replace(QLatin1Char('\\'), QLatin1Char('/'));
        return p == QLatin1String("manifest.json") || (p.count(QLatin1Char('/')) == 1 && p.endsWith(QLatin1String("/manifest.json")));
    }, 1 << 20, 4 << 20, 20000);
    if (!zip.ok || zip.entries.size() != 1) return {};
    return QJsonDocument::fromJson(zip.entries.first().data).object().value("id").toString();
}

// ---------------------------------------------------------------- Zwischenspeicher
QString PluginUpdater::cacheFile() const { return QDir(m_dataDir).filePath(QStringLiteral("cache/plugin-catalog.json")); }

void PluginUpdater::loadCache()
{
    QFile f(cacheFile());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value("branch").toString() != m_branch) return;
    m_commit = o.value("commit").toString();
    m_fetchedAt = QDateTime::fromString(o.value("fetchedAt").toString(), Qt::ISODate);
    m_catalog.clear();
    for (const auto &v : o.value("files").toArray()) {
        const auto e = v.toObject();
        CatalogEntry c;
        c.file = e.value("file").toString();
        if (!splitPackageName(c.file, &c.key, &c.version)) continue;
        c.size = e.value("size").toInteger(-1);
        c.sha256 = e.value("sha256").toString();
        c.gitSha = e.value("gitSha").toString();
        m_catalog << c;
    }
}

void PluginUpdater::saveCache() const
{
    QJsonArray files;
    for (const auto &c : m_catalog)
        files << QJsonObject{{"file", c.file}, {"size", c.size}, {"sha256", c.sha256}, {"gitSha", c.gitSha}};
    QDir().mkpath(QFileInfo(cacheFile()).absolutePath());
    QSaveFile f(cacheFile());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(QJsonObject{{"branch", m_branch}, {"commit", m_commit},
                                      {"fetchedAt", m_fetchedAt.toUTC().toString(Qt::ISODate)}, {"files", files}}).toJson());
    f.commit();
}

// ---------------------------------------------------------------- Netzwerk
// Nacheinander die Adressen probieren; jede Adresse bekommt bei Netzwerkfehlern oder Begrenzung (403/429/5xx) einen zweiten Versuch
void PluginUpdater::fetch(const QStringList &urls, FetchDone done)
{
    if (urls.isEmpty()) return done(false, 0, {});
    QNetworkRequest req{QUrl(urls.first())};
    req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("UVE-Player"));
    req.setRawHeader("Accept", "*/*");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);
    auto *r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, urls, done] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (r->error() == QNetworkReply::NoError) return done(true, status, r->readAll());
        Logger::write("plugin-update", urls.first() + ": " + (status ? QString::number(status) + QLatin1Char(' ') : QString()) + r->errorString());
        fetch(urls.mid(1), done);
    });
}

void PluginUpdater::check(bool force)
{
    if (m_checking) return;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const bool fresh = !m_catalog.isEmpty() && m_fetchedAt.isValid() && m_fetchedAt.secsTo(now) < kFreshSeconds;
    const bool tooSoon = m_lastAttempt.isValid() && m_lastAttempt.secsTo(now) < m_minIntervalSec;
    if ((fresh && !force) || (tooSoon && !m_catalog.isEmpty())) {
        recompute();
        emit checkFinished();
        return;
    }
    m_lastAttempt = now;
    m_checking = true;
    m_error.clear();
    for (auto it = m_status.begin(); it != m_status.end(); ++it) {   // Fehler früherer Downloads zurücksetzen
        QVariantMap s = it.value().toMap();
        if (s.value("state").toString() == QLatin1String("error")) { s.insert("state", QString()); it.value() = s; }
    }
    recompute();

    fetch(m_refsUrls, [this](bool ok, int, const QByteArray &body) {
        QString commit;
        if (ok) {
            const QJsonObject o = QJsonDocument::fromJson(body).object();   // REST-API: {"object":{"sha":…}}
            commit = o.value("object").toObject().value("sha").toString();
            if (commit.isEmpty()) commit = AppUpdater::parseRefs(body).value(QStringLiteral("refs/heads/") + m_branch);
        }
        onCommit(commit);
    });
}

void PluginUpdater::onCommit(const QString &commit)
{
    if (!commit.isEmpty() && commit == m_commit && !m_catalog.isEmpty()) {
        // Zweig unverändert: gespeicherte Liste gilt weiter, keine weitere Anfrage
        m_fetchedAt = QDateTime::currentDateTimeUtc();
        saveCache();
        return finishCheck({});
    }
    // Ohne Commit (Git-Endpunkt und API nicht erreichbar) über den Zweignamen; jsDelivr kann dann einige Stunden alt sein
    const QString ref = commit.isEmpty() ? m_branch : commit;
    loadList(ref, m_listTemplates, [this, commit](bool ok) {
        if (ok) {
            m_commit = commit;
            m_fetchedAt = QDateTime::currentDateTimeUtc();
            saveCache();
            return finishCheck({});
        }
        finishCheck(m_catalog.isEmpty() ? tr("Plugin-Katalog auf GitHub nicht erreichbar")
                                        : tr("GitHub nicht erreichbar – gespeicherte Liste vom %1").arg(lastCheck()));
    });
}

void PluginUpdater::loadList(const QString &ref, const QStringList &templates, std::function<void(bool)> done)
{
    if (templates.isEmpty()) return done(false);
    fetch({expand(templates.first(), ref)}, [this, ref, templates, done](bool ok, int, const QByteArray &body) {
        if (ok) {
            const auto list = parseCatalog(body);
            // Eine leere Liste ist bei Zweigen mit Paketen ein Fehler (z. B. Fehlerseite statt JSON)
            if (!list.isEmpty()) { m_catalog = list; return done(true); }
        }
        loadList(ref, templates.mid(1), done);
    });
}

void PluginUpdater::finishCheck(const QString &error)
{
    m_checking = false;
    m_error = error;
    if (!error.isEmpty()) Logger::write("plugin-update", error);
    recompute();
    emit checkFinished();
}

// ---------------------------------------------------------------- Zustand je Plugin
void PluginUpdater::setState(const QString &id, const QVariantMap &values)
{
    QVariantMap s = m_status.value(id).toMap();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) s.insert(it.key(), it.value());
    m_status.insert(id, s);
    emit changed();
}

void PluginUpdater::recompute()
{
    if (!m_plugins) { emit changed(); return; }
    QVariantMap next;
    for (const auto &v : m_plugins->plugins()) {
        const QVariantMap p = v.toMap();
        const QString id = p.value("id").toString();
        if (id.isEmpty()) continue;
        const QVariantMap old = m_status.value(id).toMap();
        const QString oldState = old.value("state").toString();
        if (oldState == QLatin1String("downloading")) { next.insert(id, old); continue; }

        const QString installedVersion = p.value("version").toString();
        QVariantMap s{{"installedVersion", installedVersion}};
        const int i = findEntry(m_catalog, id, p.value("name").toString());
        if (i >= 0) {
            s.insert("latestVersion", m_catalog[i].version);
            s.insert("file", m_catalog[i].file);
            s.insert("size", m_catalog[i].size);
        }
        const QString pending = m_installedVersions.value(id);
        if (!pending.isEmpty() && pending != installedVersion) {
            s.insert("state", QStringLiteral("installed"));
            s.insert("pendingVersion", pending);
        } else if (oldState == QLatin1String("error")) {
            s.insert("state", QStringLiteral("error"));
            s.insert("error", old.value("error"));
        } else if (m_checking && m_catalog.isEmpty()) {
            s.insert("state", QStringLiteral("checking"));
        } else if (i < 0) {
            s.insert("state", m_catalog.isEmpty() ? QString() : QStringLiteral("unknown"));
        } else {
            s.insert("state", compareVersions(m_catalog[i].version, installedVersion) > 0 ? QStringLiteral("available") : QStringLiteral("current"));
        }
        next.insert(id, s);
    }
    m_status = next;
    emit changed();
}

// ---------------------------------------------------------------- Installation
void PluginUpdater::install(const QString &pluginId)
{
    if (m_downloads.value(pluginId)) return;
    const QVariantMap s = m_status.value(pluginId).toMap();
    const QString file = s.value("file").toString();
    if (file.isEmpty()) return;
    setState(pluginId, {{"state", QStringLiteral("downloading")}, {"error", QString()}, {"received", 0}, {"total", s.value("size")}});
    tryDownload(pluginId, 0);
}

void PluginUpdater::cancel(const QString &pluginId)
{
    if (auto d = m_downloads.value(pluginId)) {
        m_cancelled.insert(pluginId);
        d->abort();
    }
}

void PluginUpdater::tryDownload(const QString &id, int urlIndex)
{
    const QString file = m_status.value(id).toMap().value("file").toString();
    const QString ref = m_commit.isEmpty() ? m_branch : m_commit;
    const QString dir = QDir(m_dataDir).filePath(QStringLiteral("updates/plugins"));
    QDir().mkpath(dir);
    const QString target = QDir(dir).filePath(file);

    auto *d = new ResumableDownload(m_net, QUrl(expand(m_fileTemplates.value(urlIndex), ref, file)), target, this);
    d->setMaxAttempts(3);
    d->setResume(false);   // andere Quelle: neu beginnen statt Bytes verschiedener Server zu mischen
    m_downloads.insert(id, d);
    connect(d, &ResumableDownload::progress, this, [this, id](qint64 received, qint64 total) {
        QVariantMap s = m_status.value(id).toMap();
        s.insert("received", received);
        if (total > 0) s.insert("total", total);
        m_status.insert(id, s);
        emit changed();
    });
    connect(d, &ResumableDownload::finished, this, [this, d, id, urlIndex, target](bool ok, const QString &error) {
        d->deleteLater();
        const bool aborted = m_cancelled.remove(id);
        m_downloads.remove(id);
        if (ok) return onDownloaded(id, target);
        QFile::remove(target);
        if (!aborted && urlIndex + 1 < m_fileTemplates.size()) {
            Logger::write("plugin-update", QStringLiteral("%1: %2 – nächste Quelle").arg(id, error));
            return tryDownload(id, urlIndex + 1);
        }
        m_status.remove(id);
        recompute();
        if (aborted) return;
        setState(id, {{"state", QStringLiteral("error")}, {"error", error}});
        emit installFailed(id, error);
    });
    d->start();
}

void PluginUpdater::onDownloaded(const QString &id, const QString &file)
{
    const QString name = QFileInfo(file).fileName();
    auto fail = [this, id, file](const QString &message) {
        QFile::remove(file);
        Logger::write("plugin-update", id + ": " + message);
        m_status.remove(id);
        recompute();
        setState(id, {{"state", QStringLiteral("error")}, {"error", message}});
        emit installFailed(id, message);
    };
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) return fail(tr("Datei nicht lesbar: %1").arg(file));
    const QByteArray data = in.readAll();
    in.close();

    const auto entry = std::find_if(m_catalog.cbegin(), m_catalog.cend(), [&name](const CatalogEntry &c) { return c.file == name; });
    if (entry != m_catalog.cend()) {
        if (entry->size >= 0 && entry->size != data.size()) return fail(tr("Download unvollständig (%1 von %2 Bytes)").arg(data.size()).arg(entry->size));
        if (!entry->sha256.isEmpty() && QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()) != entry->sha256)
            return fail(tr("Prüfsumme stimmt nicht – Download verworfen"));
        if (!entry->gitSha.isEmpty() && gitBlobSha(data) != entry->gitSha)
            return fail(tr("Prüfsumme stimmt nicht – Download verworfen"));
    }
    // Der Dateiname ist nur ein Hinweis: Das Paket muss genau dieses Plugin enthalten
    const QString packageIdValue = packageId(data);
    if (packageIdValue != id)
        return fail(tr("Das Paket %1 enthält ein anderes Plugin (%2)").arg(name, packageIdValue.isEmpty() ? tr("keine manifest.json") : packageIdValue));

    const QVariantMap info = m_plugins->installPackageInfo(file);
    QFile::remove(file);
    if (!info.value("ok").toBool()) return fail(info.value("error").toString());
    Logger::write("plugin-update", QStringLiteral("%1 %2 installiert (aktiv nach Neustart)").arg(id, info.value("version").toString()));
    m_installedVersions.insert(id, info.value("version").toString());
    m_status.remove(id);
    recompute();
    emit installed(info);
}

} // namespace uve
