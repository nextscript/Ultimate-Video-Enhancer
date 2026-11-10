#include "OpticalMediaService.h"
#include "DiscNavigationBridge.h"

#include "core/Logger.h"
#include "player/MpvController.h"
#include "services/PlaylistModel.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace uve {

using namespace optical;

namespace {

// Windows: "D:" ist für Dateizugriffe das aktuelle Verzeichnis auf D: – die Wurzel ist "D:\"
QString fsRoot(const QString &device)
{
    if (device.size() == 2 && device[0].isLetter() && device[1] == QLatin1Char(':')) return device + QLatin1Char('\\');
    return device;
}

bool isDiscMrl(const QString &path)
{
    for (const char *p : {"dvd://", "dvdnav://", "bd://", "bluray://", "cdda://", "uvecdda://"})
        if (path.startsWith(QLatin1String(p))) return true;
    return false;
}

template<typename T, typename Fn>
void runInBackground(QObject *context, std::function<T()> work, Fn done)
{
    auto *watcher = new QFutureWatcher<T>(context);
    QObject::connect(watcher, &QFutureWatcher<T>::finished, context, [watcher, done] {
        done(watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(std::move(work)));
}

} // namespace

OpticalMediaService::OpticalMediaService(MpvController *player, PlaylistModel *playlist, const QString &appDir, QObject *parent)
    : QObject(parent), m_player(player), m_playlist(playlist), m_appDir(appDir)
{
    // Wird aus mpv-Threads aufgerufen: ohne gemeinsamen Zustand
    m_cdFactory = [](const QString &device) { return openCdDevice(device); };
}

void OpticalMediaService::attach()
{
    QVariantMap probe = DiscCapabilities::probe(m_player);
    const bool reader = systemCdReaderAvailable() || qEnvironmentVariableIsSet("UVE_TEST_FAKE_CD");
    probe.insert("cddaReader", reader && registerCdAudioProtocol(m_player->handle(), &m_cdFactory));
    QString bdjReason;
    probe.insert("bdj", DiscCapabilities::findBdjRuntime(m_appDir, &bdjReason));
    probe.insert("bdjReason", bdjReason);
    m_caps = DiscCapabilities::fromProbe(probe);
    Logger::write("disc", QStringLiteral("Fähigkeiten: DVD %1 (Menüs %2), Blu-ray %3 (Menüs %4, BD-J %5), Audio-CD %6 (%7), discnav %8; %9")
                              .arg(m_caps.supports(DiscKind::Dvd)).arg(m_caps.menus(DiscKind::Dvd))
                              .arg(m_caps.supports(DiscKind::Bluray)).arg(m_caps.menus(DiscKind::Bluray)).arg(m_caps.bdj)
                              .arg(m_caps.supports(DiscKind::AudioCd))
                              .arg(m_caps.cddaReader ? QStringLiteral("uvecdda") : m_caps.cddaNative ? QStringLiteral("cdda") : QStringLiteral("-"))
                              .arg(m_caps.discnav).arg(m_caps.buildFlags.join(' ')));
    emit capabilitiesChanged();

    for (const char *p : {"disc-menu-active", "chapter-list", "chapter", "edition-list", "current-edition"})
        m_player->observe(QString::fromLatin1(p));
    connect(m_player, &MpvController::observedPropertyChanged, this, &OpticalMediaService::onObserved);
    connect(m_player, &MpvController::fileLoaded, this, &OpticalMediaService::onFileLoaded);
    connect(m_player, &MpvController::endOfFile, this, [this](const QString &reason) {
        if (reason != QLatin1String("eof") && reason != QLatin1String("redirect")) setActive(DiscKind::Unknown);
    });
}

bool OpticalMediaService::navigationAvailable() const
{
    return (m_activeKind == DiscKind::Dvd || m_activeKind == DiscKind::Bluray) && m_caps.discnav && m_caps.supports(m_activeKind);
}

void OpticalMediaService::setActive(DiscKind kind)
{
    if (kind == m_activeKind) return;
    m_activeKind = kind;
    if (kind == DiscKind::Unknown) {
        m_menuActive = false;
        m_chapters.clear(); m_titles.clear(); m_chapter = -1;
        emit chaptersChanged();
    }
    emit stateChanged();
}

void OpticalMediaService::onFileLoaded()
{
    const QString path = m_player->getProperty(QStringLiteral("path")).toString();
    setActive(isDiscMrl(path) ? m_current.kind : DiscKind::Unknown);
    if (active()) Logger::write("disc", QStringLiteral("geladen: %1 (%2)").arg(path, m_current.device));
}

void OpticalMediaService::onObserved(const QString &name, const QVariant &value)
{
    if (name == QLatin1String("disc-menu-active")) {
        const bool on = value.toBool();
        if (on != m_menuActive) { m_menuActive = on; emit stateChanged(); }
        return;
    }
    if (name == QLatin1String("chapter-list")) {
        m_chapters.clear();
        int i = 0;
        for (const auto &v : value.toList()) {
            const auto m = v.toMap();
            m_chapters << QVariantMap{{"index", i}, {"title", m.value("title")}, {"time", m.value("time")}};
            ++i;
        }
    } else if (name == QLatin1String("chapter")) {
        m_chapter = value.isValid() ? value.toInt() : -1;
    } else if (name == QLatin1String("edition-list")) {
        m_titles.clear();
        for (const auto &v : value.toList()) {
            const auto m = v.toMap();
            m_titles << QVariantMap{{"id", m.value("id")}, {"title", m.value("title")}, {"current", m.value("selected")}};
        }
    } else if (name == QLatin1String("current-edition")) {
        for (auto &v : m_titles) { auto m = v.toMap(); m.insert("current", m.value("id").toInt() == value.toInt()); v = m; }
    } else {
        return;
    }
    if (active()) emit chaptersChanged();
}

bool OpticalMediaService::load(const QString &entry, double start, bool paused, QString *error)
{
    DiscSource s;
    if (!resolver::fromEntry(entry, &s)) { if (error) *error = errorText(int(DiscError::Incompatible)); return false; }
    const auto plan = resolver::loadPlan(s, m_caps);
    if (plan.error != DiscError::None) {
        if (error) *error = errorText(int(plan.error));
        Logger::write("disc", QStringLiteral("nicht ladbar: %1 – %2").arg(s.device, errorText(int(plan.error))));
        return false;
    }
    m_current = s;
    Logger::write("disc", QStringLiteral("lade %1 (%2)%3").arg(plan.url, s.device, plan.menu ? QStringLiteral(" im Disc-Menü") : QString()));
    // Titel/Disc-Menü: kein Startpunkt (der Menüstart bestimmt die Disc), Audio-CD-Spuren wie Dateien
    QStringList options = plan.fileOptions;
    if (!s.name.isEmpty()) options << QStringLiteral("force-media-title=") + resolver::quoteOptionValue(s.name);   // statt "dvd://" bzw. "2"
    m_player->load(plan.url, plan.menu ? 0 : start, paused, options);
    return true;
}

void OpticalMediaService::refreshDrives()
{
    if (m_scanning) return;
    m_scanning = true;
    emit drivesChanged();
    runInBackground<QList<DriveInfo>>(this, [] { return enumerateOpticalDrives(); }, [this](const QList<DriveInfo> &list) {
        m_drives.clear();
        for (const auto &d : list) m_drives << d.toMap();
        m_scanning = false;
        Logger::write("disc", QStringLiteral("%1 optische(s) Laufwerk(e)").arg(list.size()));
        emit drivesChanged();
    });
}

void OpticalMediaService::inspect(const QString &pathOrUrl)
{
    const QString path = pathOrUrl;
    runInBackground<resolver::Detection>(this, [path] { return resolver::detect(path); }, [this, path](const resolver::Detection &d) {
        QVariantMap m{{"kind", kindId(d.kind)}, {"device", d.device}, {"error", int(d.error)},
                      {"errorText", d.error == DiscError::None ? QString() : errorText(int(d.error))},
                      {"encrypted", d.encrypted}, {"bdj", d.bdj}, {"image", d.image},
                      {"supported", m_caps.supports(d.kind)}, {"menus", m_caps.menus(d.kind)}};
        if (d.error == DiscError::None && !m_caps.supports(d.kind)) m.insert("errorText", errorText(int(DiscError::MissingFeature)));
        else if (d.kind == DiscKind::Bluray && d.encrypted) m.insert("errorText", errorText(int(DiscError::Encrypted)));
        emit inspected(path, m);
    });
}

OpticalMediaService::Prepared OpticalMediaService::prepare(const QString &device, bool isDrive, bool menu, DiscKind knownKind)
{
    Prepared p;
    p.source.menu = menu;
    if (!isDrive) {
        const auto d = resolver::detect(device);
        p.error = d.error;
        p.source.kind = d.kind;
        p.source.device = d.device;
        p.source.encrypted = d.encrypted;
        p.source.name = QFileInfo(d.device).fileName();
        if (d.bdj) p.source.title = -2;   // Markierung für finishOpen (BD-J-Disc), wird dort zurückgesetzt
        return p;
    }
    p.source.device = device;
    const QString root = fsRoot(device);
    if (QFileInfo(root).isDir()) {
        const auto d = resolver::detectFolder(root);
        if (d.kind != DiscKind::Unknown) {
            p.source.kind = d.kind;
            p.source.encrypted = d.encrypted;
            if (d.bdj) p.source.title = -2;
            return p;
        }
    } else if (knownKind == DiscKind::Dvd || knownKind == DiscKind::Bluray) {
        p.source.kind = knownKind;   // Gerätedatei (z. B. /dev/sr0): libdvdread/libbluray öffnen sie selbst
        return p;
    }
    QString err;
    if (auto dev = openCdDevice(device, &err)) {
        for (const auto &t : dev->tracks(&err)) if (t.audio) p.tracks << t;
        if (!p.tracks.isEmpty()) { p.source.kind = DiscKind::AudioCd; return p; }
    }
    p.error = err == QLatin1String("access") ? DiscError::NoAccess
            : QFileInfo(root).isDir() ? DiscError::Incompatible : DiscError::NoMedium;
    return p;
}

void OpticalMediaService::openDrive(const QString &device, bool startInMenu)
{
    // Unbekannte Geräte (nicht in der Liste) werden nicht geöffnet – keine beliebigen Pfade über diesen Weg
    QVariantMap drive;
    for (const auto &v : m_drives) if (v.toMap().value("device").toString() == device) drive = v.toMap();
    if (drive.isEmpty()) { emit openFailed(errorText(int(DiscError::NoDrive))); return; }
    // Linux/macOS mit libmpv-cdda: Audio-CD direkt über cdda:// (Spuren als Kapitel)
    if (drive.value("kind").toString() == QLatin1String("cdda") && !m_caps.cddaReader) {
        Prepared p;
        p.source.kind = DiscKind::AudioCd;
        p.source.device = device;
        p.source.name = drive.value("label").toString();
        finishOpen(p);
        return;
    }
    const QString label = drive.value("label").toString();
    const DiscKind known = kindFromId(drive.value("kind").toString());
    runInBackground<Prepared>(this, [device, startInMenu, known] { return prepare(device, true, startInMenu, known); }, [this, label](Prepared p) {
        p.source.name = label;
        finishOpen(p);
    });
}

void OpticalMediaService::openSource(const QString &pathOrUrl, bool startInMenu)
{
    const QString path = pathOrUrl;
    runInBackground<Prepared>(this, [path, startInMenu] { return prepare(path, false, startInMenu, DiscKind::Unknown); }, [this](const Prepared &p) { finishOpen(p); });
}

void OpticalMediaService::finishOpen(const Prepared &prepared)
{
    Prepared p = prepared;
    const bool bdjDisc = p.source.title == -2;
    if (bdjDisc) p.source.title = -1;
    if (p.error != DiscError::None) { emit openFailed(errorText(int(p.error))); return; }
    const DiscKind kind = p.source.kind;
    if (!m_caps.supports(kind)) { emit openFailed(errorText(int(DiscError::MissingFeature))); return; }
    if (kind == DiscKind::Bluray && p.source.encrypted) { emit openFailed(errorText(int(DiscError::Encrypted))); return; }
    if (p.source.menu) {
        if (!m_caps.menus(kind)) {
            p.source.menu = false;
            emit osd(errorText(int(DiscError::MenuUnsupported)), 5000);
        } else if (kind == DiscKind::Bluray && bdjDisc && !m_caps.bdj) {
            p.source.menu = false;
            emit osd(errorText(int(DiscError::BdjNeedsRuntime)), 6000);
        }
    }
    const QString base = kindLabel(kindId(kind));
    if (kind == DiscKind::AudioCd && !p.tracks.isEmpty()) {
        // Eigener CD-Leser: jede Spur ein Eintrag (Weiter/Zurück, Zufall und Wiederholen wie bei Dateien)
        QStringList entries, titles;
        QList<double> durations;
        for (const auto &t : p.tracks) {
            DiscSource s = p.source;
            s.title = t.number;
            s.name = QStringLiteral("%1 – %2").arg(p.source.name.isEmpty() ? base : p.source.name, tr("Track %1").arg(t.number, 2, 10, QLatin1Char('0')));
            entries << resolver::toEntry(s);
            titles << s.name;
            durations << t.seconds();
        }
        const int at = m_playlist->add(entries);
        for (int i = 0; i < entries.size(); ++i) { m_playlist->setTitle(at + i, titles[i]); m_playlist->setDuration(at + i, durations[i]); }
        Logger::write("disc", QStringLiteral("Audio-CD %1: %2 Spuren").arg(p.source.device).arg(entries.size()));
        emit playRequested(at);
        return;
    }
    DiscSource s = p.source;
    s.name = s.name.isEmpty() || s.name == base ? base + QStringLiteral(" – ") + s.device : base + QStringLiteral(" – ") + s.name;
    const int at = m_playlist->add({resolver::toEntry(s)});
    m_playlist->setTitle(at, s.name);
    emit playRequested(at);
}

bool OpticalMediaService::navigate(const QString &action)
{
    const QStringList cmd = nav::command(action, m_activeKind, active(), m_caps);
    if (cmd.isEmpty()) return false;
    Logger::write("disc", cmd.join(QLatin1Char(' ')));
    return m_player->command(cmd);
}

QString OpticalMediaService::menuKeyAction(const QString &key) const { return nav::keyAction(key, menuActive()); }
QStringList OpticalMediaService::menuKeys() const { return nav::menuKeys(); }

void OpticalMediaService::selectChapter(int index)
{
    if (active() && index >= 0 && index < m_chapters.size()) m_player->setProperty(QStringLiteral("chapter"), qlonglong(index));
}

void OpticalMediaService::selectTitle(int editionId)
{
    if (!active()) return;
    for (const auto &t : m_titles)
        if (t.toMap().value("id").toInt() == editionId) { m_player->setProperty(QStringLiteral("edition"), qlonglong(editionId)); return; }
}

QString OpticalMediaService::kindLabel(const QString &kind) const
{
    if (kind == QLatin1String("dvd")) return tr("DVD");
    if (kind == QLatin1String("bluray")) return tr("Blu-ray");
    if (kind == QLatin1String("cdda")) return tr("Audio-CD");
    return tr("Unbekannt");
}

QString OpticalMediaService::errorText(int code) const
{
    switch (DiscError(code)) {
    case DiscError::None: return {};
    case DiscError::NoDrive: return tr("Kein optisches Laufwerk gefunden");
    case DiscError::NoMedium: return tr("Kein Medium im Laufwerk");
    case DiscError::Incompatible: return tr("Kein unterstützter Disc-Typ (DVD-Video, Blu-ray oder Audio-CD)");
    case DiscError::NoAccess: return tr("Kein Lesezugriff auf das Laufwerk oder die Datei");
    case DiscError::MissingFeature: return tr("Die mitgelieferte libmpv kann diesen Disc-Typ nicht öffnen");
    case DiscError::MenuUnsupported: return tr("Disc-Menüs werden von diesem Build nicht unterstützt – der Haupttitel wird abgespielt");
    case DiscError::BdjNeedsRuntime: return tr("BD-J-Menüs benötigen Java und libbluray-j2se – der Haupttitel wird abgespielt");
    case DiscError::Encrypted: return tr("Verschlüsselte Blu-ray in dieser Konfiguration nicht unterstützt");
    case DiscError::Removed: return tr("Die Disc wurde während der Wiedergabe entfernt");
    case DiscError::InvalidImage: return tr("Image beschädigt oder kein DVD-/Blu-ray-Image");
    }
    return {};
}

QString OpticalMediaService::failureText() const
{
    if (m_current.kind == DiscKind::Unknown) return {};
    const QString root = fsRoot(m_current.device);
    bool present = QFileInfo::exists(root);
    if (present && m_current.kind == DiscKind::AudioCd && m_caps.cddaReader) {
        QString err;
        auto dev = openCdDevice(m_current.device, &err);
        present = dev && !dev->tracks(&err).isEmpty();
    }
    if (!present) return errorText(int(DiscError::Removed));
    if (m_current.kind == DiscKind::Bluray && m_current.encrypted) return errorText(int(DiscError::Encrypted));
    return tr("Disc nicht abspielbar (beschädigt, kopiergeschützt oder vom Build nicht unterstützt)");
}

QVariantList OpticalMediaService::info() const
{
    const auto yes = [this](bool b) { return b ? tr("ja") : tr("nein"); };
    QVariantList out;
    auto row = [&out](const QString &label, const QString &value) { out << QVariantMap{{"label", label}, {"value", value}}; };
    if (m_current.kind != DiscKind::Unknown) {
        row(tr("Quelltyp"), kindLabel(kindId(m_current.kind)));
        row(tr("Gerät / Image"), m_current.device);
        if (m_current.kind == DiscKind::AudioCd) row(tr("Spur"), QString::number(qMax(1, m_current.title)));
        row(tr("Titel / Kapitel"), QStringLiteral("%1 / %2").arg(m_titles.size()).arg(m_chapters.size()));
        row(tr("Menümodus"), menuActive() ? tr("Disc-Menü aktiv") : m_current.menu ? tr("Start im Disc-Menü") : tr("Titelwiedergabe"));
        row(tr("Aktiv"), yes(active()));
    }
    row(tr("libmpv"), m_caps.mpvVersion);
    row(tr("DVD-Video"), yes(m_caps.supports(DiscKind::Dvd)));
    row(tr("DVD-Menüs"), yes(m_caps.menus(DiscKind::Dvd)));
    row(tr("Blu-ray (Titel)"), yes(m_caps.supports(DiscKind::Bluray)));
    row(tr("Blu-ray-Menüs (HDMV)"), yes(m_caps.menus(DiscKind::Bluray)));
    row(tr("BD-J"), m_caps.bdj ? tr("ja") : m_caps.bdjReason == QLatin1String("jar") ? tr("nein – libbluray-j2se-*.jar fehlt")
                                         : m_caps.bdjReason == QLatin1String("java") ? tr("nein – keine Java-Laufzeit")
                                                                                    : tr("nein – nicht in diesem Paket enthalten"));
    row(tr("Audio-CD"), m_caps.cddaReader ? tr("ja (eigener CD-Leser über libmpv)") : m_caps.cddaNative ? tr("ja (libmpv cdda://)") : tr("nein"));
    row(tr("Disc-Navigation (discnav)"), yes(m_caps.discnav));
    if (!m_caps.buildFlags.isEmpty()) row(tr("libmpv-Build"), m_caps.buildFlags.join(QStringLiteral(", ")));
    return out;
}

} // namespace uve
