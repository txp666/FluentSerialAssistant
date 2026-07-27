#include "app/view/main_window.h"

#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/core/font_preferences.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyleFactory>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    Q_INIT_RESOURCE(app);
    Q_INIT_RESOURCE(fluentqtwidgets);
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QCoreApplication::setOrganizationName(QStringLiteral("txp"));
    QCoreApplication::setApplicationName(QStringLiteral("FluentSerialAssistant"));
    QCoreApplication::setApplicationVersion(QStringLiteral(FLUENT_SERIAL_ASSISTANT_VERSION));

    const QString configDir = AppSettings::directoryPath();
    const QString legacyConfigDir = QCoreApplication::applicationDirPath();
    const auto migrateLegacyConfig = [&configDir, &legacyConfigDir](const QString &fileName) {
        const QString targetPath = QDir(configDir).filePath(fileName);
        const QString legacyPath = QDir(legacyConfigDir).filePath(fileName);
        if (!QFile::exists(targetPath) && QFile::exists(legacyPath)) {
            QFile::copy(legacyPath, targetPath);
        }
    };
    migrateLegacyConfig(QStringLiteral("FluentSerialAssistant.ini"));
    migrateLegacyConfig(QStringLiteral("FluentSerialAssistant.fluent.json"));

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configDir);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, configDir);
    FluentQt::FluentConfig::instance()->setFileName(
        QDir(configDir).filePath(QStringLiteral("FluentSerialAssistant.fluent.json")));

    AppFontPreferences::loadCustomFonts();
    FluentQt::FluentConfig::instance()->load();
    AppI18n::installTranslators(&app, FluentQt::FluentConfig::instance()->localeName());
    FluentQt::ThemeManager::instance()->setTheme(FluentQt::FluentConfig::instance()->themeMode());
    FluentQt::ThemeManager::instance()->setAccentColor(FluentQt::FluentConfig::instance()->themeColor());
    AppFontPreferences::applyConfiguredUiFont();

    MainWindow window;
    window.show();

    return app.exec();
}
