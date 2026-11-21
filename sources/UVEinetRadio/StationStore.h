#pragma once
// Favoriten (auch eigene Sender) und Wiedergabeverlauf in <plugin.dataDir>/library.json.
// Versioniertes JSON, atomar geschrieben (QSaveFile). Funktioniert ohne Katalog.
#include "RadioTypes.h"

#include <QList>
#include <QString>

namespace uve::radio {

class StationStore
{
public:
    static constexpr int kFormatVersion = 1;
    static constexpr int kHistoryLimit = 20;

    explicit StationStore(const QString &dataDir);
    QString filePath() const { return m_path; }
    QString lastError() const { return m_error; }

    bool load();
    bool save();

    const QList<Station> &favorites() const { return m_favorites; }
    const QList<Station> &history() const { return m_history; }

    bool isFavorite(const QString &id) const;
    // true = jetzt Favorit
    bool toggleFavorite(const Station &s);
    void addFavorite(const Station &s);
    void removeFavorite(const QString &id);
    // Nach erfolgreichem Start: zuletzt funktionierende Adresse merken; Katalogdaten aktualisieren
    void markWorking(const Station &s, const QString &url);
    // Katalogeintrag neu: Adressen ersetzen (lastWorkingUrl verwerfen, wenn sie nicht mehr dazu gehört)
    bool updateFromCatalog(const Station &fresh);

    void addHistory(const Station &s);
    void removeHistory(const QString &id);
    void clearHistory();

private:
    QString m_path;
    QString m_error;
    QList<Station> m_favorites;
    QList<Station> m_history;
};

} // namespace uve::radio
