#pragma once
#include <QObject>
#include <QStringList>

class QLocalServer;

namespace uve {

// Nur eine Player-Instanz pro Benutzer: weitere Starts übergeben ihre Argumente und beenden sich.
class SingleInstance : public QObject
{
    Q_OBJECT
public:
    explicit SingleInstance(const QString &key, QObject *parent = nullptr);
    // true = diese Instanz ist primär; false = Argumente wurden an die laufende Instanz gesendet
    bool claim(const QStringList &argsToForward);

signals:
    void messageReceived(const QStringList &args);

private:
    QString m_key;
    QLocalServer *m_server = nullptr;
};

} // namespace uve
