#include "StationStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace uve::radio {

StationStore::StationStore(const QString &dataDir) : m_path(QDir(dataDir).filePath(QStringLiteral("library.json"))) {}

static QList<Station> readList(const QJsonArray &a, int limit)
{
    QList<Station> out;
    for (const auto &v : a) {
        const Station s = Station::fromJson(v.toObject());
        if (!s.isValid()) continue;
        bool dup = false;
        for (const auto &o : out) if (o.id == s.id) { dup = true; break; }
        if (!dup) out << s;
        if (limit > 0 && out.size() >= limit) break;
    }
    return out;
}

bool StationStore::load()
{
    m_error.clear();
    m_favorites.clear();
    m_history.clear();
    QFile f(m_path);
    if (!f.exists()) return true;
    if (!f.open(QIODevice::ReadOnly)) { m_error = f.errorString(); return false; }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    f.close();
    if (!doc.isObject()) {
        // Beschädigt: Kopie behalten, damit nichts endgültig verloren geht, und leer weitermachen
        const QString bad = m_path + QStringLiteral(".broken-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss"));
        QFile::copy(m_path, bad);
        m_error = pe.errorString();
        return false;
    }
    const QJsonObject root = doc.object();
    // Spätere Formatversionen lesen wir, soweit die Felder bekannt sind
    m_favorites = readList(root.value(QStringLiteral("favorites")).toArray(), 0);
    m_history = readList(root.value(QStringLiteral("history")).toArray(), kHistoryLimit);
    return true;
}

bool StationStore::save()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QJsonArray fav, hist;
    for (const auto &s : m_favorites) fav << s.toJson();
    for (const auto &s : m_history) hist << s.toJson();
    const QJsonObject root{{"format", "uveinetradio-library"}, {"version", kFormatVersion},
                           {"saved", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                           {"favorites", fav}, {"history", hist}};
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) { m_error = f.errorString(); return false; }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) { m_error = f.errorString(); return false; }
    return true;
}

bool StationStore::isFavorite(const QString &id) const
{
    for (const auto &s : m_favorites) if (s.id == id) return true;
    return false;
}

void StationStore::addFavorite(const Station &s)
{
    if (!s.isValid()) return;
    for (auto &o : m_favorites)
        if (o.id == s.id) {
            const QString working = o.lastWorkingUrl;
            o = s;
            if (o.lastWorkingUrl.isEmpty()) o.lastWorkingUrl = working;
            return;
        }
    m_favorites.prepend(s);
}

void StationStore::removeFavorite(const QString &id)
{
    m_favorites.removeIf([&id](const Station &s) { return s.id == id; });
}

bool StationStore::toggleFavorite(const Station &s)
{
    if (isFavorite(s.id)) { removeFavorite(s.id); return false; }
    addFavorite(s);
    return isFavorite(s.id);
}

void StationStore::markWorking(const Station &s, const QString &url)
{
    for (auto &o : m_favorites)
        if (o.id == s.id) o.lastWorkingUrl = url;
    for (auto &o : m_history)
        if (o.id == s.id) o.lastWorkingUrl = url;
}

bool StationStore::updateFromCatalog(const Station &fresh)
{
    bool changed = false;
    auto apply = [&fresh, &changed](Station &o) {
        if (o.id != fresh.id || o.custom) return;
        const bool urlsChanged = o.url != fresh.url || o.urlResolved != fresh.urlResolved;
        const QString working = o.lastWorkingUrl;
        o = fresh;
        // Gespeicherte Adresse nur behalten, wenn der Katalog sie noch kennt
        o.lastWorkingUrl = (!urlsChanged && (sameUrl(working, fresh.url) || sameUrl(working, fresh.urlResolved))) ? working : QString();
        changed = true;
    };
    for (auto &o : m_favorites) apply(o);
    for (auto &o : m_history) apply(o);
    return changed;
}

void StationStore::addHistory(const Station &s)
{
    if (!s.isValid()) return;
    m_history.removeIf([&s](const Station &o) { return o.id == s.id; });
    m_history.prepend(s);
    while (m_history.size() > kHistoryLimit) m_history.removeLast();
}

void StationStore::removeHistory(const QString &id)
{
    m_history.removeIf([&id](const Station &s) { return s.id == id; });
}

void StationStore::clearHistory()
{
    m_history.clear();
}

} // namespace uve::radio
