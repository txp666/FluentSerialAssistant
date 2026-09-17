#include "app/core/update_downloader.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <algorithm>

using AppUpdate::UpdateDownloader;

class MockUpdateServer : public QObject
{
  public:
    enum class Mode
    {
        Ranges,
        NoRanges,
        Truncated,
        Overlong,
        WrongRange,
        WrongSize,
        IgnoreRanges,
        Corrupt,
        HttpError,
        Stall,
        StallParts
    };

    explicit MockUpdateServer(Mode mode = Mode::Ranges) : mode(mode)
    {
        payload.resize(128 * 1024 + 3);
        for (qsizetype index = 0; index < payload.size(); ++index) {
            payload[index] = static_cast<char>(index % 251);
        }
        connect(&server, &QTcpServer::newConnection, this, [this]() {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QIODevice::readyRead, this, [this, socket]() {
                    QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", request);
                    if (!request.contains("\r\n\r\n") || socket->property("handled").toBool()) {
                        return;
                    }
                    socket->setProperty("handled", true);
                    respond(socket, request);
                });
            }
        });
        listening = server.listen(QHostAddress::LocalHost);
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/update.bin").arg(server.serverPort())); }
    QByteArray sha256() const { return QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex(); }

    void respond(QTcpSocket *socket, const QByteArray &request)
    {
        ++requests;
        if (!redirectTarget.isEmpty() && request.startsWith("GET /update.bin ")) {
            ++redirects;
            send(socket, "HTTP/1.1 302 Found\r\nLocation: " + redirectTarget +
                             "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        QByteArray rangeHeader;
        for (const QByteArray &line : request.split('\n')) {
            if (line.toLower().startsWith("range:")) {
                rangeHeader = line.mid(6).trimmed();
            }
        }
        if (mode == Mode::Stall) {
            return;
        }
        if (mode == Mode::HttpError) {
            send(socket, QByteArrayLiteral(
                             "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
            return;
        }
        if (mode == Mode::NoRanges || rangeHeader.isEmpty()) {
            if (rangeHeader.isEmpty()) {
                ++singleRequests;
            } else {
                ++probes;
            }
            if (!rangeHeader.isEmpty() && headersOnlyProbe) {
                connect(socket, &QTcpSocket::disconnected, this, [this]() { probeDisconnected = true; });
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(payload.size()) +
                              "\r\nConnection: close\r\n\r\n");
                return;
            }
            send(socket, "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(payload.size()) +
                             "\r\nConnection: close\r\n\r\n" + payload);
            return;
        }
        const auto match =
            QRegularExpression(QStringLiteral("^bytes=([0-9]+)-([0-9]+)$")).match(QString::fromLatin1(rangeHeader));
        const qint64 first = match.captured(1).toLongLong();
        const qint64 last = match.captured(2).toLongLong();
        if (!match.hasMatch() || first < 0 || last < first || last >= payload.size()) {
            send(socket, QByteArrayLiteral(
                             "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
            return;
        }
        const bool probe = first == 0 && last == 0;
        if (probe) {
            ++probes;
        } else {
            ranges.append(qMakePair(first, last));
        }
        if (!probe && mode == Mode::IgnoreRanges) {
            send(socket, "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(payload.size()) +
                             "\r\nConnection: close\r\n\r\n" + payload);
            return;
        }
        const qint64 size = last - first + 1;
        QByteArray body = payload.mid(first, size);
        if (!probe && mode == Mode::Truncated) {
            body.chop(1);
        } else if (!probe && mode == Mode::Overlong) {
            body.append('!');
        } else if (!probe && mode == Mode::Corrupt) {
            body[0] = static_cast<char>(body[0] ^ 0x20);
        }
        const qint64 responseFirst = mode == Mode::WrongRange ? first + 1 : first;
        const qint64 responseSize = mode == Mode::WrongSize ? payload.size() + 1 : payload.size();
        QByteArray response = "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " +
                              QByteArray::number(responseFirst) + '-' + QByteArray::number(last) + '/' +
                              QByteArray::number(responseSize) + "\r\n";
        if (probe || mode != Mode::Overlong) {
            response += "Content-Length: " + QByteArray::number(size) + "\r\n";
        }
        response += "Connection: close\r\n\r\n" + body;
        if (!probe && mode == Mode::StallParts) {
            const qsizetype bodyStart = response.indexOf("\r\n\r\n") + 4;
            socket->write(response.left(bodyStart + 8192));
            return;
        }
        if (!probe && holdRanges) {
            held.append(qMakePair(QPointer<QTcpSocket>(socket), response));
            maximumHeld = qMax(maximumHeld, held.size());
            if (held.size() == 4) {
                const auto ready = held;
                held.clear();
                QTimer::singleShot(30, this, [this, ready]() {
                    for (const auto &item : ready) {
                        if (item.first) {
                            send(item.first, item.second);
                        }
                    }
                });
            }
        } else {
            send(socket, response);
        }
    }

    void send(QTcpSocket *socket, const QByteArray &response)
    {
        socket->write(response);
        socket->disconnectFromHost();
    }

    QTcpServer server;
    Mode mode;
    QByteArray payload;
    bool listening = false;
    bool holdRanges = false;
    bool headersOnlyProbe = false;
    bool probeDisconnected = false;
    QByteArray redirectTarget;
    int redirects = 0;
    int requests = 0;
    int probes = 0;
    int singleRequests = 0;
    qsizetype maximumHeld = 0;
    QList<QPair<qint64, qint64>> ranges;
    QList<QPair<QPointer<QTcpSocket>, QByteArray>> held;
};

class UpdateDownloaderTest : public QObject
{
    Q_OBJECT

  private:
    static QByteArray readFile(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return file.readAll();
    }

    static QStringList directoryEntries(const QString &path)
    {
        return QDir(path).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    }

  private slots:
    void downloadsFourConcurrentRangesWithVerifiedBinaryContents()
    {
        MockUpdateServer server;
        server.holdRanges = true;
        QVERIFY2(server.listening, qPrintable(server.server.errorString()));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString destination = directory.filePath(QStringLiteral("installer.bin"));
        UpdateDownloader downloader;
        QSignalSpy finished(&downloader, &UpdateDownloader::finished);
        QSignalSpy failed(&downloader, &UpdateDownloader::failed);
        QSignalSpy cancelled(&downloader, &UpdateDownloader::cancelled);
        QSignalSpy verification(&downloader, &UpdateDownloader::verificationStarted);
        QSignalSpy progress(&downloader, &UpdateDownloader::progress);
        bool signalsOnUiThread = true;
        bool stoppedBeforeTerminalSignal = false;
        connect(&downloader, &UpdateDownloader::progress, &downloader,
                [&]() { signalsOnUiThread &= QThread::currentThread() == thread(); });
        connect(&downloader, &UpdateDownloader::finished, &downloader,
                [&]() { stoppedBeforeTerminalSignal = !downloader.isRunning(); });
        int uiTicks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(1);
        connect(&heartbeat, &QTimer::timeout, this, [&]() { ++uiTicks; });
        heartbeat.start();
        downloader.start(server.url(), server.payload.size(), server.sha256().toUpper(), destination);
        QVERIFY(downloader.isRunning());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size() + failed.size(), 1, 10000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(cancelled.size(), 0);
        QCOMPARE(verification.size(), 1);
        QVERIFY(!downloader.isRunning());
        QVERIFY(stoppedBeforeTerminalSignal);
        QVERIFY(signalsOnUiThread);
        QVERIFY(uiTicks > 0);
        QCOMPARE(server.probes, 1);
        QCOMPARE(server.ranges.size(), 4);
        QCOMPARE(server.maximumHeld, 4);
        QCOMPARE(server.singleRequests, 0);
        qint64 next = 0;
        std::sort(server.ranges.begin(), server.ranges.end());
        for (const auto &range : server.ranges) {
            QCOMPARE(range.first, next);
            next = range.second + 1;
        }
        QCOMPARE(next, server.payload.size());
        QCOMPARE(readFile(destination), server.payload);
        QCOMPARE(finished.first().first().toString(), destination);
        QCOMPARE(directoryEntries(directory.path()), QStringList{QStringLiteral("installer.bin")});
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last().at(0).toLongLong(), server.payload.size());
        QCOMPARE(progress.last().at(1).toLongLong(), server.payload.size());
    }

    void fallsBackWhenServerIgnoresProbeRange()
    {
        MockUpdateServer server(MockUpdateServer::Mode::NoRanges);
        server.headersOnlyProbe = true;
        QVERIFY2(server.listening, qPrintable(server.server.errorString()));
        QTemporaryDir directory;
        const QString destination = directory.filePath(QStringLiteral("installer.bin"));
        UpdateDownloader downloader;
        QSignalSpy finished(&downloader, &UpdateDownloader::finished);
        QSignalSpy failed(&downloader, &UpdateDownloader::failed);
        downloader.start(server.url(), server.payload.size(), server.sha256(), destination);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size() + failed.size(), 1, 10000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(server.probes, 1);
        QCOMPARE(server.singleRequests, 1);
        QTRY_VERIFY(server.probeDisconnected);
        QVERIFY(server.ranges.isEmpty());
        QCOMPARE(readFile(destination), server.payload);
        QCOMPARE(directoryEntries(directory.path()), QStringList{QStringLiteral("installer.bin")});
    }

    void rejectsInvalidResponsesAndPreservesExistingDestination_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("truncated") << int(MockUpdateServer::Mode::Truncated);
        QTest::newRow("overlong") << int(MockUpdateServer::Mode::Overlong);
        QTest::newRow("wrong-range") << int(MockUpdateServer::Mode::WrongRange);
        QTest::newRow("wrong-size") << int(MockUpdateServer::Mode::WrongSize);
        QTest::newRow("ranges-ignored-after-probe") << int(MockUpdateServer::Mode::IgnoreRanges);
        QTest::newRow("hash-mismatch") << int(MockUpdateServer::Mode::Corrupt);
        QTest::newRow("http-error") << int(MockUpdateServer::Mode::HttpError);
    }

    void followsSafeRelativeRedirectsAndRejectsRemoteHttpRedirects()
    {
        MockUpdateServer server;
        server.redirectTarget = QByteArrayLiteral("/download.bin");
        QVERIFY2(server.listening, qPrintable(server.server.errorString()));
        QTemporaryDir directory;
        const QString destination = directory.filePath(QStringLiteral("installer.bin"));
        UpdateDownloader downloader;
        QSignalSpy finished(&downloader, &UpdateDownloader::finished);
        QSignalSpy failed(&downloader, &UpdateDownloader::failed);
        downloader.start(server.url(), server.payload.size(), server.sha256(), destination);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size() + failed.size(), 1, 10000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QCOMPARE(finished.size(), 1);
        QVERIFY(server.redirects >= 5);
        const int safeRedirects = server.redirects;
        QCOMPARE(readFile(destination), server.payload);

        server.redirectTarget = QByteArrayLiteral("http://example.com/insecure.bin");
        downloader.start(server.url(), server.payload.size(), server.sha256(), destination);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(finished.size(), 1);
        QVERIFY(server.redirects > safeRedirects);
        QCOMPARE(readFile(destination), server.payload);
        QCOMPARE(directoryEntries(directory.path()), QStringList{QStringLiteral("installer.bin")});
    }

    void rejectsInvalidResponsesAndPreservesExistingDestination()
    {
        QFETCH(int, mode);
        MockUpdateServer server(static_cast<MockUpdateServer::Mode>(mode));
        QVERIFY2(server.listening, qPrintable(server.server.errorString()));
        QTemporaryDir directory;
        const QString destination = directory.filePath(QStringLiteral("installer.bin"));
        QFile old(destination);
        QVERIFY(old.open(QIODevice::WriteOnly));
        QCOMPARE(old.write("previous download"), 17);
        old.close();
        UpdateDownloader downloader;
        QSignalSpy finished(&downloader, &UpdateDownloader::finished);
        QSignalSpy failed(&downloader, &UpdateDownloader::failed);
        QSignalSpy cancelled(&downloader, &UpdateDownloader::cancelled);
        bool stoppedBeforeFailure = false;
        connect(&downloader, &UpdateDownloader::failed, &downloader,
                [&]() { stoppedBeforeFailure = !downloader.isRunning(); });
        downloader.start(server.url(), server.payload.size(), server.sha256(), destination);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size() + finished.size(), 1, 10000);
        QCOMPARE(failed.size(), 1);
        QVERIFY(!failed.first().first().toString().isEmpty());
        QVERIFY(stoppedBeforeFailure);
        QCOMPARE(finished.size(), 0);
        QCOMPARE(cancelled.size(), 0);
        QCOMPARE(readFile(destination), QByteArrayLiteral("previous download"));
        QCOMPARE(directoryEntries(directory.path()), QStringList{QStringLiteral("installer.bin")});

        server.mode = MockUpdateServer::Mode::Ranges;
        downloader.start(server.url(), server.payload.size(), server.sha256(), destination);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(readFile(destination), server.payload);
    }

    void cancellationCleansPartialFilesAndAllowsRetry()
    {
        MockUpdateServer server(MockUpdateServer::Mode::StallParts);
        QVERIFY2(server.listening, qPrintable(server.server.errorString()));
        QTemporaryDir directory;
        const QString destination = directory.filePath(QStringLiteral("installer.bin"));
        UpdateDownloader downloader;
        QSignalSpy finished(&downloader, &UpdateDownloader::finished);
        QSignalSpy failed(&downloader, &UpdateDownloader::failed);
        QSignalSpy cancelled(&downloader, &UpdateDownloader::cancelled);
        bool stoppedBeforeCancellation = false;
        connect(&downloader, &UpdateDownloader::cancelled, &downloader,
                [&]() { stoppedBeforeCancellation = !downloader.isRunning(); });
        downloader.start(server.url(), server.payload.size(), server.sha256(), destination);
        QTRY_COMPARE(server.ranges.size(), 4);
        QVERIFY(!directoryEntries(directory.path()).isEmpty());
        downloader.cancel();
        downloader.cancel();
        QTRY_COMPARE(cancelled.size(), 1);
        QVERIFY(stoppedBeforeCancellation);
        QVERIFY(finished.isEmpty());
        QVERIFY(failed.isEmpty());
        QVERIFY(directoryEntries(directory.path()).isEmpty());
        server.mode = MockUpdateServer::Mode::Ranges;
        downloader.start(server.url(), server.payload.size(), server.sha256(), destination);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QCOMPARE(cancelled.size(), 1);
        QVERIFY(failed.isEmpty());
        QCOMPARE(readFile(destination), server.payload);
    }

    void cancellationDuringVerificationNeverDeliversAFile()
    {
        MockUpdateServer server;
        QVERIFY2(server.listening, qPrintable(server.server.errorString()));
        QTemporaryDir directory;
        const QString destination = directory.filePath(QStringLiteral("installer.bin"));
        UpdateDownloader downloader;
        QSignalSpy finished(&downloader, &UpdateDownloader::finished);
        QSignalSpy failed(&downloader, &UpdateDownloader::failed);
        QSignalSpy cancelled(&downloader, &UpdateDownloader::cancelled);
        connect(&downloader, &UpdateDownloader::verificationStarted, &downloader, &UpdateDownloader::cancel);
        downloader.start(server.url(), server.payload.size(), server.sha256(), destination);
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.size(), 1, 10000);
        QVERIFY(finished.isEmpty());
        QVERIFY(failed.isEmpty());
        QVERIFY(directoryEntries(directory.path()).isEmpty());
    }

    void destructionCleansAnActiveDownload()
    {
        MockUpdateServer server(MockUpdateServer::Mode::Stall);
        QVERIFY2(server.listening, qPrintable(server.server.errorString()));
        QTemporaryDir directory;
        const QString destination = directory.filePath(QStringLiteral("installer.bin"));
        {
            UpdateDownloader downloader;
            downloader.start(server.url(), server.payload.size(), server.sha256(), destination);
            QTRY_COMPARE(server.requests, 1);
        }
        QVERIFY(directoryEntries(directory.path()).isEmpty());
    }

    void invalidMetadataAndInsecureRemoteUrlAreRejected()
    {
        QTemporaryDir directory;
        UpdateDownloader downloader;
        QSignalSpy failed(&downloader, &UpdateDownloader::failed);
        const QString destination = directory.filePath(QStringLiteral("installer.bin"));
        downloader.start(QUrl(QStringLiteral("http://example.com/update.bin")), 100, QByteArray(64, '0'), destination);
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(!downloader.isRunning());
        downloader.start(QUrl(QStringLiteral("https://example.com/update.bin")), 0, QByteArray(64, '0'), destination);
        QTRY_COMPARE(failed.size(), 2);
        downloader.start(QUrl(QStringLiteral("https://example.com/update.bin")), 100, QByteArray(64, 'z'), destination);
        QTRY_COMPARE(failed.size(), 3);
        QVERIFY(directoryEntries(directory.path()).isEmpty());
    }

    void stalledServerTimesOut()
    {
        MockUpdateServer server(MockUpdateServer::Mode::Stall);
        QVERIFY2(server.listening, qPrintable(server.server.errorString()));
        QTemporaryDir directory;
        UpdateDownloader downloader;
        QSignalSpy failed(&downloader, &UpdateDownloader::failed);
        QSignalSpy finished(&downloader, &UpdateDownloader::finished);
        downloader.start(server.url(), server.payload.size(), server.sha256(),
                         directory.filePath(QStringLiteral("installer.bin")));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 40000);
        QVERIFY(!downloader.isRunning());
        QVERIFY(finished.isEmpty());
        QVERIFY(directoryEntries(directory.path()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(UpdateDownloaderTest)

#include "tst_update_downloader.moc"
