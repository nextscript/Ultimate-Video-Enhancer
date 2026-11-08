#include "ResumableDownload.h"
#include "../core/Logger.h"

#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>

namespace uve {

static const int kMaxDelaySeconds = 120;

ResumableDownload::ResumableDownload(QNetworkAccessManager *net, const QUrl &url, const QString &file, QObject *parent)
    : QObject(parent), m_net(net), m_url(url), m_file(file)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &ResumableDownload::startAttempt);
}

void ResumableDownload::start()
{
    if (!m_resume) QFile::remove(m_file.fileName());
    m_attempt = 0;
    startAttempt();
}

void ResumableDownload::abort()
{
    m_aborted = true;
    m_timer.stop();
    if (m_reply) m_reply->abort();
    else done(false, tr("Download abgebrochen"));
}

void ResumableDownload::startAttempt()
{
    ++m_attempt;
    m_headersSeen = false;
    m_base = 0;
    m_atAttemptStart = QFileInfo(m_file.fileName()).size();

    QNetworkRequest req{m_url};
    req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("UVE-Player"));
    req.setRawHeader("Accept", "*/*");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);   // ohne übertragene Daten
    if (m_atAttemptStart > 0 && m_url.scheme().startsWith(QLatin1String("http")))
        req.setRawHeader("Range", "bytes=" + QByteArray::number(m_atAttemptStart) + "-");

    m_reply = m_net->get(req);
    auto *r = m_reply.data();
    connect(r, &QNetworkReply::readyRead, this, [this, r] {
        if (!m_headersSeen) onHeaders();
        const QByteArray data = r->readAll();
        if (m_file.isOpen()) m_file.write(data);
    });
    connect(r, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (m_file.isOpen()) emit progress(m_base + received, total > 0 ? m_base + total : -1);
    });
    connect(r, &QNetworkReply::finished, this, &ResumableDownload::onFinished);
}

// Erst die Antwort des letzten Sprungs entscheidet, ob angehängt, neu begonnen oder verworfen wird
void ResumableDownload::onHeaders()
{
    m_headersSeen = true;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 206) {
        // Antwortet der Server ab einer anderen Stelle als angefragt, wäre die Datei unbrauchbar → neu beginnen
        static const QRegularExpression re(QStringLiteral("^bytes (\\d+)-"));
        const auto m = re.match(QString::fromLatin1(m_reply->rawHeader("Content-Range")));
        if (m.hasMatch() && m.captured(1).toLongLong() == m_atAttemptStart) {
            m_base = m_atAttemptStart;
            if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append)) m_reply->abort();
            return;
        }
        QFile::remove(m_file.fileName());
        m_atAttemptStart = 0;
        m_reply->abort();
        return;
    }
    if (status == 200 || status == 0) {   // 0: file://
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) m_reply->abort();
    }
    // sonst Fehlerseite: Inhalt nicht in die Datei schreiben
}

void ResumableDownload::onFinished()
{
    auto *r = m_reply.data();
    if (!r) return;
    r->deleteLater();
    if (!m_headersSeen && r->error() == QNetworkReply::NoError) onHeaders();
    if (m_file.isOpen()) { m_file.write(r->readAll()); m_file.close(); }
    m_reply.clear();

    if (m_aborted) return done(false, tr("Download abgebrochen"));

    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = r->error();
    if (err == QNetworkReply::NoError && (status == 200 || status == 206 || status == 0)) return done(true, {});

    // Verwertete Daten: ein Fortschritt zählt nicht als Fehlversuch
    if (QFileInfo(m_file.fileName()).size() > m_atAttemptStart) m_attempt = 0;

    if (status == 416) {   // angefragter Bereich passt nicht mehr → von vorne
        QFile::remove(m_file.fileName());
        return retryOrFail(tr("HTTP 416"), 0);
    }
    if (status == 401 || status == 404 || status == 410 || err == QNetworkReply::ContentNotFoundError
        || err == QNetworkReply::ProtocolUnknownError)
        return done(false, status ? tr("HTTP-Fehler %1").arg(status) : r->errorString());

    int retryAfter = -1;
    bool isNumber = false;
    const int seconds = r->rawHeader("Retry-After").toInt(&isNumber);
    if (isNumber && seconds >= 0) retryAfter = seconds;
    retryOrFail(status ? tr("HTTP-Fehler %1").arg(status) : r->errorString(), retryAfter);
}

void ResumableDownload::retryOrFail(const QString &reason, int retryAfterSeconds)
{
    if (m_attempt >= m_maxAttempts) return done(false, reason);
    int delayMs = m_baseDelayMs << qMin(m_attempt, 6);
    if (retryAfterSeconds >= 0) delayMs = retryAfterSeconds * 1000;
    delayMs = qMin(delayMs, kMaxDelaySeconds * 1000);
    Logger::write("update", QStringLiteral("%1: %2 – neuer Versuch in %3 ms").arg(m_url.toString(), reason).arg(delayMs));
    emit retrying(m_attempt, m_maxAttempts, (delayMs + 999) / 1000);
    m_timer.start(delayMs);
}

void ResumableDownload::done(bool ok, const QString &error)
{
    if (m_finished) return;
    m_finished = true;
    emit finished(ok, error);
}

} // namespace uve
