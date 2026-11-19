#pragma once
// UVESponsorSkip, Kern ohne Netzwerk und ohne Player: YouTube-ID erkennen, SponsorBlock-Antwort lesen und
// entscheiden, wann übersprungen, nachgefragt oder stummgeschaltet wird. Vollständig testbar (tst_uvesponsorskip).
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace uve::sponsor {

enum class Mode { Off = 0, Manual = 1, Auto = 2 };

struct Category
{
    const char *id;
    const char *de;
    const char *en;
    Mode defaultMode;
    const char *color;   // Farbe wie in der SponsorBlock-Erweiterung
};

const QList<Category> &categories();
const Category *category(const QString &id);

struct Segment
{
    double start = 0;
    double end = 0;
    QString category;
    QString actionType;   // "skip" oder "mute"
    QString uuid;
};

// 11-stellige YouTube-ID aus watch?v=, youtu.be/, /shorts/, /live/, /embed/, /v/ (auch m., music., nocookie); sonst leer
QString videoIdFromUrl(const QString &url);
// Erste Zeichen des SHA-256 der ID: der Server erfährt nicht, welches Video läuft
QString hashPrefix(const QString &videoId, int length = 4);
QString skipSegmentsUrl(const QString &server, const QString &prefix);
QString viewedUrl(const QString &server, const QString &uuid);
// Antwort auf skipSegments/<präfix>: nur Segmente des gesuchten Videos, nach Start sortiert
QList<Segment> segmentsFromJson(const QByteArray &body, const QString &videoId);
QString formatTime(double seconds);

class SkipEngine
{
public:
    struct Jump
    {
        bool valid = false;
        double to = 0;
        QStringList uuids;
        QString category;   // Kategorie des ersten Segments
    };
    struct Tick
    {
        Jump jump;            // automatisch springen
        QString prompt;       // Kategorie eines gerade betretenen "Per Taste"-Segments
        bool mute = false;    // ein automatisches Stumm-Segment läuft
    };

    void setSegments(const QList<Segment> &segments);
    void clear();
    QList<Segment> segments() const { return m_segments; }

    void setMode(const QString &category, Mode mode) { m_modes.insert(category, mode); }
    Mode mode(const QString &category) const;

    Tick tick(double position);
    // Taste "Überspringen": jedes nicht ignorierte Segment an der Stelle, auch eines, dessen Sprung rückgängig gemacht wurde
    Jump manualJump(double position) const;
    // Nach "Sprung rückgängig": diese Segmente in diesem Video nicht mehr automatisch überspringen
    void suppress(const QStringList &uuids);

    static constexpr double kMinRemaining = 0.5;   // kürzer als das lohnt kein Sprung

private:
    bool active(const Segment &s, double position) const;
    template <typename Pred> Jump chain(double position, Pred pred) const;

    QList<Segment> m_segments;
    QHash<QString, Mode> m_modes;
    QSet<QString> m_suppressed;
    QSet<QString> m_prompted;
};

} // namespace uve::sponsor
