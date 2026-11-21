#pragma once
// UVEinetRadio – gemeinsame Datentypen: Sender (Katalog oder eigene Adresse), Umwandlung JSON ↔ QVariantMap
// und die Prüfung von Stream-Adressen.
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace uve::radio {

inline constexpr const char *kPluginId = "org.uveplayer.uveinetradio";
inline constexpr const char *kPluginVersion = "1.0.0";
QString userAgent();

struct Station
{
    QString id;              // stationuuid (Katalog) oder "custom:<hash>" (eigene Adresse)
    QString name;
    QString url;             // Original-URL aus dem Katalog bzw. eingegebene URL
    QString urlResolved;     // url_resolved aus dem Katalog
    QString lastWorkingUrl;  // zuletzt erfolgreich gestartete Adresse (nur gespeicherte Sender)
    QString favicon;
    QString homepage;
    QString country;
    QString countryCode;
    QString language;
    QString tags;
    QString codec;
    int bitrate = 0;
    bool hls = false;
    bool custom = false;
    qint64 clickCount = 0;
    qint64 votes = 0;

    bool isValid() const { return !id.isEmpty() && !name.isEmpty() && !playUrl().isEmpty(); }
    // Bevorzugt: zuletzt funktionierende Adresse, dann gültige url_resolved, dann url
    QString playUrl() const;
    QString catalogUrl() const;   // ohne lastWorkingUrl

    static Station fromApi(const QJsonObject &o);    // Antwort von /json/stations/…
    static Station fromJson(const QJsonObject &o);   // eigene Speicherung
    static Station fromVariant(const QVariantMap &m);
    static Station customStation(const QString &name, const QString &url);
    QJsonObject toJson() const;
    QVariantMap toVariant() const;
};

// Ergebnis der statischen Adressprüfung
enum class UrlCheck {
    Ok,
    Empty,
    BadScheme,     // kein http/https
    NoHost,
    WebPage,       // als Browser-Webseite erkennbar (Endung oder bekannter Dienst)
};

// Gültige Stream-Adresse? (http/https mit Host)
bool isStreamUrl(const QString &url);
UrlCheck checkStreamUrl(const QString &url);
QString urlCheckCode(UrlCheck check);   // Code für die Oberfläche ("", "empty", "scheme", "host", "webpage")

// Gleiche Adresse? (Groß/Klein beim Host, Standard-Port, abschließender Schrägstrich egal)
bool sameUrl(const QString &a, const QString &b);

// Titelinformation aus mpv-Metadaten (icy-title "Künstler - Titel", oder artist/title). Nur Text, gekürzt.
struct TrackInfo
{
    QString artist;
    QString title;
    bool isEmpty() const { return artist.isEmpty() && title.isEmpty(); }
    bool operator==(const TrackInfo &o) const { return artist == o.artist && title == o.title; }
};
TrackInfo trackFromMetadata(const QVariantMap &metadata, const QString &stationName);
QString cleanText(const QString &text, int maxLength = 300);

} // namespace uve::radio
