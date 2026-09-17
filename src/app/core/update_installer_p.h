#pragma once

#include <QtCore/QStringList>

namespace AppUpdate::InstallerDetail {

struct InstallPlan
{
    QString packagePath;
    QString targetPath;
    QString workDirectory;
    qint64 parentPid = 0;
    int waitSeconds = 120;
};

QString shellQuote(const QString &value);
QString powerShellQuote(const QString &value);
QString macInstallerScript();
QString linuxInstallerScript();
QString windowsInstallerScript(const InstallPlan &plan);
QString windowsAuthorizationScript(const QString &executable, const QString &encodedScript,
                                   const QString &workDirectory);
QStringList macInstallerArguments(const InstallPlan &plan);
QStringList linuxInstallerArguments(const InstallPlan &plan);
QString authorizationWrapper(const QString &command, const QString &workDirectory);
bool waitForHandoff(const QString &workDirectory, QString *error);

} // namespace AppUpdate::InstallerDetail
