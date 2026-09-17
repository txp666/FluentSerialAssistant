#include "app/core/app_settings.h"

#include <QtCore/QDir>
#include <QtCore/QTemporaryDir>

AppSettings::AppSettings() : QSettings(filePath(), QSettings::IniFormat) {}

QString AppSettings::directoryPath()
{
    static QTemporaryDir directory;
    if (!directory.isValid()) {
        qFatal("Unable to create temporary settings directory");
    }
    return directory.path();
}

QString AppSettings::filePath() { return QDir(directoryPath()).filePath(QStringLiteral("FluentSerialAssistant.ini")); }
