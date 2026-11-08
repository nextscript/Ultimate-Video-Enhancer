#include "AppUpdater.h"
#include "ResumableDownload.h"
#include "ToolUpdater.h"
#include "../core/Logger.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
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
#include <QSettings>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace uve {

static const char *kRepo = "https://github.com/nextscript/Ultimate-Video-Enhancer";
static const char *kBranch = "uve-player";
static const char *kTagPrefix = "uve-player-v";
static const char *kRefsApi = "https://api.github.com/repos/nextscript/Ultimate-Video-Enhancer/git/refs?per_page=100";

AppUpdater::AppUpdater(const Build &build, const QString &appDir, const QString &dataDir, QObject *parent)
    : QObject(parent), m_build(build), m_appDir(appDir), m_dataDir(dataDir), m_kind(detectInstallKind(appDir)),
      m_repo(QString::fromLatin1(kRepo)), m_refsFallback(QString::fromLatin1(kRefsApi)), m_net(new QNetworkAccessManager(this))
{
}

void AppUpdater::setBusy(bool b)
{
    if (m_busy == b) return;
    m_busy = b;
    emit busyChanged();
}

QString AppUpdater::updateDir() const { return QDir(m_dataDir).filePath(QStringLiteral("updates")); }

// ---------------------------------------------------------------- Reine Hilfsfunktionen
QString AppUpdater::detectInstallKind(const QString &appDir)
{
#if defined(Q_OS_WIN)
    if (!qEnvironmentVariableIsEmpty("UVE_PORTABLE_EXE")) return QStringLiteral("portable-exe");
    if (QFileInfo::exists(QDir(appDir).filePath(QStringLiteral("portable.txt")))) return QStringLiteral("portable-zip");
    if (QFileInfo::exists(QDir(appDir).filePath(QStringLiteral("Uninstall.exe")))) return QStringLiteral("setup");
#elif defined(Q_OS_MACOS)
    if (appDir.contains(QLatin1String(".app/Contents/MacOS"))) return QStringLiteral("dmg");
#else
    Q_UNUSED(appDir);
    if (!qEnvironmentVariableIsEmpty("APPIMAGE")) return QStringLiteral("appimage");
#endif
    return QStringLiteral("manual");
}

QString AppUpdater::assetName(const QString &kind, const QString &version, const QString &cpuArch)
{
    const QString base = QStringLiteral("UVE-Player-") + version;
    if (kind == QLatin1String("setup")) return base + QStringLiteral("-setup.exe");
    if (kind == QLatin1String("portable-exe")) return base + QStringLiteral("-portable.exe");
    if (kind == QLatin1String("portable-zip")) return base + QStringLiteral("-portable.zip");
    if (kind == QLatin1String("appimage")) return base + QStringLiteral("-linux-x86_64.AppImage");
    if (kind == QLatin1String("dmg")) return base + QStringLiteral("-macos-") + (cpuArch == QLatin1String("x86_64") ? cpuArch : QStringLiteral("arm64")) + QStringLiteral(".dmg");
    return {};
}

QHash<QString, QString> AppUpdater::parseRefs(const QByteArray &data)
{
    QHash<QString, QString> refs;
    static const QRegularExpression shaRe(QStringLiteral("^[0-9a-f]{40}$"));

    if (data.trimmed().startsWith('[')) {   // REST-API
        for (const auto &v : QJsonDocument::fromJson(data).array()) {
            const auto o = v.toObject();
            const QString sha = o.value("object").toObject().value("sha").toString();
            if (shaRe.match(sha).hasMatch()) refs.insert(o.value("ref").toString(), sha);
        }
        return refs;
    }
    // Git-pkt-line: 4 Hex-Zeichen Länge (inkl. dieser 4), "0000" trennt Abschnitte
    qsizetype pos = 0;
    while (pos + 4 <= data.size()) {
        bool ok = false;
        const int len = data.mid(pos, 4).toInt(&ok, 16);
        if (!ok) break;
        if (len < 4) { pos += 4; continue; }   // Flush-/Trennpaket
        if (pos + len > data.size()) break;
        QByteArray line = data.mid(pos + 4, len - 4);
        pos += len;
        const qsizetype nul = line.indexOf('\0');   // Fähigkeiten hinter dem ersten Ref
        if (nul >= 0) line.truncate(nul);
        const QString text = QString::fromUtf8(line).trimmed();
        const qsizetype sp = text.indexOf(QLatin1Char(' '));
        if (sp != 40 || !shaRe.match(text.left(40)).hasMatch()) continue;
        QString ref = text.mid(41);
        // Annotierter Tag: Eintrag "^{}" nennt den eigentlichen Commit und ersetzt das Tag-Objekt
        if (ref.endsWith(QLatin1String("^{}"))) ref.chop(3);
        refs.insert(ref, text.left(40));
    }
    return refs;
}

QStringList AppUpdater::newerReleaseVersions(const QHash<QString, QString> &refs, const QString &currentVersion)
{
    static const QRegularExpression re(QStringLiteral("^refs/tags/%1(\\d+(?:\\.\\d+)*)$").arg(QString::fromLatin1(kTagPrefix)));
    QStringList out;
    for (auto it = refs.constBegin(); it != refs.constEnd(); ++it) {
        const auto m = re.match(it.key());
        if (m.hasMatch() && ToolUpdater::compareToolVersions(m.captured(1), currentVersion) > 0) out << m.captured(1);
    }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return ToolUpdater::compareToolVersions(a, b) > 0; });
    return out;
}

QVariantList AppUpdater::parseCommitFeed(const QByteArray &atom)
{
    QVariantList out;
    static const QRegularExpression shaRe(QStringLiteral("Commit/([0-9a-f]{40})$"));
    QXmlStreamReader x(atom);
    QString sha, title, date;
    bool inEntry = false;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement()) {
            const auto name = x.name();
            if (name == QLatin1String("entry")) { inEntry = true; sha.clear(); title.clear(); date.clear(); }
            else if (inEntry && name == QLatin1String("id")) {
                const auto m = shaRe.match(x.readElementText().trimmed());
                if (m.hasMatch()) sha = m.captured(1);
            }
            else if (inEntry && name == QLatin1String("title")) title = x.readElementText().simplified();
            else if (inEntry && name == QLatin1String("updated")) date = x.readElementText().trimmed();
        } else if (x.isEndElement() && x.name() == QLatin1String("entry")) {
            inEntry = false;
            if (!sha.isEmpty()) out << QVariantMap{{"sha", sha}, {"shortSha", sha.left(7)}, {"title", title}, {"date", date}};
        }
    }
    return out;
}

QVariantList AppUpdater::commitsSince(const QVariantList &feed, const QString &commit, const QString &commitTime, bool *truncated)
{
    QVariantList out;
    if (truncated) *truncated = false;
    if (commit.isEmpty()) return out;
    for (const auto &v : feed) {
        if (v.toMap().value("sha").toString().startsWith(commit)) return out;   // eigener Stand erreicht
        out << v;
    }
    // Eigener Commit nicht im Feed: älter als der Feed reicht – oder ein lokaler, nie hochgeladener Stand.
    // Das Datum trennt beides, damit ein lokaler Build keine alten Commits als "neu" meldet.
    const QDateTime own = QDateTime::fromString(commitTime, Qt::ISODate);
    if (own.isValid()) {
        QVariantList newer;
        for (const auto &v : out)
            if (QDateTime::fromString(v.toMap().value("date").toString(), Qt::ISODate) > own) newer << v;
        if (newer.size() < out.size()) return newer;
    }
    if (truncated) *truncated = !out.isEmpty();
    return out;
}

QHash<QString, QString> AppUpdater::parseSha256Sums(const QByteArray &text)
{
    QHash<QString, QString> sums;
    static const QRegularExpression re(QStringLiteral("^([0-9a-fA-F]{64})\\s+\\*?(\\S.*)$"));
    for (const auto &line : QString::fromUtf8(text).split(QLatin1Char('\n'))) {
        const auto m = re.match(line.trimmed());
        if (m.hasMatch()) sums.insert(m.captured(2), m.captured(1).toLower());
    }
    return sums;
}

// ---------------------------------------------------------------- Netzwerk
// Holt die erste erreichbare Adresse. Vorübergehende Fehler werden je Adresse einmal wiederholt.
void AppUpdater::fetch(const QStringList &urls, FetchDone done, int attempt)
{
    QNetworkRequest req{QUrl(urls.first())};
    req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("UVE-Player"));
    req.setRawHeader("Accept", "*/*");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);
    auto *r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, urls, done, attempt] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (r->error() == QNetworkReply::NoError) return done(true, status, r->readAll());
        if (status == 404) return done(false, status, {});
        if (attempt == 0) {
            QTimer::singleShot(1500, this, [this, urls, done] { fetch(urls, done, 1); });
            return;
        }
        Logger::write("update", urls.first() + ": " + r->errorString());
        if (urls.size() > 1) return fetch(urls.mid(1), done, 0);
        done(false, status, {});
    });
}

// ---------------------------------------------------------------- Prüfen
void AppUpdater::check()
{
    if (m_busy) return;
    setBusy(true);
    m_result = {};
    m_error.clear();
    m_refs.clear();
    m_candidates.clear();
    m_result.insert("currentVersion", m_build.version);
    m_result.insert("currentCommit", m_build.commit);
    m_result.insert("currentShort", m_build.commit.left(7));
    m_result.insert("installKind", m_kind);
    m_result.insert("updateAvailable", false);
    m_result.insert("canInstall", false);
    m_result.insert("newCommits", QVariantList{});

    fetch({m_repo + QStringLiteral(".git/info/refs?service=git-upload-pack"), m_refsFallback},
          [this](bool ok, int, const QByteArray &body) {
              if (!ok) { m_error = tr("GitHub ist nicht erreichbar"); return finishCheck(); }
              onRefs(body);
          });
}

void AppUpdater::onRefs(const QByteArray &body)
{
    m_refs = parseRefs(body);
    const QString tip = m_refs.value(QStringLiteral("refs/heads/") + QString::fromLatin1(kBranch));
    if (tip.isEmpty()) { m_error = tr("Unerwartete Antwort von GitHub"); return finishCheck(); }
    m_result.insert("branchCommit", tip);
    m_result.insert("branchShort", tip.left(7));
    m_candidates = newerReleaseVersions(m_refs, m_build.version);
    probeRelease();
}

// Ein Tag allein reicht nicht: Während der Release-Workflow läuft, gibt es ihn schon, die Pakete aber noch nicht.
// Deshalb gilt die neueste Version, deren SHA256SUMS.txt und Paket tatsächlich abrufbar sind.
void AppUpdater::probeRelease()
{
    if (m_candidates.isEmpty()) return fetchCommits();
    const QString version = m_candidates.takeFirst();
    const QString tag = QString::fromLatin1(kTagPrefix) + version;
    fetch({m_repo + QStringLiteral("/releases/download/") + tag + QStringLiteral("/SHA256SUMS.txt")},
          [this, version, tag](bool ok, int status, const QByteArray &body) {
              if (!ok) {
                  if (status == 404) return probeRelease();
                  m_error = tr("Release-Dateien sind gerade nicht abrufbar");
                  return fetchCommits();
              }
              const auto sums = parseSha256Sums(body);
              const QString asset = assetName(m_kind, version, QSysInfo::currentCpuArchitecture());
              if (asset.isEmpty() || !sums.contains(asset)) return onReleaseFound(version, sums, 0);

              // Größe per HEAD (zählt nicht gegen ein API-Kontingent); 404 = Paket wird gerade noch hochgeladen
              QNetworkRequest req{QUrl(m_repo + QStringLiteral("/releases/download/") + tag + QLatin1Char('/') + asset)};
              req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("UVE-Player"));
              req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
              req.setTransferTimeout(20000);
              auto *r = m_net->head(req);
              connect(r, &QNetworkReply::finished, this, [this, r, version, sums] {
                  r->deleteLater();
                  if (r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404) return probeRelease();
                  onReleaseFound(version, sums, r->header(QNetworkRequest::ContentLengthHeader).toLongLong());
              });
          });
}

void AppUpdater::onReleaseFound(const QString &version, const QHash<QString, QString> &sums, qint64 size)
{
    const QString tag = QString::fromLatin1(kTagPrefix) + version;
    const QString commit = m_refs.value(QStringLiteral("refs/tags/") + tag);
    const QString asset = assetName(m_kind, version, QSysInfo::currentCpuArchitecture());
    m_sha256 = sums.value(asset);
    m_candidates.clear();
    m_result.insert("updateAvailable", true);
    m_result.insert("latestVersion", version);
    m_result.insert("latestCommit", commit);
    m_result.insert("latestShort", commit.left(7));
    m_result.insert("releaseTag", tag);
    m_result.insert("assetName", m_sha256.isEmpty() ? QString() : asset);
    m_result.insert("assetSize", size);
    m_result.insert("canInstall", !m_sha256.isEmpty());
    fetchCommits();
}

void AppUpdater::fetchCommits()
{
    fetch({m_repo + QStringLiteral("/commits/") + QString::fromLatin1(kBranch) + QStringLiteral(".atom")},
          [this](bool ok, int, const QByteArray &body) {
              const QString tip = m_result.value("branchCommit").toString();
              const bool sameCommit = m_build.commit.isEmpty() || tip.startsWith(m_build.commit);
              if (ok && !sameCommit) {
                  bool truncated = false;
                  const auto list = commitsSince(parseCommitFeed(body), m_build.commit, m_build.commitTime, &truncated);
                  m_result.insert("newCommits", list);
                  m_result.insert("commitsTruncated", truncated);
                  m_result.insert("newerCommits", !list.isEmpty());
              } else {
                  // Ohne Feed bleibt nur der Vergleich der Commit-Kennungen; die Liste fehlt dann in der Anzeige
                  m_result.insert("newerCommits", !sameCommit);
              }
              finishCheck();
          });
}

void AppUpdater::finishCheck()
{
    setBusy(false);
    if (!m_error.isEmpty()) Logger::write("update", "UVE Player: " + m_error);
    if (m_error.isEmpty()) cleanupUpdateFolder(m_result.value("latestVersion").toString());
    emit checkFinished(m_result, m_error);
}

// Pakete früherer Versionen entfernen; das Paket und ein angefangener Download der aktuellen Version bleiben
void AppUpdater::cleanupUpdateFolder(const QString &keepVersion)
{
    QDir d(updateDir());
    for (const auto &name : d.entryList(QDir::Files)) {
        if (name == QLatin1String("install-update.log")) continue;   // Protokoll des letzten Updates zur Fehlersuche
        if (!keepVersion.isEmpty() && name.contains(QStringLiteral("-") + keepVersion + QStringLiteral("-"))) continue;
        d.remove(name);
    }
}

void AppUpdater::openReleasePage() const
{
    const QString tag = m_result.value("releaseTag").toString();
    QDesktopServices::openUrl(QUrl(tag.isEmpty() ? m_repo + QStringLiteral("/commits/") + QString::fromLatin1(kBranch)
                                                 : m_repo + QStringLiteral("/releases/tag/") + tag));
}

// ---------------------------------------------------------------- Installieren
static QString sha256File(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

void AppUpdater::install()
{
    if (m_busy) return;
    if (!m_result.value("canInstall").toBool()) { emit installFailed(tr("Keine Update-Information – bitte erneut prüfen")); return; }
    setBusy(true);
    QDir().mkpath(updateDir());
    const QString file = QDir(updateDir()).filePath(m_result.value("assetName").toString());
    if (QFileInfo::exists(file) && sha256File(file) == m_sha256) {   // z. B. nach abgelehnter Windows-Abfrage
        setBusy(false);
        emit packageReady(file);
        if (m_launchEnabled) launch(file);
        return;
    }
    QFile::remove(file);
    startDownload(true);
}

void AppUpdater::startDownload(bool retryAfterMismatch)
{
    const QString asset = m_result.value("assetName").toString();
    const QUrl url(m_repo + QStringLiteral("/releases/download/") + m_result.value("releaseTag").toString() + QLatin1Char('/') + asset);
    auto *dl = new ResumableDownload(m_net, url, QDir(updateDir()).filePath(asset + QStringLiteral(".part")), this);
    m_download = dl;
    connect(dl, &ResumableDownload::progress, this, &AppUpdater::progress);
    connect(dl, &ResumableDownload::retrying, this, &AppUpdater::retrying);
    connect(dl, &ResumableDownload::finished, this, [this, dl, retryAfterMismatch](bool ok, const QString &error) {
        dl->deleteLater();
        if (!ok) { setBusy(false); emit installFailed(error); return; }
        onDownloaded(retryAfterMismatch);
    });
    dl->start();
}

void AppUpdater::onDownloaded(bool retryAfterMismatch)
{
    const QString file = QDir(updateDir()).filePath(m_result.value("assetName").toString());
    const QString part = file + QStringLiteral(".part");
    if (sha256File(part) != m_sha256) {
        QFile::remove(part);   // nie fortsetzen: Der Fehler könnte im vorhandenen Teil stecken
        if (retryAfterMismatch) return startDownload(false);
        setBusy(false);
        emit installFailed(tr("Prüfsumme stimmt nicht – Download verworfen"));
        return;
    }
    if (!QFile::rename(part, file)) {
        setBusy(false);
        emit installFailed(tr("Schreiben fehlgeschlagen: %1").arg(file));
        return;
    }
    setBusy(false);
    emit packageReady(file);
    if (m_launchEnabled) launch(file);
}

void AppUpdater::cancelInstall()
{
    if (m_download) m_download->abort();
}

void AppUpdater::launch(const QString &file)
{
    if (m_kind == QLatin1String("setup") || m_kind == QLatin1String("portable-exe") || m_kind == QLatin1String("portable-zip"))
        return launchWindowsHelper(file);

    if (m_kind == QLatin1String("dmg")) {
        // Ein Programm in /Applications lässt sich nicht still ersetzen → Image öffnen, Benutzer zieht es hinüber
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(file))) { emit installFailed(tr("Datei konnte nicht geöffnet werden: %1").arg(file)); return; }
        emit installHandedOff(tr("Das Installations-Image wurde geöffnet. Bitte UVE Player beenden und das Programm in den Ordner „Programme“ ziehen."));
        return;
    }
    if (m_kind == QLatin1String("appimage")) {
        // Unter Linux darf eine laufende Datei ersetzt werden; rename nur innerhalb desselben Ordners ist atomar
        const QString target = qEnvironmentVariable("APPIMAGE");
        const QString tmp = target + QStringLiteral(".new");
        QFile::remove(tmp);
        if (!QFile::copy(file, tmp)
            || !QFile::setPermissions(tmp, QFile::permissions(target) | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther)) {
            QFile::remove(tmp);
            emit installFailed(tr("Schreiben fehlgeschlagen: %1").arg(tmp));
            return;
        }
        QFile::remove(target);
        if (!QFile::rename(tmp, target)) { emit installFailed(tr("Schreiben fehlgeschlagen: %1").arg(target)); return; }
        QFile::remove(file);
        emit restartRequested();
        return;
    }
    emit installFailed(tr("Diese Installationsart kann nicht automatisch aktualisiert werden"));
}

// Wartet außerhalb des Players auf dessen Ende, installiert und startet ihn wieder (ohne erhöhte Rechte).
// Pfade kommen als Parameter und werden nie in den Skripttext eingesetzt.
static const char kHelperScript[] = R"PS(
param([int]$WaitPid, [string]$Mode, [string]$Package, [string]$Target, [string]$OldExe, [string]$Restart, [string]$SetupMode, [string]$Log)
$ErrorActionPreference = 'Stop'
function Write-Log([string]$m) { try { Add-Content -LiteralPath $Log -Value ("{0} {1}" -f (Get-Date -Format s), $m) } catch {} }
Write-Log "start mode=$Mode package=$Package"
try {
    $p = Get-Process -Id $WaitPid -ErrorAction SilentlyContinue
    if ($p) {
        if (-not $p.WaitForExit(60000)) { Stop-Process -Id $WaitPid -Force; [void]$p.WaitForExit(10000) }
    }
    Start-Sleep -Milliseconds 500
    switch ($Mode) {
        'setup' {
            $installerArgs = @('/S'); if ($SetupMode) { $installerArgs += $SetupMode }
            # ShellExecute beachtet das Manifest des Installers (Windows-Abfrage nur bei Bedarf)
            $r = $null
            try { $r = Start-Process -FilePath $Package -ArgumentList $installerArgs -Wait -PassThru }
            catch { Write-Log "Installer nicht gestartet (Windows-Abfrage abgelehnt?): $_" }
            if ($r) {
                Write-Log "Installer beendet: $($r.ExitCode)"
                if ($r.ExitCode -eq 0) { Remove-Item -LiteralPath $Package -Force -ErrorAction SilentlyContinue }
                else {
                    # Stille Installation gescheitert: sichtbaren Assistenten zeigen, er startet den Player selbst
                    Start-Process -FilePath $Package
                    exit 0
                }
            }
        }
        'portable-exe' {
            # Der alte Starter läuft noch kurz weiter (räumt seinen Temp-Ordner auf)
            $removed = $false
            for ($i = 0; $i -lt 240 -and -not $removed; $i++) {
                try { Remove-Item -LiteralPath $OldExe -Force; $removed = $true } catch { Start-Sleep -Milliseconds 500 }
            }
            if (-not $removed -and $OldExe -ieq $Target) { throw "Alte EXE bleibt gesperrt: $OldExe" }
            Move-Item -LiteralPath $Package -Destination $Target -Force
            $Restart = $Target
        }
        'portable-zip' {
            $tmp = Join-Path (Split-Path -Parent $Package) 'extract'
            if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
            Expand-Archive -LiteralPath $Package -DestinationPath $tmp -Force
            Get-ChildItem -LiteralPath $tmp -Force | Copy-Item -Destination $Target -Recurse -Force
            Remove-Item -LiteralPath $tmp -Recurse -Force
            Remove-Item -LiteralPath $Package -Force -ErrorAction SilentlyContinue
        }
    }
    Write-Log 'fertig'
} catch {
    Write-Log "Fehler: $_"
}
Start-Process -FilePath $Restart -WorkingDirectory (Split-Path -Parent $Restart)
)PS";

void AppUpdater::launchWindowsHelper(const QString &file)
{
#ifdef Q_OS_WIN
    const QString script = QDir(updateDir()).filePath(QStringLiteral("install-update.ps1"));
    QSaveFile out(script);
    if (!out.open(QIODevice::WriteOnly) || out.write(kHelperScript) < 0 || !out.commit()) {
        emit installFailed(tr("Schreiben fehlgeschlagen: %1").arg(script));
        return;
    }

    const QString appExe = QCoreApplication::applicationFilePath();
    QString target, oldExe, restart = appExe, setupMode;
    if (m_kind == QLatin1String("portable-exe")) {
        oldExe = qEnvironmentVariable("UVE_PORTABLE_EXE");
        const QFileInfo old(oldExe);
        // Trägt die alte EXE den Standardnamen, zieht der Name mit; einen eigenen Namen behält der Benutzer
        static const QRegularExpression standard(QStringLiteral("^UVE-Player-[0-9.]+-portable\\.exe$"), QRegularExpression::CaseInsensitiveOption);
        target = standard.match(old.fileName()).hasMatch() ? old.dir().filePath(QFileInfo(file).fileName()) : oldExe;
        restart = oldExe;
    } else if (m_kind == QLatin1String("portable-zip")) {
        target = m_appDir;
    } else {
        // Der Modus der bestehenden Installation, damit keine zweite Kopie im anderen Bereich entsteht
        const QString native = QDir::toNativeSeparators(QDir(m_appDir).absolutePath());
        for (const char *hive : {"HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE"}) {
            QSettings reg(QString::fromLatin1(hive) + QStringLiteral("\\Software\\UVE Player"), QSettings::Registry64Format);
            const QString dir = reg.value("InstallDir").toString();
            if (!dir.isEmpty() && QDir::toNativeSeparators(QDir(dir).absolutePath()).compare(native, Qt::CaseInsensitive) == 0) {
                setupMode = QLatin1Char('/') + reg.value("InstallMode").toString();
                break;
            }
        }
        if (setupMode.size() < 2) setupMode.clear();
    }

    // Leere Werte würde Windows PowerShell bei -File verschlucken → optionale Parameter nur setzen, wenn gefüllt
    QStringList args{QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                     QStringLiteral("-File"), QDir::toNativeSeparators(script),
                     QStringLiteral("-WaitPid"), QString::number(QCoreApplication::applicationPid()),
                     QStringLiteral("-Mode"), m_kind,
                     QStringLiteral("-Package"), QDir::toNativeSeparators(file),
                     QStringLiteral("-Restart"), QDir::toNativeSeparators(restart),
                     QStringLiteral("-Log"), QDir::toNativeSeparators(QDir(updateDir()).filePath(QStringLiteral("install-update.log")))};
    if (!target.isEmpty()) args << QStringLiteral("-Target") << QDir::toNativeSeparators(target);
    if (!oldExe.isEmpty()) args << QStringLiteral("-OldExe") << QDir::toNativeSeparators(oldExe);
    if (!setupMode.isEmpty()) args << QStringLiteral("-SetupMode") << setupMode;

    QProcess p;
    p.setProgram(qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows")) + QStringLiteral("\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"));
    p.setArguments(args);
    p.setWorkingDirectory(updateDir());   // nie im Temp-Ordner der portablen EXE: er wird beim Beenden gelöscht
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
    if (!p.startDetached()) {
        emit installFailed(tr("Das Update konnte nicht gestartet werden: %1").arg(p.errorString()));
        return;
    }
    Logger::write("update", "Installation übergeben: " + file);
    emit quitRequested();
#else
    Q_UNUSED(file);
    emit installFailed(tr("Diese Installationsart kann nicht automatisch aktualisiert werden"));
#endif
}

} // namespace uve
