#include "ToolUpdater.h"
#include "../core/Logger.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace uve {

static const char *kYtdlpApi = "https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest";
static const char *kMpvApi = "https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases/latest";

// yt-dlp: lokaler Dateiname und Name des Release-Assets je Plattform
#if defined(Q_OS_WIN)
static const QString kYtdlpFile = QStringLiteral("yt-dlp.exe");
static const QString kYtdlpAsset = QStringLiteral("yt-dlp.exe");
#elif defined(Q_OS_MACOS)
static const QString kYtdlpFile = QStringLiteral("yt-dlp");
static const QString kYtdlpAsset = QStringLiteral("yt-dlp_macos");
#else
static const QString kYtdlpFile = QStringLiteral("yt-dlp");
static const QString kYtdlpAsset = QStringLiteral("yt-dlp_linux");
#endif

ToolUpdater::ToolUpdater(const QString &appDir, const QString &dataDir, QObject *parent)
    : QObject(parent), m_appDir(appDir), m_dataDir(dataDir), m_net(new QNetworkAccessManager(this))
{
}

int ToolUpdater::compareToolVersions(const QString &a, const QString &b)
{
    // yt-dlp: 2026.10.26(.1) · libmpv-Build: 20261102 → numerisch je Segment
    const auto pa = a.split(QRegularExpression("[^0-9]+"), Qt::SkipEmptyParts);
    const auto pb = b.split(QRegularExpression("[^0-9]+"), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const qlonglong x = i < pa.size() ? pa[i].toLongLong() : 0, y = i < pb.size() ? pb[i].toLongLong() : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

QString ToolUpdater::versionOf(const QString &toolDir)
{
    QFile f(QDir(toolDir).filePath("version.json"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object().value("version").toString();
}

static QStringList versionedDirs(const QString &base, const QString &fileName)
{
    QStringList out;
    QDir d(base);
    for (const auto &e : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (QFileInfo::exists(d.filePath(e + "/" + fileName))) out << d.filePath(e);
    return out;
}

QStringList ToolUpdater::mpvDllCandidates(const QString &appDir, const QString &dataDir)
{
    struct C { QString file; QString ver; };
    QList<C> list;
    for (const auto &dir : versionedDirs(QDir(dataDir).filePath("tools/mpv"), "libmpv-2.dll"))
        list << C{QDir(dir).filePath("libmpv-2.dll"), versionOf(dir)};
    const QString bundled = QDir(appDir).filePath("mpv/libmpv-2.dll");
    if (QFileInfo::exists(bundled)) list << C{bundled, versionOf(QDir(appDir).filePath("mpv"))};
    std::stable_sort(list.begin(), list.end(), [](const C &a, const C &b) { return compareToolVersions(a.ver, b.ver) > 0; });
    QStringList out;
    for (const auto &c : list) out << c.file;
    return out;
}

QString ToolUpdater::preferredYtdlp(const QString &appDir, const QString &dataDir)
{
    QString best, bestVer;
    for (const auto &dir : versionedDirs(QDir(dataDir).filePath("tools/yt-dlp"), kYtdlpFile)) {
        const auto v = versionOf(dir);
        if (best.isEmpty() || compareToolVersions(v, bestVer) > 0) { best = QDir(dir).filePath(kYtdlpFile); bestVer = v; }
    }
    const QString bundledDir = QDir(appDir).filePath("tools/yt-dlp");
    const QString bundled = QDir(bundledDir).filePath(kYtdlpFile);
    if (QFileInfo::exists(bundled) && (best.isEmpty() || compareToolVersions(versionOf(bundledDir), bestVer) > 0)) best = bundled;
#ifndef Q_OS_WIN
    // Linux/macOS: ohne mitgelieferte Version ein installiertes yt-dlp aus dem PATH verwenden
    if (best.isEmpty()) best = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
#endif
    return best;
}

QVariantMap ToolUpdater::installed() const
{
    QVariantMap m;
    const auto mpv = mpvDllCandidates(m_appDir, m_dataDir);
    if (!mpv.isEmpty()) m.insert("libmpv", QVariantMap{{"path", mpv.first()}, {"version", versionOf(QFileInfo(mpv.first()).absolutePath())}});
    const auto yt = preferredYtdlp(m_appDir, m_dataDir);
    if (!yt.isEmpty()) m.insert("yt-dlp", QVariantMap{{"path", yt}, {"version", versionOf(QFileInfo(yt).absolutePath())}});
    return m;
}

void ToolUpdater::setBusy(bool b)
{
    if (m_busy == b) return;
    m_busy = b;
    emit busyChanged();
}

QNetworkReply *ToolUpdater::get(const QString &url)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "UVE-Player");
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    return m_net->get(req);
}

void ToolUpdater::check()
{
    if (m_busy) return;
    setBusy(true);
    m_updates.clear();
    m_errors.clear();
    m_latest.clear();
#ifdef Q_OS_WIN
    m_pending = 2;
#else
    m_pending = 1; // libmpv kommt auf Linux/macOS aus dem System bzw. App-Bundle
#endif
    const auto inst = installed();

    auto *yt = get(kYtdlpApi);
    connect(yt, &QNetworkReply::finished, this, [this, yt, inst] {
        yt->deleteLater();
        if (yt->error() != QNetworkReply::NoError) m_errors << "yt-dlp: " + yt->errorString();
        else {
            const auto rel = QJsonDocument::fromJson(yt->readAll()).object();
            const QString tag = rel.value("tag_name").toString();
            QString exeUrl, sumsUrl; qint64 size = 0;
            for (const auto &a : rel.value("assets").toArray()) {
                const auto o = a.toObject();
                if (o.value("name").toString() == kYtdlpAsset) { exeUrl = o.value("browser_download_url").toString(); size = o.value("size").toInteger(); }
                if (o.value("name").toString() == "SHA2-256SUMS") sumsUrl = o.value("browser_download_url").toString();
            }
            const QString cur = inst.value("yt-dlp").toMap().value("version").toString();
            QVariantMap info{{"tool", "yt-dlp"}, {"name", "yt-dlp"}, {"current", cur.isEmpty() ? tr("nicht installiert") : cur},
                             {"latest", tag}, {"id", tag}, {"size", size}, {"url", exeUrl}, {"sums", sumsUrl},
                             {"date", rel.value("published_at").toString()}};
            m_latest.insert("yt-dlp", info);
            if (!exeUrl.isEmpty() && (cur.isEmpty() || compareToolVersions(tag, cur) > 0)) m_updates << info;
        }
        finishCheckIfDone();
    });

#ifdef Q_OS_WIN
    auto *mp = get(kMpvApi);
    connect(mp, &QNetworkReply::finished, this, [this, mp, inst] {
        mp->deleteLater();
        if (mp->error() != QNetworkReply::NoError) m_errors << "libmpv: " + mp->errorString();
        else {
            const auto rel = QJsonDocument::fromJson(mp->readAll()).object();
            const QString tag = rel.value("tag_name").toString(); // Builddatum, z. B. 20261102
            static const QRegularExpression re(QStringLiteral("^mpv-dev-x86_64-(\\d{8})-git-[0-9a-f]+\\.7z$"));
            QString url, digest; qint64 size = 0;
            for (const auto &a : rel.value("assets").toArray()) {
                const auto o = a.toObject();
                if (re.match(o.value("name").toString()).hasMatch()) {
                    url = o.value("browser_download_url").toString();
                    size = o.value("size").toInteger();
                    digest = o.value("digest").toString(); // "sha256:…", falls von GitHub geliefert
                }
            }
            const QString cur = inst.value("libmpv").toMap().value("version").toString();
            QVariantMap info{{"tool", "libmpv"}, {"name", "libmpv (mpv-Wiedergabe-Engine)"}, {"current", cur.isEmpty() ? tr("nicht installiert") : cur},
                             {"latest", tag}, {"id", tag}, {"size", size}, {"url", url}, {"digest", digest},
                             {"date", rel.value("published_at").toString()}};
            m_latest.insert("libmpv", info);
            if (!url.isEmpty() && (cur.isEmpty() || compareToolVersions(tag, cur) > 0)) m_updates << info;
        }
        finishCheckIfDone();
    });
#endif
}

void ToolUpdater::finishCheckIfDone()
{
    if (--m_pending > 0) return;
    setBusy(false);
    emit checkFinished(m_updates, m_errors);
}

void ToolUpdater::install(const QString &tool)
{
    const auto info = m_latest.value(tool).toMap();
    if (info.isEmpty() || m_busy) { emit installFinished(tool, false, tr("Keine Update-Information – bitte erneut prüfen"), false); return; }
    setBusy(true);
    if (tool == "yt-dlp") installYtdlp(info);
    else installMpv(info);
}

static QString sha256Of(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

void ToolUpdater::installYtdlp(const QVariantMap &info)
{
    const QString ver = info.value("latest").toString();
    const QString dir = QDir(m_dataDir).filePath("tools/yt-dlp/" + ver);
    QDir().mkpath(dir);
    const QString part = QDir(dir).filePath(kYtdlpFile + ".part");
    auto *file = new QFile(part, this);
    if (!file->open(QIODevice::WriteOnly)) { setBusy(false); emit installFinished("yt-dlp", false, tr("Zielordner nicht beschreibbar"), false); return; }
    auto *r = get(info.value("url").toString());
    connect(r, &QNetworkReply::readyRead, this, [r, file] { file->write(r->readAll()); });
    connect(r, &QNetworkReply::downloadProgress, this, [this](qint64 a, qint64 b) { emit progress("yt-dlp", a, b); });
    connect(r, &QNetworkReply::finished, this, [this, r, file, part, dir, ver, info] {
        r->deleteLater();
        file->write(r->readAll());
        file->close();
        file->deleteLater();
        auto fail = [&](const QString &msg) { QDir(dir).removeRecursively(); setBusy(false); emit installFinished("yt-dlp", false, msg, false); };
        if (r->error() != QNetworkReply::NoError) return fail(r->errorString());
        // Prüfsumme gegen die offizielle SHA2-256SUMS
        auto *s = get(info.value("sums").toString());
        connect(s, &QNetworkReply::finished, this, [this, s, part, dir, ver] {
            s->deleteLater();
            const QString sums = QString::fromUtf8(s->readAll());
            QString expected;
            for (const auto &line : sums.split('\n')) {
                const auto parts = line.trimmed().split(QRegularExpression("\\s+"));
                if (parts.size() == 2 && QString(parts[1]).remove('*') == kYtdlpAsset) expected = parts[0].toLower();
            }
            if (expected.isEmpty() || expected != sha256Of(part)) {
                QDir(dir).removeRecursively(); setBusy(false);
                emit installFinished("yt-dlp", false, tr("Prüfsumme stimmt nicht – Download verworfen"), false);
                return;
            }
            const QString exe = QDir(dir).filePath(kYtdlpFile);
            QFile::remove(exe);
            QFile::rename(part, exe);
            QFile::setPermissions(exe, QFile::permissions(exe) | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
            QSaveFile vf(QDir(dir).filePath("version.json"));
            if (vf.open(QIODevice::WriteOnly)) { vf.write(QJsonDocument(QJsonObject{{"version", ver}}).toJson()); vf.commit(); }
            Logger::write("update", "yt-dlp " + ver + " installiert");
            setBusy(false);
            emit installedChanged();
            emit installFinished("yt-dlp", true, "yt-dlp " + ver + " installiert", false);
        });
    });
}

void ToolUpdater::installMpv(const QVariantMap &info)
{
    const QString build = info.value("latest").toString();
    // QTemporaryDir legt keine fehlenden Elternordner an: ohne <Daten>/tools (frische Installation, noch nie
    // ein Werkzeug aktualisiert) schlug das libmpv-Update mit "Temporärer Ordner nicht beschreibbar" fehl
    const QString toolsDir = QDir(m_dataDir).filePath("tools");
    QDir().mkpath(toolsDir);
    auto *tmp = new QTemporaryDir(QDir(toolsDir).filePath(".tmp-mpv-XXXXXX"));
    const QString archive = QDir(tmp->path()).filePath("mpv-dev.7z");
    auto *file = new QFile(archive, this);
    if (!tmp->isValid() || !file->open(QIODevice::WriteOnly)) {
        const QString why = !tmp->isValid() ? tmp->errorString() : file->errorString();
        Logger::write("update", "libmpv: temporärer Ordner unter " + toolsDir + " nicht beschreibbar: " + why);
        delete tmp; setBusy(false);
        emit installFinished("libmpv", false, tr("Temporärer Ordner nicht beschreibbar") + " (" + QDir::toNativeSeparators(toolsDir) + ": " + why + ")", false);
        return;
    }
    auto *r = get(info.value("url").toString());
    connect(r, &QNetworkReply::readyRead, this, [r, file] { file->write(r->readAll()); });
    connect(r, &QNetworkReply::downloadProgress, this, [this](qint64 a, qint64 b) { emit progress("libmpv", a, b); });
    connect(r, &QNetworkReply::finished, this, [this, r, file, tmp, archive, build, info] {
        r->deleteLater();
        file->write(r->readAll());
        file->close();
        file->deleteLater();
        auto done = [&](bool ok, const QString &msg) { delete tmp; setBusy(false); if (ok) emit installedChanged(); emit installFinished("libmpv", ok, msg, ok); };
        if (r->error() != QNetworkReply::NoError) return done(false, r->errorString());
        const QString digest = info.value("digest").toString();
        if (digest.startsWith("sha256:") && digest.mid(7).toLower() != sha256Of(archive))
            return done(false, tr("Prüfsumme stimmt nicht – Download verworfen"));
        const QString sevenZip = QDir(m_appDir).filePath("tools/7zr.exe");
        if (!QFileInfo::exists(sevenZip)) return done(false, "7zr.exe fehlt im Programmordner (tools/)");
        QProcess p;
        p.start(sevenZip, {"x", "-y", "-bso0", "-bsp0", "-o" + QDir(tmp->path()).filePath("x"), archive});
        if (!p.waitForFinished(180000) || p.exitCode() != 0) return done(false, "Entpacken fehlgeschlagen");
        QString dll;
        QDirIterator it(QDir(tmp->path()).filePath("x"), {"libmpv-2.dll"}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) dll = it.next();
        if (dll.isEmpty()) return done(false, tr("libmpv-2.dll nicht im Archiv gefunden"));
        const QString dest = QDir(m_dataDir).filePath("tools/mpv/" + build);
        QDir().mkpath(dest);
        QFile::remove(QDir(dest).filePath("libmpv-2.dll"));
        if (!QFile::copy(dll, QDir(dest).filePath("libmpv-2.dll"))) return done(false, "Kopieren fehlgeschlagen");
        QSaveFile vf(QDir(dest).filePath("version.json"));
        if (vf.open(QIODevice::WriteOnly)) { vf.write(QJsonDocument(QJsonObject{{"version", build}}).toJson()); vf.commit(); }
        Logger::write("update", "libmpv " + build + " installiert (aktiv nach Neustart)");
        done(true, tr("libmpv-Build ") + build + tr(" installiert – wird nach einem Neustart verwendet"));
    });
}

} // namespace uve
