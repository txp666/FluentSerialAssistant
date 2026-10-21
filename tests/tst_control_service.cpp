#include "app/control/control_protocol.h"
#include "app/control/local_control_client.h"
#include "app/control/local_control_server.h"
#include "app/control/workbench_control_service.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QScopeGuard>
#include <QtNetwork/QLocalSocket>
#include <QtTest/QTest>

#include <atomic>
#include <thread>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

class ControlServiceTest : public QObject
{
    Q_OBJECT

  private slots:
    void socketLivesInADirectoryPrivateToTheUser()
    {
#ifdef Q_OS_UNIX
        const QString name = AppControl::localServerName();
        QVERIFY(!name.isEmpty());
        QVERIFY(QDir::isAbsolutePath(name));
        // A Unix socket path must fit in sockaddr_un (104 bytes on macOS).
        QVERIFY(name.toLocal8Bit().size() < 104);
        const QFileInfo directory(QFileInfo(name).absolutePath());
        QVERIFY(directory.isDir());
        QCOMPARE(directory.ownerId(), uint(::geteuid()));
        const auto shared = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                            QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        QVERIFY((directory.permissions() & shared) == 0);

        // The GUI, CLI and MCP server are different applications but must agree.
        const QString applicationName = QCoreApplication::applicationName();
        const auto restoreName = qScopeGuard([applicationName]() {
            QCoreApplication::setApplicationName(applicationName);
        });
        QCoreApplication::setApplicationName(QStringLiteral("fluentserial-cli"));
        QCOMPARE(AppControl::localServerName(), name);
#else
        QSKIP("Windows uses a named pipe restricted to the current user");
#endif
    }

    void clientReachesTheServiceByTheSharedName()
    {
        QLocalSocket running;
        running.connectToServer(AppControl::localServerName());
        if (running.waitForConnected(100)) {
            QSKIP("A running Fluent Serial Assistant owns the control service");
        }

        AppControl::WorkbenchControlService service(nullptr);
        AppControl::LocalControlServer server(&service);
        QString error;
        QVERIFY2(server.start(&error), qPrintable(error));

        // The client blocks, so it runs beside the event loop that serves it.
        AppControl::ClientReply reply;
        std::atomic_bool done = false;
        std::thread client([&reply, &done]() {
            reply = AppControl::LocalControlClient::call(QStringLiteral("app.ping"), {}, 5000);
            done = true;
        });
        const auto joinClient = qScopeGuard([&client]() { client.join(); });
        QTRY_VERIFY_WITH_TIMEOUT(done.load(), 10000);
        QVERIFY2(reply.transportOk, qPrintable(reply.errorMessage));
        QVERIFY(reply.response.value(QStringLiteral("ok")).toBool());
        QCOMPARE(reply.response.value(QStringLiteral("result")).toObject().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Fluent Serial Assistant"));
    }
};

QTEST_GUILESS_MAIN(ControlServiceTest)
#include "tst_control_service.moc"
