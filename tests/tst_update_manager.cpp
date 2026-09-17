#include "app/core/update_checker.h"
#include "app/core/update_downloader.h"
#include "app/core/update_manager.h"
#include "app/core/update_release.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <atomic>

namespace {
QJsonObject releaseJson(const QString &version = QStringLiteral("0.1.14"))
{
    QJsonArray assets;
    const QStringList suffixes{QStringLiteral("windows-x64-setup.exe"), QStringLiteral("macos-arm64.dmg"),
                               QStringLiteral("linux-x64.deb"), QStringLiteral("linux-arm64.deb")};
    for (const QString &suffix : suffixes) {
        const QString name = QStringLiteral("FluentSerialAssistant-%1-%2").arg(version, suffix);
        assets.append(
            QJsonObject{{QStringLiteral("name"), name},
                        {QStringLiteral("size"), 12345},
                        {QStringLiteral("state"), QStringLiteral("uploaded")},
                        {QStringLiteral("digest"), QStringLiteral("sha256:") + QString(64, QLatin1Char('a'))},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/txp666/FluentSerialAssistant/releases/download/v%1/%2")
                             .arg(version, name)}});
    }
    return {{QStringLiteral("tag_name"), QStringLiteral("v") + version},
            {QStringLiteral("html_url"),
             QStringLiteral("https://github.com/txp666/FluentSerialAssistant/releases/tag/v") + version},
            {QStringLiteral("draft"), false},
            {QStringLiteral("prerelease"), false},
            {QStringLiteral("body"), QStringLiteral("## New version\n\n- Improve updates.")},
            {QStringLiteral("assets"), assets}};
}

class ReleaseServer : public QTcpServer
{
  public:
    QByteArray body = QJsonDocument(releaseJson()).toJson();
    int requests = 0;
    explicit ReleaseServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    QByteArray data = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", data);
                    if (!data.contains("\r\n\r\n") || socket->property("answered").toBool()) {
                        return;
                    }
                    socket->setProperty("answered", true);
                    ++requests;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                  QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/latest").arg(serverPort())); }
};
} // namespace

class UpdateManagerTest : public QObject
{
    Q_OBJECT
    using State = AppUpdate::UpdateManager::State;

  private slots:
    void initTestCase() { QCoreApplication::setApplicationVersion(QStringLiteral("0.1.13")); }

    void selectsMatchingPlatform_data()
    {
        QTest::addColumn<QString>("platform");
        QTest::addColumn<QString>("suffix");
        QTest::newRow("windows") << QStringLiteral("windows-x64") << QStringLiteral("windows-x64-setup.exe");
        QTest::newRow("mac") << QStringLiteral("macos-arm64") << QStringLiteral("macos-arm64.dmg");
        QTest::newRow("linux-x64") << QStringLiteral("linux-x64") << QStringLiteral("linux-x64.deb");
        QTest::newRow("linux-arm64") << QStringLiteral("linux-arm64") << QStringLiteral("linux-arm64.deb");
    }
    void selectsMatchingPlatform()
    {
        QFETCH(QString, platform);
        QFETCH(QString, suffix);
        AppUpdate::ReleaseInfo release;
        bool available = false;
        QString error;
        QVERIFY2(AppUpdate::parseRelease(QJsonDocument(releaseJson()).toJson(), platform, QStringLiteral("0.1.13"),
                                         &release, &available, &error),
                 qPrintable(error));
        QVERIFY(available);
        QCOMPARE(release.version, QStringLiteral("0.1.14"));
        QCOMPARE(release.assetName, QStringLiteral("FluentSerialAssistant-0.1.14-") + suffix);
        QCOMPARE(release.size, 12345);
        QCOMPARE(release.sha256, QByteArray(64, 'a'));
        QVERIFY(release.notes.contains(QStringLiteral("Improve updates")));
    }

    void rejectsUnsafeOrIncompleteRelease_data()
    {
        QTest::addColumn<QString>("mutation");
        for (const QString &value :
             {QStringLiteral("version"), QStringLiteral("draft"), QStringLiteral("prerelease"),
              QStringLiteral("release-url"), QStringLiteral("download-url"), QStringLiteral("size"),
              QStringLiteral("digest"), QStringLiteral("missing"), QStringLiteral("duplicate"), QStringLiteral("state"),
              QStringLiteral("overflow-version")}) {
            QTest::newRow(qPrintable(value)) << value;
        }
    }
    void rejectsUnsafeOrIncompleteRelease()
    {
        QFETCH(QString, mutation);
        QJsonObject object = releaseJson();
        auto assets = object.value(QStringLiteral("assets")).toArray();
        auto asset = assets.first().toObject();
        if (mutation == QStringLiteral("version"))
            object[QStringLiteral("tag_name")] = QStringLiteral("v0.2.0-beta.1");
        if (mutation == QStringLiteral("overflow-version"))
            object[QStringLiteral("tag_name")] = QStringLiteral("v9999999999999999999999.1.1");
        if (mutation == QStringLiteral("draft"))
            object[QStringLiteral("draft")] = true;
        if (mutation == QStringLiteral("prerelease"))
            object[QStringLiteral("prerelease")] = true;
        if (mutation == QStringLiteral("release-url"))
            object[QStringLiteral("html_url")] = QStringLiteral("https://github.com/other/repo/releases/tag/v0.1.14");
        if (mutation == QStringLiteral("download-url"))
            asset[QStringLiteral("browser_download_url")] = QStringLiteral("https://example.com/update.exe");
        if (mutation == QStringLiteral("size"))
            asset[QStringLiteral("size")] = -1;
        if (mutation == QStringLiteral("digest"))
            asset.remove(QStringLiteral("digest"));
        if (mutation == QStringLiteral("state"))
            asset[QStringLiteral("state")] = QStringLiteral("new");
        assets[0] = asset;
        if (mutation == QStringLiteral("missing"))
            assets.removeAt(0);
        if (mutation == QStringLiteral("duplicate"))
            assets.append(asset);
        object[QStringLiteral("assets")] = assets;
        AppUpdate::ReleaseInfo release;
        bool available = true;
        QString error;
        QVERIFY(!AppUpdate::parseRelease(QJsonDocument(object).toJson(), QStringLiteral("windows-x64"),
                                         QStringLiteral("0.1.13"), &release, &available, &error));
        QVERIFY(!available);
        QVERIFY(!error.isEmpty());
        QVERIFY(release.downloadUrl.isEmpty());
    }

    void neverDowngradesOrOffersSameVersion()
    {
        for (const auto &version : {QStringLiteral("0.1.9"), QStringLiteral("0.1.13")}) {
            AppUpdate::ReleaseInfo release;
            bool available = true;
            QString error;
            QVERIFY(AppUpdate::parseRelease(QJsonDocument(releaseJson(version)).toJson(), QStringLiteral("windows-x64"),
                                            QStringLiteral("0.1.13"), &release, &available, &error));
            QVERIFY(!available);
            QVERIFY(release.downloadUrl.isEmpty());
        }
    }

    void startupOnlyChecksAndWaitsForUserConfirmation()
    {
        ReleaseServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QTemporaryDir cache;
        QVERIFY(cache.isValid());
        AppUpdate::UpdateManager manager;
        manager.m_cacheRoot = cache.path();
        manager.m_checker->m_releaseApiUrl = server.url();
        QSignalSpy found(&manager, &AppUpdate::UpdateManager::updateFound);
        QSignalSpy ready(&manager, &AppUpdate::UpdateManager::installationReady);
        manager.downloadUpdate();
        QCOMPARE(manager.state(), State::Idle);
        manager.checkForUpdates(true);
        manager.checkForUpdates(true);
        QTRY_COMPARE(found.size(), 1);
        QCOMPARE(server.requests, 1);
        QCOMPARE(manager.state(), State::Available);
        QVERIFY(manager.isAutomaticCheck());
        QVERIFY(!manager.m_downloader->isRunning());
        QVERIFY(!manager.m_downloadAuthorized);
        QTest::qWait(100);
        QVERIFY(ready.isEmpty());
        QVERIFY(manager.m_downloadPath.isEmpty());
    }

    void cancelPreventsLateCompletionFromInstalling()
    {
        AppUpdate::UpdateManager manager;
        std::atomic_bool installed = false;
        manager.m_install = [&installed](const QString &, QString *) {
            installed = true;
            return true;
        };
        manager.m_state = State::Verifying;
        manager.m_downloadAuthorized = true;
        manager.m_downloadPath = QStringLiteral("verified-package");
        QSignalSpy ready(&manager, &AppUpdate::UpdateManager::installationReady);
        manager.cancelDownload();
        manager.handleDownloaded(QStringLiteral("verified-package"));
        QCOMPARE(manager.state(), State::Cancelled);
        QVERIFY(!installed);
        QVERIFY(ready.isEmpty());
    }

    void installationRunsOffTheUiThreadAndOnlyQuitsAfterHandoff_data()
    {
        QTest::addColumn<bool>("succeeds");
        QTest::newRow("success") << true;
        QTest::newRow("authorization-rejected") << false;
    }
    void installationRunsOffTheUiThreadAndOnlyQuitsAfterHandoff()
    {
        QFETCH(bool, succeeds);
        AppUpdate::UpdateManager manager;
        manager.m_state = State::Verifying;
        manager.m_downloadAuthorized = true;
        manager.m_downloadPath = QStringLiteral("verified-package");
        std::atomic_bool workerThread = false;
        QThread *uiThread = QThread::currentThread();
        manager.m_install = [succeeds, &workerThread, uiThread](const QString &, QString *error) {
            workerThread = QThread::currentThread() != uiThread;
            QThread::msleep(100);
            if (!succeeds)
                *error = QStringLiteral("Authorization rejected");
            return succeeds;
        };
        QSignalSpy ready(&manager, &AppUpdate::UpdateManager::installationReady);
        int ticks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(5);
        connect(&heartbeat, &QTimer::timeout, this, [&]() { ++ticks; });
        heartbeat.start();
        manager.handleDownloaded(QStringLiteral("verified-package"));
        QCOMPARE(manager.state(), State::Installing);
        QVERIFY(ready.isEmpty());
        QTRY_VERIFY(ready.size() == 1 || manager.state() == State::Failed);
        QVERIFY(workerThread);
        QVERIFY(ticks > 0);
        QCOMPARE(ready.size(), succeeds ? 1 : 0);
        if (!succeeds)
            QCOMPARE(manager.statusMessage(), QStringLiteral("Authorization rejected"));
    }

    void liveReleaseDownloadWithoutInstalling()
    {
        if (!qEnvironmentVariableIsSet("FLUENT_UPDATE_LIVE_TEST")) {
            QSKIP("Opt-in read-only live release download; installer is replaced with a test callback.");
        }
        QTemporaryDir cache;
        QVERIFY(cache.isValid());
        AppUpdate::UpdateManager manager;
        manager.m_cacheRoot = cache.path();
        bool installed = false;
        manager.m_install = [&installed](const QString &path, QString *) {
            installed = QFileInfo(path).size() > 0;
            return installed;
        };
        QCoreApplication::setApplicationVersion(QStringLiteral("0.0.0"));
        QSignalSpy found(&manager, &AppUpdate::UpdateManager::updateFound);
        QSignalSpy ready(&manager, &AppUpdate::UpdateManager::installationReady);
        manager.checkForUpdates(true);
        QTRY_VERIFY_WITH_TIMEOUT(found.size() == 1 || manager.state() == State::Failed, 60000);
        QVERIFY2(found.size() == 1, qPrintable(manager.statusMessage()));
        QVERIFY(!manager.m_downloader->isRunning());
        manager.downloadUpdate();
        QTRY_VERIFY_WITH_TIMEOUT(ready.size() == 1 || manager.state() == State::Failed, 180000);
        QVERIFY2(ready.size() == 1, qPrintable(manager.statusMessage()));
        QVERIFY(installed);
        QFile file(manager.m_downloadPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.size(), manager.release().size);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QVERIFY(hash.addData(&file));
        QCOMPARE(hash.result().toHex(), manager.release().sha256);
        QCoreApplication::setApplicationVersion(QStringLiteral("0.1.13"));
    }
};

QTEST_GUILESS_MAIN(UpdateManagerTest)
#include "tst_update_manager.moc"
