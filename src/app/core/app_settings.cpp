#include "app/core/app_settings.h"

#include <QtCore/QDir>
#include <QtCore/QStandardPaths>

AppSettings::AppSettings() : QSettings(filePath(), QSettings::IniFormat) {}

QString AppSettings::directoryPath()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (path.isEmpty()) {
        path = QDir::home().filePath(QStringLiteral(".config/FluentSerialAssistant"));
    }
    QDir().mkpath(path);
    return path;
}

QString AppSettings::filePath() { return QDir(directoryPath()).filePath(QStringLiteral("FluentSerialAssistant.ini")); }
