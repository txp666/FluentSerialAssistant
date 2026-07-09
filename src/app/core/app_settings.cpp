#include "app/core/app_settings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>

AppSettings::AppSettings() : QSettings(filePath(), QSettings::IniFormat) {}

QString AppSettings::filePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("FluentSerialAssistant.ini"));
}
