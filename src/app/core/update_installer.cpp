#include "app/core/update_installer.h"

#include "app/core/app_i18n.h"
#include "app/core/update_installer_p.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QStorageInfo>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>

#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>
#endif

// The installer may run as root or as an administrator, while the work directory, where this
// process left the package, belongs to the user. Another process of the user could replace the
// package or anything else in it at any time, also with a link pointing elsewhere. So the
// installer only reads from the work directory: it makes a directory of its own under a random
// name, which the user cannot change, copies the package there and checks the copy against the
// release digest before installing it, and keeps its log and results there too. An empty
// directory "state.<name>" in the work directory tells this process where that is.

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

QString powerShellEncodedCommand(const QString &script)
{
    QByteArray utf16;
    utf16.reserve(script.size() * 2);
    for (QChar character : script) {
        utf16.append(static_cast<char>(character.unicode() & 0xff));
        utf16.append(static_cast<char>(character.unicode() >> 8));
    }
    return QString::fromLatin1(utf16.toBase64());
}

// Shared by the macOS and Linux scripts: makes the installer's own directory, which only it can
// change, and leaves the pointer to it in the work directory.
static QString posixStateScript()
{
    return QStringLiteral(R"SH(
case "$work" in /?*) ;; *) echo 'Invalid work directory.'; exit 1;; esac
# Until the app can find the installer's own directory through the pointer to it, an empty
# directory "failed" in the work directory is the only failure the installer can report there:
# writing a file could follow a link put in its place.
abort() {
    echo "ERROR: $*"
    /bin/mkdir "$work/failed" 2>/dev/null || true
    exit 1
}
case "$parent_pid:$wait_limit" in *[!0-9:]*|:*|*:) abort 'Invalid process or timeout.';; esac
test "$parent_pid" -gt 1 || abort 'Invalid parent process.'
case "$expected_sha" in *[!0-9a-f]*) abort 'Invalid package digest.';; esac
test "${#expected_sha}" -eq 64 || abort 'Invalid package digest.'
state=$(mktemp -d "$state_parent/fluentserial-update.XXXXXX") || abort 'Cannot create the installer directory.'
/bin/chmod 755 "$state" || abort 'Cannot create the installer directory.'
exec >> "$state/install.log" 2>&1
)SH");
}

// Needs $verified, fail() and digest(), and the trap that reports the result.
static QString posixVerifyScript()
{
    return QStringLiteral(R"SH(
/bin/mkdir "$work/state.${state##*/}" || abort 'Cannot point to the installer directory.'
test -f "$package" || fail 'Update package is missing.'
# The package may be a link to a file only root can read; the copy stays private.
(umask 077 && /bin/cp "$package" "$verified")
actual=$(digest < "$verified")
actual=${actual%% *}
test "$actual" = "$expected_sha" || fail 'Update package does not match the release.'
)SH");
}

static QString waitForParentScript()
{
    return QStringLiteral(R"SH(
test ! -e "$work/cancel" || fail 'Update handoff was cancelled.'
: > "$state/ready"
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
    return QStringLiteral(R"SH(set -eu
umask 022
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
shasum=${11}
expected_sha=${12}
state_parent=${13}
)SH") + posixStateScript() +
           QStringLiteral(R"SH(
fail() {
    echo "ERROR: $*"
    exit 1
}
digest() { "$shasum" -a 256; }
mount="$state/mount"
verified="$state/package.dmg"
stage="$(dirname "$target")/.$(basename "$target").update-${state##*/}"
backup="$(dirname "$target")/.$(basename "$target").backup-${state##*/}"
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
    /bin/rm -f "$verified"
    echo "Installer exit status: $status"
    printf '%s\n' "$status" > "$state/result"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
)SH") + posixVerifyScript() +
           QStringLiteral(R"SH(
case "$target" in /*.app) ;; *) fail 'Invalid application destination.';; esac
test ! -L "$target" || fail 'Application destination must not be a symbolic link.'
test ! -e "$stage" && test ! -L "$stage" || fail 'Staging path already exists.'
test ! -e "$backup" && test ! -L "$backup" || fail 'Backup path already exists.'
if test -e "$target"; then
    test -d "$target" || fail 'Application destination is not a directory.'
    existing_id=$("$plistbuddy" -c 'Print :CFBundleIdentifier' "$target/Contents/Info.plist")
    test "$existing_id" = 'tech.zhangshu.FluentSerialAssistant' || fail 'Existing application identity does not match.'
fi
/bin/mkdir "$mount"
"$hdiutil" attach "$verified" -mountpoint "$mount" -readonly -nobrowse -noautoopen -quiet
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
    return QStringLiteral(R"SH(set -eu
umask 022
parent_pid=$1
package=$2
work=$3
wait_limit=$4
dpkg_deb=$5
dpkg=$6
apt_get=$7
sha256sum=$8
expected_sha=$9
state_parent=${10}
)SH") + posixStateScript() +
           QStringLiteral(R"SH(
fail() { echo "ERROR: $*"; exit 1; }
digest() { "$sha256sum"; }
verified="$state/package.deb"
trap 'status=$?; /bin/rm -f "$verified"; printf "%s\n" "$status" > "$state/result"' EXIT
)SH") + posixVerifyScript() +
           QStringLiteral(R"SH(
package_name=$("$dpkg_deb" --field "$verified" Package)
test "$package_name" = 'fluent-serial-assistant' || fail 'DEB package identity does not match.'
package_arch=$("$dpkg_deb" --field "$verified" Architecture)
system_arch=$("$dpkg" --print-architecture)
test "$package_arch" = "$system_arch" || fail 'DEB architecture does not match this system.'
)SH") + waitForParentScript() +
           QStringLiteral(R"SH(
export DEBIAN_FRONTEND=noninteractive
"$apt_get" --yes --no-remove install "$verified"
echo 'Update installed successfully.'
)SH");
}

QString windowsInstallerScript(const InstallPlan &plan)
{
    return QStringLiteral(R"PS($ErrorActionPreference = 'Stop'
$package = %1
$expected = %2
$target = %3
$work = %4
$stateParent = %5
$parentId = %6
$waitLimit = %7
# Only the system and administrators may change the installer's directories; users may read the
# one with the log and the results, but not the one with the copy of the package.
function New-InstallerDirectory([string]$path, [bool]$readable) {
    $acl = New-Object System.Security.AccessControl.DirectorySecurity
    $acl.SetOwner((New-Object System.Security.Principal.SecurityIdentifier 'S-1-5-32-544'))
    $acl.SetAccessRuleProtection($true, $false)
    $rules = @(@('S-1-5-18', 'FullControl'), @('S-1-5-32-544', 'FullControl'))
    if ($readable) { $rules += ,@('S-1-5-32-545', 'ReadAndExecute') }
    foreach ($rule in $rules) {
        $identity = New-Object System.Security.Principal.SecurityIdentifier $rule[0]
        $acl.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule($identity, $rule[1], 'ContainerInherit, ObjectInherit', 'None', 'Allow')))
    }
    if (Test-Path -LiteralPath $path) { throw 'Update state directory already exists.' }
    [System.IO.Directory]::CreateDirectory($path, $acl) | Out-Null
}
# Until the app can find the installer's own directory through the pointer to it, an empty
# directory "failed" in the work directory is the only failure the installer can report there:
# writing a file could follow a link put in its place.
function Set-Failed {
    try { New-Item -ItemType Directory -Path (Join-Path $work 'failed') | Out-Null } catch {}
}
# The installer's directory must sit where only the system and administrators can rename, replace
# or re-permission anything on its path. Otherwise the checked copy could be swapped after the check,
# for one that then runs as an administrator.
function Assert-ProtectedLocation([string]$path) {
    $trusted = @('S-1-5-18', 'S-1-5-32-544', 'S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464', 'S-1-3-0')
    # Delete, DeleteSubdirectoriesAndFiles, ChangePermissions, TakeOwnership and GENERIC_ALL.
    $unsafe = 0x100D0040
    $directory = Get-Item -LiteralPath $path -Force
    while ($directory) {
        if ($directory -isnot [System.IO.DirectoryInfo] -or ($directory.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
            throw ('Not a protected location: ' + $directory.FullName)
        }
        $acl = Get-Acl -LiteralPath $directory.FullName
        $owner = $acl.GetOwner([System.Security.Principal.SecurityIdentifier]).Value
        $open = @($acl.GetAccessRules($true, $true, [System.Security.Principal.SecurityIdentifier]) | Where-Object {
            $_.AccessControlType -eq 'Allow' -and $trusted -notcontains $_.IdentityReference.Value -and
            !($_.PropagationFlags -band [System.Security.AccessControl.PropagationFlags]::InheritOnly) -and
            ([int64]$_.FileSystemRights -band $unsafe)
        })
        if ($trusted -notcontains $owner -or $open.Count -gt 0) { throw ('Not a protected location: ' + $directory.FullName) }
        $directory = $directory.Parent
    }
}
$name = 'FluentSerialAssistant-update-' + [guid]::NewGuid().ToString('N')
$state = Join-Path $stateParent $name
try {
    Assert-ProtectedLocation $stateParent
    New-InstallerDirectory $state $true
} catch {
    Set-Failed
    exit 1
}
$logPath = Join-Path $state 'install.log'
$verified = Join-Path $state 'package\setup.exe'
try {
    try {
        New-Item -ItemType Directory -Path (Join-Path $work ('state.' + $name)) | Out-Null
    } catch {
        Set-Failed
        throw
    }
    New-InstallerDirectory (Join-Path $state 'package') $false
    if (!(Test-Path -LiteralPath $package -PathType Leaf)) { throw 'Update package is missing.' }
    Copy-Item -LiteralPath $package -Destination $verified
    if ((Get-FileHash -LiteralPath $verified -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
        throw 'Update package does not match the release.'
    }
    if (Test-Path -LiteralPath (Join-Path $work 'cancel')) { throw 'Update handoff was cancelled.' }
    New-Item -ItemType File -Path (Join-Path $state 'ready') | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds($waitLimit)
    while (Get-Process -Id $parentId -ErrorAction SilentlyContinue) {
        if (Test-Path -LiteralPath (Join-Path $work 'cancel')) { throw 'Update handoff was cancelled.' }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Application did not exit; installation was cancelled.' }
        Start-Sleep -Milliseconds 200
    }
    if (Test-Path -LiteralPath (Join-Path $work 'cancel')) { throw 'Update handoff was cancelled.' }
    $arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS /DIR="' + $target + '" /LOG="' + (Join-Path $state 'setup.log') + '"'
    $installer = Start-Process -FilePath $verified -ArgumentList $arguments -Wait -PassThru
    if ($installer.ExitCode -ne 0) { throw ('Installer failed with exit code ' + $installer.ExitCode) }
    'Update installed successfully.' | Out-File -LiteralPath $logPath -Append -Encoding utf8
    0 | Out-File -LiteralPath (Join-Path $state 'result') -Encoding ascii
} catch {
    $_ | Out-File -LiteralPath $logPath -Append -Encoding utf8
    1 | Out-File -LiteralPath (Join-Path $state 'result') -Encoding ascii
    exit 1
} finally {
    Remove-Item -LiteralPath $verified -Force -ErrorAction SilentlyContinue
}
)PS")
        .arg(powerShellQuote(QDir::toNativeSeparators(plan.packagePath)),
             powerShellQuote(QString::fromLatin1(plan.sha256)),
             powerShellQuote(QDir::toNativeSeparators(plan.targetPath)),
             powerShellQuote(QDir::toNativeSeparators(plan.workDirectory)),
             powerShellQuote(QDir::toNativeSeparators(plan.stateParent)), QString::number(plan.parentPid),
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
            QStringLiteral("/bin/mv"),
            QStringLiteral("/usr/bin/shasum"),
            QString::fromLatin1(plan.sha256),
            plan.stateParent};
}

QString windowsInstallerCommand(const QString &script)
{
    // This travels inside the encoded bootstrap, and both must fit on a command line. UTF-8 in base64
    // is half the size of -EncodedCommand's UTF-16, and indentation and comment lines go: no string
    // in these scripts spans lines.
    // Started by a process that inherited PowerShell 7's module path, Windows PowerShell finds
    // PowerShell 7's modules first and cannot load them (Get-Acl failed so), so the script runs
    // with the system's module path.
    QStringList code{
        QStringLiteral("$env:PSModulePath = (@([Environment]::GetEnvironmentVariable('PSModulePath', 'Machine'), "
                       "(Join-Path $PSHOME 'Modules')) | Where-Object { $_ }) -join ';'")};
    for (const QString &line : script.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#'))) {
            code.append(trimmed);
        }
    }
    return QStringLiteral("-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"& ([ScriptBlock]::Create("
                          "[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('") +
           QString::fromLatin1(code.join(QLatin1Char('\n')).toUtf8().toBase64()) + QStringLiteral("'))))\"");
}

QString windowsAuthorizationScript(const QString &executable, const QString &installerCommand,
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
             powerShellQuote(QDir::toNativeSeparators(executable)), powerShellQuote(installerCommand));
}

QStringList linuxInstallerArguments(const InstallPlan &plan)
{
    return {QString::number(plan.parentPid),
            plan.packagePath,
            plan.workDirectory,
            QString::number(plan.waitSeconds),
            QStringLiteral("/usr/bin/dpkg-deb"),
            QStringLiteral("/usr/bin/dpkg"),
            QStringLiteral("/usr/bin/apt-get"),
            QStringLiteral("/usr/bin/sha256sum"),
            QString::fromLatin1(plan.sha256),
            plan.stateParent};
}

QStringList appleScriptArguments(const QString &command, bool administrator)
{
    QString escaped = command;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    // The script spans lines; escaped line feeds reach the shell unchanged.
    escaped.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    QString script = QStringLiteral("do shell script \"") + escaped + QLatin1Char('"');
    if (administrator) {
        script += QStringLiteral(" with administrator privileges");
    }
    return {QStringLiteral("-e"), script};
}

QString authorizationWrapper(const QString &command, const QString &workDirectory)
{
    return command + QStringLiteral(" >>") + shellQuote(QDir(workDirectory).filePath(QStringLiteral("install.log"))) +
           QStringLiteral(" 2>&1; result=$?; if test \"$result\" -ne 0; then printf '%s\\n' \"$result\" >") +
           shellQuote(QDir(workDirectory).filePath(QStringLiteral("result"))) + QStringLiteral("; fi");
}

QString stateDirectory(const QString &workDirectory, const QString &stateParent)
{
    // Only names the installers make; anything else in the work directory is not a pointer.
    static const QRegularExpression pointer(
        QStringLiteral("^state\\.(fluentserial-update\\.[A-Za-z0-9]{6,}|FluentSerialAssistant-update-[0-9a-f]{32})$"));
    const QStringList entries =
        QDir(workDirectory).entryList({QStringLiteral("state.*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        const QRegularExpressionMatch match = pointer.match(entry);
        if (match.hasMatch()) {
            const QString state = QDir(stateParent).filePath(match.captured(1));
            return QFileInfo(state).isDir() ? state : QString();
        }
    }
    return {};
}

} // namespace AppUpdate::InstallerDetail

namespace {

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size() &&
           file.flush();
}

// A result file that exists and does not say 0.
bool reportsFailure(const QString &path)
{
    QFile result(path);
    return result.open(QIODevice::ReadOnly) && result.readAll().trimmed() != QByteArray("0");
}

// The installer's log, after what the authorization step wrote to the work directory.
QString logFailure(const QString &message, const QString &work, const QString &state)
{
    QStringList logs{QDir(work).filePath(QStringLiteral("install.log"))};
    if (!state.isEmpty()) {
        logs.append(QDir(state).filePath(QStringLiteral("install.log")));
    }
    QString detail;
    for (const QString &path : std::as_const(logs)) {
        QFile log(path);
        if (log.open(QIODevice::ReadOnly)) {
            detail += QString::fromUtf8(log.readAll());
        }
    }
    detail = detail.right(2000).trimmed();
    return AppI18n::text("%1\n安装日志：%2")
        .arg(detail.isEmpty() ? message : message + QLatin1Char('\n') + detail, logs.last());
}

QString defaultStateParent()
{
#ifdef Q_OS_WIN
    // From the system, not the environment, which the user sets. The installer refuses a location
    // that anyone but the system and administrators could change in any case.
    PWSTR path = nullptr;
    QString programData;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &path))) {
        programData = QDir::fromNativeSeparators(QString::fromWCharArray(path));
    }
    CoTaskMemFree(path);
    return programData;
#else
    return QStringLiteral("/tmp");
#endif
}

} // namespace

namespace AppUpdate::InstallerDetail {

bool waitForHandoff(const QString &work, const QString &stateParent, QString *error)
{
    const QDir directory(work);
    QString state;
    QElapsedTimer timer;
    timer.start();
    // Includes the system authorization dialog and macOS package preparation.
    while (timer.elapsed() < 180000) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            break;
        }
        // The authorization step reports a refusal in the work directory, the installer everything
        // else in its own, once it has made it.
        if (reportsFailure(directory.filePath(QStringLiteral("result"))) ||
            QFileInfo(directory.filePath(QStringLiteral("failed"))).isDir()) {
            break;
        }
        if (state.isEmpty()) {
            state = stateDirectory(work, stateParent);
        }
        if (!state.isEmpty()) {
            if (reportsFailure(QDir(state).filePath(QStringLiteral("result")))) {
                break;
            }
            if (QFile::exists(QDir(state).filePath(QStringLiteral("ready")))) {
                return true;
            }
        }
        QThread::msleep(50);
    }
    writeFile(directory.filePath(QStringLiteral("cancel")), QByteArray("cancelled\n"));
    if (error) {
        *error = logFailure(AppI18n::text("未能准备更新安装，当前应用将保持运行。"), work, state);
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

bool launchUpdateInstaller(const QString &packagePath, const QByteArray &sha256, QString *error)
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
    static const QRegularExpression digest(QStringLiteral("^[0-9a-f]{64}$"));
    if (!digest.match(QString::fromLatin1(sha256.toLower())).hasMatch()) {
        return fail(AppI18n::text("更新安装包的校验值无效。"));
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
    plan.sha256 = sha256.toLower();
    plan.stateParent = defaultStateParent();
    if (plan.stateParent.isEmpty()) {
        return fail(AppI18n::text("无法创建更新安装目录。"));
    }
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
    const QString bootstrap = InstallerDetail::windowsAuthorizationScript(
        powerShell, InstallerDetail::windowsInstallerCommand(InstallerDetail::windowsInstallerScript(plan)),
        plan.workDirectory);
    const QString parameters = QStringLiteral("-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand ") +
                               InstallerDetail::powerShellEncodedCommand(bootstrap);
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
        return fail(logFailure(AppI18n::text("系统未授权启动更新安装程序。"), plan.workDirectory, QString()));
    }
    if (info.hProcess) {
        CloseHandle(info.hProcess);
    }
#else
    // The script goes on the command line, not into a file the user could change before it runs
    // with system rights.
    QString command;
#ifdef Q_OS_MACOS
    QStringList arguments{QStringLiteral("-c"), InstallerDetail::macInstallerScript(), QStringLiteral("sh")};
    arguments.append(InstallerDetail::macInstallerArguments(plan));
    command = shellCommand(QStringLiteral("/bin/sh"), arguments);
    if (!QFileInfo(QFileInfo(plan.targetPath).absolutePath()).isWritable()) {
        const QString background = QStringLiteral("/usr/bin/nohup ") + command + QStringLiteral(" >/dev/null 2>&1 &");
        command =
            shellCommand(QStringLiteral("/usr/bin/osascript"), InstallerDetail::appleScriptArguments(background, true));
    }
#else
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty() || !QFileInfo::exists(QStringLiteral("/usr/bin/apt-get"))) {
        return fail(AppI18n::text("自动安装需要系统提供 pkexec 和 apt-get，请使用系统软件安装器安装已下载的 DEB。"));
    }
    QStringList arguments{QStringLiteral("/bin/sh"), QStringLiteral("-c"), InstallerDetail::linuxInstallerScript(),
                          QStringLiteral("sh")};
    arguments.append(InstallerDetail::linuxInstallerArguments(plan));
    command = shellCommand(pkexec, arguments);
#endif
    const QString wrapper = InstallerDetail::authorizationWrapper(command, plan.workDirectory);
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), wrapper}, plan.workDirectory)) {
        return fail(logFailure(AppI18n::text("无法启动更新安装程序。"), plan.workDirectory, QString()));
    }
#endif
    return InstallerDetail::waitForHandoff(plan.workDirectory, plan.stateParent, error);
#endif
}

} // namespace AppUpdate
