#include "ProxyConfig.h"

#include <QCoreApplication>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")
#endif

namespace uve {

QString ProxyConfig::validate() const
{
    if (mode != QLatin1String("none") && mode != QLatin1String("system") && mode != QLatin1String("manual"))
        return QCoreApplication::translate("ProxyConfig", "Unbekannter Proxy-Modus");
    if (mode != QLatin1String("manual")) return {};
    if (type != QLatin1String("http") && type != QLatin1String("socks5")) return QCoreApplication::translate("ProxyConfig", "Proxy-Typ muss HTTP oder SOCKS5 sein");
    static const QRegularExpression hostRe(QStringLiteral("^(\\[[0-9A-Fa-f:.]+\\]|[A-Za-z0-9.\\-]+)$"));
    if (host.trimmed().isEmpty()) return QCoreApplication::translate("ProxyConfig", "Proxy-Adresse fehlt");
    if (!hostRe.match(host.trimmed()).hasMatch()) return QCoreApplication::translate("ProxyConfig", "Proxy-Adresse ungültig (nur Hostname oder IP, ohne http://)");
    if (port < 1 || port > 65535) return QCoreApplication::translate("ProxyConfig", "Port muss zwischen 1 und 65535 liegen");
    if (!user.isEmpty() && user.contains(':')) return QCoreApplication::translate("ProxyConfig", "Benutzername darf keinen Doppelpunkt enthalten");
    return {};
}

QString ProxyConfig::url(bool withCredentials) const
{
    if (mode != QLatin1String("manual") || !validate().isEmpty()) return {};
    // Zugangsdaten vollständig prozentkodieren (auch ':' und '@'), damit FFmpeg/yt-dlp korrekt trennen
    QString auth;
    if (withCredentials && !user.isEmpty()) {
        auth = QString::fromLatin1(QUrl::toPercentEncoding(user));
        if (!password.isEmpty()) auth += ':' + QString::fromLatin1(QUrl::toPercentEncoding(password));
        auth += '@';
    }
    QString h = host.trimmed();
    if (h.contains(':') && !h.startsWith('[')) h = '[' + h + ']'; // IPv6
    // Pfad leer lassen: FFmpeg erwartet "http://host:port"
    return (isSocks() ? QStringLiteral("socks5://") : QStringLiteral("http://")) + auth + h + ':' + QString::number(port);
}

QNetworkProxy ProxyConfig::toQNetworkProxy() const
{
    if (mode == QLatin1String("manual") && validate().isEmpty())
        return QNetworkProxy(isSocks() ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy, host.trimmed(), quint16(port), user, password);
    return QNetworkProxy(QNetworkProxy::NoProxy);
}

ProxyConfig ProxyConfig::fromSystem(const QUrl &probe)
{
    ProxyConfig c;
    const auto list = QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(probe));
    for (const auto &p : list) {
        if (p.type() != QNetworkProxy::HttpProxy && p.type() != QNetworkProxy::Socks5Proxy) continue;
        if (p.hostName().isEmpty()) continue;
        c.mode = QStringLiteral("manual");
        c.type = p.type() == QNetworkProxy::Socks5Proxy ? QStringLiteral("socks5") : QStringLiteral("http");
        c.host = p.hostName();
        c.port = p.port();
        c.user = p.user();
        c.password = p.password();
        return c;
    }
    c.mode = QStringLiteral("none"); // System: direkte Verbindung
    return c;
}

QJsonObject ProxyConfig::toJson() const
{
    return {{"mode", mode}, {"type", type}, {"host", host.trimmed()}, {"port", port}, {"user", user},
            {"password", password.isEmpty() ? QString() : protectSecret(password)}, {"bypass", bypass}};
}

ProxyConfig ProxyConfig::fromJson(const QJsonObject &o)
{
    ProxyConfig c;
    c.mode = o.value("mode").toString(c.mode);
    c.type = o.value("type").toString(c.type);
    c.host = o.value("host").toString();
    c.port = o.value("port").toInt(c.port);
    c.user = o.value("user").toString();
    c.password = unprotectSecret(o.value("password").toString());
    c.bypass = o.value("bypass").toString(c.bypass);
    if (!c.validate().isEmpty() && c.mode == QLatin1String("manual")) c.mode = QStringLiteral("none"); // ungültig → aus
    return c;
}

QString protectSecret(const QString &plain)
{
    if (plain.isEmpty()) return {};
#ifdef Q_OS_WIN
    const QByteArray in = plain.toUtf8();
    DATA_BLOB src{DWORD(in.size()), reinterpret_cast<BYTE *>(const_cast<char *>(in.constData()))};
    DATA_BLOB dst{};
    if (CryptProtectData(&src, L"UVE Player Proxy", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &dst)) {
        const QByteArray out(reinterpret_cast<const char *>(dst.pbData), int(dst.cbData));
        LocalFree(dst.pbData);
        return QStringLiteral("dpapi:") + QString::fromLatin1(out.toBase64());
    }
#endif
    return QStringLiteral("b64:") + QString::fromLatin1(plain.toUtf8().toBase64());
}

QString unprotectSecret(const QString &stored)
{
    if (stored.startsWith(QLatin1String("b64:"))) return QString::fromUtf8(QByteArray::fromBase64(stored.mid(4).toLatin1()));
#ifdef Q_OS_WIN
    if (stored.startsWith(QLatin1String("dpapi:"))) {
        QByteArray in = QByteArray::fromBase64(stored.mid(6).toLatin1());
        DATA_BLOB src{DWORD(in.size()), reinterpret_cast<BYTE *>(in.data())};
        DATA_BLOB dst{};
        if (CryptUnprotectData(&src, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &dst)) {
            const QString out = QString::fromUtf8(reinterpret_cast<const char *>(dst.pbData), int(dst.cbData));
            SecureZeroMemory(dst.pbData, dst.cbData);
            LocalFree(dst.pbData);
            return out;
        }
    }
#endif
    return {}; // nicht entschlüsselbar (anderes Benutzerkonto/anderer PC) → Passwort neu eingeben
}

} // namespace uve
