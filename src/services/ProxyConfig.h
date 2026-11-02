#pragma once
// Proxy-Einstellungen für Netzwerkstreams und eigene Downloads.
//
// Wirkung je Ebene:
//  - mpv/FFmpeg (Mediendaten: HTTP(S), HLS, DASH):  Option "http-proxy" – FFmpeg unterstützt nur HTTP-Proxys
//    (HTTPS-Ziele per CONNECT-Tunnel). Rohe tcp://, udp://, rtmp://, rtsp://, srt:// laufen nicht über Proxys.
//  - yt-dlp (Auflösung von YouTube/Twitch …):        "ytdl-raw-options" proxy=… (HTTP und SOCKS5)
//  - Qt (Update-Prüfung, LUT-Pakete, Beispielprofil): QNetworkProxy (HTTP und SOCKS5)
#include <QJsonObject>
#include <QNetworkProxy>
#include <QString>
#include <QUrl>

namespace uve {

struct ProxyConfig
{
    QString mode = QStringLiteral("none");   // none | system | manual
    QString type = QStringLiteral("http");   // http | socks5 (nur bei manual)
    QString host;
    int port = 8080;
    QString user;
    QString password;                        // im Speicher im Klartext, auf der Platte verschlüsselt
    QString bypass = QStringLiteral("localhost,127.0.0.1,::1");

    bool active() const { return mode != QLatin1String("none"); }
    bool isSocks() const { return type == QLatin1String("socks5"); }

    // Fehlertext oder leer
    QString validate() const;
    // Proxy-URL wie von FFmpeg/yt-dlp erwartet, z. B. "http://user:pass@host:8080"; leer = kein Proxy
    QString url(bool withCredentials = true) const;
    QNetworkProxy toQNetworkProxy() const;

    // Systemproxy (Windows: Internetoptionen/WinHTTP) für eine Beispiel-URL ermitteln → manual-Konfiguration
    static ProxyConfig fromSystem(const QUrl &probe = QUrl(QStringLiteral("https://www.youtube.com/")));

    QJsonObject toJson() const;                       // Passwort verschlüsselt (Windows: DPAPI)
    static ProxyConfig fromJson(const QJsonObject &o);
};

// Geheimnis für die Ablage schützen (Windows: DPAPI, an das Benutzerkonto gebunden; sonst Base64)
QString protectSecret(const QString &plain);
QString unprotectSecret(const QString &stored);

} // namespace uve
