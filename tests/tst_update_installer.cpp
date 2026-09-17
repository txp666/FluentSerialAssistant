#include "app/core/update_installer.h"
#include "app/core/update_installer_p.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtTest/QTest>

using namespace AppUpdate::InstallerDetail;

namespace {

bool writeFixture(const QString &path, const QByteArray &data, bool executable = false)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        return false;
    }
    file.close();
    if (executable) {
        return file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    return true;
}

QByteArray readFixture(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

int runShell(const QString &script, const QStringList &arguments, QByteArray *output = nullptr)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral("/bin/sh"), QStringList{script} + arguments);
    if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(3000);
        return -1;
    }
    if (output) {
        *output = process.readAll();
    }
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

struct MacFixture
{
    QTemporaryDir directory;
    InstallPlan plan;
    QString script;
    QStringList arguments;

    bool initialize()
    {
        if (!directory.isValid()) {
            return false;
        }
        // These names intentionally contain shell metacharacters and spaces.
        const QDir root(directory.path());
        plan.workDirectory = root.filePath(QStringLiteral("work's $data; safe"));
        plan.packagePath = root.filePath(QStringLiteral("package's $(touch INJECTED).dmg"));
        plan.targetPath = root.filePath(QStringLiteral("App's $name; test.app"));
        plan.parentPid = 2147483647; // no live process is signalled or terminated
        plan.waitSeconds = 0;
        if (!QDir().mkpath(plan.workDirectory) ||
            !QDir().mkpath(QDir(plan.targetPath).filePath(QStringLiteral("Contents/MacOS"))) ||
            !writeFixture(plan.packagePath, "fake disk image") ||
            !writeFixture(QDir(plan.targetPath).filePath(QStringLiteral("Contents/Info.plist")), "old plist") ||
            !writeFixture(QDir(plan.targetPath).filePath(QStringLiteral("Contents/MacOS/FluentSerialAssistant")), "old",
                          true)) {
            return false;
        }
        script = root.filePath(QStringLiteral("install.sh"));
        const QString mountTool = root.filePath(QStringLiteral("mock-hdiutil"));
        const QString copyTool = root.filePath(QStringLiteral("mock-ditto"));
        const QString signTool = root.filePath(QStringLiteral("mock-codesign"));
        const QString plistTool = root.filePath(QStringLiteral("mock-plistbuddy"));
        if (!writeFixture(script, macInstallerScript().toUtf8()) ||
            !writeFixture(mountTool, R"SH(#!/bin/sh
set -eu
if test "$1" = attach; then
    /bin/mkdir -p "$4/FluentSerialAssistant.app/Contents/MacOS"
    printf new > "$4/FluentSerialAssistant.app/Contents/MacOS/FluentSerialAssistant"
    /bin/chmod 700 "$4/FluentSerialAssistant.app/Contents/MacOS/FluentSerialAssistant"
    printf plist > "$4/FluentSerialAssistant.app/Contents/Info.plist"
fi
)SH",
                          true) ||
            !writeFixture(copyTool, "#!/bin/sh\nexec /bin/cp -R \"$1\" \"$2\"\n", true) ||
            !writeFixture(signTool, "#!/bin/sh\nexit 0\n", true) ||
            !writeFixture(plistTool, "#!/bin/sh\nprintf 'tech.zhangshu.FluentSerialAssistant\\n'\n", true)) {
            return false;
        }
        arguments = macInstallerArguments(plan);
        arguments[5] = mountTool;
        arguments[6] = copyTool;
        arguments[7] = signTool;
        arguments[8] = plistTool;
        return true;
    }

    QByteArray installedBytes() const
    {
        return readFixture(QDir(plan.targetPath).filePath(QStringLiteral("Contents/MacOS/FluentSerialAssistant")));
    }

    QByteArray log() const { return readFixture(QDir(plan.workDirectory).filePath(QStringLiteral("install.log"))); }
};

} // namespace

class UpdateInstallerTest : public QObject
{
    Q_OBJECT

  private slots:
    void rejectsMissingAndWrongPackageWithoutLaunching()
    {
        QString error;
        QVERIFY(!AppUpdate::launchUpdateInstaller(QStringLiteral("/not/a/real/update-package"), &error));
        QVERIFY(!error.isEmpty());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString file = QDir(directory.path()).filePath(QStringLiteral("package.txt"));
        QVERIFY(writeFixture(file, "not an installer"));
        QVERIFY(!AppUpdate::launchUpdateInstaller(file, &error));
        QVERIFY(!error.isEmpty());
    }

    void powerShellArgumentsAreLiteralAndWaitForExit()
    {
        InstallPlan plan;
        plan.packagePath = QStringLiteral("C:/temp/a's $var; %4/setup.exe");
        plan.targetPath = QStringLiteral("C:/Program Files/App's name");
        plan.workDirectory = QStringLiteral("C:/temp/a b");
        plan.parentPid = 34567;
        plan.waitSeconds = 120;
        const QString script = windowsInstallerScript(plan);
        QVERIFY(script.contains(QStringLiteral("a''s $var; %4")));
        QVERIFY(script.contains(QStringLiteral("$parentId = 34567")));
        QVERIFY(script.contains(QStringLiteral("$waitLimit = 120")));
        QVERIFY(script.indexOf(QStringLiteral("while (Get-Process")) <
                script.indexOf(QStringLiteral("Start-Process -FilePath $package")));
        QVERIFY(script.contains(QStringLiteral("/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS")));
        QVERIFY(script.contains(QStringLiteral("$installer.ExitCode -ne 0")));
        const QString bootstrap = windowsAuthorizationScript(QStringLiteral("C:/Windows/powershell.exe"),
                                                             QStringLiteral("ZQBjAGgAbwA="), plan.workDirectory);
        QVERIFY(bootstrap.contains(QStringLiteral("-Verb RunAs")));
        QVERIFY(bootstrap.contains(QStringLiteral("Join-Path $work 'result'")));
        QVERIFY(bootstrap.contains(QStringLiteral("catch {")));
    }

    void handoffWaitCanBeInterrupted()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        bool handedOff = true;
        QString error;
        QThread *worker = QThread::create([&]() { handedOff = waitForHandoff(directory.path(), &error); });
        worker->start();
        QTest::qWait(30);
        QElapsedTimer timer;
        timer.start();
        worker->requestInterruption();
        const bool finished = worker->wait(2000);
        QVERIFY(finished);
        delete worker;
        QVERIFY(timer.elapsed() < 1500);
        QVERIFY(!handedOff);
        QVERIFY(!error.isEmpty());
        QVERIFY(QFile::exists(QDir(directory.path()).filePath(QStringLiteral("cancel"))));
    }

    void deniedSystemAuthorizationFailsPromptly()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX authorization wrappers are covered by Linux and macOS CI.");
#else
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QProcess wrapper;
        wrapper.start(QStringLiteral("/bin/sh"),
                      {QStringLiteral("-c"), authorizationWrapper(QStringLiteral("/usr/bin/false"), directory.path())});
        QVERIFY(wrapper.waitForFinished(3000));
        QElapsedTimer timer;
        timer.start();
        QString error;
        QVERIFY(!waitForHandoff(directory.path(), &error));
        QVERIFY(timer.elapsed() < 500);
        QVERIFY(QFile::exists(QDir(directory.path()).filePath(QStringLiteral("cancel"))));
#endif
    }

    void shellQuotingDoesNotExecutePayload()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX shell execution is covered by Linux and macOS CI.");
#else
        const QString value = QStringLiteral("space ' quote \" $HOME $(printf injected);\nnext line");
        QProcess process;
        process.start(QStringLiteral("/bin/sh"),
                      {QStringLiteral("-c"), QStringLiteral("printf %s ") + shellQuote(value)});
        QVERIFY(process.waitForFinished(3000));
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(QString::fromUtf8(process.readAllStandardOutput()), value);
#endif
    }

    void macInstallsAndRetainsPreviousApplication()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX shell execution is covered by Linux and macOS CI.");
#else
        MacFixture fixture;
        QVERIFY(fixture.initialize());
        QCOMPARE(runShell(fixture.script, fixture.arguments), 0);
        QCOMPARE(fixture.installedBytes(), QByteArray("new"));
        QVERIFY(QFile::exists(QDir(fixture.plan.workDirectory).filePath(QStringLiteral("ready"))));
        QVERIFY(fixture.log().contains("Previous application retained at:"));
        const QStringList backups =
            QDir(fixture.directory.path())
                .entryList({QStringLiteral("*.backup-*")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
        QCOMPARE(backups.size(), 1);
        QCOMPARE(readFixture(QDir(fixture.directory.path())
                                 .filePath(backups.first() + QStringLiteral("/Contents/MacOS/FluentSerialAssistant"))),
                 QByteArray("old"));
#endif
    }

    void macWaitTimeoutKeepsRunningApplication()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX shell execution is covered by Linux and macOS CI.");
#else
        MacFixture fixture;
        QVERIFY(fixture.initialize());
        fixture.arguments[0] = QString::number(QCoreApplication::applicationPid());
        QCOMPARE(runShell(fixture.script, fixture.arguments), 1);
        QCOMPARE(fixture.installedBytes(), QByteArray("old"));
        QVERIFY(fixture.log().contains("Application did not exit"));
#endif
    }

    void macCopyFailureDoesNotTouchOldApplication()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX shell execution is covered by Linux and macOS CI.");
#else
        MacFixture fixture;
        QVERIFY(fixture.initialize());
        QVERIFY(writeFixture(fixture.arguments.at(6), "#!/bin/sh\nexit 42\n", true));
        QCOMPARE(runShell(fixture.script, fixture.arguments), 42);
        QCOMPARE(fixture.installedBytes(), QByteArray("old"));
        QVERIFY(!QFile::exists(QDir(fixture.plan.workDirectory).filePath(QStringLiteral("ready"))));
#endif
    }

    void macRenameFailureRollsBack()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX shell execution is covered by Linux and macOS CI.");
#else
        MacFixture fixture;
        QVERIFY(fixture.initialize());
        const QString failingMove = QDir(fixture.directory.path()).filePath(QStringLiteral("mock-move"));
        QVERIFY(writeFixture(failingMove, R"SH(#!/bin/sh
case "$1" in *.update-*) exit 43;; esac
exec /bin/mv "$1" "$2"
)SH",
                             true));
        fixture.arguments[9] = failingMove;
        QCOMPARE(runShell(fixture.script, fixture.arguments), 43);
        QCOMPARE(fixture.installedBytes(), QByteArray("old"));
        QVERIFY(fixture.log().contains("Previous application restored."));
#endif
    }

    void macRejectsIdentityMismatchAndCancelledHandoff()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX shell execution is covered by Linux and macOS CI.");
#else
        MacFixture identity;
        QVERIFY(identity.initialize());
        QVERIFY(writeFixture(identity.arguments.at(8), "#!/bin/sh\nprintf 'another.application\\n'\n", true));
        QCOMPARE(runShell(identity.script, identity.arguments), 1);
        QCOMPARE(identity.installedBytes(), QByteArray("old"));
        QVERIFY(!QFile::exists(QDir(identity.plan.workDirectory).filePath(QStringLiteral("ready"))));

        MacFixture cancelled;
        QVERIFY(cancelled.initialize());
        QVERIFY(writeFixture(QDir(cancelled.plan.workDirectory).filePath(QStringLiteral("cancel")), "cancel"));
        QCOMPARE(runShell(cancelled.script, cancelled.arguments), 1);
        QCOMPARE(cancelled.installedBytes(), QByteArray("old"));
        QVERIFY(cancelled.log().contains("handoff was cancelled"));
#endif
    }

    void linuxChecksIdentityAndInvokesPackageManagerAfterParentExit()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX shell execution is covered by Linux and macOS CI.");
#else
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root(directory.path());
        InstallPlan plan;
        plan.packagePath = root.filePath(QStringLiteral("package's $data.deb"));
        plan.workDirectory = directory.path();
        plan.parentPid = 2147483647;
        plan.waitSeconds = 0;
        QVERIFY(writeFixture(plan.packagePath, "fake deb"));
        const QString script = root.filePath(QStringLiteral("install.sh"));
        const QString packageInfo = root.filePath(QStringLiteral("dpkg-deb"));
        const QString architecture = root.filePath(QStringLiteral("dpkg"));
        const QString install = root.filePath(QStringLiteral("apt-get"));
        const QString invoked = root.filePath(QStringLiteral("arguments"));
        QVERIFY(writeFixture(script, linuxInstallerScript().toUtf8()));
        QVERIFY(writeFixture(packageInfo, R"SH(#!/bin/sh
case "$3" in Package) echo fluent-serial-assistant;; Architecture) echo amd64;; esac
)SH",
                             true));
        QVERIFY(writeFixture(architecture, "#!/bin/sh\necho amd64\n", true));
        QVERIFY(writeFixture(
            install, (QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$@\" > ") + shellQuote(invoked)).toUtf8(), true));
        QStringList arguments = linuxInstallerArguments(plan);
        arguments[4] = packageInfo;
        arguments[5] = architecture;
        arguments[6] = install;
        QCOMPARE(runShell(script, arguments), 0);
        QCOMPARE(readFixture(invoked), QByteArray("--yes\n--no-remove\ninstall\n") + plan.packagePath.toUtf8() + '\n');
        QVERIFY(QFile::remove(invoked));
        arguments[0] = QString::number(QCoreApplication::applicationPid());
        QCOMPARE(runShell(script, arguments), 1);
        QVERIFY(!QFile::exists(invoked));
        arguments[0] = QString::number(plan.parentPid);
        QVERIFY(writeFixture(architecture, "#!/bin/sh\necho arm64\n", true));
        QCOMPARE(runShell(script, arguments), 1);
        QVERIFY(!QFile::exists(invoked));
#endif
    }
};

QTEST_GUILESS_MAIN(UpdateInstallerTest)
#include "tst_update_installer.moc"
