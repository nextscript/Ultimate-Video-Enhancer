#include "StationListModel.h"

#include <QSet>

namespace uve::radio {

QVariant StationListModel::data(const QModelIndex &index, int role) const
{
    if (role != Qt::UserRole || !index.isValid() || index.row() >= m_items.size()) return {};
    return m_items.at(index.row());
}

void StationListModel::setItems(const QVariantList &items)
{
    const int before = int(m_items.size());
    QSet<QString> wanted;
    for (const auto &v : items) wanted.insert(idOf(v));

    // 1. Weggefallene Zeilen entfernen (von hinten, damit die Indizes stimmen)
    for (int i = int(m_items.size()) - 1; i >= 0; --i) {
        if (wanted.contains(idOf(m_items.at(i)))) continue;
        beginRemoveRows({}, i, i);
        m_items.removeAt(i);
        endRemoveRows();
    }
    // 2. Neue Zeilen an ihrer Position einfügen, vorhandene aktualisieren
    for (int i = 0; i < items.size(); ++i) {
        const QString id = idOf(items.at(i));
        if (i < m_items.size() && idOf(m_items.at(i)) == id) {
            if (m_items.at(i) != items.at(i)) {
                m_items[i] = items.at(i);
                emit dataChanged(index(i), index(i), {Qt::UserRole});
            }
            continue;
        }
        bool laterInOld = false;
        for (int j = i + 1; j < m_items.size(); ++j)
            if (idOf(m_items.at(j)) == id) { laterInOld = true; break; }
        if (laterInOld) {
            // Reihenfolge geändert (z. B. Verlaufseintrag nach oben): Zeile verschieben
            int from = i + 1;
            while (idOf(m_items.at(from)) != id) ++from;
            beginMoveRows({}, from, from, {}, i);
            m_items.move(from, i);
            endMoveRows();
            if (m_items.at(i) != items.at(i)) {
                m_items[i] = items.at(i);
                emit dataChanged(index(i), index(i), {Qt::UserRole});
            }
            continue;
        }
        beginInsertRows({}, i, i);
        m_items.insert(i, items.at(i));
        endInsertRows();
    }
    if (int(m_items.size()) != before) emit countChanged();
}

} // namespace uve::radio
