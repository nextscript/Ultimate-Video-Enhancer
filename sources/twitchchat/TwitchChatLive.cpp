// Live-Kanäle für das Fenster „Live auf Twitch“ (Teil von TwitchChatController).
//  – „Gefolgt“:     Helix /streams/followed mit dem Chat-Login (Recht user:read:follows)
//  – „Beliebt“:     gql.twitch.tv streams (öffentlich, ohne Login)
//  – „Kategorien“:  gql.twitch.tv games, Kanäle einer Kategorie über game(name).streams
//  – „Suche“:       gql.twitch.tv searchStreams + searchFor.games
// Sortierung „Für dich empfohlen“: Twitch liefert ohne Website-Login (auch mit recommendationsContext) dieselbe Reihenfolge
// wie nach Zuschauern, und die offizielle Helix-API kennt keine Empfehlungen. Darum empfiehlt das Plugin selbst:
// gefolgte Kanäle, im Fenster geschaute Kanäle und deren Kategorien zuerst, dann Zuschauer. Bei „Beliebt“ kommen
// zusätzlich Live-Kanäle aus den meistgeschauten Kategorien dazu.
// Ein Klick spielt den Kanal über den Player (player.load → vorhandener Twitch-Resolver), der Chat wechselt dorthin.
#include "TwitchChatController.h"
#include "WhisperService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

namespace {
const char *kLiveGqlClient = "kimne78kx3ncx6brgo4mv6wki5h1ko";   // öffentliche Twitch-Web-Client-ID (wie die Emote-Kataloge)
constexpr int kLiveTimeoutMs = 15000;
constexpr int kThumbW = 440, kThumbH = 248;
constexpr int kHistoryMax = 200;          // gemerkte Kanäle bzw. Kategorien
constexpr int kFavoriteGames = 2;         // so viele Lieblingskategorien fließen in „Beliebt“ ein
constexpr int kPerFavoriteGame = 12;
constexpr int kClickedCategories = 10;    // so viele meistgeklickte Kategorien werden in „Kategorien“ nachgeladen

const char *kNodeFields =
    "title viewersCount language previewImageURL(width: 440, height: 248) game { displayName } "
    "broadcaster { login displayName profileImageURL(width: 50) }";
const char *kGameFields = "name displayName viewersCount boxArtURL(width: 188, height: 250)";

QString normLang(QString code) { return code.trimmed().toLower().replace(QLatin1Char('-'), QLatin1Char('_')); }

QVariantMap liveRow(const QString &login, const QString &name, const QString &title, const QString &game,
                    int viewers, const QString &thumb, const QString &avatar, const QString &language)
{
    return {{QStringLiteral("login"), login.toLower()}, {QStringLiteral("name"), name.isEmpty() ? login : name},
            {QStringLiteral("title"), title}, {QStringLiteral("game"), game}, {QStringLiteral("viewers"), viewers},
            {QStringLiteral("thumb"), thumb}, {QStringLiteral("avatar"), avatar}, {QStringLiteral("language"), normLang(language)}};
}

void appendEdges(QVariantList &out, QSet<QString> &seen, const QJsonArray &edges)
{
    for (const QJsonValue &e : edges) {
        const QJsonObject n = e.toObject().value(QStringLiteral("node")).toObject();
        const QJsonObject b = n.value(QStringLiteral("broadcaster")).toObject();
        const QString login = b.value(QStringLiteral("login")).toString().toLower();
        if (login.isEmpty() || seen.contains(login)) continue;
        seen.insert(login);
        out << liveRow(login, b.value(QStringLiteral("displayName")).toString(), n.value(QStringLiteral("title")).toString(),
                       n.value(QStringLiteral("game")).toObject().value(QStringLiteral("displayName")).toString(),
                       n.value(QStringLiteral("viewersCount")).toInt(), n.value(QStringLiteral("previewImageURL")).toString(),
                       b.value(QStringLiteral("profileImageURL")).toString(), n.value(QStringLiteral("language")).toString());
    }
}

QJsonArray streamEdges(const QJsonObject &o) { return o.value(QStringLiteral("streams")).toObject().value(QStringLiteral("edges")).toArray(); }

void bumpHistory(QJsonObject &map, const QString &key)
{
    if (key.isEmpty()) return;
    map.insert(key, map.value(key).toInt() + 1);
    while (map.size() > kHistoryMax) {   // seltenstes vergessen
        auto lowest = map.begin();
        for (auto it = map.begin(); it != map.end(); ++it)
            if (it.value().toInt() < lowest.value().toInt()) lowest = it;
        map.erase(lowest);
    }
}
} // namespace

bool TwitchChatController::canListFollowed() const
{
    return !m_token.isEmpty() && m_whispers && !m_whispers->userId().isEmpty()
           && m_whispers->hasScope(QStringLiteral("user:read:follows"));
}

// ------------------------------------------------------------------------------------------------ Parser

QVariantList TwitchChatController::liveRowsFromGql(const QByteArray &json)
{
    QVariantList out;
    QSet<QString> seen;
    const QJsonObject data = QJsonDocument::fromJson(json).object().value(QStringLiteral("data")).toObject();
    // streams (Beliebt), searchStreams (Suche), game.streams (Kategorie), fav0/fav1.streams (Lieblingskategorien)
    appendEdges(out, seen, data.value(QStringLiteral("streams")).toObject().value(QStringLiteral("edges")).toArray());
    appendEdges(out, seen, data.value(QStringLiteral("searchStreams")).toObject().value(QStringLiteral("edges")).toArray());
    appendEdges(out, seen, streamEdges(data.value(QStringLiteral("game")).toObject()));
    for (auto it = data.begin(); it != data.end(); ++it)
        if (it.key().startsWith(QLatin1String("fav"))) appendEdges(out, seen, streamEdges(it.value().toObject()));
    return out;
}

QVariantList TwitchChatController::liveRowsFromHelix(const QByteArray &json)
{
    QVariantList out;
    const QJsonArray data = QJsonDocument::fromJson(json).object().value(QStringLiteral("data")).toArray();
    for (const QJsonValue &v : data) {
        const QJsonObject o = v.toObject();
        const QString login = o.value(QStringLiteral("user_login")).toString();
        if (login.isEmpty()) continue;
        // nur wer wirklich sendet (Helix setzt type "live"; leer = Fehler/offline)
        if (o.contains(QStringLiteral("type")) && o.value(QStringLiteral("type")).toString() != QLatin1String("live")) continue;
        QString thumb = o.value(QStringLiteral("thumbnail_url")).toString();
        thumb.replace(QStringLiteral("{width}"), QString::number(kThumbW)).replace(QStringLiteral("{height}"), QString::number(kThumbH));
        out << liveRow(login, o.value(QStringLiteral("user_name")).toString(), o.value(QStringLiteral("title")).toString(),
                       o.value(QStringLiteral("game_name")).toString(), o.value(QStringLiteral("viewer_count")).toInt(), thumb,
                       QString(), o.value(QStringLiteral("language")).toString());
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("viewers")).toInt() > b.toMap().value(QStringLiteral("viewers")).toInt();
    });
    return out;
}

QVariantList TwitchChatController::liveCategoriesFromGql(const QByteArray &json)
{
    QVariantList out;
    const QJsonObject data = QJsonDocument::fromJson(json).object().value(QStringLiteral("data")).toObject();
    QJsonArray nodes;
    for (const QJsonValue &e : data.value(QStringLiteral("games")).toObject().value(QStringLiteral("edges")).toArray())
        nodes << e.toObject().value(QStringLiteral("node"));
    // Suche: searchFor.games.items (Kategorien ohne Zuschauer = gerade niemand live → ausblenden)
    for (const QJsonValue &v : data.value(QStringLiteral("searchFor")).toObject().value(QStringLiteral("games")).toObject()
                                   .value(QStringLiteral("items")).toArray())
        nodes << v;
    // clk0, clk1 …: meistgeklickte Kategorien, auch wenn sie nicht unter den 30 größten sind
    for (auto it = data.begin(); it != data.end(); ++it)
        if (it.key().startsWith(QLatin1String("clk")) && it.value().isObject()) nodes << it.value();
    QSet<QString> seen;
    for (const QJsonValue &v : std::as_const(nodes)) {
        const QJsonObject o = v.toObject();
        const QString name = o.value(QStringLiteral("name")).toString();
        const int viewers = o.value(QStringLiteral("viewersCount")).toInt();
        if (name.isEmpty() || viewers <= 0 || seen.contains(name.toLower())) continue;
        seen.insert(name.toLower());
        const QString display = o.value(QStringLiteral("displayName")).toString();
        out << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("title"), display.isEmpty() ? name : display},
                           {QStringLiteral("viewers"), viewers}, {QStringLiteral("boxArt"), o.value(QStringLiteral("boxArtURL")).toString()}};
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ Reihenfolge

static int viewersOf(const QVariant &v) { return v.toMap().value(QStringLiteral("viewers")).toInt(); }

QVariantList TwitchChatController::rankLiveStreams(const QVariantList &rows, const QString &sort, const QString &language,
                                                   const QHash<QString, int> &channelHistory, const QHash<QString, int> &gameHistory,
                                                   const QSet<QString> &followed, const QSet<QString> &followedGames)
{
    QVariantList out;
    const QString lang = normLang(language);
    for (const QVariant &v : rows) {
        const QString rowLang = v.toMap().value(QStringLiteral("language")).toString();
        // Unbekannte Sprache nicht wegfiltern (z. B. ältere Antworten ohne Feld)
        if (lang.isEmpty() || rowLang.isEmpty() || rowLang == lang) out << v;
    }
    if (sort == QLatin1String("viewers")) {
        std::stable_sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) { return viewersOf(a) > viewersOf(b); });
        return out;
    }
    // Für dich: gefolgt > oft geschaut > Lieblingskategorie > Zuschauer (logarithmisch, damit Große nicht alles verdrängen)
    const auto score = [&](const QVariant &v) {
        const QVariantMap m = v.toMap();
        const QString login = m.value(QStringLiteral("login")).toString();
        const QString game = m.value(QStringLiteral("game")).toString().toLower();
        double s = std::log10(double(qMax(0, m.value(QStringLiteral("viewers")).toInt())) + 10.0);
        if (followed.contains(login)) s += 4.0;
        if (const int c = channelHistory.value(login)) s += 2.0 + 0.4 * qMin(c, 5);
        if (!game.isEmpty()) {
            if (const int g = gameHistory.value(game)) s += 1.5 + 0.25 * qMin(g, 10);
            if (followedGames.contains(game)) s += 1.0;
        }
        return s;
    };
    std::stable_sort(out.begin(), out.end(), [&](const QVariant &a, const QVariant &b) { return score(a) > score(b); });
    return out;
}

QVariantList TwitchChatController::rankLiveCategories(const QVariantList &categories, const QString &sort,
                                                      const QHash<QString, int> &gameHistory, const QSet<QString> &followedGames,
                                                      const QHash<QString, int> &categoryClicks)
{
    QVariantList out = categories;
    if (sort == QLatin1String("viewers")) {
        std::stable_sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) { return viewersOf(a) > viewersOf(b); });
        return out;
    }
    // Für dich: meistgeklickte Kategorien zuerst (nach Anzahl der Klicks), dann solche, in denen Kanäle geschaut
    // wurden, dann Kategorien gefolgter Live-Kanäle, zuletzt nach Zuschauern
    struct Key { int clicks, plays, followed, viewers; };
    const auto key = [&](const QVariant &v) {
        const QVariantMap m = v.toMap();
        const QString name = m.value(QStringLiteral("name")).toString().toLower();
        const QString title = m.value(QStringLiteral("title")).toString().toLower();
        return Key{qMax(categoryClicks.value(name), categoryClicks.value(title)),
                   qMax(gameHistory.value(name), gameHistory.value(title)),
                   (followedGames.contains(name) || followedGames.contains(title)) ? 1 : 0, viewersOf(v)};
    };
    // Grund mitgeben: die Karte zeigt ein Abzeichen („2× geöffnet“, „geschaut“, „gefolgt live“)
    for (QVariant &v : out) {
        const Key k = key(v);
        QVariantMap m = v.toMap();
        m.insert(QStringLiteral("clicks"), k.clicks);
        m.insert(QStringLiteral("plays"), k.plays);
        m.insert(QStringLiteral("followedGame"), k.followed == 1);
        v = m;
    }
    std::stable_sort(out.begin(), out.end(), [&](const QVariant &a, const QVariant &b) {
        const Key x = key(a), y = key(b);
        if (x.clicks != y.clicks) return x.clicks > y.clicks;
        if (x.plays != y.plays) return x.plays > y.plays;
        if (x.followed != y.followed) return x.followed > y.followed;
        return x.viewers > y.viewers;
    });
    return out;
}

void TwitchChatController::liveHistory(QHash<QString, int> &channels, QHash<QString, int> &games, QHash<QString, int> *categories) const
{
    channels.clear();
    games.clear();
    if (categories) categories->clear();
    if (!m_host) return;
    const QJsonObject h = m_host->loadPluginSettings(m_id).value(QStringLiteral("liveHistory")).toObject();
    const QJsonObject c = h.value(QStringLiteral("channels")).toObject(), g = h.value(QStringLiteral("games")).toObject();
    for (auto it = c.begin(); it != c.end(); ++it) channels.insert(it.key(), it.value().toInt());
    for (auto it = g.begin(); it != g.end(); ++it) games.insert(it.key(), it.value().toInt());
    if (categories) {
        const QJsonObject k = h.value(QStringLiteral("categories")).toObject();
        for (auto it = k.begin(); it != k.end(); ++it) categories->insert(it.key(), it.value().toInt());
    }
}

QStringList TwitchChatController::liveTopCategories(int count) const
{
    QHash<QString, int> channels, games, cats;
    liveHistory(channels, games, &cats);
    // erst die meistgeklickten, dann Kategorien, in denen Kanäle geschaut wurden
    QStringList out;
    for (const QHash<QString, int> *src : {&cats, &games}) {
        QList<QPair<int, QString>> list;
        for (auto it = src->begin(); it != src->end(); ++it) list << qMakePair(it.value(), it.key());
        std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (const auto &p : std::as_const(list)) {
            if (out.size() >= count) return out;
            if (!out.contains(p.second)) out << p.second;
        }
    }
    return out;
}

void TwitchChatController::noteLiveCategory(const QString &raw)
{
    const QString name = raw.trimmed().toLower();
    if (name.isEmpty() || !m_host) return;
    QJsonObject s = m_host->loadPluginSettings(m_id);   // Lesen–Ändern–Schreiben
    QJsonObject h = s.value(QStringLiteral("liveHistory")).toObject();
    QJsonObject cats = h.value(QStringLiteral("categories")).toObject();
    bumpHistory(cats, name);
    h.insert(QStringLiteral("categories"), cats);
    s.insert(QStringLiteral("liveHistory"), h);
    m_host->savePluginSettings(m_id, s);
}

QStringList TwitchChatController::liveFavoriteGames(int count) const
{
    QHash<QString, int> channels, games;
    liveHistory(channels, games);
    QList<QPair<int, QString>> list;
    for (auto it = games.begin(); it != games.end(); ++it) list << qMakePair(it.value(), it.key());
    std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList out;
    for (const auto &p : std::as_const(list)) {
        if (out.size() >= count) break;
        out << p.second;
    }
    return out;
}

bool TwitchChatController::hasLiveProfile() const
{
    QHash<QString, int> channels, games, cats;
    liveHistory(channels, games, &cats);
    return !channels.isEmpty() || !games.isEmpty() || !cats.isEmpty() || !m_followedLive.isEmpty();
}

void TwitchChatController::publishLive(const QString &tab, const QString &error)
{
    QHash<QString, int> channels, games, cats;
    liveHistory(channels, games, &cats);
    // „Gefolgt“: alle gefolgten Live-Kanäle, ohne Sprachfilter und ohne „Für dich empfohlen“ (immer nach Zuschauern)
    const bool follow = tab == QLatin1String("follow");
    const QString lang = follow ? QString() : m_liveLangFilter;
    const QString sort = follow ? QStringLiteral("viewers") : m_liveSortMode;
    applyLiveResult(tab, rankLiveStreams(m_liveRawStreams, sort, lang, channels, games, m_followedLive, m_followedGames),
                    error, rankLiveCategories(m_liveRawCategories, m_liveSortMode, games, m_followedGames, cats), m_liveCategory);
}

void TwitchChatController::noteFollowed(const QVariantList &followedRows)
{
    m_followedLive.clear();
    m_followedGames.clear();
    for (const QVariant &v : followedRows) {
        const QVariantMap m = v.toMap();
        m_followedLive.insert(m.value(QStringLiteral("login")).toString());
        const QString game = m.value(QStringLiteral("game")).toString().toLower();
        if (!game.isEmpty()) m_followedGames.insert(game);
    }
    m_followedKnown = true;
}

// Für „Für dich empfohlen“ im Hintergrund wissen, welche gefolgten Kanäle gerade live sind (einmal pro Sitzung)
void TwitchChatController::ensureFollowedKnown()
{
    if (m_followedKnown || m_followedFetching || !canListFollowed()) return;
    m_followedFetching = true;
    QUrl url(QStringLiteral("https://api.twitch.tv/helix/streams/followed"));
    url.setQuery(QUrlQuery{{QStringLiteral("user_id"), m_whispers->userId()}, {QStringLiteral("first"), QStringLiteral("100")}});
    const auto done = [this](int status, const QByteArray &body) {
        m_followedFetching = false;
        if (status != 200) return;
        noteFollowed(liveRowsFromHelix(body));
        // Liste neu ordnen, falls gerade eine Empfehlung angezeigt wird
        if (!m_liveBusy && m_liveSortMode == QLatin1String("relevance") && m_liveTab != QLatin1String("follow")
            && (!m_liveRawStreams.isEmpty() || !m_liveRawCategories.isEmpty()))
            publishLive(m_liveTab, m_liveError);
    };
    if (m_helixHook) { m_helixHook(QStringLiteral("GET"), url, QByteArray(), done); return; }
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QNetworkRequest req{url};
    req.setTransferTimeout(kLiveTimeoutMs);
    req.setRawHeader("Client-Id", m_whispers->clientId().toUtf8());
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        done(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll());
    });
}

// ------------------------------------------------------------------------------------------------ Laden

// „Gefolgt“: alle gefolgten Kanäle, die gerade live sind – seitenweise (je 100), Offline-Kanäle liefert Helix nicht
void TwitchChatController::fetchFollowedPage(quint64 gen, const QString &tab, const QString &cursor, QVariantList acc, int page)
{
    QUrl url(QStringLiteral("https://api.twitch.tv/helix/streams/followed"));
    QUrlQuery q{{QStringLiteral("user_id"), m_whispers->userId()}, {QStringLiteral("first"), QStringLiteral("100")}};
    if (!cursor.isEmpty()) q.addQueryItem(QStringLiteral("after"), cursor);
    url.setQuery(q);
    const auto done = [this, gen, tab, prev = std::move(acc), page](int status, const QByteArray &body) {
        if (gen != m_liveGeneration) return;
        QVariantList acc = prev;
        if (status == 401) return applyLiveResult(tab, {}, QStringLiteral("expired"));
        if (status != 200) {
            if (acc.isEmpty()) return applyLiveResult(tab, {}, QStringLiteral("HTTP %1").arg(status));
        } else {
            QSet<QString> seen;
            for (const QVariant &v : std::as_const(acc)) seen.insert(v.toMap().value(QStringLiteral("login")).toString());
            for (const QVariant &v : liveRowsFromHelix(body))
                if (!seen.contains(v.toMap().value(QStringLiteral("login")).toString())) acc << v;
            const QString next = QJsonDocument::fromJson(body).object().value(QStringLiteral("pagination")).toObject()
                                     .value(QStringLiteral("cursor")).toString();
            if (!next.isEmpty() && page + 1 < 10) return fetchFollowedPage(gen, tab, next, acc, page + 1);
        }
        std::stable_sort(acc.begin(), acc.end(), [](const QVariant &a, const QVariant &b) { return viewersOf(a) > viewersOf(b); });
        m_liveRawStreams = acc;
        noteFollowed(m_liveRawStreams);
        publishLive(tab);
    };
    if (m_helixHook) { m_helixHook(QStringLiteral("GET"), url, QByteArray(), done); return; }
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QNetworkRequest req{url};
    req.setTransferTimeout(kLiveTimeoutMs);
    req.setRawHeader("Client-Id", m_whispers->clientId().toUtf8());
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        done(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll());
    });
}

void TwitchChatController::applyLiveResult(const QString &tab, const QVariantList &rows, const QString &error,
                                           const QVariantList &categories, const QString &category)
{
    ++m_liveGeneration;          // ein noch laufender Abruf darf dieses Ergebnis nicht mehr überschreiben
    m_liveTab = tab;
    m_liveCategories = categories;
    m_liveCategory = category;
    m_liveStreams = rows;
    m_liveError = error;
    m_liveBusy = false;
    emit liveStreamsChanged();
}

void TwitchChatController::loadLiveStreams(const QString &tab, const QString &query, const QString &language, const QString &sort)
{
    const quint64 gen = ++m_liveGeneration;
    m_liveTab = tab;
    m_liveCategory = tab == QLatin1String("category") ? query.trimmed() : QString();
    m_liveSortMode = sort == QLatin1String("viewers") ? sort : QStringLiteral("relevance");
    // Twitch-Sprachcodes: zweistellig (de, en …) sowie zh_hk, asl, other
    static const QRegularExpression code(QStringLiteral("^([a-z]{2}(_[a-z]{2})?|asl|other)$"));
    const QString lang = normLang(language);
    m_liveLangFilter = code.match(lang).hasMatch() ? lang : QString();
    m_liveRawStreams.clear();
    m_liveRawCategories.clear();
    m_liveError.clear();
    m_liveBusy = true;
    emit liveStreamsChanged();
    if (!m_net) m_net = new QNetworkAccessManager(this);
    if (m_liveSortMode == QLatin1String("relevance")) ensureFollowedKnown();

    if (tab == QLatin1String("follow")) {
        if (!canListFollowed()) {
            applyLiveResult(tab, {}, m_token.isEmpty()
                ? QStringLiteral("login")          // QML zeigt „Mit Twitch anmelden“
                : QStringLiteral("scope"));        // Login ohne user:read:follows → neu anmelden
            return;
        }
        fetchFollowedPage(gen, tab, QString(), {}, 0);
        return;
    }

    // Beliebt / Kategorien / Suche über gql (Variablen, damit Suchtexte nie die Abfrage verändern)
    const QString filter = m_liveLangFilter.isEmpty() ? QString()
                                                       : QStringLiteral(", broadcasterLanguages: [%1]").arg(m_liveLangFilter.toUpper());
    const QString order = m_liveSortMode == QLatin1String("viewers") ? QStringLiteral("VIEWER_COUNT") : QStringLiteral("RELEVANCE");
    QJsonObject vars;
    QString gql;
    if (tab == QLatin1String("search")) {
        const QString text = query.trimmed();
        if (text.isEmpty()) { applyLiveResult(tab, {}); return; }
        vars.insert(QStringLiteral("q"), text);
        // Kanäle und passende Kategorien in einer Abfrage; Sprache filtert danach das Plugin
        gql = QStringLiteral("query($q: String!) { searchStreams(userQuery: $q, first: 30) { edges { node { %1 } } } "
                             "searchFor(userQuery: $q, platform: \"web\") { games { items { %2 } } } }")
                  .arg(QLatin1String(kNodeFields), QLatin1String(kGameFields));
    } else if (tab == QLatin1String("categories")) {
        // Für dich: die meistgeklickten Kategorien zusätzlich direkt abfragen (clk0 …), damit sie auch dann oben
        // stehen, wenn sie nicht zu den 30 größten gehören
        QString clicked, params;
        if (m_liveSortMode == QLatin1String("relevance")) {
            const QStringList top = liveTopCategories(kClickedCategories);
            for (int i = 0; i < top.size(); ++i) {
                const QString var = QStringLiteral("c%1").arg(i);
                vars.insert(var, top.at(i));
                params += QStringLiteral("%1$%2: String!").arg(params.isEmpty() ? QString() : QStringLiteral(", "), var);
                clicked += QStringLiteral(" clk%1: game(name: $%2) { %3 }").arg(i).arg(var, QLatin1String(kGameFields));
            }
        }
        gql = QStringLiteral("%1{ games(first: 30, options: {sort: %3}) { edges { node { %2 } } }%4 }")
                  .arg(params.isEmpty() ? QString() : QStringLiteral("query(%1) ").arg(params), QLatin1String(kGameFields),
                       order, clicked);
    } else if (tab == QLatin1String("category")) {
        if (m_liveCategory.isEmpty()) { applyLiveResult(QStringLiteral("categories"), {}); return; }
        vars.insert(QStringLiteral("n"), m_liveCategory);
        gql = QStringLiteral("query($n: String!) { game(name: $n) { streams(first: 30, options: {sort: %3%1}) "
                             "{ edges { node { %2 } } } } }")
                  .arg(filter, QLatin1String(kNodeFields), order);
    } else {
        // Beliebt; bei „Für dich“ zusätzlich Live-Kanäle aus den meistgeschauten Kategorien (Aliase fav0, fav1)
        QString favs, params;
        if (m_liveSortMode == QLatin1String("relevance")) {
            const QStringList games = liveFavoriteGames(kFavoriteGames);
            for (int i = 0; i < games.size(); ++i) {
                const QString var = QStringLiteral("g%1").arg(i);
                vars.insert(var, games.at(i));
                params += QStringLiteral("%1$%2: String!").arg(params.isEmpty() ? QString() : QStringLiteral(", "), var);
                favs += QStringLiteral(" fav%1: game(name: $%2) { streams(first: %3, options: {sort: VIEWER_COUNT%4}) "
                                       "{ edges { node { %5 } } } }")
                            .arg(i).arg(var).arg(kPerFavoriteGame).arg(filter, QLatin1String(kNodeFields));
            }
        }
        gql = QStringLiteral("%1{ streams(first: 30, options: {sort: %2%3}) { edges { node { %4 } } }%5 }")
                  .arg(params.isEmpty() ? QString() : QStringLiteral("query(%1) ").arg(params), order, filter,
                       QLatin1String(kNodeFields), favs);
    }
    QJsonObject payload{{QStringLiteral("query"), gql}};
    if (!vars.isEmpty()) payload.insert(QStringLiteral("variables"), vars);

    QNetworkRequest req{QUrl(QStringLiteral("https://gql.twitch.tv/gql"))};
    req.setTransferTimeout(kLiveTimeoutMs);
    req.setRawHeader("Client-ID", QByteArray(kLiveGqlClient));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_net->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, tab] {
        reply->deleteLater();
        if (gen != m_liveGeneration) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            applyLiveResult(tab, {}, status ? QStringLiteral("HTTP %1").arg(status) : reply->errorString(), {}, m_liveCategory);
            return;
        }
        const QByteArray body = reply->readAll();
        m_liveRawStreams = liveRowsFromGql(body);
        m_liveRawCategories = liveCategoriesFromGql(body);
        // gql meldet Fehler mit HTTP 200 im Feld "errors" – nicht als „keine Kanäle“ verschlucken
        const QJsonArray errors = QJsonDocument::fromJson(body).object().value(QStringLiteral("errors")).toArray();
        if (m_liveRawStreams.isEmpty() && m_liveRawCategories.isEmpty() && !errors.isEmpty()) {
            const QString msg = errors.first().toObject().value(QStringLiteral("message")).toString();
            if (m_host) m_host->log(m_id, QStringLiteral("warning"), QStringLiteral("Live-Kanäle gql: %1").arg(msg));
            applyLiveResult(tab, {}, msg.isEmpty() ? QStringLiteral("gql") : msg, {}, m_liveCategory);
            return;
        }
        publishLive(tab);
    });
}

// ------------------------------------------------------------------------------------------------ Einstellungen

QVariantMap TwitchChatController::liveWindowSize() const
{
    if (!m_host) return {};
    const QJsonObject o = m_host->loadPluginSettings(m_id).value(QStringLiteral("liveWindow")).toObject();
    const int w = o.value(QStringLiteral("width")).toInt(), h = o.value(QStringLiteral("height")).toInt();
    if (w < 300 || h < 240) return {};
    return {{QStringLiteral("width"), w}, {QStringLiteral("height"), h}};
}

void TwitchChatController::setLiveWindowSize(int width, int height)
{
    if (!m_host || width < 300 || height < 240) return;
    QJsonObject s = m_host->loadPluginSettings(m_id);   // Lesen–Ändern–Schreiben: alles andere bleibt
    s.insert(QStringLiteral("liveWindow"), QJsonObject{{QStringLiteral("width"), width}, {QStringLiteral("height"), height}});
    m_host->savePluginSettings(m_id, s);
}

QString TwitchChatController::liveLanguage() const
{
    return m_host ? m_host->loadPluginSettings(m_id).value(QStringLiteral("liveLanguage")).toString() : QString();
}

void TwitchChatController::setLiveLanguage(const QString &code)
{
    if (!m_host) return;
    QJsonObject s = m_host->loadPluginSettings(m_id);   // Lesen–Ändern–Schreiben
    if (code.isEmpty()) s.remove(QStringLiteral("liveLanguage"));
    else s.insert(QStringLiteral("liveLanguage"), code.toLower());
    m_host->savePluginSettings(m_id, s);
}

QString TwitchChatController::liveSort() const
{
    const QString s = m_host ? m_host->loadPluginSettings(m_id).value(QStringLiteral("liveSort")).toString() : QString();
    return s == QLatin1String("viewers") ? s : QStringLiteral("relevance");
}

void TwitchChatController::setLiveSort(const QString &sort)
{
    if (!m_host) return;
    QJsonObject s = m_host->loadPluginSettings(m_id);   // Lesen–Ändern–Schreiben
    s.insert(QStringLiteral("liveSort"), sort == QLatin1String("viewers") ? sort : QStringLiteral("relevance"));
    m_host->savePluginSettings(m_id, s);
}

// ------------------------------------------------------------------------------------------------ Abspielen

bool TwitchChatController::playLiveChannel(const QString &raw, const QString &game)
{
    const QString login = raw.trimmed().toLower();
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9_]{2,50}$"));
    if (!valid.match(login).hasMatch() || !m_host) return false;
    // Erst der Chat: so steht der neue Kanal schon, wenn der Player die Adresse meldet (Auto-Kanal ändert dann nichts mehr)
    const bool wasManualClose = m_manualClose;
    setChannel(login);
    if (wasManualClose && !m_ws.isOpen()) openChat();
    QString err;
    const QVariant ok = m_host->call(m_id, QStringLiteral("player.load"),
                                     {{QStringLiteral("url"), QStringLiteral("https://www.twitch.tv/%1").arg(login)}}, &err);
    if (!ok.toBool()) {
        m_host->log(m_id, QStringLiteral("warning"), QStringLiteral("player.load %1: %2").arg(login, err));
        m_host->showOsd(QStringLiteral("Twitch: %1 nicht abspielbar").arg(login), 2500);
        return false;
    }
    m_host->showOsd(QStringLiteral("Twitch: #%1").arg(login), 2000);

    // Für „Für dich empfohlen“ merken (nur Name und Anzahl, lokal in den Plugin-Einstellungen)
    QJsonObject s = m_host->loadPluginSettings(m_id);
    QJsonObject h = s.value(QStringLiteral("liveHistory")).toObject();
    QJsonObject channels = h.value(QStringLiteral("channels")).toObject(), games = h.value(QStringLiteral("games")).toObject();
    bumpHistory(channels, login);
    bumpHistory(games, game.trimmed().toLower());
    h.insert(QStringLiteral("channels"), channels);
    h.insert(QStringLiteral("games"), games);
    s.insert(QStringLiteral("liveHistory"), h);
    m_host->savePluginSettings(m_id, s);
    return true;
}
