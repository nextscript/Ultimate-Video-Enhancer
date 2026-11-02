#include "PlaylistModel.h"
#include "MediaFiles.h"

#include <QCoreApplication>
#include <QCollator>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

namespace uve {

static QString displayName(const QString &path)
{
    if (media::isUrl(path)) {
        const QUrl u(path);
        const QString host = u.host().remove(QStringLiteral("www."));
        if (host.contains("youtube") || host == "youtu.be")
            return QCoreApplication::translate("uve::PlaylistModel", "YouTube: ") + (u.hasQuery() ? QUrlQuery(u).queryItemValue("v") : u.path().section('/', -1));
        if (host.contains("twitch.tv")) return QStringLiteral("Twitch: ") + u.path().section('/', 1, 1);
        if (u.scheme() != "http" && u.scheme() != "https") return u.scheme().toUpper() + ": " + u.host() + (u.port() > 0 ? ":" + QString::number(u.port()) : "");
        const auto last = u.path().section('/', -1);
        return last.isEmpty() ? u.host() : QUrl::fromPercentEncoding(last.toUtf8());
    }
    return QFileInfo(path).fileName();
}

PlaylistModel::PlaylistModel(QObject *parent) : QAbstractListModel(parent) {}

int PlaylistModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : count(); }

QVariant PlaylistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= count()) return {};
    const auto &it = m_items[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: return it.title;
    case PathRole: return it.path;
    case DurationRole: return it.duration;
    case ErrorRole: return it.error;
    case IsCurrentRole: return index.row() == m_current;
    case IsUrlRole: return media::isUrl(it.path);
    default: return {};
    }
}

QHash<int, QByteArray> PlaylistModel::roleNames() const
{
    return {{PathRole, "path"}, {TitleRole, "title"}, {DurationRole, "duration"}, {ErrorRole, "error"},
            {IsCurrentRole, "isCurrent"}, {IsUrlRole, "isUrl"}};
}

void PlaylistModel::changedRow(int row)
{
    if (row >= 0 && row < count()) emit dataChanged(index(row), index(row));
}

void PlaylistModel::setCurrentIndex(int i)
{
    if (i < -1 || i >= count() || i == m_current) return;
    const int old = m_current;
    if (old >= 0) m_history << old;
    m_current = i;
    changedRow(old);
    changedRow(i);
    emit currentIndexChanged();
}

void PlaylistModel::setRepeat(const QString &r)
{
    const QString v = (r == "all" || r == "one") ? r : QStringLiteral("none");
    if (v == m_repeat) return;
    m_repeat = v;
    emit repeatChanged();
}

QString PlaylistModel::cycleRepeat()
{
    setRepeat(m_repeat == "none" ? "all" : m_repeat == "all" ? "one" : "none");
    return m_repeat;
}

void PlaylistModel::setShuffle(bool s)
{
    if (s == m_shuffle) return;
    m_shuffle = s;
    m_history.clear();
    emit shuffleChanged();
}

int PlaylistModel::add(const QStringList &entries, bool insertNext)
{
    if (entries.isEmpty()) return -1;
    const int at = (insertNext && m_current >= 0) ? m_current + 1 : count();
    beginInsertRows({}, at, at + int(entries.size()) - 1);
    int i = at;
    for (const auto &e : entries) m_items.insert(i++, Item{e, displayName(e)});
    endInsertRows();
    emit countChanged();
    return at;
}

void PlaylistModel::remove(const QList<int> &rows)
{
    QList<int> sorted = rows;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    bool currentRemoved = false;
    for (int r : sorted) {
        if (r < 0 || r >= count()) continue;
        beginRemoveRows({}, r, r);
        m_items.removeAt(r);
        endRemoveRows();
        if (r == m_current) { currentRemoved = true; m_current = -1; }
        else if (r < m_current) --m_current;
    }
    m_history.clear();
    emit countChanged();
    if (currentRemoved || !sorted.isEmpty()) emit currentIndexChanged();
}

void PlaylistModel::removeOthers(const QList<int> &keepRows)
{
    QList<int> drop;
    for (int i = 0; i < count(); ++i) if (!keepRows.contains(i)) drop << i;
    remove(drop);
}

void PlaylistModel::removeErrors()
{
    QList<int> drop;
    for (int i = 0; i < count(); ++i) if (m_items[i].error) drop << i;
    remove(drop);
}

void PlaylistModel::clear()
{
    beginResetModel();
    m_items.clear();
    m_current = -1;
    m_history.clear();
    endResetModel();
    emit countChanged();
    emit currentIndexChanged();
}

void PlaylistModel::move(int from, int to)
{
    if (from < 0 || from >= count() || to < 0 || to >= count() || from == to) return;
    // beginMoveRows erwartet das Ziel "vor" dem Element; beim Verschieben nach unten +1
    if (!beginMoveRows({}, from, from, {}, to > from ? to + 1 : to)) return;
    m_items.move(from, to);
    if (m_current == from) m_current = to;
    else if (from < m_current && to >= m_current) --m_current;
    else if (from > m_current && to <= m_current) ++m_current;
    endMoveRows();
    emit currentIndexChanged();
}

void PlaylistModel::moveRows(const QList<int> &rows, const QString &where)
{
    QList<int> sel;
    for (int r : rows) if (r >= 0 && r < count() && !sel.contains(r)) sel << r;
    std::sort(sel.begin(), sel.end());
    if (sel.isEmpty()) return;

    if (where == "up") {
        // "limit" = erste Position, in die noch verschoben werden darf (Blöcke am Anfang bleiben stehen)
        int limit = 0;
        for (int r : sel) {
            if (r > limit) { move(r, r - 1); limit = r; }
            else limit = r + 1;
        }
        return;
    }
    if (where == "down") {
        int limit = count() - 1;
        for (auto it = sel.rbegin(); it != sel.rend(); ++it) {
            const int r = *it;
            if (r < limit) { move(r, r + 1); limit = r; }
            else limit = r - 1;
        }
        return;
    }

    // top / bottom / next: ausgewählte Einträge herausnehmen und als Block einfügen
    if (where == "next" && m_current >= 0) sel.removeAll(m_current); // aktueller Titel bleibt stehen
    if (sel.isEmpty()) return;
    struct Tagged { Item item; bool current; };
    QList<Tagged> picked, rest;
    for (int i = 0; i < count(); ++i) (sel.contains(i) ? picked : rest).append({m_items[i], i == m_current});

    qsizetype insertAt = 0;
    if (where == "bottom") insertAt = rest.size();
    else if (where == "next") {
        insertAt = 0;
        for (qsizetype i = 0; i < rest.size(); ++i) if (rest[i].current) { insertAt = i + 1; break; }
    }
    QList<Tagged> merged = rest.mid(0, insertAt) + picked + rest.mid(insertAt);

    beginResetModel();
    m_items.clear();
    m_current = -1;
    for (qsizetype i = 0; i < merged.size(); ++i) {
        m_items << merged[i].item;
        if (merged[i].current) m_current = int(i);
    }
    m_history.clear();
    endResetModel();
    emit currentIndexChanged();
}

void PlaylistModel::sortByName()
{
    if (count() < 2) return;
    const QString cur = m_current >= 0 ? m_items[m_current].path : QString();
    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    beginResetModel();
    std::stable_sort(m_items.begin(), m_items.end(), [&](const Item &a, const Item &b) { return coll.compare(a.title, b.title) < 0; });
    m_current = -1;
    for (int i = 0; i < count(); ++i) if (m_items[i].path == cur) { m_current = i; break; }
    endResetModel();
    emit currentIndexChanged();
}

int PlaylistModel::nextIndex(bool automatic) const
{
    const int n = count();
    if (n == 0) return -1;
    if (automatic && m_repeat == "one" && m_current >= 0) return m_current;
    if (m_shuffle && n > 1) {
        int i;
        do { i = int(QRandomGenerator::global()->bounded(n)); } while (i == m_current);
        return i;
    }
    if (m_current + 1 < n) return m_current + 1;
    return m_repeat == "all" ? 0 : -1;
}

int PlaylistModel::previousIndex()
{
    if (m_shuffle && !m_history.isEmpty()) return m_history.takeLast();
    if (m_current > 0) return m_current - 1;
    if (m_repeat == "all" && count() > 0) return count() - 1;
    return -1;
}

QString PlaylistModel::pathAt(int row) const { return row >= 0 && row < count() ? m_items[row].path : QString(); }
QString PlaylistModel::titleAt(int row) const { return row >= 0 && row < count() ? m_items[row].title : QString(); }

void PlaylistModel::setDuration(int row, double s)
{
    if (row < 0 || row >= count() || qFuzzyCompare(m_items[row].duration + 1, s + 1)) return;
    m_items[row].duration = s;
    changedRow(row);
}

void PlaylistModel::setTitle(int row, const QString &title)
{
    if (row < 0 || row >= count() || title.isEmpty() || m_items[row].title == title) return;
    m_items[row].title = title;
    changedRow(row);
}

void PlaylistModel::setError(int row, bool error)
{
    if (row < 0 || row >= count() || m_items[row].error == error) return;
    m_items[row].error = error;
    changedRow(row);
}

bool PlaylistModel::saveM3U(const QString &file) const
{
    QStringList titles;
    for (const auto &i : m_items) titles << i.title;
    return media::writeM3U(file, paths(), titles);
}

QStringList PlaylistModel::paths() const
{
    QStringList out;
    for (const auto &i : m_items) out << i.path;
    return out;
}

} // namespace uve
