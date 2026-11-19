// Unit-Tests für den Kanal-Browser: reine API-Parser (Helix/OAuth), Thumbnail-
// Erweiterung, DPAPI-Vault (inkl. "kein Klartext auf der Platte"), und
// Fake-Host-Integration (playChannel → player.load, Einstellungen überschreiben
// sich gegenseitig nicht, Favoriten-Persistenz).
// Kein echtes Twitch-Konto nötig; es wird kein Netzwerkanfrage ausgelöst.

#include "fakehost.h"

#include "../TwitchAuthService.h"
#include "../TwitchBrowserController.h"
#include "../TwitchChatController.h"
#include "../TwitchHelix.h"
#include "../ThumbnailCache.h"
#include "../TokenVault.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <cstdio>

static int g_pass = 0, g_fail = 0;

static void check(bool ok, const char *what)
{
    if (ok) { ++g_pass; }
    else { ++g_fail; std::printf("  FEHLER: %s\n", what); }
}

// ---------------------------------------------------------------- Helix-Parser
static void testHelixParsers()
{
    std::printf("— Helix-Parser —\n");

    // Thumbnail-Platzhalter
    check(TwitchHelix::expandThumbnail("https://g.tv/a/{width}x{height}.jpg", 320, 180)
              == "https://g.tv/a/320x180.jpg", "expandThumbnail {width}x{height}");
    check(TwitchHelix::expandThumbnail("https://g.tv/b/{width}/c/{height}", 640, 480)
              == "https://g.tv/b/640/c/480", "expandThumbnail getrennte Platzhalter");
    check(TwitchHelix::expandThumbnail("https://g.tv/plain.jpg", 320, 180)
              == "https://g.tv/plain.jpg", "expandThumbnail ohne Platzhalter");

    // Cursor
    QJsonObject body = QJsonDocument::fromJson(
        R"({"data":[],"pagination":{"cursor":"abc123"}})").object();
    check(TwitchHelix::nextCursor(body) == "abc123", "nextCursor");
    check(TwitchHelix::nextCursor(QJsonObject()) == QString(), "nextCursor leer");

    // /streams/followed
    QJsonArray streams = QJsonDocument::fromJson(R"([
      {"user_id":"111","user_name":"Xnowski","id":"s1","title":"Codierte Titel","game_name":"Just Chatting",
       "viewer_count":2000,"started_at":"2026-11-06T10:00:00Z",
       "thumbnail_url":"https://s.twitch.tv/{width}x{height}"}
    ])").array();
    QList<QVariantMap> rows = TwitchHelix::followedStreamsToRows(streams);
    check(rows.size() == 1, "streams: eine Zeile");
    check(rows[0]["login"].toString() == "xnowski", "streams: login lowercased");
    check(rows[0]["name"].toString() == "Xnowski", "streams: Name in Originalschreibweise");
    check(rows[0]["viewers"].toInt() == 2000, "streams: Zuschauerzahl");
    check(rows[0]["live"].toBool() == true, "streams: live=true");
    check(rows[0]["userId"].toString() == "111", "streams: userId");
    check(rows[0]["thumbRaw"].toString().contains("{width}x{height}"), "streams: thumbRaw roh");

    // /channels/followed
    QJsonArray channels = QJsonDocument::fromJson(R"([
      {"broadcaster_id":"222","broadcaster_login":"SomeChannel","broadcaster_name":"SomeChannel",
       "followed_at":"2026-01-01T00:00:00Z"}
    ])").array();
    QList<QVariantMap> rows2 = TwitchHelix::followedChannelsToRows(channels);
    check(rows2.size() == 1 && rows2[0]["login"].toString() == "somechannel", "channels: login lowercased");
    check(rows2[0]["live"].toBool() == false, "channels: live=false");

    // /search/channels – ID-Feld heißt "id" (broadcaster_id ist scope-geschützt)
    QJsonArray search = QJsonDocument::fromJson(R"([
      {"id":"333","broadcaster_login":"SearchedChan","display_name":"SearchedChan",
       "is_live":true,"title":"Live-Titel","game_name":"Elden Ring",
       "thumbnail_url":"https://s.twitch.tv/avatar333.jpg"}
    ])").array();
    QList<QVariantMap> rows3 = TwitchHelix::searchChannelsToRows(search);
    check(rows3[0]["userId"].toString() == "333", "search: userId aus 'id'");
    check(rows3[0]["live"].toBool() == true, "search: is_live");
    check(rows3[0]["avatar"].toString().contains("avatar333"), "search: Profilbild übernommen");
    check(rows3[0]["viewers"].toInt() == 0, "search: keine Zuschauerzahl (0)");

    // /users?id=
    QJsonArray users = QJsonDocument::fromJson(R"([
      {"id":"111","login":"Xnowski","display_name":"Xnowski",
       "description":"Bio","profile_image_url":"https://s.twitch.tv/av111.jpg"}
    ])").array();
    QList<QVariantMap> rows4 = TwitchHelix::usersToRows(users);
    check(rows4[0]["login"].toString() == "xnowski", "users: login lowercased");
    check(rows4[0]["avatar"].toString().contains("av111"), "users: Profilbild");
}

// ------------------------------------------------------------- OAuth-Helfer
static void testOAuthParsers()
{
    std::printf("— OAuth-Parser —\n");

    TwitchAuthService::DeviceInfo di;
    QString err;
    check(TwitchAuthService::parseDeviceResponse(
              R"({"device_code":"dc","user_code":"ABCD-1234","verification_uri":"https://twitch.tv/activate",
                  "verification_uri_complete":"https://twitch.tv/activate?dc","expires_in":1800,"interval":5})",
              &di, &err),
          "parseDeviceResponse gültig");
    check(di.userCode == "ABCD-1234", "parseDeviceResponse user_code");
    check(di.verificationUriComplete == "https://twitch.tv/activate?dc", "parseDeviceResponse complete-URI");
    check(di.interval == 5, "parseDeviceResponse interval");
    err.clear();
    check(!TwitchAuthService::parseDeviceResponse(R"({"user_code":"x"})", &di, &err),
          "parseDeviceResponse unvollständig → false");

    TwitchAuthService::TokenInfo ti;
    check(TwitchAuthService::parseTokenResponse(
              R"({"access_token":"at","refresh_token":"rt","expires_in":14400,"scope":["user:read:follows","chat:read"]})",
              &ti, &err),
          "parseTokenResponse gültig");
    check(ti.accessToken == "at" && ti.refreshToken == "rt", "parseTokenResponse Tokens");
    check(ti.scope.contains("chat:read"), "parseTokenResponse scope");
    check(ti.expiresAtMs > QDateTime::currentMSecsSinceEpoch(), "parseTokenResponse Ablaufzeit in der Zukunft");
    check(!TwitchAuthService::parseTokenResponse(R"({"message":"nope"})", &ti, &err),
          "parseTokenResponse ohne access_token → false");

    check(TwitchAuthService::tokenFlowError(R"({"status":400,"message":"authorization_pending"})")
              == "authorization_pending",
          "tokenFlowError authorization_pending");
    check(TwitchAuthService::tokenFlowError(R"({"status":400,"message":"slow_down"})") == "slow_down",
          "tokenFlowError slow_down");
    check(TwitchAuthService::tokenFlowError(R"({"status":400,"message":"expired_token"})") == "expired_token",
          "tokenFlowError expired_token");

    check(TwitchAuthService::scopeAllows({"user:read:follows", "chat:read"}, "chat:read"),
          "scopeAllows chat:read");
    check(!TwitchAuthService::scopeAllows({"user:read:follows"}, "chat:read"),
          "scopeAllows verweigert fehlenden Scope");
}

// ---------------------------------------------------------------- DPAPI-Vault
static void testVault(const QString &dir)
{
    std::printf("— TokenVault (DPAPI) —\n");
    const QString path = dir + "/vault_roundtrip.dat";
    QFile::remove(path);
    const QString secret = "GEHEIM-1234567890";

    {
        TokenVault v(path, "test-entropy");
        QJsonObject d;
        d["chatUser"] = "tester";
        d["chatToken"] = secret;
        QJsonObject session;
        session["accessToken"] = "sess-at";
        session["userId"] = "1";
        d["session"] = session;
        check(v.save(d), "Vault speichern");
        check(v.usingDisk(), "Vault: DPAPI/Disk verfügbar");

        // Vault neu öffnen (entspricht neuem Plugin-Start)
        TokenVault v2(path, "test-entropy");
        QJsonObject d2 = v2.data();
        check(d2.value("chatToken").toString() == secret, "Vault: Token wieder lesbar");
        check(d2.value("session").toObject().value("accessToken").toString() == "sess-at",
              "Vault: Session wieder lesbar");

        // Platte darf kein Klartext enthalten
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray raw = f.readAll();
            check(!raw.contains(secret.toUtf8()), "Vault: kein Klartext-Token auf der Platte");
            check(!raw.contains("tester"), "Vault: kein Klartext-User auf der Platte");
            check(raw.size() > 0, "Vault: Datei nicht leer");
        }

        // Falsche Entropie darf nicht entschlüsseln (schützt vor Fremd-Replik)
        TokenVault v3(path, "andere-entropy");
        check(v3.data().isEmpty(), "Vault: fremde Entropie liefert nichts");

        // Legacy-Migration aus Klartext-Einstellungen
        TokenVault v4(path, "test-entropy");
        v4.migrateLegacyToken("altuser", "alttoken");     // chatToken schon gesetzt → unverändert
        check(v4.data().value("chatToken").toString() == secret, "Vault: Legacy-Migration überschreibt nichts");
        TokenVault v5(dir + "/vault_empty.dat", "test-entropy");
        v5.migrateLegacyToken("altuser", "alttoken");     // leer → übernimmt
        check(v5.data().value("chatToken").toString() == "alttoken", "Vault: Legacy-Migration übernimmt");
        v5.clear();
        check(!QFile::exists(v5.path()), "Vault: clear() entfernt Datei");
    }
    QFile::remove(path);
}

// ------------------------------------------------------- Fake-Host-Integration
static void testBrowserIntegration(const QString &dir)
{
    std::printf("— Browser am Fake-Host —\n");
    const QString id = "cc.ts3x.twitch-chat";

    FakeHost host;
    QJsonObject initial;
    initial["channel"] = "xnowski";
    initial["clientId"] = "";
    initial["user"] = "altuser";
    initial["token"] = "altklartexttoken";
    host.preloadSettings(id, initial);

    TokenVault vault(dir + "/vault_browser.dat", id.toUtf8());
    vault.clear();

    TwitchChatController chat(&host, id);
    chat.applyVault(&vault);
    check(chat.authenticated(), "Chat: altes Klartext-Token in Vault migriert");

    TwitchBrowserController browser(&host, id, &vault, &chat);
    browser.init();
    chat.setBrowserController(&browser);

    // Login ohne Client-ID muss abgewiesen werden (mit Hinweis, ohne Netzwerk)
    browser.login();
    check(browser.authState() == "idle", "Login ohne Client-ID: kein Login-Versuch");
    check(browser.statusText().contains("Client-ID"), "Login ohne Client-ID: deutlicher Hinweis");

    check(!browser.manualSignIn(QString()), "manuelles Leer-Token abgewiesen");

    // ▶ spielt im Player: genau ein player.load mit twitch.tv-URL
    check(browser.hasPlayerLoad(), "player.load verfügbar (Fake-Host)");
    host.clearCalls();
    check(browser.playChannel("Xnowski"), "playChannel erfolgreich");
    check(host.calls().size() == 1 && host.calls()[0].first == "player.load", "player.load aufgerufen");
    check(host.calls()[0].second.value("url").toString() == "https://www.twitch.tv/xnowski",
          "player.load URL (lowercase-Kanonisierung)");
    check(browser.activeChannel() == "xnowski", "aktiver Kanal gemerkt");

    // Player ohne player.load → klare Meldung, kein Absturz
    host.disableCommand("player.load");
    check(!browser.playChannel("anderer"), "playChannel ohne player.load schlägt sauber fehl");
    check(browser.statusText().contains("player.load"), "Fehlender-Befehl-Hinweis");
    host = FakeHost();   // Befehle wieder da – Einstellungen bleiben? nein: neu setzen
    host.preloadSettings(id, QJsonObject());

    // Einstellungen: Chat und Browser schreiben denselben JSON, überschreiben sich nicht
    QJsonObject s;
    s["clientId"] = "meine-client-id";
    s["channel"] = "somechan";
    s["user"] = "altuser";
    s["token"] = "altklartexttoken";
    host.preloadSettings(id, s);
    TwitchBrowserController b2(&host, id, &vault, nullptr);
    b2.init();
    check(b2.clientId() == "meine-client-id", "Browser liest Client-ID aus Einstellungen");
    b2.setRefreshSec(120);                       // persistiert Browser-Sicht
    QJsonObject afterBrowser = host.storedSettings(id);
    check(afterBrowser.value("channel").toString() == "somechan", "Browser-Persistenz erhält Chat-Schlüssel");
    check(afterBrowser.value("clientId").toString() == "meine-client-id", "Browser: Client-ID gespeichert");

    TwitchChatController chat2(&host, id);
    chat2.setAutoChannel(false);                 // persistiert Chat-Sicht
    QJsonObject afterChat = host.storedSettings(id);
    check(afterChat.value("clientId").toString() == "meine-client-id", "Chat-Persistenz erhält Browser-Schlüssel");
    check(afterChat.contains("autoChannel") && !afterChat.value("autoChannel").toBool(),
          "Chat-Einstellung gespeichert");
    check(!afterChat.contains("user") && !afterChat.contains("token"),
          "Klartext-Login aus Einstellungen entfernt");

    // Favoriten (lokal, lowercase, persistiert)
    b2.toggleFavorite("Xnowski");
    check(b2.isFavorite("xnowski"), "Favorit lowercase gesetzt");
    check(host.storedSettings(id).value("favorites").toArray().contains("xnowski"),
          "Favorit persistiert");
    b2.toggleFavorite("xnowski");
    check(!b2.isFavorite("xnowski"), "Favorit wieder entfernt");

    // Tab-Ansichten ohne Konto sind leer, aber wohlgeformt
    check(b2.channels().isEmpty(), "ohne Login: keine Kanäle (keine erfundenen Daten)");
    b2.setTab("search");
    check(b2.tab() == "search", "Tab-Wechsel");
    b2.setSplitRatio(0.9);   // wird auf 0.15..0.85 begrenzt
    check(b2.splitRatio() <= 0.85 && b2.splitRatio() >= 0.15, "splitRatio wird begrenzt");

    // Thumbnail-Schlüssel stabil (Cache-Dateiname)
    check(ThumbnailCache::keyFor("https://a/1.jpg") == ThumbnailCache::keyFor("https://a/1.jpg"),
          "ThumbnailCache::keyFor stabil");
    check(ThumbnailCache::keyFor("https://a/1.jpg") != ThumbnailCache::keyFor("https://a/2.jpg"),
          "ThumbnailCache::keyFor unterscheidet");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString dir = QDir::tempPath() + "/twitchchat_browsertest";
    QDir().mkpath(dir);

    testHelixParsers();
    testOAuthParsers();
    testVault(dir);
    testBrowserIntegration(dir);

    std::printf("\nErgebnis: %d bestanden, %d fehlgeschlagen.\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
