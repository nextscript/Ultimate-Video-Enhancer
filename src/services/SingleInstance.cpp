#include "SingleInstance.h"

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>

namespace uve {

SingleInstance::SingleInstance(const QString &key, QObject *parent) : QObject(parent), m_key(key) {}

bool SingleInstance::claim(const QStringList &argsToForward)
{
    QLocalSocket probe;
    probe.connectToServer(m_key);
    if (probe.waitForConnected(400)) {
        QByteArray data;
        QDataStream ds(&data, QIODevice::WriteOnly);
        ds << argsToForward;
        probe.write(data);
        probe.waitForBytesWritten(1000);
        probe.disconnectFromServer();
        return false;
    }
    QLocalServer::removeServer(m_key); // verwaister Socket nach Absturz
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_key)) return true; // Weiterleitung nicht möglich – trotzdem starten
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (auto *s = m_server->nextPendingConnection()) {
            connect(s, &QLocalSocket::readyRead, this, [this, s] {
                QDataStream ds(s->readAll());
                QStringList args;
                ds >> args;
                emit messageReceived(args);
                s->deleteLater();
            });
        }
    });
    return true;
}

} // namespace uve
