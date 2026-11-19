#include "LanguageToolClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

namespace {
constexpr int kPauseMs = 1200;
constexpr int kMaxPerMinute = 18;           // knapp unter dem Limit von 20
constexpr qint64 kBlockAfter429Ms = 5 * 60 * 1000;
constexpr int kTimeoutMs = 8000;
}

LanguageToolClient::LanguageToolClient(QObject *parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this))
{
    m_clock.start();
    m_pause.setSingleShot(true);
    connect(&m_pause, &QTimer::timeout, this, &LanguageToolClient::fire);
}

QByteArray LanguageToolClient::buildAnnotation(const QString &text, const QList<Range> &hidden)
{
    QJsonArray parts;
    int position = 0;
    for (const Range &r : hidden) {
        if (r.first > position)
            parts.append(QJsonObject{{QStringLiteral("text"), text.mid(position, r.first - position)}});
        parts.append(QJsonObject{{QStringLiteral("markup"), text.mid(r.first, r.second)},
                                 {QStringLiteral("interpretAs"), QStringLiteral("Name")}});
        position = r.first + r.second;
    }
    if (position < text.size())
        parts.append(QJsonObject{{QStringLiteral("text"), text.mid(position)}});
    return QJsonDocument(QJsonObject{{QStringLiteral("annotation"), parts}}).toJson(QJsonDocument::Compact);
}

QVariantList LanguageToolClient::parseMatches(const QByteArray &json)
{
    QVariantList issues;
    const QJsonArray matches = QJsonDocument::fromJson(json).object().value(QStringLiteral("matches")).toArray();
    for (const QJsonValue &value : matches) {
        const QJsonObject match = value.toObject();
        const QString category = match.value(QStringLiteral("rule")).toObject()
                                     .value(QStringLiteral("category")).toObject()
                                     .value(QStringLiteral("id")).toString();
        if (category == QStringLiteral("TYPOS")) continue;
        QStringList replacements;
        const QJsonArray found = match.value(QStringLiteral("replacements")).toArray();
        for (const QJsonValue &r : found) {
            if (replacements.size() == 3) break;
            replacements << r.toObject().value(QStringLiteral("value")).toString();
        }
        const QString message = match.value(QStringLiteral("message")).toString();
        issues.append(QVariantMap{{QStringLiteral("start"), match.value(QStringLiteral("offset")).toInt()},
                                  {QStringLiteral("length"), match.value(QStringLiteral("length")).toInt()},
                                  {QStringLiteral("message"), message},
                                  {QStringLiteral("messageEn"), message},
                                  {QStringLiteral("replacements"), replacements},
                                  {QStringLiteral("source"), QStringLiteral("languagetool")}});
    }
    return issues;
}

void LanguageToolClient::cancel()
{
    m_pause.stop();
    m_text.clear();
    if (m_reply) m_reply->abort();
}

void LanguageToolClient::schedule(const QString &text, const QList<Range> &hidden, bool german, bool english)
{
    const QString language = german && english ? QStringLiteral("auto")
                           : german            ? QStringLiteral("de-DE")
                                               : QStringLiteral("en-GB");
    if (language == m_language && text == m_text && (m_pause.isActive() || m_reply)) return;
    if (m_lastKey == language + QLatin1Char('|') + text) {
        m_pause.stop();
        if (m_reply) m_reply->abort();
        m_text = text;
        emit result(text, m_lastIssues);
        return;
    }
    if (m_reply) m_reply->abort();
    m_text = text;
    m_hidden = hidden;
    m_language = language;
    m_pause.start(kPauseMs);
}

void LanguageToolClient::fire()
{
    if (m_text.isEmpty()) return;
    const qint64 now = m_clock.elapsed();
    if (now < m_blockedUntil) return;

    m_sentAt.erase(std::remove_if(m_sentAt.begin(), m_sentAt.end(), [now](qint64 t) { return now - t > 60000; }),
                   m_sentAt.end());
    if (m_sentAt.size() >= kMaxPerMinute) {
        m_pause.start(int(60000 - (now - m_sentAt.first()) + 50));
        return;
    }
    m_sentAt.append(now);

    QByteArray body = "language=" + m_language.toUtf8();
    if (m_language == QStringLiteral("auto")) body += "&preferredVariants=de-DE%2Cen-GB";
    body += "&data=" + QUrl::toPercentEncoding(QString::fromUtf8(buildAnnotation(m_text, m_hidden)));

    QNetworkRequest request(QUrl(QStringLiteral("https://api.languagetool.org/v2/check")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("uve-twitchchat-plugin"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(kTimeoutMs);

    const QString text = m_text;
    const QString key = m_language + QLatin1Char('|') + m_text;
    QNetworkReply *reply = m_net->post(request, body);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, text, key] {
        reply->deleteLater();
        if (m_reply == reply) m_reply = nullptr;
        if (reply->error() == QNetworkReply::OperationCanceledError) return;   // weitergetippt oder Zeitlimit
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429) {
            m_blockedUntil = m_clock.elapsed() + kBlockAfter429Ms;
            emit failed(QStringLiteral("Limit erreicht, Pause für 5 Minuten"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            return;
        }
        const QVariantList issues = parseMatches(reply->readAll());
        m_lastKey = key;
        m_lastIssues = issues;
        if (text == m_text) emit result(text, issues);
    });
}
