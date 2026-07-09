#pragma once

#include <QtCore/QSettings>
#include <QtCore/QString>

class AppSettings : public QSettings
{
  public:
    AppSettings();

    static QString filePath();
};
