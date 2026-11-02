#pragma once
#include <QAbstractListModel>
#include <QRandomGenerator>

namespace uve {

// Wiedergabeliste (eigenes Modell, unabhängig von der internen mpv-Playlist)
class PlaylistModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString repeat READ repeat WRITE setRepeat NOTIFY repeatChanged)   // none | all | one
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)

public:
    enum Roles { PathRole = Qt::UserRole + 1, TitleRole, DurationRole, ErrorRole, IsCurrentRole, IsUrlRole };

    struct Item { QString path; QString title; double duration = 0; bool error = false; };

    explicit PlaylistModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_items.size()); }
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int i);
    QString repeat() const { return m_repeat; }
    void setRepeat(const QString &r);
    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool s);

    // Fügt Einträge hinzu (bereits expandiert). insertNext: direkt hinter dem aktuellen Titel.
    Q_INVOKABLE int add(const QStringList &entries, bool insertNext = false);
    Q_INVOKABLE void remove(const QList<int> &rows);
    Q_INVOKABLE void removeOthers(const QList<int> &keepRows);
    Q_INVOKABLE void removeErrors();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void moveRows(const QList<int> &rows, const QString &where); // up|down|top|bottom|next
    Q_INVOKABLE void sortByName();
    Q_INVOKABLE int nextIndex(bool automatic) const; // -1 = Ende
    Q_INVOKABLE int previousIndex();
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE QString titleAt(int row) const;
    Q_INVOKABLE void setDuration(int row, double seconds);
    Q_INVOKABLE void setTitle(int row, const QString &title);
    Q_INVOKABLE void setError(int row, bool error);
    Q_INVOKABLE bool saveM3U(const QString &file) const;
    Q_INVOKABLE QStringList paths() const;
    Q_INVOKABLE QString cycleRepeat();

signals:
    void countChanged();
    void currentIndexChanged();
    void repeatChanged();
    void shuffleChanged();

private:
    void changedRow(int row);
    QList<Item> m_items;
    int m_current = -1;
    QString m_repeat = QStringLiteral("none");
    bool m_shuffle = false;
    QList<int> m_history;
};

} // namespace uve
