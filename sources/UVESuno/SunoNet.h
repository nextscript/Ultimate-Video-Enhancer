#pragma once
// UVESuno, Netzwerk: Song, Playlist, Profil oder Kurzlink → abspielbare Adresse(n). Nichts wird gespeichert.
#include "SunoCore.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <functional>

class QNetworkAccessManager;

namespace uve::suno {

struct Resolution
{
    QString url;                 // erster Titel: abspielbare Adresse
    QString title;               // "Interpret – Titel"
    QString collection;          // Name der Playlist/des Profils, sonst leer
    int count = 1;
    QStringList more;            // weitere Titel als Suno-Links (werden beim Abspielen einzeln aufgelöst)
    QVariantList moreTitles;
    QVariantList moreDurations;
};

class ResolveJob : public QObject
{
    Q_OBJECT
public:
    ResolveJob(const QString &url, QObject *parent = nullptr);
    void start();

signals:
    void finished(const uve::suno::Resolution &result);
    void failed(const QString &error, bool definitive);

private:
    void get(const QString &url, const std::function<void(int status, const QByteArray &body, const QUrl &finalUrl)> &cb);
    void fail(const QString &error, bool definitive = false);
    void handle(const Link &link);
    void song(const QString &id);
    void pages(Kind kind, const QString &id, int page, QList<Clip> clips);
    void begin(const QList<Clip> &clips);

    QNetworkAccessManager *m_nam;
    QString m_url;
    bool m_done = false;
    Resolution m_result;
};

} // namespace uve::suno
