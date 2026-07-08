#include "app/view/main_window.h"

#include "app/core/font_preferences.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QCoreApplication>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyleFactory>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    Q_INIT_RESOURCE(fluentqtwidgets);
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QCoreApplication::setOrganizationName(QStringLiteral("txp"));
    QCoreApplication::setApplicationName(QStringLiteral("FluentSerialAssistant"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.4"));

    AppFontPreferences::loadCustomFonts();
    FluentQt::FluentConfig::instance()->load();
    FluentQt::ThemeManager::instance()->setTheme(FluentQt::FluentConfig::instance()->themeMode());
    FluentQt::ThemeManager::instance()->setAccentColor(FluentQt::FluentConfig::instance()->themeColor());
    AppFontPreferences::applyConfiguredUiFont();

    MainWindow window;
    window.show();

    return app.exec();
}
