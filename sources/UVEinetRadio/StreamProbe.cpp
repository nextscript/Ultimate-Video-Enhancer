#include "StreamProbe.h"
#include "RadioTypes.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>

namespace uve::radio {

QString ProbeResult::code() const
{
    switch (kind) {
    case Ok: case Unknown: return {};
    case WebPage: return QStringLiteral("webpage");
    case BadRedirect: return QStringLiteral("redirect");
    case Insecure: return QStringLiteral("insecure");
    case Certificate: return QStringLiteral("certificate");
    case Unreachable: return QStringLiteral("unreachable");
    }
    return {};
}

StreamProbe::StreamProbe(QNetworkAccessManager *nam, QObject *parent) : QObject(parent), m_nam(nam)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        // Kein Ergebnis in der Zeit: der Player soll es selbst versuchen (er hat eigene Zeitlimits)
        finish(ProbeResult{ProbeResult::Unknown, QStringLiteral("timeout"), {}});
    });
}

StreamProbe::~StreamProbe() { cancel(); }

void StreamProbe::cancel()
{
    m_timer.stop();
    m_done = nullptr;
    if (m_reply) {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
}

void StreamProbe::probe(const QString &url, std::function<void(const ProbeResult &)> done)
{
    cancel();
    m_done = std::move(done);
    m_head.clear();
    m_redirects = 0;
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setRawHeader("Icy-MetaData", "0");
    req.setRawHeader("Accept", "*/*");
    // Weiterleitungen selbst prüfen
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    req.setMaximumRedirectsAllowed(8);
    QNetworkReply *reply = m_nam->get(req);
    m_reply = reply;
    m_timer.start(m_timeoutMs);

    connect(reply, &QNetworkReply::redirected, this, [this, reply](const QUrl &target) {
        const QString from = reply->url().scheme().toLower();
        const QString to = target.scheme().toLower();
        if (to != QLatin1String("http") && to != QLatin1String("https")) {
            finish(ProbeResult{ProbeResult::BadRedirect, target.toString(), {}});
            return;
        }
        if (from == QLatin1String("https") && to == QLatin1String("http")) {
            finish(ProbeResult{ProbeResult::BadRedirect, target.toString(), {}});
            return;
        }
        ++m_redirects;
        emit reply->redirectAllowed();
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [this] { inspect(); });
    connect(reply, &QNetworkReply::readyRead, this, [this] { inspect(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != m_reply) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Weiterleitung, der Qt nicht gefolgt ist (z. B. zu ftp:// oder ohne gültiges Ziel): abgelehnt
        if (status >= 300 && status < 400) {
            finish(ProbeResult{ProbeResult::BadRedirect, reply->header(QNetworkRequest::LocationHeader).toUrl().toString(), {}});
            return;
        }
        if (reply->error() == QNetworkReply::NoError) { inspect(); if (m_reply) finish(ProbeResult{ProbeResult::Ok, {}, {}}); return; }
        ProbeResult r;
        r.detail = reply->errorString();
        switch (reply->error()) {
        case QNetworkReply::SslHandshakeFailedError:
            r.kind = ProbeResult::Certificate; break;
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::ContentNotFoundError:
        case QNetworkReply::ContentGoneError:
        case QNetworkReply::ContentAccessDenied:
            r.kind = ProbeResult::Unreachable; break;
        case QNetworkReply::InsecureRedirectError:
        case QNetworkReply::TooManyRedirectsError:
        case QNetworkReply::ProtocolUnknownError:
            r.kind = ProbeResult::BadRedirect; break;
        default:
            // Protokollfehler (ICY-Server), Zeitlimit usw.: der Player entscheidet
            r.kind = (status == 404 || status == 410 || status == 403) ? ProbeResult::Unreachable : ProbeResult::Unknown;
        }
        if (status >= 400 && r.kind == ProbeResult::Unreachable) r.detail = QStringLiteral("HTTP %1").arg(status);
        finish(r);
    });
}

void StreamProbe::inspect()
{
    QNetworkReply *reply = m_reply;
    if (!reply) return;
    const QVariant statusV = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!statusV.isValid()) return;
    const int status = statusV.toInt();
    if (status >= 300 && status < 400) return;   // Weiterleitung läuft
    const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
    if (status >= 400) {
        if (status == 404 || status == 410 || status == 403)
            finish(ProbeResult{ProbeResult::Unreachable, QStringLiteral("HTTP %1").arg(status), type});
        return;   // andere Fehler beendet finished()
    }
    m_head += reply->read(qMax<qint64>(0, 1024 - m_head.size()));
    const bool htmlType = type.startsWith(QLatin1String("text/html")) || type.startsWith(QLatin1String("application/xhtml"));
    const QByteArray start = m_head.left(256).trimmed().toLower();
    const bool htmlBody = start.startsWith("<!doctype html") || start.startsWith("<html") || start.contains("<head");
    if (htmlType || htmlBody) {
        // Einige Server senden text/html für echte Streams – dann zeigt der Inhalt Audio (MP3-Sync, ID3, ADTS, Ogg)
        if (m_head.size() < 64 && !reply->isFinished()) return;   // erst etwas Inhalt abwarten
        const bool audio = m_head.startsWith("ID3") || m_head.startsWith("OggS") || m_head.startsWith("fLaC")
                        || (m_head.size() > 1 && uchar(m_head[0]) == 0xFF && (uchar(m_head[1]) & 0xE0) == 0xE0)
                        || m_head.startsWith("#EXTM3U") || m_head.toLower().startsWith("[playlist]");
        if (!audio && (htmlBody || !m_head.isEmpty())) {
            finish(ProbeResult{ProbeResult::WebPage, type, type});
            return;
        }
    }
    if (!m_head.isEmpty() || !type.isEmpty())
        finish(ProbeResult{ProbeResult::Ok, type, type});
}

void StreamProbe::finish(ProbeResult r)
{
    auto done = std::move(m_done);
    m_done = nullptr;
    m_timer.stop();
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (done) done(r);
}

} // namespace uve::radio
