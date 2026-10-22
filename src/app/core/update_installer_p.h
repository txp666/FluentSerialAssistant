#pragma once

#include <QtCore/QStringList>

namespace AppUpdate::InstallerDetail {

struct InstallPlan
{
    QString packagePath;
    // Lower-case hex SHA-256 of the package, from the release.
    QByteArray sha256;
    QString targetPath;
    QString workDirectory;
    // Where the installer makes the directory for its copy of the package, its log and its results
    // (/tmp, or %ProgramData% on Windows).
    QString stateParent;
    qint64 parentPid = 0;
    int waitSeconds = 120;
};

QString shellQuote(const QString &value);
QString powerShellQuote(const QString &value);
QString powerShellEncodedCommand(const QString &script);
QString macInstallerScript();
QString linuxInstallerScript();
QString windowsInstallerScript(const InstallPlan &plan);
// powershell.exe arguments that run the script.
QString windowsInstallerCommand(const QString &script);
// Asks for administrator rights to run powershell.exe with installerCommand.
QString windowsAuthorizationScript(const QString &executable, const QString &installerCommand,
                                   const QString &workDirectory);
QStringList macInstallerArguments(const InstallPlan &plan);
QStringList linuxInstallerArguments(const InstallPlan &plan);
// Arguments for osascript to run a shell command, as an administrator when asked.
QStringList appleScriptArguments(const QString &command, bool administrator);
QString authorizationWrapper(const QString &command, const QString &workDirectory);
// The installer's own directory, named by the pointer it leaves in the work directory; empty
// until it exists.
QString stateDirectory(const QString &workDirectory, const QString &stateParent);
bool waitForHandoff(const QString &workDirectory, const QString &stateParent, QString *error);

} // namespace AppUpdate::InstallerDetail
