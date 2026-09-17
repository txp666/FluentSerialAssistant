#include "app/core/update_installer.h"

#include "app/core/app_i18n.h"
#include "app/core/update_installer_p.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QStorageInfo>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>

#ifdef Q_OS_WIN
#include <windows.h>

#include <shellapi.h>
#endif

namespace AppUpdate::InstallerDetail {

QString shellQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QString powerShellQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

static QString waitForParentScript()
{
    return QStringLiteral(R"SH(
test ! -e "$work/cancel" || fail 'Update handoff was cancelled.'
: > "$work/ready"
remaining="$wait_limit"
while kill -0 "$parent_pid" 2>/dev/null; do
    test ! -e "$work/cancel" || fail 'Update handoff was cancelled.'
    test "$remaining" -gt 0 || fail 'Application did not exit; installation was cancelled.'
    sleep 1
    remaining=$((remaining - 1))
done
test ! -e "$work/cancel" || fail 'Update handoff was cancelled.'
)SH");
}

QString macInstallerScript()
{
    return QStringLiteral(R"SH(#!/bin/sh
set -eu
umask 077
parent_pid=$1
package=$2
target=$3
work=$4
wait_limit=$5
hdiutil=$6
ditto=$7
codesign=$8
plistbuddy=$9
move=${10}
exec >> "$work/install.log" 2>&1
fail() {
    echo "ERROR: $*"
    printf '1\n' > "$work/result"
    /bin/chmod 644 "$work/result"
    exit 1
}
case "$parent_pid:$wait_limit" in *[!0-9:]*|:*|*:) fail 'Invalid process or timeout.';; esac
test "$parent_pid" -gt 1 || fail 'Invalid parent process.'
case "$target" in /*.app) ;; *) fail 'Invalid application destination.';; esac
test ! -L "$target" || fail 'Application destination must not be a symbolic link.'
test -f "$package" || fail 'Update package is missing.'
test -d "$work" || fail 'Update work directory is missing.'
mount="$work/mount"
stage="$(dirname "$target")/.$(basename "$target").update-$(basename "$work")"
backup="$(dirname "$target")/.$(basename "$target").backup-$(basename "$work")"
test ! -e "$stage" && test ! -L "$stage" || fail 'Staging path already exists.'
test ! -e "$backup" && test ! -L "$backup" || fail 'Backup path already exists.'
mounted=0
old_moved=0
new_installed=0
cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    if test "$status" -ne 0 && test "$old_moved" -eq 1 && test "$new_installed" -eq 0; then
        if test ! -e "$target" && test ! -L "$target" && "$move" "$backup" "$target"; then
            echo 'Previous application restored.'
        else
            echo "ERROR: Restore failed; previous application is preserved at $backup"
        fi
    fi
    if test "$mounted" -eq 1; then "$hdiutil" detach "$mount" -quiet || true; fi
    if test -d "$stage" && test ! -L "$stage"; then /bin/rm -rf "$stage"; fi
    echo "Installer exit status: $status"
    printf '%s\n' "$status" > "$work/result"
    /bin/chmod 644 "$work/result"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
if test -e "$target"; then
    test -d "$target" || fail 'Application destination is not a directory.'
    existing_id=$("$plistbuddy" -c 'Print :CFBundleIdentifier' "$target/Contents/Info.plist")
    test "$existing_id" = 'tech.zhangshu.FluentSerialAssistant' || fail 'Existing application identity does not match.'
fi
/bin/mkdir "$mount"
"$hdiutil" attach "$package" -mountpoint "$mount" -readonly -nobrowse -noautoopen -quiet
mounted=1
source="$mount/FluentSerialAssistant.app"
test -d "$source" && test ! -L "$source" || fail 'Disk image does not contain the expected application.'
bundle_id=$("$plistbuddy" -c 'Print :CFBundleIdentifier' "$source/Contents/Info.plist")
test "$bundle_id" = 'tech.zhangshu.FluentSerialAssistant' || fail 'Update application identity does not match.'
test -x "$source/Contents/MacOS/FluentSerialAssistant" || fail 'Update executable is missing.'
"$codesign" --verify --deep --strict "$source"
"$ditto" "$source" "$stage"
"$codesign" --verify --deep --strict "$stage"
"$hdiutil" detach "$mount" -quiet
mounted=0
)SH") + waitForParentScript() +
           QStringLiteral(R"SH(
test ! -L "$target" || fail 'Application destination changed during preparation.'
if test -e "$target"; then
    existing_id=$("$plistbuddy" -c 'Print :CFBundleIdentifier' "$target/Contents/Info.plist")
    test "$existing_id" = 'tech.zhangshu.FluentSerialAssistant' || fail 'Existing application identity changed.'
    "$move" "$target" "$backup"
    old_moved=1
fi
test ! -e "$target" && test ! -L "$target" || fail 'Application destination changed during installation.'
"$move" "$stage" "$target"
new_installed=1
echo "Update installed: $target"
if test "$old_moved" -eq 1; then echo "Previous application retained at: $backup"; fi
)SH");
}

QString linuxInstallerScript()
{
    return QStringLiteral(R"SH(#!/bin/sh
set -eu
umask 077
parent_pid=$1
package=$2
work=$3
wait_limit=$4
dpkg_deb=$5
dpkg=$6
apt_get=$7
exec >> "$work/install.log" 2>&1
fail() { echo "ERROR: $*"; exit 1; }
trap 'status=$?; printf "%s\n" "$status" > "$work/result"; /bin/chmod 644 "$work/result"' EXIT
case "$parent_pid:$wait_limit" in *[!0-9:]*|:*|*:) fail 'Invalid process or timeout.';; esac
test "$parent_pid" -gt 1 || fail 'Invalid parent process.'
test -f "$package" || fail 'Update package is missing.'
package_name=$("$dpkg_deb" --field "$package" Package)
test "$package_name" = 'fluent-serial-assistant' || fail 'DEB package identity does not match.'
package_arch=$("$dpkg_deb" --field "$package" Architecture)
system_arch=$("$dpkg" --print-architecture)
test "$package_arch" = "$system_arch" || fail 'DEB architecture does not match this system.'
)SH") + waitForParentScript() +
           QStringLiteral(R"SH(
export DEBIAN_FRONTEND=noninteractive
"$apt_get" --yes --no-remove install "$package"
echo 'Update installed successfully.'
)SH");
}

QString windowsInstallerScript(const InstallPlan &plan)
{
    return QStringLiteral(R"PS($ErrorActionPreference = 'Stop'
$package = %1
$target = %2
$work = %3
$parentId = %4
$waitLimit = %5
$logPath = Join-Path $work 'install.log'
try {
    if (!(Test-Path -LiteralPath $package -PathType Leaf)) { throw 'Update package is missing.' }
    if (Test-Path -LiteralPath (Join-Path $work 'cancel')) { throw 'Update handoff was cancelled.' }
    New-Item -ItemType File -Path (Join-Path $work 'ready') -Force | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds($waitLimit)
    while (Get-Process -Id $parentId -ErrorAction SilentlyContinue) {
        if (Test-Path -LiteralPath (Join-Path $work 'cancel')) { throw 'Update handoff was cancelled.' }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Application did not exit; installation was cancelled.' }
        Start-Sleep -Milliseconds 200
    }
    if (Test-Path -LiteralPath (Join-Path $work 'cancel')) { throw 'Update handoff was cancelled.' }
    $arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS /DIR="' + $target + '" /LOG="' + (Join-Path $work 'setup.log') + '"'
    $installer = Start-Process -FilePath $package -ArgumentList $arguments -Wait -PassThru
    if ($installer.ExitCode -ne 0) { throw ('Installer failed with exit code ' + $installer.ExitCode) }
    'Update installed successfully.' | Out-File -LiteralPath $logPath -Append -Encoding utf8
    0 | Out-File -LiteralPath (Join-Path $work 'result') -Encoding ascii
} catch {
    $_ | Out-File -LiteralPath $logPath -Append -Encoding utf8
    1 | Out-File -LiteralPath (Join-Path $work 'result') -Encoding ascii
    exit 1
}
)PS")
        .arg(powerShellQuote(QDir::toNativeSeparators(plan.packagePath)),
             powerShellQuote(QDir::toNativeSeparators(plan.targetPath)),
             powerShellQuote(QDir::toNativeSeparators(plan.workDirectory)), QString::number(plan.parentPid),
             QString::number(plan.waitSeconds));
}

QStringList macInstallerArguments(const InstallPlan &plan)
{
    return {QString::number(plan.parentPid),
            plan.packagePath,
            plan.targetPath,
            plan.workDirectory,
            QString::number(plan.waitSeconds),
            QStringLiteral("/usr/bin/hdiutil"),
            QStringLiteral("/usr/bin/ditto"),
            QStringLiteral("/usr/bin/codesign"),
            QStringLiteral("/usr/libexec/PlistBuddy"),
            QStringLiteral("/bin/mv")};
}

QString windowsAuthorizationScript(const QString &executable, const QString &encodedScript,
                                   const QString &workDirectory)
{
    return QStringLiteral(R"PS($ErrorActionPreference = 'Stop'
$work = %1
try {
    if (Test-Path -LiteralPath (Join-Path $work 'cancel')) { throw 'Update handoff was cancelled.' }
    Start-Process -FilePath %2 -Verb RunAs -ArgumentList %3 | Out-Null
} catch {
    $_ | Out-File -LiteralPath (Join-Path $work 'install.log') -Append -Encoding utf8
    1 | Out-File -LiteralPath (Join-Path $work 'result') -Encoding ascii
    exit 1
}
)PS")
        .arg(powerShellQuote(QDir::toNativeSeparators(workDirectory)),
             powerShellQuote(QDir::toNativeSeparators(executable)),
             powerShellQuote(QStringLiteral("-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand ") +
                             encodedScript));
}

QStringList linuxInstallerArguments(const InstallPlan &plan)
{
    return {QString::number(plan.parentPid),
            plan.packagePath,
            plan.workDirectory,
            QString::number(plan.waitSeconds),
            QStringLiteral("/usr/bin/dpkg-deb"),
            QStringLiteral("/usr/bin/dpkg"),
            QStringLiteral("/usr/bin/apt-get")};
}

QString authorizationWrapper(const QString &command, const QString &workDirectory)
{
    return command + QStringLiteral(" >>") + shellQuote(QDir(workDirectory).filePath(QStringLiteral("install.log"))) +
           QStringLiteral(" 2>&1; result=$?; if test \"$result\" -ne 0; then printf '%s\\n' \"$result\" >") +
           shellQuote(QDir(workDirectory).filePath(QStringLiteral("result"))) + QStringLiteral("; fi");
}

} // namespace AppUpdate::InstallerDetail

namespace {

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size() &&
           file.flush();
}

QString logFailure(const QString &message, const QString &work)
{
    QFile log(QDir(work).filePath(QStringLiteral("install.log")));
    QString detail;
    if (log.open(QIODevice::ReadOnly)) {
        detail = QString::fromUtf8(log.readAll()).right(2000).trimmed();
    }
    return AppI18n::text("%1\n安装日志：%2")
        .arg(detail.isEmpty() ? message : message + QLatin1Char('\n') + detail, log.fileName());
}

} // namespace

namespace AppUpdate::InstallerDetail {

bool waitForHandoff(const QString &work, QString *error)
{
    const QDir directory(work);
    QElapsedTimer timer;
    timer.start();
    // Includes the system authorization dialog and macOS package preparation.
    while (timer.elapsed() < 180000) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            break;
        }
        QFile result(directory.filePath(QStringLiteral("result")));
        if (result.open(QIODevice::ReadOnly) && result.readAll().trimmed() != QByteArray("0")) {
            break;
        }
        if (QFile::exists(directory.filePath(QStringLiteral("ready")))) {
            return true;
        }
        QThread::msleep(50);
    }
    writeFile(directory.filePath(QStringLiteral("cancel")), QByteArray("cancelled\n"));
    if (error) {
        *error = logFailure(AppI18n::text("未能准备更新安装，当前应用将保持运行。"), work);
    }
    return false;
}

} // namespace AppUpdate::InstallerDetail

namespace {

#ifndef Q_OS_WIN
QString shellCommand(const QString &program, const QStringList &arguments)
{
    QStringList words{AppUpdate::InstallerDetail::shellQuote(program)};
    for (const QString &argument : arguments) {
        words.append(AppUpdate::InstallerDetail::shellQuote(argument));
    }
    return words.join(QLatin1Char(' '));
}
#endif

} // namespace

namespace AppUpdate {

bool launchUpdateInstaller(const QString &packagePath, QString *error)
{
    if (error) {
        error->clear();
    }
    const auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    const QFileInfo package(packagePath);
    if (!package.isFile() || package.isSymLink() || package.size() <= 0) {
        return fail(AppI18n::text("更新安装包不存在或为空。"));
    }
#if defined(Q_OS_WIN)
    const QString suffix = QStringLiteral("exe");
#elif defined(Q_OS_MACOS)
    const QString suffix = QStringLiteral("dmg");
#elif defined(Q_OS_LINUX)
    const QString suffix = QStringLiteral("deb");
#else
    return fail(AppI18n::text("当前系统不支持自动安装更新。"));
#endif
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    if (package.suffix().compare(suffix, Qt::CaseInsensitive) != 0) {
        return fail(AppI18n::text("更新安装包格式与当前系统不匹配。"));
    }

    InstallerDetail::InstallPlan plan;
    plan.parentPid = QCoreApplication::applicationPid();
    plan.targetPath = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    QDir bundleDirectory(plan.targetPath);
    bundleDirectory.cdUp();
    bundleDirectory.cdUp();
    const QFileInfo bundle(bundleDirectory.absolutePath());
    if (!bundle.fileName().endsWith(QStringLiteral(".app")) ||
        !QFileInfo::exists(bundleDirectory.filePath(QStringLiteral("Contents/Info.plist")))) {
        return fail(AppI18n::text("请从已安装的应用程序启动后再更新。"));
    }
    plan.targetPath = bundle.canonicalFilePath();
    if (plan.targetPath.startsWith(QStringLiteral("/Volumes/")) || QStorageInfo(plan.targetPath).isReadOnly()) {
        plan.targetPath = QStringLiteral("/Applications/FluentSerialAssistant.app");
    }
#endif
    const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataDirectory.isEmpty() || !QDir::isAbsolutePath(dataDirectory)) {
        return fail(AppI18n::text("无法创建更新安装目录。"));
    }
    const QString base = QDir(dataDirectory).filePath(QStringLiteral("update-installers"));
    if (!QDir().mkpath(base)) {
        return fail(AppI18n::text("无法创建更新安装目录。"));
    }
    QTemporaryDir directory(QDir(base).filePath(QStringLiteral("install-XXXXXX")));
    if (!directory.isValid()) {
        return fail(AppI18n::text("无法创建更新安装目录。"));
    }
    plan.workDirectory = directory.path();
    plan.packagePath = QDir(plan.workDirectory).filePath(QStringLiteral("package.") + suffix);
    // Keep the verified package alive independently of the download object's
    // lifetime and the application's shutdown cleanup.
    if (!QFile::copy(package.canonicalFilePath(), plan.packagePath) ||
        QFileInfo(plan.packagePath).size() != package.size() ||
        !writeFile(QDir(plan.workDirectory).filePath(QStringLiteral("install.log")), {})) {
        return fail(AppI18n::text("无法准备更新安装文件。"));
    }
    directory.setAutoRemove(false);
    if (QThread::currentThread()->isInterruptionRequested()) {
        writeFile(QDir(plan.workDirectory).filePath(QStringLiteral("cancel")), QByteArray("cancelled\n"));
        return fail(AppI18n::text("未能准备更新安装，当前应用将保持运行。"));
    }

#ifdef Q_OS_WIN
    const QString powerShell = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows")))
                                   .filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
    const auto encodedCommand = [](const QString &script) {
        QByteArray utf16;
        utf16.reserve(script.size() * 2);
        for (QChar character : script) {
            utf16.append(static_cast<char>(character.unicode() & 0xff));
            utf16.append(static_cast<char>(character.unicode() >> 8));
        }
        return QString::fromLatin1(utf16.toBase64());
    };
    const QString bootstrap = InstallerDetail::windowsAuthorizationScript(
        powerShell, encodedCommand(InstallerDetail::windowsInstallerScript(plan)), plan.workDirectory);
    const QString parameters = QStringLiteral("-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand ") +
                               encodedCommand(bootstrap);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    // The external bootstrap owns the UAC dialog. Keeping authorization outside
    // this worker lets application shutdown interrupt the ready handshake.
    info.lpVerb = L"open";
    const std::wstring executable = QDir::toNativeSeparators(powerShell).toStdWString();
    const std::wstring arguments = parameters.toStdWString();
    info.lpFile = executable.c_str();
    info.lpParameters = arguments.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        return fail(logFailure(AppI18n::text("系统未授权启动更新安装程序。"), plan.workDirectory));
    }
    if (info.hProcess) {
        CloseHandle(info.hProcess);
    }
#else
    const QString scriptPath = QDir(plan.workDirectory).filePath(QStringLiteral("install.sh"));
    QString command;
#ifdef Q_OS_MACOS
    if (!writeFile(scriptPath, InstallerDetail::macInstallerScript().toUtf8())) {
        return fail(AppI18n::text("无法创建更新安装脚本。"));
    }
    QStringList arguments{scriptPath};
    arguments.append(InstallerDetail::macInstallerArguments(plan));
    command = shellCommand(QStringLiteral("/bin/sh"), arguments);
    if (!QFileInfo(QFileInfo(plan.targetPath).absolutePath()).isWritable()) {
        const QString background = QStringLiteral("/usr/bin/nohup ") + command + QStringLiteral(" >/dev/null 2>&1 &");
        QString escaped = background;
        escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        command = shellCommand(QStringLiteral("/usr/bin/osascript"),
                               {QStringLiteral("-e"), QStringLiteral("do shell script \"") + escaped +
                                                          QStringLiteral("\" with administrator privileges")});
    }
#else
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty() || !QFileInfo::exists(QStringLiteral("/usr/bin/apt-get"))) {
        return fail(AppI18n::text("自动安装需要系统提供 pkexec 和 apt-get，请使用系统软件安装器安装已下载的 DEB。"));
    }
    if (!writeFile(scriptPath, InstallerDetail::linuxInstallerScript().toUtf8())) {
        return fail(AppI18n::text("无法创建更新安装脚本。"));
    }
    QStringList arguments{QStringLiteral("/bin/sh"), scriptPath};
    arguments.append(InstallerDetail::linuxInstallerArguments(plan));
    command = shellCommand(pkexec, arguments);
#endif
    const QString wrapper = InstallerDetail::authorizationWrapper(command, plan.workDirectory);
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), wrapper}, plan.workDirectory)) {
        return fail(logFailure(AppI18n::text("无法启动更新安装程序。"), plan.workDirectory));
    }
#endif
    return InstallerDetail::waitForHandoff(plan.workDirectory, error);
#endif
}

} // namespace AppUpdate
