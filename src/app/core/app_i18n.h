#pragma once

#include <QtCore/QString>

class QCoreApplication;
class QObject;

namespace AppI18n {

QString normalizedLocaleName(const QString &localeName);
QString effectiveLocaleName(const QString &localeName = QString());
QString localeDisplayName(const QString &localeName);
int localeIndex(const QString &localeName);
QString localeNameForIndex(int index);
QString toggledChineseEnglishLocaleName(const QString &localeName = QString());
QString text(const char *sourceText);
bool installTranslators(QCoreApplication *application, const QString &localeName = QString());
void retranslateObjectTree(QObject *root, const QString &localeName = QString());
void retranslateApplication(const QString &localeName = QString());
QString applyLocale(const QString &localeName, QCoreApplication *application = nullptr);

} // namespace AppI18n
