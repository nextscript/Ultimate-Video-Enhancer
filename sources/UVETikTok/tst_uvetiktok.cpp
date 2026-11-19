// UVETikTok: Erkennung der Stream-Adressen ohne Netzwerk.
// Mit UVETIKTOK_LIVE=<Datei mit einem Link pro Zeile> prüft live() zusätzlich die echten Seiten (Ausgabe je Link).
#include "TikTokExtractor.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

using namespace uve::tiktok;

// JSON mit ' statt " schreibt sich im Test leichter
static QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object();
}

class TstUveTikTok : public QObject
{
    Q_OBJECT
private slots:
    void linkKinds()
    {
        Link l = parseLink("https://www.tiktok.com/@scout2015/video/6718335390845095173?lang=de");
        QCOMPARE(int(l.kind), int(LinkKind::Video));
        QCOMPARE(l.user, QString("scout2015"));
        QCOMPARE(l.id, QString("6718335390845095173"));

        l = parseLink("https://m.tiktok.com/v/6718335390845095173.html");
        QCOMPARE(int(l.kind), int(LinkKind::Video));
        QCOMPARE(l.id, QString("6718335390845095173"));

        l = parseLink("https://www.tiktok.com/embed/v2/6718335390845095173");
        QCOMPARE(int(l.kind), int(LinkKind::Video));
        QVERIFY(l.user.isEmpty());

        l = parseLink("https://www.tiktok.com/@some.user/photo/7300000000000000000");
        QCOMPARE(int(l.kind), int(LinkKind::Video));

        l = parseLink("https://www.tiktok.com/@some.user/live?enter_from_merge=others");
        QCOMPARE(int(l.kind), int(LinkKind::Live));
        QCOMPARE(l.user, QString("some.user"));

        l = parseLink("https://www.tiktok.com/@some_user/");
        QCOMPARE(int(l.kind), int(LinkKind::Profile));
        QCOMPARE(l.user, QString("some_user"));

        QCOMPARE(int(parseLink("https://vm.tiktok.com/ZMabcdef/").kind), int(LinkKind::Short));
        QCOMPARE(int(parseLink("https://vt.tiktok.com/ZSabcdef/").kind), int(LinkKind::Short));
        QCOMPARE(int(parseLink("https://www.tiktok.com/t/ZTabcdef/").kind), int(LinkKind::Short));
        QCOMPARE(int(parseLink("https://www.tiktok.com/foryou").kind), int(LinkKind::Unknown));
        QCOMPARE(int(parseLink("https://nottiktok.com/@a/video/1").kind), int(LinkKind::Unknown));
    }

    void scriptJsonFindsRehydration()
    {
        const QString html = "<script id=\"other\">x</script><script id=\"__UNIVERSAL_DATA_FOR_REHYDRATION__\" type=\"application/json\">{\"a\":1}</script>";
        QCOMPARE(scriptJson(html, "__UNIVERSAL_DATA_FOR_REHYDRATION__"), QString("{\"a\":1}"));
        QCOMPARE(scriptJson(html, "missing"), QString());
    }

    void videoVariantsUseShortSide()
    {
        const auto video = json("{"
"            'width': 576, 'height': 1024, 'playAddr': 'https://fallback',"
"            'bitrateInfo': ["
"              {'Bitrate': 2240963, 'PlayAddr': {'Width': 576, 'Height': 1024, 'UrlList': ['https://a540hi', 'https://backup']}},"
"              {'Bitrate': 1504834, 'PlayAddr': {'Width': 720, 'Height': 1280, 'UrlList': ['https://a720']}},"
"              {'Bitrate': 992286,  'PlayAddr': {'Width': 576, 'Height': 1024, 'UrlList': ['https://a540lo']}}"
"            ],"
"            'subtitleInfos': ["
"              {'Format': 'webvtt', 'Url': 'https://sub.en'}, {'Format': 'creator_caption', 'Url': 'https://x'}"
"            ]}");
        const auto variants = videoVariants(video);
        QCOMPARE(variants.size(), 3);
        QCOMPARE(variants.first().height, 576);
        QCOMPARE(pickVariant(variants, 0), QString("https://a720"));
        QCOMPARE(pickVariant(variants, 576), QString("https://a540hi"));   // gleiche Höhe: höhere Bitrate
        QCOMPARE(pickVariant(variants, 360), QString("https://a540hi"));   // nichts passt: niedrigste Stufe
        QCOMPARE(videoSubtitles(video), QStringList{"https://sub.en"});
    }

    void dashVariantsGetSeparateAudio()
    {
        // Wie @freak288/video/7708146946057178390: 1080p/720p nur als DASH-Bild, Ton getrennt
        QJsonObject video{
            {"bitrateInfo", QJsonArray{
                QJsonObject{{"Bitrate", 143277}, {"Format", "mp4"}, {"PlayAddr", QJsonObject{{"Width", 576}, {"Height", 1024}, {"UrlList", QJsonArray{"https://mp4;540"}}}}},
                QJsonObject{{"Bitrate", 69534}, {"Format", "dash"}, {"PlayAddr", QJsonObject{{"Width", 1080}, {"Height", 1920}, {"UrlList", QJsonArray{"https://v1080"}}}}}}},
            {"bitrateAudioInfo", QJsonArray{
                QJsonObject{{"Bitrate", "32000"}, {"UrlList", QJsonObject{{"MainUrl", "https://alow"}}}},
                QJsonObject{{"Bitrate", "65006"}, {"UrlList", QJsonObject{{"MainUrl", "https://a,hi"}}}}}}};
        QCOMPARE(videoAudio(video), QString("https://a,hi"));
        QCOMPARE(pickVariant(videoVariants(video), 0), QString("edl://!no_clip;%13%https://v1080;!new_stream;!no_clip;%12%https://a,hi"));
        QCOMPARE(pickVariant(videoVariants(video), 720), QString("https://mp4;540"));

        // Ohne Tonspur: stumme DASH-Stufe nicht nehmen
        video.remove("bitrateAudioInfo");
        const auto variants = videoVariants(video);
        QCOMPARE(variants.size(), 1);
        QCOMPARE(pickVariant(variants, 0), QString("https://mp4;540"));
    }

    void videoVariantsFallBackToPlayAddr()
    {
        const auto video = json("{'width': 1080, 'height': 1920, 'playAddr': 'https://play'}");
        const auto variants = videoVariants(video);
        QCOMPARE(variants.size(), 1);
        QCOMPARE(variants.first().height, 1080);
        QCOMPARE(variants.first().url, QString("https://play"));
    }

    void titleCombinesAuthorAndDescription()
    {
        QCOMPARE(videoTitle(json("{'desc': '  hello   world ', 'author': {'uniqueId': 'me'}}")),
                 QString("@me: hello world"));
        QCOMPARE(videoTitle(json("{'desc': '', 'author': {'uniqueId': 'me'}}")), QString("@me"));
        QCOMPARE(videoTitle(QJsonObject{}), QString("TikTok"));
    }

    void liveVariantsFromStreamData()
    {
        const auto entry = [](const char *flv, const char *hls, const char *res, int bitrate) {
            return QJsonObject{{"main", QJsonObject{{"flv", flv}, {"hls", hls},
                {"sdk_params", QString::fromUtf8(QJsonDocument(QJsonObject{{"resolution", res}, {"vbitrate", bitrate}}).toJson(QJsonDocument::Compact))}}}};
        };
        const QJsonObject data{{"ao", entry("https://ao.flv", "", "", 0)},
                               {"origin", entry("https://origin.flv", "https://origin.m3u8", "1080x1920", 0)},
                               {"uhd", entry("https://uhd.flv", "https://uhd.m3u8", "1080x1920", 1800000)},
                               {"hd", entry("https://hd.flv", "https://hd.m3u8", "720x1280", 1200000)},
                               {"ld", entry("https://ld.flv", "", "360x640", 600000)}};
        const QString streamData = QString::fromUtf8(QJsonDocument(QJsonObject{{"data", data}}).toJson(QJsonDocument::Compact));

        const auto flv = liveVariants(streamData, "flv");
        QCOMPARE(flv.size(), 4);   // ohne "ao"
        QCOMPARE(pickVariant(flv, 0), QString("https://origin.flv"));
        QCOMPARE(pickVariant(flv, 720), QString("https://hd.flv"));
        QCOMPARE(pickVariant(flv, 540), QString("https://ld.flv"));

        const auto hls = liveVariants(streamData, "hls");
        QCOMPARE(hls.size(), 3);
        QCOMPARE(pickVariant(hls, 0), QString("https://origin.m3u8"));
        QVERIFY(liveVariants(QString(), "flv").isEmpty());
    }

    void legacyLiveVariants()
    {
        const QJsonObject streamUrl{{"flv_pull_url", QJsonObject{{"FULL_HD1", "https://fhd"}, {"HD1", "https://hd"}, {"SD1", "https://sd"}}},
                                    {"hls_pull_url", "https://single.m3u8"}};
        QCOMPARE(pickVariant(uve::tiktok::legacyLiveVariants(streamUrl, "flv"), 720), QString("https://hd"));
        QCOMPARE(pickVariant(uve::tiktok::legacyLiveVariants(streamUrl, "hls"), 0), QString("https://single.m3u8"));
    }

    void profileSecUidFromPage()
    {
        const QString html = "<script id=\"__UNIVERSAL_DATA_FOR_REHYDRATION__\" type=\"application/json\">"
                             "{\"__DEFAULT_SCOPE__\":{\"webapp.user-detail\":{\"userInfo\":{\"user\":{\"uniqueId\":\"a\",\"secUid\":\"MS4wABC\"},"
                             "\"stats\":{\"videoCount\":77}}}}}</script>";
        int count = 0;
        QCOMPARE(profileSecUid(html, &count), QString("MS4wABC"));
        QCOMPARE(count, 77);
        QVERIFY(profileSecUid("<html></html>", &count).isEmpty());
        QCOMPARE(count, -1);
    }

    void profileVideosSkipPhotos()
    {
        const QJsonObject response = json("{'hasMorePrevious':true,'itemList':["
                                          "{'id':'111','desc':'erstes','createTime':1700000300,'author':{'uniqueId':'freak'},'video':{'duration':12}},"
                                          "{'id':'222','desc':'foto','createTime':1700000200,'imagePost':{},'video':{}},"
                                          "{'id':'333','desc':'','createTime':1700000100,'video':{'duration':7.5}}]}");
        const QList<ProfileVideo> videos = profileVideos(response, "fallback");
        QCOMPARE(videos.size(), 2);
        QCOMPARE(videos.at(0).id, QString("111"));
        QCOMPARE(videos.at(0).title, QString("@freak: erstes"));
        QCOMPARE(videos.at(0).duration, 12.0);
        QCOMPARE(videos.at(1).user, QString("fallback"));
        QCOMPARE(videos.at(1).duration, 7.5);
        QCOMPARE(videoPageUrl(videos.at(1).user, videos.at(1).id), QString("https://www.tiktok.com/@fallback/video/333"));
    }

    void profileCursor()
    {
        const qint64 now = 1700000400000;
        // Weiter beim ältesten Video der Seite
        QCOMPARE(nextProfileCursor(json("{'hasMorePrevious':true,'itemList':[{'createTime':1700000300},{'createTime':1700000100}]}"), now),
                 qint64(1700000100000));
        // Leere Woche: selbst eine Woche zurück
        QCOMPARE(nextProfileCursor(json("{'hasMorePrevious':true,'itemList':[]}"), now), now - 7LL * 86400 * 1000);
        // Ende
        QCOMPARE(nextProfileCursor(json("{'hasMorePrevious':false,'itemList':[{'createTime':1700000100}]}"), now), qint64(0));
        // Vor TikTok: Ende, auch wenn hasMorePrevious falsch gesetzt ist
        QCOMPARE(nextProfileCursor(json("{'hasMorePrevious':true,'itemList':[]}"), 1472706000000LL + 1000), qint64(0));
    }

    void live()
    {
        const QString list = qEnvironmentVariable("UVETIKTOK_LIVE");
        if (list.isEmpty()) QSKIP("UVETIKTOK_LIVE not set");
        QFile file(list);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList urls = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
        for (const QString &raw : urls) {
            const QString url = raw.trimmed();
            if (url.isEmpty()) continue;
            ExtractJob job(url, qEnvironmentVariableIntValue("UVETIKTOK_MAX"), qEnvironmentVariable("UVETIKTOK_FORMAT"));
            QSignalSpy ok(&job, &ExtractJob::finished);
            QSignalSpy bad(&job, &ExtractJob::failed);
            job.start();
            QTRY_VERIFY_WITH_TIMEOUT(ok.count() + bad.count() > 0, 70000);
            if (ok.count()) {
                const auto r = ok.first().first().value<Extraction>();
                printf("OK   %s\n     -> %s\n     live=%d subs=%d cookie=%s title=%s\n", qPrintable(url), qPrintable(r.url.left(160)), int(r.live),
                       int(r.subtitles.size()), qPrintable(r.cookie.left(60)), qPrintable(r.title.left(60)));
                if (!r.collection.isEmpty())
                    printf("     list %s: %d videos, next=%s\n", qPrintable(r.collection), r.count, qPrintable(r.more.value(0)));
            } else {
                printf("FAIL %s: %s (definitive=%d)\n", qPrintable(url), qPrintable(bad.first().first().toString()), int(bad.first().at(1).toBool()));
            }
            fflush(stdout);
        }
    }
};

QTEST_GUILESS_MAIN(TstUveTikTok)
#include "tst_uvetiktok.moc"
