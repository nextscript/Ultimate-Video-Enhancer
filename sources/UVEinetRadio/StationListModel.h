#pragma once
// Listenmodell für die Senderlisten im Panel. Änderungen werden als Einfügen/Entfernen/Ändern gemeldet statt die
// ganze Liste zu ersetzen – sonst springt die ListView beim Nachladen oder beim Stern wieder nach oben.
// Eine einzige Rolle "modelData" (QVariantMap des Senders), damit Delegates wie bei einer JS-Liste arbeiten.
#include <QAbstractListModel>
#include <QVariantList>

namespace uve::radio {

class StationListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_items.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "modelData"}}; }
    int count() const { return int(m_items.size()); }

    // Übernimmt die neue Liste mit möglichst kleinen Änderungen (gleiche "id" = gleiche Zeile)
    void setItems(const QVariantList &items);

signals:
    void countChanged();

private:
    static QString idOf(const QVariant &v) { return v.toMap().value(QStringLiteral("id")).toString(); }
    QVariantList m_items;
};

} // namespace uve::radio
