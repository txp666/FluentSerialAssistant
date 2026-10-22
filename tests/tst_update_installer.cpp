#include "app/core/update_installer.h"
#include "app/core/update_installer_p.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtTest/QTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

QByteArray sha256Of(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

int runProcess(const QString &program, const QStringList &arguments, int timeout = 10000)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(3000) || !process.waitForFinished(timeout)) {
        process.kill();
        process.waitForFinished(3000);
        return -1;
    }
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

// Runs the script the way the installer does: inline, never from a file.
int runShell(const QString &script, const QStringList &arguments)
{
    return runProcess(QStringLiteral("/bin/sh"),
                      QStringList{QStringLiteral("-c"), script, QStringLiteral("sh")} + arguments);
}

QString shellWords(const QString &program, const QStringList &arguments)
{
    QStringList words{shellQuote(program)};
    for (const QString &argument : arguments) {
        words.append(shellQuote(argument));
    }
    return words.join(QLatin1Char(' '));
}

// Stands in for shasum and sha256sum, whichever the host has.
const QByteArray digestTool = R"SH(#!/bin/sh
if command -v sha256sum >/dev/null 2>&1; then exec sha256sum; fi
exec shasum -a 256
)SH";

struct MacFixture
{
    QTemporaryDir directory;
    InstallPlan plan;
    QString script;
    QStringList arguments;
    QString attached;

    bool initialize()
    {
        if (!directory.isValid()) {
            return false;
        }
        // These names intentionally contain shell metacharacters and spaces.
        const QDir root(directory.path());
        plan.workDirectory = root.filePath(QStringLiteral("work's $data; safe"));
        plan.stateParent = root.filePath(QStringLiteral("state's $parent"));
        plan.packagePath = root.filePath(QStringLiteral("package's $(touch INJECTED).dmg"));
        plan.targetPath = root.filePath(QStringLiteral("App's $name; test.app"));
        plan.sha256 = sha256Of("fake disk image");
        plan.parentPid = 2147483647; // no live process is signalled or terminated
        plan.waitSeconds = 0;
        if (!QDir().mkpath(plan.workDirectory) || !QDir().mkpath(plan.stateParent) ||
            !QDir().mkpath(QDir(plan.targetPath).filePath(QStringLiteral("Contents/MacOS"))) ||
            !writeFixture(plan.packagePath, "fake disk image") ||
            !writeFixture(QDir(plan.targetPath).filePath(QStringLiteral("Contents/Info.plist")), "old plist") ||
            !writeFixture(QDir(plan.targetPath).filePath(QStringLiteral("Contents/MacOS/FluentSerialAssistant")), "old",
                          true)) {
            return false;
        }
        script = macInstallerScript();
        attached = root.filePath(QStringLiteral("attached"));
        const QString mountTool = root.filePath(QStringLiteral("mock-hdiutil"));
        const QString copyTool = root.filePath(QStringLiteral("mock-ditto"));
        const QString signTool = root.filePath(QStringLiteral("mock-codesign"));
        const QString plistTool = root.filePath(QStringLiteral("mock-plistbuddy"));
        const QString hashTool = root.filePath(QStringLiteral("mock-shasum"));
        if (!writeFixture(mountTool,
                          (R"SH(#!/bin/sh
set -eu
if test "$1" = attach; then
    printf '%s' "$2" > )SH" +
                           shellQuote(attached).toUtf8() +
                           R"SH(
    /bin/mkdir -p "$4/FluentSerialAssistant.app/Contents/MacOS"
    printf new > "$4/FluentSerialAssistant.app/Contents/MacOS/FluentSerialAssistant"
    /bin/chmod 700 "$4/FluentSerialAssistant.app/Contents/MacOS/FluentSerialAssistant"
    printf plist > "$4/FluentSerialAssistant.app/Contents/Info.plist"
fi
)SH"),
                          true) ||
            !writeFixture(copyTool, "#!/bin/sh\nexec /bin/cp -R \"$1\" \"$2\"\n", true) ||
            !writeFixture(signTool, "#!/bin/sh\nexit 0\n", true) ||
            !writeFixture(plistTool, "#!/bin/sh\nprintf 'tech.zhangshu.FluentSerialAssistant\\n'\n", true) ||
            !writeFixture(hashTool, digestTool, true)) {
            return false;
        }
        arguments = macInstallerArguments(plan);
        arguments[5] = mountTool;
        arguments[6] = copyTool;
        arguments[7] = signTool;
        arguments[8] = plistTool;
        if (!QFile::exists(arguments.at(10))) {
            arguments[10] = hashTool;
        }
        return true;
    }

    QByteArray installedBytes() const
    {
        return readFixture(QDir(plan.targetPath).filePath(QStringLiteral("Contents/MacOS/FluentSerialAssistant")));
    }

    QString state() const { return stateDirectory(plan.workDirectory, plan.stateParent); }
    QByteArray log() const { return readFixture(QDir(state()).filePath(QStringLiteral("install.log"))); }
    bool ready() const { return !state().isEmpty() && QFile::exists(QDir(state()).filePath(QStringLiteral("ready"))); }
};

#ifdef Q_OS_WIN
bool isElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool queried = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return queried && elevation.TokenIsElevated;
}

QString powerShellExecutable()
{
    return QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows")))
        .filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
}

// A copy of whoami.exe, which refuses the setup arguments, stands in for the setup program.
struct WindowsFixture
{
    QTemporaryDir directory;
    InstallPlan plan;

    bool initialize(bool matches)
    {
        if (!directory.isValid()) {
            return false;
        }
        const QDir root(directory.path());
        plan.workDirectory = root.filePath(QStringLiteral("work's $data"));
        plan.stateParent = root.filePath(QStringLiteral("state"));
        plan.packagePath = QDir(plan.workDirectory).filePath(QStringLiteral("package.exe"));
        plan.targetPath = root.filePath(QStringLiteral("target"));
        plan.parentPid = 2147483647;
        plan.waitSeconds = 0;
        const QString whoami = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows")))
                                   .filePath(QStringLiteral("System32/whoami.exe"));
        if (!QDir().mkpath(plan.workDirectory) || !QDir().mkpath(plan.stateParent) ||
            !QFile::copy(whoami, plan.packagePath)) {
            return false;
        }
        plan.sha256 = matches ? sha256Of(readFixture(plan.packagePath)) : QByteArray(64, '0');
        return true;
    }

    QString state() const { return stateDirectory(plan.workDirectory, plan.stateParent); }
    QByteArray read(const QString &name) const { return readFixture(QDir(state()).filePath(name)); }
};

QByteArray runPowerShell(const QString &script, int *exitCode = nullptr)
{
    // The same command line the elevated installer gets.
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setProgram(powerShellExecutable());
    process.setNativeArguments(windowsInstallerCommand(script));
    process.start();
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished(3000);
    }
    if (exitCode) {
        *exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    }
    return process.readAll();
}
#endif

} // namespace

class UpdateInstallerTest : public QObject
{
    Q_OBJECT

  private slots:
    void rejectsMissingAndWrongPackageWithoutLaunching()
    {
        const QByteArray digest(64, 'a');
        QString error;
        QVERIFY(!AppUpdate::launchUpdateInstaller(QStringLiteral("/not/a/real/update-package"), digest, &error));
        QVERIFY(!error.isEmpty());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString file = QDir(directory.path()).filePath(QStringLiteral("package.txt"));
        QVERIFY(writeFixture(file, "not an installer"));
        QVERIFY(!AppUpdate::launchUpdateInstaller(file, digest, &error));
        QVERIFY(!error.isEmpty());
        // Without the release digest the installer could not check its copy.
        error.clear();
        QVERIFY(!AppUpdate::launchUpdateInstaller(file, QByteArray("not a digest"), &error));
        QVERIFY(!error.isEmpty());
    }

    void powerShellArgumentsAreLiteralAndWaitForExit()
    {
        InstallPlan plan;
        plan.packagePath = QStringLiteral("C:/temp/a's $var; %4/setup.exe");
        plan.sha256 = QByteArray(64, 'b');
        plan.targetPath = QStringLiteral("C:/Program Files/App's name");
        plan.workDirectory = QStringLiteral("C:/temp/a b");
        plan.stateParent = QStringLiteral("C:/ProgramData");
        plan.parentPid = 34567;
        plan.waitSeconds = 120;
        const QString script = windowsInstallerScript(plan);
        QVERIFY(script.contains(QStringLiteral("a''s $var; %4")));
        QVERIFY(script.contains(QStringLiteral("$expected = '") + QString(64, QLatin1Char('b')) + QLatin1Char('\'')));
        QVERIFY(script.contains(QStringLiteral("$stateParent = ") +
                                powerShellQuote(QDir::toNativeSeparators(plan.stateParent))));
        QVERIFY(script.contains(QStringLiteral("$parentId = 34567")));
        QVERIFY(script.contains(QStringLiteral("$waitLimit = 120")));
        // The copy is checked before the handoff and is what runs once the application exits.
        QVERIFY(script.indexOf(QStringLiteral("Get-FileHash -LiteralPath $verified")) <
                script.indexOf(QStringLiteral("'ready'")));
        QVERIFY(script.indexOf(QStringLiteral("while (Get-Process")) <
                script.indexOf(QStringLiteral("Start-Process -FilePath $verified")));
        QVERIFY(!script.contains(QStringLiteral("-FilePath $package")));
        QVERIFY(script.contains(QStringLiteral("/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS")));
        QVERIFY(script.contains(QStringLiteral("$installer.ExitCode -ne 0")));
        const QString bootstrap =
            windowsAuthorizationScript(QStringLiteral("C:/Windows/powershell.exe"),
                                       windowsInstallerCommand(QStringLiteral("echo 'x'")), plan.workDirectory);
        QVERIFY(bootstrap.contains(QStringLiteral("-Verb RunAs")));
        QVERIFY(bootstrap.contains(QStringLiteral("FromBase64String(''ZWNobyAneCc='')")));
        QVERIFY(bootstrap.contains(QStringLiteral("Join-Path $work 'result'")));
        QVERIFY(bootstrap.contains(QStringLiteral("catch {")));
    }

    void powerShellCommandFitsOnTheCommandLine()
    {
        const QString script = QStringLiteral("'Grüße' + \"$x\"\n");
        const QByteArray decoded = QByteArray::fromBase64(powerShellEncodedCommand(script).toLatin1());
        QCOMPARE(QString::fromUtf16(reinterpret_cast<const char16_t *>(decoded.constData()), decoded.size() / 2),
                 script);

        // The installer command travels inside the encoded bootstrap; Windows allows 32767
        // characters on a command line.
        InstallPlan plan;
        const QString deep = QStringLiteral("C:/Users/") + QString(40, QLatin1Char('u')) +
                             QStringLiteral("/AppData/Local/FluentSerialAssistant/update-installers/install-abcdef");
        plan.packagePath = deep + QStringLiteral("/package.exe");
        plan.sha256 = QByteArray(64, 'c');
        plan.targetPath = QStringLiteral("C:/Program Files/FluentSerialAssistant");
        plan.workDirectory = deep;
        plan.stateParent = QStringLiteral("C:/ProgramData");
        plan.parentPid = 4294967;
        const QString powerShell = QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe");
        const QString bootstrap =
            windowsAuthorizationScript(powerShell, windowsInstallerCommand(windowsInstallerScript(plan)), deep);
        const QString commandLine =
            powerShell + QStringLiteral(" -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand ") +
            powerShellEncodedCommand(bootstrap);
        QVERIFY2(commandLine.size() < 20000, qPrintable(QString::number(commandLine.size())));
    }

    void handoffWaitCanBeInterrupted()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        bool handedOff = true;
        QString error;
        QThread *worker =
            QThread::create([&]() { handedOff = waitForHandoff(directory.path(), directory.path(), &error); });
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

    void handoffReadsOnlyTheInstallersDirectory()
    {
        QTemporaryDir work;
        QTemporaryDir stateParent;
        QVERIFY(work.isValid() && stateParent.isValid());
        const QDir root(work.path());
        const QDir parent(stateParent.path());
        // The work directory belongs to the user: what is in it decides nothing by itself.
        QVERIFY(writeFixture(root.filePath(QStringLiteral("ready")), {}));
        QVERIFY(root.mkdir(QStringLiteral("state.elsewhere")));
        QVERIFY(parent.mkpath(QStringLiteral("elsewhere")));
        QVERIFY(writeFixture(parent.filePath(QStringLiteral("elsewhere/ready")), {}));
        QVERIFY(stateDirectory(work.path(), stateParent.path()).isEmpty());
        QThread *worker = QThread::create([&]() { waitForHandoff(work.path(), stateParent.path(), nullptr); });
        worker->start();
        QTest::qWait(150);
        QVERIFY(worker->isRunning());
        worker->requestInterruption();
        QVERIFY(worker->wait(2000));
        delete worker;
        QVERIFY(QFile::remove(root.filePath(QStringLiteral("cancel"))));

        const QString name = QStringLiteral("fluentserial-update.Ab12Cd");
        QVERIFY(root.mkdir(QStringLiteral("state.") + name));
        QVERIFY(parent.mkdir(name));
        QCOMPARE(stateDirectory(work.path(), stateParent.path()), parent.filePath(name));
        QVERIFY(writeFixture(parent.filePath(name + QStringLiteral("/ready")), {}));
        QString error;
        QVERIFY2(waitForHandoff(work.path(), stateParent.path(), &error), qPrintable(error));

        // A failure the installer reports in its directory ends the wait at once, with its log.
        QVERIFY(QFile::remove(parent.filePath(name + QStringLiteral("/ready"))));
        QVERIFY(writeFixture(parent.filePath(name + QStringLiteral("/result")), "1\n"));
        QVERIFY(writeFixture(parent.filePath(name + QStringLiteral("/install.log")), "ERROR: from the installer\n"));
        QElapsedTimer timer;
        timer.start();
        QVERIFY(!waitForHandoff(work.path(), stateParent.path(), &error));
        QVERIFY(timer.elapsed() < 500);
        QVERIFY(error.contains(QStringLiteral("ERROR: from the installer")));
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
        QVERIFY(!waitForHandoff(directory.path(), directory.path(), &error));
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
        QVERIFY(fixture.ready());
        QCOMPARE(readFixture(QDir(fixture.state()).filePath(QStringLiteral("result"))), QByteArray("0\n"));
        QVERIFY(fixture.log().contains("Previous application retained at:"));
        // Only the checked copy was mounted, and it is gone afterwards.
        const QString verified = QDir(fixture.state()).filePath(QStringLiteral("package.dmg"));
        QCOMPARE(QString::fromUtf8(readFixture(fixture.attached)), verified);
        QVERIFY(!QFile::exists(verified));
        QVERIFY(!QFile::exists(QDir(fixture.directory.path()).filePath(QStringLiteral("INJECTED"))));
        const QStringList backups =
            QDir(fixture.directory.path())
                .entryList({QStringLiteral("*.backup-*")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
        QCOMPARE(backups.size(), 1);
        QCOMPARE(readFixture(QDir(fixture.directory.path())
                                 .filePath(backups.first() + QStringLiteral("/Contents/MacOS/FluentSerialAssistant"))),
                 QByteArray("old"));
#endif
    }

    void macRejectsPackageThatDoesNotMatchTheRelease()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX shell execution is covered by Linux and macOS CI.");
#else
        MacFixture fixture;
        QVERIFY(fixture.initialize());
        fixture.arguments[11] = QString(64, QLatin1Char('0'));
        QCOMPARE(runShell(fixture.script, fixture.arguments), 1);
        QCOMPARE(fixture.installedBytes(), QByteArray("old"));
        QVERIFY(fixture.log().contains("Update package does not match the release."));
        QCOMPARE(readFixture(QDir(fixture.state()).filePath(QStringLiteral("result"))), QByteArray("1\n"));
        QVERIFY(!fixture.ready());
        QVERIFY(!QFile::exists(fixture.attached));
        QVERIFY(!QFile::exists(QDir(fixture.state()).filePath(QStringLiteral("package.dmg"))));

        // Nothing the installer could later trust is made from a malformed digest.
        MacFixture malformed;
        QVERIFY(malformed.initialize());
        malformed.arguments[11] = QStringLiteral("$(touch INJECTED)") + QString(47, QLatin1Char('0'));
        QCOMPARE(runShell(malformed.script, malformed.arguments), 1);
        QVERIFY(malformed.state().isEmpty());
        QCOMPARE(malformed.installedBytes(), QByteArray("old"));
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
        QVERIFY(!fixture.ready());
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
        QVERIFY(!identity.ready());

        MacFixture cancelled;
        QVERIFY(cancelled.initialize());
        QVERIFY(writeFixture(QDir(cancelled.plan.workDirectory).filePath(QStringLiteral("cancel")), "cancel"));
        QCOMPARE(runShell(cancelled.script, cancelled.arguments), 1);
        QCOMPARE(cancelled.installedBytes(), QByteArray("old"));
        QVERIFY(cancelled.log().contains("handoff was cancelled"));
#endif
    }

    void macScriptSurvivesAppleScript()
    {
#ifndef Q_OS_MACOS
        QSKIP("osascript is only on macOS.");
#else
        // The same quoting as the administrator prompt, without asking for the password.
        MacFixture fixture;
        QVERIFY(fixture.initialize());
        const QString command =
            shellWords(QStringLiteral("/bin/sh"),
                       QStringList{QStringLiteral("-c"), fixture.script, QStringLiteral("sh")} + fixture.arguments);
        QCOMPARE(runProcess(QStringLiteral("/usr/bin/osascript"), appleScriptArguments(command, false), 30000), 0);
        QCOMPARE(fixture.installedBytes(), QByteArray("new"));
        QVERIFY(fixture.ready());
        QVERIFY(appleScriptArguments(command, true).last().endsWith(QStringLiteral("with administrator privileges")));
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
        plan.workDirectory = root.filePath(QStringLiteral("work"));
        plan.stateParent = directory.path();
        plan.sha256 = sha256Of("fake deb");
        plan.parentPid = 2147483647;
        plan.waitSeconds = 0;
        QVERIFY(QDir().mkpath(plan.workDirectory));
        QVERIFY(writeFixture(plan.packagePath, "fake deb"));
        const QString script = linuxInstallerScript();
        const QString packageInfo = root.filePath(QStringLiteral("dpkg-deb"));
        const QString architecture = root.filePath(QStringLiteral("dpkg"));
        const QString install = root.filePath(QStringLiteral("apt-get"));
        const QString hash = root.filePath(QStringLiteral("sha256sum"));
        const QString invoked = root.filePath(QStringLiteral("arguments"));
        QVERIFY(writeFixture(packageInfo, R"SH(#!/bin/sh
case "$3" in Package) echo fluent-serial-assistant;; Architecture) echo amd64;; esac
)SH",
                             true));
        QVERIFY(writeFixture(architecture, "#!/bin/sh\necho amd64\n", true));
        QVERIFY(writeFixture(
            install, (QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$@\" > ") + shellQuote(invoked)).toUtf8(), true));
        QVERIFY(writeFixture(hash, digestTool, true));
        QStringList arguments = linuxInstallerArguments(plan);
        arguments[4] = packageInfo;
        arguments[5] = architecture;
        arguments[6] = install;
        arguments[7] = hash;
        QCOMPARE(runShell(script, arguments), 0);
        // The package manager gets the checked copy, never the file in the user's directory.
        const QString state = stateDirectory(plan.workDirectory, plan.stateParent);
        QVERIFY(!state.isEmpty());
        QCOMPARE(readFixture(invoked), QByteArray("--yes\n--no-remove\ninstall\n") +
                                           QDir(state).filePath(QStringLiteral("package.deb")).toUtf8() + '\n');
        QVERIFY(QFile::exists(QDir(state).filePath(QStringLiteral("ready"))));
        QCOMPARE(readFixture(QDir(state).filePath(QStringLiteral("result"))), QByteArray("0\n"));
        QVERIFY(QFile::remove(invoked));

        arguments[0] = QString::number(QCoreApplication::applicationPid());
        QCOMPARE(runShell(script, arguments), 1);
        QVERIFY(!QFile::exists(invoked));
        arguments[0] = QString::number(plan.parentPid);
        arguments[8] = QString(64, QLatin1Char('d'));
        QCOMPARE(runShell(script, arguments), 1);
        QVERIFY(!QFile::exists(invoked));
        arguments[8] = QString::fromLatin1(plan.sha256);
        QVERIFY(writeFixture(architecture, "#!/bin/sh\necho arm64\n", true));
        QCOMPARE(runShell(script, arguments), 1);
        QVERIFY(!QFile::exists(invoked));
#endif
    }

    void windowsInstallerCommandRunsTheScriptAsWritten()
    {
#ifndef Q_OS_WIN
        QSKIP("The PowerShell installer runs on Windows.");
#else
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString output = QDir(directory.path()).filePath(QStringLiteral("out's Grüße.txt"));
        const QString text = QStringLiteral("Grüße \"quoted\" $(1+1) %1");
        const QString script = QStringLiteral("$ErrorActionPreference = 'Stop'\n") + powerShellQuote(text) +
                               QStringLiteral(" | Out-File -LiteralPath ") +
                               powerShellQuote(QDir::toNativeSeparators(output)) +
                               QStringLiteral(" -Encoding utf8\ntry { throw 'stop' } catch { exit 3 }\n");
        int exitCode = 0;
        const QByteArray log = runPowerShell(script, &exitCode);
        QVERIFY2(exitCode == 3, log.constData());
        QString written = QString::fromUtf8(readFixture(output));
        written.remove(QChar(0xfeff));
        QCOMPARE(written.trimmed(), text);
#endif
    }

    void windowsBootstrapHandsOffToTheElevatedInstaller()
    {
#ifndef Q_OS_WIN
        QSKIP("The PowerShell installer runs on Windows.");
#else
        // From an elevated process RunAs asks for no consent, so the whole chain runs unattended.
        if (!isElevated()) {
            QSKIP("Without elevation this would ask for consent; run this test elevated.");
        }
        WindowsFixture fixture;
        QVERIFY(fixture.initialize(true));
        const QString bootstrap = windowsAuthorizationScript(
            powerShellExecutable(), windowsInstallerCommand(windowsInstallerScript(fixture.plan)),
            fixture.plan.workDirectory);
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(powerShellExecutable(), {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                                               QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                                               QStringLiteral("-EncodedCommand"), powerShellEncodedCommand(bootstrap)});
        QVERIFY(process.waitForFinished(60000));
        QVERIFY2(process.exitCode() == 0, process.readAll().constData());
        QString error;
        QVERIFY2(waitForHandoff(fixture.plan.workDirectory, fixture.plan.stateParent, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.read(QStringLiteral("result")).isEmpty(), 60000);
        QVERIFY2(fixture.read(QStringLiteral("install.log")).contains("Installer failed with exit code"),
                 fixture.read(QStringLiteral("install.log")).constData());
#endif
    }

    void windowsInstallerChecksItsOwnCopy_data()
    {
        QTest::addColumn<bool>("matches");
        QTest::newRow("matching") << true;
        QTest::newRow("altered") << false;
    }
    void windowsInstallerChecksItsOwnCopy()
    {
#ifndef Q_OS_WIN
        QSKIP("The PowerShell installer runs on Windows.");
#else
        if (!isElevated()) {
            QSKIP("The installer runs as an administrator; run this test elevated.");
        }
        QFETCH(bool, matches);
        WindowsFixture fixture;
        QVERIFY(fixture.initialize(matches));
        int exitCode = 0;
        const QByteArray output = runPowerShell(windowsInstallerScript(fixture.plan), &exitCode);
        QVERIFY2(exitCode == 1, output.constData());
        const QString state = fixture.state();
        QVERIFY2(!state.isEmpty(), output.constData());
        const QByteArray log = fixture.read(QStringLiteral("install.log"));
        QCOMPARE(fixture.read(QStringLiteral("result")).trimmed(), QByteArray("1"));
        QVERIFY(!QFile::exists(QDir(state).filePath(QStringLiteral("package/setup.exe"))));
        if (matches) {
            QVERIFY2(log.contains("Installer failed with exit code"), log.constData());
            QVERIFY(QFile::exists(QDir(state).filePath(QStringLiteral("ready"))));
        } else {
            QVERIFY2(log.contains("Update package does not match the release."), log.constData());
            QVERIFY(!QFile::exists(QDir(state).filePath(QStringLiteral("ready"))));
        }

        // Users may read the log and the results but change nothing, and may not read the copy.
        const QString rules =
            QStringLiteral(R"PS($ErrorActionPreference = 'Stop'
foreach ($path in @(%1, %2)) {
    $acl = Get-Acl -LiteralPath $path
    $owner = $acl.GetOwner([System.Security.Principal.SecurityIdentifier]).Value
    $entries = $acl.Access | ForEach-Object { $_.IdentityReference.Translate([System.Security.Principal.SecurityIdentifier]).Value + '=' + $_.FileSystemRights }
    'owner=' + $owner + ' protected=' + $acl.AreAccessRulesProtected + ' ' + (($entries | Sort-Object) -join ' ')
}
)PS")
                .arg(powerShellQuote(QDir::toNativeSeparators(state)),
                     powerShellQuote(QDir::toNativeSeparators(QDir(state).filePath(QStringLiteral("package")))));
        const QList<QByteArray> lines = runPowerShell(rules, &exitCode).trimmed().split('\n');
        QCOMPARE(exitCode, 0);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0).trimmed(),
                 QByteArray("owner=S-1-5-32-544 protected=True S-1-5-18=FullControl "
                            "S-1-5-32-544=FullControl S-1-5-32-545=ReadAndExecute, Synchronize"));
        QCOMPARE(lines.at(1).trimmed(),
                 QByteArray("owner=S-1-5-32-544 protected=True S-1-5-18=FullControl S-1-5-32-544=FullControl"));
#endif
    }
};

QTEST_GUILESS_MAIN(UpdateInstallerTest)
#include "tst_update_installer.moc"
