// Diagnose: MiniWebSocket gegen Twitch – meldet Zustandswechsel, Rohtext und Fortschritt.
// Aufruf: ws_diag.exe [host] [port]

#include "MiniWebSocket.h"

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString host = argc > 1 ? QString::fromLatin1(argv[1]) : QStringLiteral("irc-ws.chat.twitch.tv");
    const quint16 port = argc > 2 ? quint16(QString::fromLatin1(argv[2]).toUShort()) : 443;

    MiniWebSocket ws;
    QObject::connect(&ws, &MiniWebSocket::opened, [&ws, argc, argv] {
        std::printf(">> opened\n"); std::fflush(stdout);
        if (argc > 3 && qstrcmp(argv[3], "join") == 0) {
            ws.sendText(QStringLiteral("CAP REQ :twitch.tv/tags twitch.tv/commands"));
            ws.sendText(QStringLiteral("NICK justinfan42424"));
            ws.sendText(QStringLiteral("JOIN #xnowski"));
            std::printf(">> CAP/NICK/JOIN gesendet\n"); std::fflush(stdout);
        }
    });
    QObject::connect(&ws, &MiniWebSocket::textMessage, [](const QString &t) {
        std::printf("<< %s\n", t.left(160).toUtf8().constData()); std::fflush(stdout); });
    QObject::connect(&ws, &MiniWebSocket::closed,      [](const QString &r) {
        std::printf(">> closed: %s\n", r.toUtf8().constData()); std::fflush(stdout); });
    QObject::connect(&ws, &MiniWebSocket::failed,      [](const QString &r) {
        std::printf(">> failed: %s\n", r.toUtf8().constData()); std::fflush(stdout); });

    std::printf("start: %s:%u\n", host.toUtf8().constData(), port); std::fflush(stdout);
    ws.open(host, port);
    if (argc > 3 && qstrcmp(argv[3], "double") == 0) {
        std::printf("(zweiter open() sofort hintereinander)\n"); std::fflush(stdout);
        ws.open(host, port);
    }

    for (int i = 1; i <= 8; ++i) {
        QTimer::singleShot(i * 2000, [&ws, i] {
            std::printf("tick %d: ws=%s sock=%d pending=%d\n", i,
                        ws.stateName().toUtf8().constData(), ws.socketState(), ws.pendingBytes());
            std::fflush(stdout);
        });
    }
    QTimer::singleShot(17000, &app, [] { QCoreApplication::exit(0); });
    return app.exec();
}
