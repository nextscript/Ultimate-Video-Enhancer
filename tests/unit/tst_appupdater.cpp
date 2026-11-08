// Update des Players: Auswerten der GitHub-Antworten, Prüfablauf und robuster Download gegen einen lokalen Testserver.
// Mit UVE_NETWORK_TESTS=1 läuft zusätzlich ein Durchgang gegen das echte GitHub.
#include "services/AppUpdater.h"
#include "services/ResumableDownload.h"
#include "services/ToolUpdater.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <functional>

using namespace uve;

namespace {

const QString kOld = QString(40, QLatin1Char('a'));
const QString kTip = QString(40, QLatin1Char('b'));
const QString kRel = QString(40, QLatin1Char('c'));
const QString kNext = QString(40, QLatin1Char('d'));

QByteArray pkt(const QString &text)
{
    const QByteArray payload = text.toUtf8();
    return QByteArray::number(qlonglong(payload.size() + 4), 16).rightJustified(4, '0') + payload;
}

QByteArray refsBody(const QList<QPair<QString, QString>> &refs)
{
    QByteArray out = pkt(QStringLiteral("# service=git-upload-pack\n")) + "0000";
    bool first = true;
    for (const auto &r : refs) {
        out += pkt(r.second + QLatin1Char(' ') + r.first + (first ? QStringLiteral("\u0000multi_ack symref=HEAD:refs/heads/main\n") : QStringLiteral("\n")));
        first = false;
    }
    return out + "0000";
}

QByteArray feedEntry(const QString &sha, const QString &title, const QString &date)
{
    return QStringLiteral("<entry><id>tag:github.com,2008:Grit::Commit/%1</id><title>\n   %2\n  </title><updated>%3</updated>"
                          "<author><name></name><email>x@example.test</email></author></entry>").arg(sha, title, date).toUtf8();
}

QByteArray feed(const QByteArray &entries)
{
    return "<?xml version=\"1.0\"?><feed xmlns=\"http://www.w3.org/2005/Atom\"><id>feed</id><title>t</title><updated>2026-01-01T00:00:00Z</updated>" + entries + "</feed>";
}

struct Response {
    int status = 200;
    QByteArray body;
    QByteArray extraHeaders;
    int declaredLength = -1;    // > body.size(): Verbindung bricht mitten in der Übertragung ab
};

QByteArray responseBytes(const Response &r)
{
    QByteArray out = "HTTP/1.1 " + QByteArray::number(r.status) + " X\r\nContent-Length: "
                     + QByteArray::number(qlonglong(r.declaredLength >= 0 ? r.declaredLength : r.body.size())) + "\r\nConnection: close\r\n" + r.extraHeaders + "\r\n";
    return out + r.body;
}

// Minimaler HTTP-Server: Antworten kommen aus einer Funktion je (Methode, Pfad, Range)
class FakeServer : public QObject
{
public:
    using Handler = std::function<Response(const QByteArray &method, const QByteArray &path, const QByteArray &range)>;
    explicit FakeServer(Handler h) : m_handler(std::move(h))
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (auto *s = m_server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    m_buffers[s] += s->readAll();
                    const QByteArray req = m_buffers[s];
                    if (!req.contains("\r\n\r\n")) return;
                    const auto lines = req.split('\n');
                    const auto first = lines.first().trimmed().split(' ');
                    QByteArray range;
                    for (const auto &l : lines)
                        if (l.toLower().startsWith("range:")) range = l.mid(6).trimmed();
                    ranges << range;
                    Response response = m_handler(first.value(0), first.value(1), range);
                    if (first.value(0) == "HEAD") { response.declaredLength = int(response.body.size()); response.body.clear(); }
                    s->write(responseBytes(response));
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/repo").arg(m_server.serverPort()); }
    QList<QByteArray> ranges;

private:
    Handler m_handler;
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

} // namespace

class TstAppUpdater : public QObject
{
    Q_OBJECT
    QTemporaryDir m_data;

    AppUpdater::Build oldBuild() const { return {QStringLiteral("1.1.0"), kOld, QStringLiteral("2026-11-07T12:00:00+01:00")}; }

private slots:
    void parsesGitAdvertisement()
    {
        const auto refs = AppUpdater::parseRefs(refsBody({{"refs/heads/uve-player", kTip},
                                                          {"refs/tags/uve-player-v1.2.0", kRel},
                                                          {"refs/tags/annotated", kOld},
                                                          {"refs/tags/annotated^{}", kNext}}));
        QCOMPARE(refs.value("refs/heads/uve-player"), kTip);
        QCOMPARE(refs.value("refs/tags/uve-player-v1.2.0"), kRel);
        QCOMPARE(refs.value("refs/tags/annotated"), kNext);   // Commit, nicht das Tag-Objekt
        QVERIFY(AppUpdater::parseRefs("<html>Fehlerseite</html>").isEmpty());
    }
    void parsesApiRefs()
    {
        const QByteArray json = QStringLiteral(R"([{"ref":"refs/heads/uve-player","object":{"sha":"%1"}},{"ref":"refs/tags/x","object":{"sha":"kurz"}}])").arg(kTip).toUtf8();
        const auto refs = AppUpdater::parseRefs(json);
        QCOMPARE(refs.value("refs/heads/uve-player"), kTip);
        QVERIFY(!refs.contains("refs/tags/x"));
    }
    void releaseVersionsAreNumericAndFiltered()
    {
        const QHash<QString, QString> refs{{"refs/tags/uve-player-v1.1.0", kOld}, {"refs/tags/uve-player-v1.9.0", kOld},
                                           {"refs/tags/uve-player-v1.10.0", kOld}, {"refs/tags/v9.0.0", kOld},
                                           {"refs/tags/uve-player-v2.0.0-beta", kOld}, {"refs/heads/uve-player-v3.0.0", kOld}};
        QCOMPARE(AppUpdater::newerReleaseVersions(refs, "1.1.0"), QStringList({"1.10.0", "1.9.0"}));
    }
    void parsesCommitFeed()
    {
        const auto list = AppUpdater::parseCommitFeed(feed(feedEntry(kTip, "Zwei &amp; drei", "2026-11-08T10:00:00Z")));
        QCOMPARE(list.size(), 1);
        const auto c = list.first().toMap();
        QCOMPARE(c.value("sha").toString(), kTip);
        QCOMPARE(c.value("shortSha").toString(), kTip.left(7));
        QCOMPARE(c.value("title").toString(), QStringLiteral("Zwei & drei"));
        QVERIFY(!c.contains("email"));
    }
    void commitsSinceOwnCommit()
    {
        const auto all = AppUpdater::parseCommitFeed(feed(feedEntry(kTip, "neu", "2026-11-08T10:00:00Z") + feedEntry(kOld, "eigen", "2026-11-07T11:00:00Z")));
        bool truncated = true;
        QCOMPARE(AppUpdater::commitsSince(all, kOld, "2026-11-07T12:00:00+01:00", &truncated).size(), 1);
        QVERIFY(!truncated);
        QCOMPARE(AppUpdater::commitsSince(all, kTip, "", &truncated).size(), 0);
        // Sehr alter Stand, nicht mehr im Feed
        QCOMPARE(AppUpdater::commitsSince(all, kNext, "2020-01-01T00:00:00Z", &truncated).size(), 2);
        QVERIFY(truncated);
        // Lokaler Commit, neuer als alles auf GitHub: nichts ist "neu"
        QCOMPARE(AppUpdater::commitsSince(all, kNext, "2030-01-01T00:00:00Z", &truncated).size(), 0);
        QVERIFY(!truncated);
        // Build ohne Git-Kennung
        QCOMPARE(AppUpdater::commitsSince(all, "", "", &truncated).size(), 0);
    }
    void parsesChecksums()
    {
        const auto sums = AppUpdater::parseSha256Sums(QString(64, 'A').toLatin1() + "  file one.exe\r\n" + QString(64, 'b').toLatin1() + " *b.zip\nkaputt\n");
        QCOMPARE(sums.value("file one.exe"), QString(64, 'a'));
        QCOMPARE(sums.value("b.zip"), QString(64, 'b'));
        QCOMPARE(sums.size(), 2);
    }
    void assetNamesMatchPackaging()
    {
        QCOMPARE(AppUpdater::assetName("setup", "1.2.0", "x86_64"), QStringLiteral("UVE-Player-1.2.0-setup.exe"));
        QCOMPARE(AppUpdater::assetName("portable-exe", "1.2.0", "x86_64"), QStringLiteral("UVE-Player-1.2.0-portable.exe"));
        QCOMPARE(AppUpdater::assetName("portable-zip", "1.2.0", "x86_64"), QStringLiteral("UVE-Player-1.2.0-portable.zip"));
        QCOMPARE(AppUpdater::assetName("appimage", "1.2.0", "x86_64"), QStringLiteral("UVE-Player-1.2.0-linux-x86_64.AppImage"));
        QCOMPARE(AppUpdater::assetName("dmg", "1.2.0", "arm64"), QStringLiteral("UVE-Player-1.2.0-macos-arm64.dmg"));
        QVERIFY(AppUpdater::assetName("manual", "1.2.0", "x86_64").isEmpty());
    }
    void parsesMpvAssetPage()
    {
        const QString digest = "sha256:" + QString(64, 'e');
        const QString html = QStringLiteral(
            "<a href=\"/shinchiro/mpv-winbuild-cmake/releases/download/20261108/mpv-dev-aarch64-20261108-git-aa.7z\">a</a>"
            "<span>sha256:%1</span><span>1.0 MB</span>"
            "<a href=\"/shinchiro/mpv-winbuild-cmake/releases/download/20261108/mpv-dev-x86_64-20261108-git-55d3feb45c.7z\">b</a>"
            "<span>%2</span><span class=\"x\">30.5 MB</span>"
            "<a href=\"/shinchiro/mpv-winbuild-cmake/releases/download/20261108/mpv-dev-x86_64-v3-20261108-git-55d3feb45c.7z\">c</a>"
            "<span>sha256:%3</span><span>31.0 MB</span>").arg(QString(64, 'f'), digest, QString(64, '0'));
        const auto m = ToolUpdater::parseMpvAssetPage(html, "20261108");
        QVERIFY(m.value("url").toString().endsWith("/mpv-dev-x86_64-20261108-git-55d3feb45c.7z"));
        QVERIFY(m.value("url").toString().startsWith("https://github.com/"));
        QCOMPARE(m.value("digest").toString(), digest);
        QCOMPARE(m.value("size").toLongLong(), qlonglong(30.5 * 1048576));
        QVERIFY(ToolUpdater::parseMpvAssetPage(QStringLiteral("nichts"), "20261108").isEmpty());
    }

    // Der neueste Tag hat noch keine Pakete (Workflow läuft): Es gilt das neueste Release mit Dateien
    void checkSkipsReleaseStillBuilding()
    {
        const QByteArray pkg = "paket";
        const QByteArray sum = QCryptographicHash::hash(pkg, QCryptographicHash::Sha256).toHex();
        FakeServer srv([&](const QByteArray &, const QByteArray &path, const QByteArray &) -> Response {
            if (path.contains("info/refs")) return {200, refsBody({{"refs/heads/uve-player", kTip}, {"refs/tags/uve-player-v1.0.9", kOld},
                                                                   {"refs/tags/uve-player-v1.2.0", kRel}, {"refs/tags/uve-player-v1.3.0", kNext}})};
            if (path.endsWith("uve-player-v1.2.0/SHA256SUMS.txt")) return {200, sum + "  UVE-Player-1.2.0-portable.zip\n"};
            if (path.endsWith("uve-player-v1.2.0/UVE-Player-1.2.0-portable.zip")) return {200, pkg};
            if (path.endsWith("uve-player.atom")) return {200, feed(feedEntry(kTip, "neuer Commit", "2026-11-08T10:00:00Z") + feedEntry(kOld, "eigen", "2026-11-07T11:00:00Z"))};
            return {404, {}};
        });
        AppUpdater up(oldBuild(), m_data.path(), m_data.filePath("a"));
        up.setInstallKind("portable-zip");
        up.setRepositoryBase(srv.base());
        QSignalSpy spy(&up, &AppUpdater::checkFinished);
        up.check();
        QVERIFY(spy.wait(15000));
        const auto r = spy.first().at(0).toMap();
        QCOMPARE(spy.first().at(1).toString(), QString());
        QVERIFY(r.value("updateAvailable").toBool());
        QVERIFY(r.value("canInstall").toBool());
        QCOMPARE(r.value("latestVersion").toString(), QStringLiteral("1.2.0"));
        QCOMPARE(r.value("latestCommit").toString(), kRel);
        QCOMPARE(r.value("branchCommit").toString(), kTip);
        QCOMPARE(r.value("assetSize").toLongLong(), qlonglong(pkg.size()));
        QCOMPARE(r.value("newCommits").toList().size(), 1);
        QVERIFY(!up.busy());
    }

    // Neue Commits ohne Release: anzeigen, aber nichts zum Installieren anbieten
    void checkCommitsWithoutRelease()
    {
        FakeServer srv([&](const QByteArray &, const QByteArray &path, const QByteArray &) -> Response {
            if (path.contains("info/refs")) return {200, refsBody({{"refs/heads/uve-player", kTip}, {"refs/tags/uve-player-v1.1.0", kOld}})};
            if (path.endsWith("uve-player.atom")) return {200, feed(feedEntry(kTip, "neu", "2026-11-08T10:00:00Z") + feedEntry(kOld, "eigen", "2026-11-07T11:00:00Z"))};
            return {404, {}};
        });
        AppUpdater up(oldBuild(), m_data.path(), m_data.filePath("b"));
        up.setInstallKind("setup");
        up.setRepositoryBase(srv.base());
        QSignalSpy spy(&up, &AppUpdater::checkFinished);
        up.check();
        QVERIFY(spy.wait(15000));
        const auto r = spy.first().at(0).toMap();
        QVERIFY(!r.value("updateAvailable").toBool());
        QVERIFY(!r.value("canInstall").toBool());
        QVERIFY(r.value("newerCommits").toBool());
    }

    // Eigener Stand = neuester Commit: nichts zu melden, auch der Feed-Ausfall stört nicht
    void checkUpToDate()
    {
        FakeServer srv([&](const QByteArray &, const QByteArray &path, const QByteArray &) -> Response {
            if (path.contains("info/refs")) return {200, refsBody({{"refs/heads/uve-player", kOld}, {"refs/tags/uve-player-v1.1.0", kOld}})};
            return {500, {}};
        });
        AppUpdater up(oldBuild(), m_data.path(), m_data.filePath("c"));
        up.setRepositoryBase(srv.base());
        QSignalSpy spy(&up, &AppUpdater::checkFinished);
        up.check();
        QVERIFY(spy.wait(15000));
        const auto r = spy.first().at(0).toMap();
        QCOMPARE(spy.first().at(1).toString(), QString());
        QVERIFY(!r.value("updateAvailable").toBool());
        QVERIFY(!r.value("newerCommits").toBool());
    }

    // Git-Endpunkt gestört → Rückfall auf die zweite Quelle; beide gestört → Fehlermeldung statt Hängen
    void checkFallsBackToSecondSource()
    {
        FakeServer srv([&](const QByteArray &, const QByteArray &path, const QByteArray &) -> Response {
            if (path.contains("info/refs")) return {429, "limit"};
            if (path.contains("/api-refs")) return {200, QStringLiteral(R"([{"ref":"refs/heads/uve-player","object":{"sha":"%1"}}])").arg(kOld).toUtf8()};
            return {404, {}};
        });
        AppUpdater up(oldBuild(), m_data.path(), m_data.filePath("d"));
        up.setRepositoryBase(srv.base());
        up.setRefsFallbackUrl(srv.base() + "/api-refs");
        QSignalSpy spy(&up, &AppUpdater::checkFinished);
        up.check();
        QVERIFY(spy.wait(15000));
        QCOMPARE(spy.first().at(1).toString(), QString());
        QCOMPARE(spy.first().at(0).toMap().value("branchCommit").toString(), kOld);

        up.setRefsFallbackUrl(srv.base() + "/gibt-es-nicht");
        spy.clear();
        up.check();
        QVERIFY(spy.wait(15000));
        QVERIFY(!spy.first().at(1).toString().isEmpty());
        QVERIFY(!up.busy());
    }

    // Limit (429 mit Retry-After), abgebrochene Übertragung, Fortsetzen per Range
    void downloadSurvivesLimitAndAbort()
    {
        const QByteArray data = QByteArray(5000, 'x') + QByteArray(5000, 'y');
        int calls = 0;
        FakeServer srv([&](const QByteArray &, const QByteArray &, const QByteArray &range) -> Response {
            ++calls;
            if (calls == 1) return {429, "Too Many Requests", "Retry-After: 0\r\n"};
            if (calls == 2) { Response r{200, data.left(4000)}; r.declaredLength = data.size(); return r; }
            if (calls == 3) return {503, "kurz weg", "Retry-After: 0\r\n"};
            const qint64 from = range.mid(6, range.indexOf('-') - 6).toLongLong();
            return {206, data.mid(from), "Content-Range: bytes " + QByteArray::number(from) + "-" + QByteArray::number(data.size() - 1) + "/" + QByteArray::number(qlonglong(data.size())) + "\r\n"};
        });
        QTemporaryDir dir;
        QNetworkAccessManager net;
        ResumableDownload dl(&net, QUrl(srv.base() + "/f"), dir.filePath("f.part"));
        dl.setBaseDelayMs(10);
        QSignalSpy done(&dl, &ResumableDownload::finished);
        QSignalSpy retry(&dl, &ResumableDownload::retrying);
        dl.start();
        QVERIFY(done.wait(15000));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QCOMPARE(retry.count(), 3);
        QCOMPARE(srv.ranges.first(), QByteArray());
        QCOMPARE(srv.ranges.last(), QByteArray("bytes=4000-"));
        QFile f(dir.filePath("f.part"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), data);
    }
    void downloadRestartsWhenServerIgnoresRange()
    {
        const QByteArray data(3000, 'z');
        FakeServer srv([&](const QByteArray &, const QByteArray &, const QByteArray &) -> Response { return {200, data}; });
        QTemporaryDir dir;
        { QFile f(dir.filePath("f.part")); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("alter Rest"); }
        QNetworkAccessManager net;
        ResumableDownload dl(&net, QUrl(srv.base() + "/f"), dir.filePath("f.part"));
        QSignalSpy done(&dl, &ResumableDownload::finished);
        dl.start();
        QVERIFY(done.wait(15000));
        QVERIFY(done.first().at(0).toBool());
        QCOMPARE(QFileInfo(dir.filePath("f.part")).size(), data.size());
    }
    void downloadDoesNotRetryNotFound()
    {
        int calls = 0;
        FakeServer srv([&](const QByteArray &, const QByteArray &, const QByteArray &) -> Response { ++calls; return {404, "nein"}; });
        QTemporaryDir dir;
        QNetworkAccessManager net;
        ResumableDownload dl(&net, QUrl(srv.base() + "/f"), dir.filePath("f.part"));
        QSignalSpy done(&dl, &ResumableDownload::finished);
        dl.start();
        QVERIFY(done.wait(15000));
        QVERIFY(!done.first().at(0).toBool());
        QCOMPARE(calls, 1);
        QVERIFY(!QFileInfo(dir.filePath("f.part")).exists() || QFileInfo(dir.filePath("f.part")).size() == 0);
    }

    void installVerifiesChecksum_data()
    {
        QTest::addColumn<bool>("good");
        QTest::newRow("richtig") << true;
        QTest::newRow("falsch") << false;
    }
    void installVerifiesChecksum()
    {
        QFETCH(bool, good);
        const QByteArray pkg = "echtes Paket";
        const QByteArray sum = good ? QCryptographicHash::hash(pkg, QCryptographicHash::Sha256).toHex() : QByteArray(64, '0');
        FakeServer srv([&](const QByteArray &, const QByteArray &path, const QByteArray &) -> Response {
            if (path.contains("info/refs")) return {200, refsBody({{"refs/heads/uve-player", kRel}, {"refs/tags/uve-player-v1.2.0", kRel}})};
            if (path.endsWith("SHA256SUMS.txt")) return {200, sum + "  UVE-Player-1.2.0-setup.exe\n"};
            if (path.endsWith("UVE-Player-1.2.0-setup.exe")) return {200, pkg};
            return {404, {}};
        });
        QTemporaryDir data;
        AppUpdater up(oldBuild(), data.path(), data.path());
        up.setInstallKind("setup");
        up.setRepositoryBase(srv.base());
        up.setLaunchEnabled(false);
        QSignalSpy checked(&up, &AppUpdater::checkFinished);
        up.check();
        QVERIFY(checked.wait(15000));

        QSignalSpy ready(&up, &AppUpdater::packageReady);
        QSignalSpy failed(&up, &AppUpdater::installFailed);
        up.install();
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() + failed.count() > 0, 15000);
        const QString file = data.filePath("updates/UVE-Player-1.2.0-setup.exe");
        if (good) {
            QCOMPARE(ready.first().at(0).toString(), file);
            QFile f(file);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), pkg);
        } else {
            QCOMPARE(failed.count(), 1);
            QVERIFY(!QFileInfo::exists(file));
            QVERIFY(!QFileInfo::exists(file + ".part"));
        }
        QVERIFY(!up.busy());
    }

    // Echtes GitHub (nur auf Wunsch, lädt ein vollständiges Paket)
    void realGitHub()
    {
        if (!qEnvironmentVariableIsSet("UVE_NETWORK_TESTS")) QSKIP("UVE_NETWORK_TESTS nicht gesetzt");
        QTemporaryDir data;
        AppUpdater up({QStringLiteral("1.0.9"), QStringLiteral("6ba7f2d415262fb7deb1b483ac5edb08fd766c0a"), QStringLiteral("2026-11-07T10:00:00+01:00")}, data.path(), data.path());
        up.setInstallKind("portable-zip");
        up.setLaunchEnabled(false);
        up.setRefsFallbackUrl(QStringLiteral("http://127.0.0.1:9/api-darf-nicht-noetig-sein"));
        QSignalSpy checked(&up, &AppUpdater::checkFinished);
        up.check();
        QVERIFY(checked.wait(60000));
        const auto r = checked.first().at(0).toMap();
        QCOMPARE(checked.first().at(1).toString(), QString());
        qInfo() << "latest" << r.value("latestVersion") << r.value("latestShort") << "tip" << r.value("branchShort")
                << "size" << r.value("assetSize") << "commits" << r.value("newCommits").toList().size();
        QVERIFY(r.value("updateAvailable").toBool());
        QVERIFY(r.value("canInstall").toBool());
        QVERIFY(r.value("assetSize").toLongLong() > 1000000);
        QVERIFY(!r.value("newCommits").toList().isEmpty());

        QSignalSpy ready(&up, &AppUpdater::packageReady);
        QSignalSpy failed(&up, &AppUpdater::installFailed);
        up.install();
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() + failed.count() > 0, 500000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(QFileInfo(ready.first().at(0).toString()).size(), r.value("assetSize").toLongLong());
    }
};

QTEST_GUILESS_MAIN(TstAppUpdater)
#include "tst_appupdater.moc"
