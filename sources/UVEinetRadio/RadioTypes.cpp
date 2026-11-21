#include "RadioTypes.h"

#include <QCryptographicHash>
#include <QJsonValue>
#include <QRegularExpression>

namespace uve::radio {

QString userAgent()
{
    return QStringLiteral("UVEinetRadio/%1 (UVE Player plugin; +https://github.com/nextscript/Ultimate-Video-Enhancer)")
        .arg(QLatin1String(kPluginVersion));
}

static QString str(const QJsonObject &o, const char *key) { return o.value(QLatin1String(key)).toString().trimmed(); }

static qint64 num(const QJsonObject &o, const char *key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isDouble()) return qint64(v.toDouble());
    if (v.isString()) return v.toString().toLongLong();
    return 0;
}

static bool flag(const QJsonObject &o, const char *key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isBool()) return v.toBool();
    if (v.isDouble()) return v.toInt() != 0;
    if (v.isString()) return v.toString() == QLatin1String("1") || v.toString() == QLatin1String("true");
    return false;
}

QString Station::catalogUrl() const
{
    if (isStreamUrl(urlResolved)) return urlResolved;
    if (isStreamUrl(url)) return url;
    return {};
}

QString Station::playUrl() const
{
    if (isStreamUrl(lastWorkingUrl)) return lastWorkingUrl;
    return catalogUrl();
}

Station Station::fromApi(const QJsonObject &o)
{
    Station s;
    s.id = str(o, "stationuuid");
    s.name = cleanText(str(o, "name"), 200);
    s.url = str(o, "url");
    s.urlResolved = str(o, "url_resolved");
    s.favicon = str(o, "favicon");
    s.homepage = str(o, "homepage");
    s.country = cleanText(str(o, "country"), 100);
    s.countryCode = str(o, "countrycode").toUpper();
    s.language = cleanText(str(o, "language"), 100);
    s.tags = cleanText(str(o, "tags"), 300);
    s.codec = cleanText(str(o, "codec"), 30);
    if (s.codec.compare(QLatin1String("UNKNOWN"), Qt::CaseInsensitive) == 0) s.codec.clear();
    s.bitrate = int(num(o, "bitrate"));
    s.hls = flag(o, "hls");
    s.clickCount = num(o, "clickcount");
    s.votes = num(o, "votes");
    return s;
}

Station Station::fromJson(const QJsonObject &o)
{
    Station s;
    s.id = str(o, "id");
    s.name = cleanText(str(o, "name"), 200);
    s.url = str(o, "url");
    s.urlResolved = str(o, "urlResolved");
    s.lastWorkingUrl = str(o, "lastWorkingUrl");
    s.favicon = str(o, "favicon");
    s.homepage = str(o, "homepage");
    s.country = str(o, "country");
    s.countryCode = str(o, "countryCode");
    s.language = str(o, "language");
    s.tags = str(o, "tags");
    s.codec = str(o, "codec");
    s.bitrate = int(num(o, "bitrate"));
    s.hls = flag(o, "hls");
    s.custom = flag(o, "custom");
    return s;
}

Station Station::fromVariant(const QVariantMap &m)
{
    return fromJson(QJsonObject::fromVariantMap(m));
}

Station Station::customStation(const QString &name, const QString &url)
{
    Station s;
    const QString u = url.trimmed();
    s.id = QStringLiteral("custom:")
         + QString::fromLatin1(QCryptographicHash::hash(u.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    s.name = cleanText(name, 200);
    if (s.name.isEmpty()) s.name = QUrl(u).host();
    s.url = u;
    s.custom = true;
    return s;
}

QJsonObject Station::toJson() const
{
    QJsonObject o{{"id", id}, {"name", name}, {"url", url}};
    auto put = [&o](const char *k, const QString &v) { if (!v.isEmpty()) o.insert(QLatin1String(k), v); };
    put("urlResolved", urlResolved);
    put("lastWorkingUrl", lastWorkingUrl);
    put("favicon", favicon);
    put("homepage", homepage);
    put("country", country);
    put("countryCode", countryCode);
    put("language", language);
    put("tags", tags);
    put("codec", codec);
    if (bitrate > 0) o.insert(QStringLiteral("bitrate"), bitrate);
    if (hls) o.insert(QStringLiteral("hls"), true);
    if (custom) o.insert(QStringLiteral("custom"), true);
    return o;
}

QVariantMap Station::toVariant() const
{
    QVariantMap m = toJson().toVariantMap();
    // Felder, die die Oberfläche immer erwartet
    for (const char *k : {"favicon", "country", "countryCode", "language", "tags", "codec", "lastWorkingUrl", "urlResolved"})
        if (!m.contains(QLatin1String(k))) m.insert(QLatin1String(k), QString());
    m.insert(QStringLiteral("bitrate"), bitrate);
    m.insert(QStringLiteral("hls"), hls);
    m.insert(QStringLiteral("custom"), custom);
    m.insert(QStringLiteral("clickCount"), clickCount);
    m.insert(QStringLiteral("votes"), votes);
    const QString play = playUrl();
    m.insert(QStringLiteral("playUrl"), play);
    m.insert(QStringLiteral("insecure"), QUrl(play).scheme() == QLatin1String("http"));
    return m;
}

bool isStreamUrl(const QString &url)
{
    if (url.isEmpty()) return false;
    const QUrl u(url, QUrl::StrictMode);
    const QString scheme = u.scheme().toLower();
    return u.isValid() && (scheme == QLatin1String("http") || scheme == QLatin1String("https")) && !u.host().isEmpty();
}

UrlCheck checkStreamUrl(const QString &input)
{
    const QString url = input.trimmed();
    if (url.isEmpty()) return UrlCheck::Empty;
    const QUrl u(url, QUrl::StrictMode);
    const QString scheme = u.scheme().toLower();
    if (!u.isValid() || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))) return UrlCheck::BadScheme;
    if (u.host().isEmpty()) return UrlCheck::NoHost;
    // Erkennbare Webseiten: Seitenendungen und Dienste, deren Links nie direkte Streams sind
    static const QRegularExpression pageSuffix(QStringLiteral("\\.(s?html?|xhtml|aspx?|jsp|cfm)$"),
                                               QRegularExpression::CaseInsensitiveOption);
    if (pageSuffix.match(u.path()).hasMatch()) return UrlCheck::WebPage;
    const QString host = u.host().toLower();
    static const char *pageHosts[] = {"youtube.com", "youtu.be", "spotify.com", "tunein.com", "radio.de", "radio.net",
                                      "radio.at", "radio.fr", "radio.it", "radio.es", "radio.pl", "radio.dk", "radio.se",
                                      "radio.pt", "radiogarden.com", "radio.garden", "facebook.com", "instagram.com",
                                      "twitter.com", "x.com", "soundcloud.com", "google.com", "wikipedia.org",
                                      "radio-browser.info"};
    for (const char *h : pageHosts) {
        const QLatin1String d(h);
        if (host == d || host.endsWith(QLatin1Char('.') + d)) {
            // Streaming-Unterdomänen der Radioportale (z. B. stream.radio.de) bleiben erlaubt
            if (host.startsWith(QLatin1String("stream")) || host.startsWith(QLatin1String("icecast"))) continue;
            return UrlCheck::WebPage;
        }
    }
    // Nur Domain ohne Pfad (z. B. https://www.sender.de/) ist fast immer die Webseite des Senders
    if ((u.path().isEmpty() || u.path() == QLatin1String("/")) && u.query().isEmpty() && u.port() < 0
        && host.startsWith(QLatin1String("www.")))
        return UrlCheck::WebPage;
    return UrlCheck::Ok;
}

QString urlCheckCode(UrlCheck check)
{
    switch (check) {
    case UrlCheck::Ok: return {};
    case UrlCheck::Empty: return QStringLiteral("empty");
    case UrlCheck::BadScheme: return QStringLiteral("scheme");
    case UrlCheck::NoHost: return QStringLiteral("host");
    case UrlCheck::WebPage: return QStringLiteral("webpage");
    }
    return {};
}

static QString normalized(const QString &url)
{
    QUrl u(url.trimmed());
    if (!u.isValid()) return url.trimmed();
    u.setHost(u.host().toLower());
    u.setScheme(u.scheme().toLower());
    if ((u.scheme() == QLatin1String("http") && u.port() == 80) || (u.scheme() == QLatin1String("https") && u.port() == 443))
        u.setPort(-1);
    QString s = u.toString(QUrl::FullyEncoded);
    while (s.endsWith(QLatin1Char('/'))) s.chop(1);
    return s;
}

bool sameUrl(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty()) return false;
    return a == b || normalized(a) == normalized(b);
}

QString cleanText(const QString &text, int maxLength)
{
    QString s;
    s.reserve(text.size());
    for (const QChar c : text) {
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format) s += QLatin1Char(' ');
        else s += c;
    }
    s = s.simplified();
    if (s.size() > maxLength) s = s.left(maxLength - 1) + QChar(0x2026);
    return s;
}

static QString metaValue(const QVariantMap &metadata, const char *key)
{
    for (auto it = metadata.begin(); it != metadata.end(); ++it)
        if (it.key().compare(QLatin1String(key), Qt::CaseInsensitive) == 0) return it.value().toString();
    return {};
}

TrackInfo trackFromMetadata(const QVariantMap &metadata, const QString &stationName)
{
    TrackInfo t;
    const QString icy = cleanText(metaValue(metadata, "icy-title"));
    if (!icy.isEmpty()) {
        const int sep = icy.indexOf(QLatin1String(" - "));
        if (sep > 0) { t.artist = icy.left(sep).trimmed(); t.title = icy.mid(sep + 3).trimmed(); }
        else t.title = icy;
    } else {
        t.artist = cleanText(metaValue(metadata, "artist"));
        t.title = cleanText(metaValue(metadata, "title"));
    }
    // Leere Platzhalter und Wiederholungen des Sendernamens sind keine Titelinformation
    auto junk = [&stationName](const QString &v) {
        static const QRegularExpression onlyPunct(QStringLiteral("^[\\s\\-–—_.:|/*]*$"));
        return onlyPunct.match(v).hasMatch() || v.compare(stationName, Qt::CaseInsensitive) == 0
            || v.compare(QLatin1String("unknown"), Qt::CaseInsensitive) == 0;
    };
    if (junk(t.artist)) t.artist.clear();
    if (junk(t.title)) t.title.clear();
    if (t.title.isEmpty() && !t.artist.isEmpty()) std::swap(t.title, t.artist);
    return t;
}

} // namespace uve::radio
