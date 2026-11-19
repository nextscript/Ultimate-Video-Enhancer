#include "TikTokExtractor.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>

#include <limits>

namespace uve::tiktok {

namespace {

constexpr auto kChromeUa = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36";
constexpr int kTimeoutMs = 30000;
constexpr qint64 kMaxBody = 16 * 1024 * 1024;
constexpr int kSourceHeight = std::numeric_limits<int>::max();   // "Original" ohne Angabe der Auflösung
constexpr int kMaxProfileVideos = 500;      // Obergrenze der Wiedergabeliste aus einem Profil
constexpr int kMaxProfilePages = 40;        // Obergrenze der Abfragen (je bis zu 15 Videos oder eine Woche zurück)
constexpr qint64 kWeekMs = 7LL * 86400 * 1000;
constexpr qint64 kTikTokStartMs = 1472706000000LL;   // September 2016: früher gibt es keine Videos

QJsonObject parseObject(const QString &text)
{
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

bool isTikTokHost(const QString &host)
{
    return host == QLatin1String("tiktok.com") || host.endsWith(QLatin1String(".tiktok.com"));
}

} // namespace

// ---------------------------------------------------------------- Schritte ohne Netzwerk
Link parseLink(const QString &url)
{
    const QUrl u(url);
    const QString host = u.host().toLower();
    if (!isTikTokHost(host)) return {};
    const QString path = u.path();

    if (host == QLatin1String("vm.tiktok.com") || host == QLatin1String("vt.tiktok.com"))
        return {LinkKind::Short, {}, {}};
    if (path.startsWith(QLatin1String("/t/"))) return {LinkKind::Short, {}, {}};

    static const QRegularExpression userVideo(QStringLiteral("^/@([^/?#]+)/(?:video|photo)/(\\d+)"));
    if (const auto m = userVideo.match(path); m.hasMatch()) return {LinkKind::Video, m.captured(1), m.captured(2)};

    static const QRegularExpression plainVideo(QStringLiteral("^/(?:embed(?:/v2)?|player/v1|v|share/video|video)/(\\d+)"));
    if (const auto m = plainVideo.match(path); m.hasMatch()) return {LinkKind::Video, {}, m.captured(1)};

    static const QRegularExpression userLive(QStringLiteral("^/@([^/?#]+)/live/?$"));
    if (const auto m = userLive.match(path); m.hasMatch()) return {LinkKind::Live, m.captured(1), {}};

    static const QRegularExpression profile(QStringLiteral("^/@([^/?#]+)/?$"));
    if (const auto m = profile.match(path); m.hasMatch()) return {LinkKind::Profile, m.captured(1), {}};

    return {};
}

QString scriptJson(const QString &html, const QString &scriptId)
{
    const QRegularExpression script(QStringLiteral("<script[^>]*\\bid=\"%1\"[^>]*>(.*?)</script>").arg(QRegularExpression::escape(scriptId)),
                                    QRegularExpression::DotMatchesEverythingOption);
    return script.match(html).captured(1);
}

int shortSide(int width, int height)
{
    if (width <= 0) return height;
    if (height <= 0) return width;
    return std::min(width, height);
}

QString videoAudio(const QJsonObject &video)
{
    QString best;
    qint64 bestBitrate = -1;
    for (const auto &v : video.value("bitrateAudioInfo").toArray()) {
        const QJsonObject info = v.toObject();
        const QJsonObject urls = info.value("UrlList").toObject();
        QString url = urls.value("MainUrl").toString();
        if (url.isEmpty()) url = urls.value("BackupUrl").toString();
        if (url.isEmpty()) url = urls.value("FallbackUrl").toString();
        const qint64 bitrate = info.value("Bitrate").toVariant().toLongLong();
        if (!url.isEmpty() && bitrate > bestBitrate) { best = url; bestBitrate = bitrate; }
    }
    return best;
}

QString mergeAudioVideo(const QString &videoUrl, const QString &audioUrl)
{
    // mpv-EDL: zwei Spuren als eine Quelle. Längenangabe in Bytes, weil Adressen ";" und "," enthalten dürfen.
    const auto entry = [](const QString &url) {
        return QStringLiteral("!no_clip;%%1%%2").arg(url.toUtf8().size()).arg(url);
    };
    return QStringLiteral("edl://") + entry(videoUrl) + QStringLiteral(";!new_stream;") + entry(audioUrl);
}

QList<Variant> videoVariants(const QJsonObject &video)
{
    QList<Variant> out;
    const QString audio = videoAudio(video);
    for (const auto &v : video.value("bitrateInfo").toArray()) {
        const QJsonObject info = v.toObject();
        const QJsonObject addr = info.value("PlayAddr").toObject();
        const QJsonArray urls = addr.value("UrlList").toArray();
        if (urls.isEmpty()) continue;
        // Die letzte Adresse ist meist der Ersatz-CDN, die erste die Web-Adresse mit passendem Cookie
        QString url = urls.first().toString();
        if (url.isEmpty()) continue;
        // DASH-Stufen (oft die höheren Auflösungen) enthalten nur Bild: Ton aus bitrateAudioInfo dazulegen, sonst auslassen
        if (info.value("Format").toString().compare(QLatin1String("dash"), Qt::CaseInsensitive) == 0) {
            if (audio.isEmpty()) continue;
            url = mergeAudioVideo(url, audio);
        }
        out << Variant{shortSide(addr.value("Width").toInt(), addr.value("Height").toInt()),
                       info.value("Bitrate").toVariant().toLongLong(), url};
    }
    if (out.isEmpty()) {
        const int height = shortSide(video.value("width").toVariant().toInt(), video.value("height").toVariant().toInt());
        for (const char *key : {"playAddr", "downloadAddr"}) {
            const QString url = video.value(QLatin1String(key)).toString();
            if (!url.isEmpty()) { out << Variant{height, 0, url}; break; }
        }
    }
    return out;
}

QString pickVariant(const QList<Variant> &variants, int maxHeight)
{
    const Variant *best = nullptr;
    const Variant *lowest = nullptr;
    const auto better = [](const Variant &a, const Variant *b) {
        return !b || a.height > b->height || (a.height == b->height && a.bitrate > b->bitrate);
    };
    for (const Variant &v : variants) {
        if (v.url.isEmpty()) continue;
        if (!lowest || v.height < lowest->height) lowest = &v;
        if (maxHeight > 0 && v.height > maxHeight) continue;
        if (better(v, best)) best = &v;
    }
    if (best) return best->url;
    return lowest ? lowest->url : QString();
}

QStringList videoSubtitles(const QJsonObject &video)
{
    QStringList out;
    for (const auto &v : video.value("subtitleInfos").toArray()) {
        const QJsonObject s = v.toObject();
        const QString url = s.value("Url").toString();
        if (url.isEmpty() || s.value("Format").toString().compare(QLatin1String("webvtt"), Qt::CaseInsensitive) != 0) continue;
        if (!out.contains(url)) out << url;
    }
    return out;
}

QString videoTitle(const QJsonObject &item)
{
    const QJsonObject author = item.value("author").toObject();
    QString name = author.value("uniqueId").toString();
    if (!name.isEmpty()) name.prepend(QLatin1Char('@'));
    QString desc = item.value("desc").toString().simplified();
    if (desc.size() > 120) desc = desc.left(117) + QStringLiteral("…");
    if (name.isEmpty()) return desc.isEmpty() ? QStringLiteral("TikTok") : desc;
    return desc.isEmpty() ? name : QStringLiteral("%1: %2").arg(name, desc);
}

QList<Variant> liveVariants(const QString &streamData, const QString &format)
{
    QList<Variant> out;
    const QJsonObject data = parseObject(streamData).value("data").toObject();
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        if (it.key() == QLatin1String("ao")) continue;   // nur Ton
        const QJsonObject main = it.value().toObject().value("main").toObject();
        const QString url = main.value(format).toString();
        if (url.isEmpty()) continue;
        const QJsonObject params = parseObject(main.value("sdk_params").toString());
        const QStringList size = params.value("resolution").toString().split(QLatin1Char('x'));
        int height = size.size() == 2 ? shortSide(size.at(0).toInt(), size.at(1).toInt()) : 0;
        qint64 bitrate = params.value("vbitrate").toVariant().toLongLong();
        if (it.key() == QLatin1String("origin")) {
            if (height == 0) height = kSourceHeight;
            bitrate = std::numeric_limits<qint64>::max();   // Original vor gleich hoher umkodierter Stufe
        }
        out << Variant{height, bitrate, url};
    }
    return out;
}

QList<Variant> legacyLiveVariants(const QJsonObject &streamUrl, const QString &format)
{
    static const QHash<QString, int> heights{{"FULL_HD1", 1080}, {"HD1", 720}, {"SD1", 540}, {"SD2", 360}, {"ORIGION", kSourceHeight}, {"ORIGIN", kSourceHeight}};
    QList<Variant> out;
    const QJsonObject map = streamUrl.value(format == QLatin1String("hls") ? "hls_pull_url_map" : "flv_pull_url").toObject();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        if (!it.value().toString().isEmpty()) out << Variant{heights.value(it.key(), 0), 0, it.value().toString()};
    if (out.isEmpty()) {
        const QString single = streamUrl.value(format == QLatin1String("hls") ? "hls_pull_url" : "rtmp_pull_url").toString();
        if (!single.isEmpty()) out << Variant{0, 0, single};
    }
    return out;
}

QString profileSecUid(const QString &html, int *videoCount)
{
    if (videoCount) *videoCount = -1;
    const QString json = scriptJson(html, QStringLiteral("__UNIVERSAL_DATA_FOR_REHYDRATION__"));
    const QJsonObject info = parseObject(json).value("__DEFAULT_SCOPE__").toObject().value("webapp.user-detail").toObject()
                                 .value("userInfo").toObject();
    if (videoCount) {
        const QJsonValue count = info.value("stats").toObject().value("videoCount");
        if (count.isDouble()) *videoCount = count.toInt();
    }
    return info.value("user").toObject().value("secUid").toString();
}

QList<ProfileVideo> profileVideos(const QJsonObject &response, const QString &fallbackUser)
{
    QList<ProfileVideo> out;
    for (const auto &v : response.value("itemList").toArray()) {
        const QJsonObject item = v.toObject();
        const QString id = item.value("id").toString();
        if (id.isEmpty() || item.contains(QLatin1String("imagePost"))) continue;   // Foto-Slideshow: kein Video
        QString user = item.value("author").toObject().value("uniqueId").toString();
        if (user.isEmpty()) user = fallbackUser;
        out << ProfileVideo{id, user, videoTitle(item), item.value("video").toObject().value("duration").toDouble(),
                            item.value("createTime").toVariant().toLongLong()};
    }
    return out;
}

qint64 nextProfileCursor(const QJsonObject &response, qint64 oldCursor)
{
    if (!response.value("hasMorePrevious").toBool()) return 0;
    const QJsonArray items = response.value("itemList").toArray();
    qint64 cursor = items.isEmpty() ? 0 : items.last().toObject().value("createTime").toVariant().toLongLong() * 1000;
    // Keine Videos in diesem Zeitraum (TikTok blickt etwa eine Woche zurück): selbst weiter zurückgehen
    if (cursor <= 0 || cursor >= oldCursor) cursor = oldCursor - kWeekMs;
    return cursor < kTikTokStartMs ? 0 : cursor;
}

QString videoPageUrl(const QString &user, const QString &id)
{
    return QStringLiteral("https://www.tiktok.com/@%1/video/%2").arg(user.isEmpty() ? QStringLiteral("_") : user, id);
}

// ---------------------------------------------------------------- Ablauf
ExtractJob::ExtractJob(const QString &url, int maxHeight, const QString &liveFormat, QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)), m_url(url.trimmed()), m_maxHeight(maxHeight),
      m_liveFormat(liveFormat == QLatin1String("hls") ? QStringLiteral("hls") : QStringLiteral("flv"))
{
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    m_nam->setCookieJar(new QNetworkCookieJar(m_nam));   // nur im Speicher
    m_result.userAgent = QString::fromLatin1(kChromeUa);
    m_result.referer = QStringLiteral("https://www.tiktok.com/");
}

void ExtractJob::start()
{
    const Link link = parseLink(m_url);
    if (link.kind == LinkKind::Unknown) return fail(QStringLiteral("unsupported TikTok link"));
    handle(link);
}

void ExtractJob::handle(const Link &link)
{
    switch (link.kind) {
    case LinkKind::Short: return resolveShort();
    case LinkKind::Video: return video(link);
    case LinkKind::Live: return live(link.user, true);
    case LinkKind::Profile: return live(link.user, false);
    case LinkKind::Unknown: break;
    }
    fail(QStringLiteral("unsupported TikTok link"));
}

void ExtractJob::get(const QString &url, const Callback &cb, bool lenient)
{
    QNetworkRequest request(QUrl::fromEncoded(url.toUtf8()));
    request.setTransferTimeout(kTimeoutMs);
    request.setRawHeader("User-Agent", kChromeUa);
    request.setRawHeader("Referer", "https://www.tiktok.com/");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    QNetworkReply *reply = m_nam->get(request);
    reply->setParent(this);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > kMaxBody) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb, lenient] {
        reply->deleteLater();
        if (m_done) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0 && !lenient) return fail(reply->errorString());
        cb(Response{status, QString::fromUtf8(reply->readAll()), reply->url()});
    });
}

void ExtractJob::fail(const QString &error, bool definitive)
{
    if (m_done) return;
    m_done = true;
    emit failed(error, definitive);
}

void ExtractJob::finish()
{
    if (m_done) return;
    if (m_result.url.isEmpty()) return fail(QStringLiteral("no playable source found"));
    m_done = true;
    if (!m_result.live) {
        // Die Videodatei liefert TikTok nur mit dem Cookie der Seite (tt_chain_token), auch auf anderen CDN-Namen
        QStringList cookies;
        const auto jar = m_nam->cookieJar()->cookiesForUrl(QUrl(QStringLiteral("https://www.tiktok.com/")))
                         + m_nam->cookieJar()->cookiesForUrl(QUrl::fromEncoded(m_result.url.toUtf8()));
        for (const QNetworkCookie &c : jar) {
            const QString pair = QString::fromUtf8(c.toRawForm(QNetworkCookie::NameAndValueOnly));
            if (!cookies.contains(pair)) cookies << pair;
        }
        m_result.cookie = cookies.join(QLatin1String("; "));
    }
    emit finished(m_result);
}

void ExtractJob::resolveShort()
{
    get(m_url, [this](const Response &r) {
        const Link link = parseLink(r.finalUrl.toString());
        if (link.kind == LinkKind::Unknown || link.kind == LinkKind::Short)
            return fail(QStringLiteral("short link leads nowhere (HTTP %1)").arg(r.status));
        m_url = r.finalUrl.toString();
        handle(link);
    });
}

void ExtractJob::video(const Link &link)
{
    get(videoPageUrl(link.user, link.id), [this](const Response &r) {
        if (r.status >= 400) return fail(QStringLiteral("HTTP %1").arg(r.status));
        const QString json = scriptJson(r.text, QStringLiteral("__UNIVERSAL_DATA_FOR_REHYDRATION__"));
        if (json.isEmpty()) return fail(QStringLiteral("page holds no video data (captcha?)"));
        const QJsonObject detail = parseObject(json).value("__DEFAULT_SCOPE__").toObject().value("webapp.video-detail").toObject();
        const int code = detail.value("statusCode").toInt();
        if (code != 0) {
            const QString msg = detail.value("statusMsg").toString();
            // 10204 = privat / nicht vorhanden, 10216 = gelöscht: yt-dlp kommt auch nicht weiter
            return fail(msg.isEmpty() ? QStringLiteral("TikTok status %1").arg(code) : QStringLiteral("%1 (%2)").arg(msg).arg(code),
                        code == 10204 || code == 10216);
        }
        const QJsonObject item = detail.value("itemInfo").toObject().value("itemStruct").toObject();
        if (item.isEmpty()) return fail(QStringLiteral("page holds no video data"));
        const QJsonObject video = item.value("video").toObject();
        m_result.title = videoTitle(item);
        m_result.url = pickVariant(videoVariants(video), m_maxHeight);
        if (m_result.url.isEmpty() && item.contains(QLatin1String("imagePost")))
            return fail(QStringLiteral("photo slideshow, no video"));
        m_result.subtitles = videoSubtitles(video);
        finish();
    });
}

void ExtractJob::live(const QString &user, bool explicitLive)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("aid"), QStringLiteral("1988"));
    query.addQueryItem(QStringLiteral("app_name"), QStringLiteral("tiktok_web"));
    query.addQueryItem(QStringLiteral("device_platform"), QStringLiteral("web_pc"));
    query.addQueryItem(QStringLiteral("sourceType"), QStringLiteral("54"));
    query.addQueryItem(QStringLiteral("uniqueId"), user);
    QUrl api(QStringLiteral("https://www.tiktok.com/api-live/user/room/"));
    api.setQuery(query);
    m_result.live = true;
    get(QString::fromUtf8(api.toEncoded()), [this, user, explicitLive](const Response &r) {
        if (r.status >= 400) return fail(QStringLiteral("HTTP %1").arg(r.status));
        const QJsonObject data = parseObject(r.text).value("data").toObject();
        const QJsonObject userInfo = data.value("user").toObject();
        const QJsonObject room = data.value("liveRoom").toObject();
        if (userInfo.isEmpty()) {
            // Profil-Link: Die LIVE-Schnittstelle kennt nicht jedes Konto, die Videos gibt es trotzdem
            if (!explicitLive) { m_result.live = false; return profile(user); }
            return fail(QStringLiteral("user @%1 not found").arg(user), true);
        }
        // 2 = sendet gerade, 4 = beendet
        const bool onAir = room.value("status").toInt() == 2 || userInfo.value("status").toInt() == 2;
        if (!onAir) {
            m_result.live = false;
            if (explicitLive) return fail(QStringLiteral("@%1 is not live").arg(user), true);
            // Profil-Link ohne laufende Sendung: Videos des Profils als Wiedergabeliste
            return profile(user);
        }
        const QString title = room.value("title").toString().simplified();
        m_result.title = title.isEmpty() ? QStringLiteral("@%1 · LIVE").arg(user) : QStringLiteral("@%1 · LIVE: %2").arg(user, title);
        const QString streamData = room.value("streamData").toObject().value("pull_data").toObject().value("stream_data").toString();
        QList<Variant> variants = liveVariants(streamData, m_liveFormat);
        if (variants.isEmpty()) variants = liveVariants(streamData, m_liveFormat == QLatin1String("hls") ? QStringLiteral("flv") : QStringLiteral("hls"));
        if (!variants.isEmpty()) {
            m_result.url = pickVariant(variants, m_maxHeight);
            return finish();
        }
        const QString roomId = userInfo.value("roomId").toString();
        if (roomId.isEmpty()) return fail(QStringLiteral("live room without stream"));
        liveFromRoomInfo(roomId);
    });
}

void ExtractJob::liveFromRoomInfo(const QString &roomId)
{
    get(QStringLiteral("https://webcast.tiktok.com/webcast/room/info/?aid=1988&room_id=%1").arg(roomId), [this](const Response &r) {
        const QJsonObject data = parseObject(r.text).value("data").toObject();
        const QJsonObject streamUrl = data.value("stream_url").toObject();
        QList<Variant> variants = liveVariants(streamUrl.value("live_core_sdk_data").toObject().value("pull_data").toObject()
                                                   .value("stream_data").toString(), m_liveFormat);
        if (variants.isEmpty()) variants = legacyLiveVariants(streamUrl, m_liveFormat);
        m_result.url = pickVariant(variants, m_maxHeight);
        finish();
    });
}

void ExtractJob::profile(const QString &user)
{
    get(QStringLiteral("https://www.tiktok.com/@%1").arg(user), [this, user](const Response &r) {
        if (r.status >= 400) return fail(QStringLiteral("@%1 is not live (profile HTTP %2)").arg(user).arg(r.status));
        const QString secUid = profileSecUid(r.text, &m_profileCount);
        if (m_profileCount == 0) return fail(QStringLiteral("@%1 is not live and has no videos").arg(user), true);
        // Ohne secUid (z. B. Captcha-Seite): yt-dlp versucht es mit dem Profil
        if (secUid.isEmpty()) return fail(QStringLiteral("@%1 is not live, profile data missing").arg(user));
        profilePage(user, secUid, QDateTime::currentMSecsSinceEpoch(), 1);
    });
}

void ExtractJob::profilePage(const QString &user, const QString &secUid, qint64 cursor, int page)
{
    // Abfrage wie die Webseite (und yt-dlp); neueste Videos zuerst, "cursor" blättert zeitlich zurück
    QUrlQuery query;
    const auto add = [&query](const char *key, const QString &value) { query.addQueryItem(QString::fromLatin1(key), value); };
    add("aid", QStringLiteral("1988"));
    add("app_language", QStringLiteral("en"));
    add("app_name", QStringLiteral("tiktok_web"));
    add("browser_language", QStringLiteral("en-US"));
    add("browser_name", QStringLiteral("Mozilla"));
    add("browser_online", QStringLiteral("true"));
    add("browser_platform", QStringLiteral("Win32"));
    add("browser_version", QStringLiteral("5.0 (Windows)"));
    add("channel", QStringLiteral("tiktok_web"));
    add("cookie_enabled", QStringLiteral("true"));
    add("count", QStringLiteral("15"));
    add("cursor", QString::number(cursor));
    add("device_id", QString::number(7250000000000000000ULL + QRandomGenerator::global()->bounded(quint64(1000000000000000000ULL))));
    add("device_platform", QStringLiteral("web_pc"));
    add("focus_state", QStringLiteral("true"));
    add("from_page", QStringLiteral("user"));
    add("history_len", QStringLiteral("2"));
    add("is_fullscreen", QStringLiteral("false"));
    add("is_page_visible", QStringLiteral("true"));
    add("language", QStringLiteral("en"));
    add("os", QStringLiteral("windows"));
    add("priority_region", QString());
    add("referer", QString());
    add("region", QStringLiteral("US"));
    add("screen_height", QStringLiteral("1080"));
    add("screen_width", QStringLiteral("1920"));
    add("secUid", secUid);
    add("type", QStringLiteral("1"));
    add("tz_name", QStringLiteral("UTC"));
    add("webcast_language", QStringLiteral("en"));
    QUrl api(QStringLiteral("https://www.tiktok.com/api/creator/item_list/"));
    api.setQuery(query);

    get(QString::fromUtf8(api.toEncoded()), [this, user, secUid, cursor, page](const Response &r) {
        const QJsonObject response = r.status == 200 ? parseObject(r.text) : QJsonObject();
        if (response.isEmpty()) {
            m_profileError = r.status ? QStringLiteral("video list HTTP %1").arg(r.status) : QStringLiteral("video list unavailable");
            return profileDone(user);   // was bis hierher kam, reicht für die Liste
        }
        QSet<QString> seen;
        for (const ProfileVideo &v : std::as_const(m_profileVideos)) seen.insert(v.id);
        for (const ProfileVideo &v : profileVideos(response, user)) {
            if (seen.contains(v.id) || m_profileVideos.size() >= kMaxProfileVideos) continue;
            seen.insert(v.id);
            m_profileVideos << v;
        }
        const qint64 next = nextProfileCursor(response, cursor);
        if (next == 0 || m_profileVideos.size() >= kMaxProfileVideos || page >= kMaxProfilePages
            || (m_profileCount > 0 && m_profileVideos.size() >= m_profileCount))
            return profileDone(user);
        profilePage(user, secUid, next, page + 1);
    }, true);
}

void ExtractJob::profileDone(const QString &user)
{
    if (m_profileVideos.isEmpty()) {
        // Liste nicht lesbar (z. B. verlangt TikTok eine Signatur): yt-dlp versucht es mit dem Profil
        return fail(QStringLiteral("@%1 is not live, %2").arg(user, m_profileError.isEmpty() ? QStringLiteral("no public videos") : m_profileError));
    }
    const ProfileVideo first = m_profileVideos.first();
    m_result.collection = QStringLiteral("@") + user;
    m_result.count = int(m_profileVideos.size());
    for (qsizetype i = 1; i < m_profileVideos.size(); ++i) {
        const ProfileVideo &v = m_profileVideos.at(i);
        m_result.more << videoPageUrl(v.user, v.id);
        m_result.moreTitles << v.title;
        m_result.moreDurations << v.duration;
    }
    // Das erste Video wie ein einzeln geöffnetes auflösen; die übrigen löst der Player beim Abspielen auf
    m_url = videoPageUrl(first.user, first.id);
    video(Link{LinkKind::Video, first.user, first.id});
}

} // namespace uve::tiktok
