#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QFont>

namespace AppFontPreferences {

struct FontImportResult
{
    bool ok = false;
    QStringList families;
    QString storedFilePath;
    QString errorMessage;
};

QStringList uiFontFamilies();
QStringList terminalFontFamilies();

QString currentUiFontFamily();
QString currentTerminalFontFamily();
int currentTerminalFontPointSize();

void loadCustomFonts();
FontImportResult importFontFile(const QString &filePath);
// False when the interface can use the family only after a restart (a font imported since start-up): the
// choice is kept and applyPendingUiFont() applies it at the next start.
bool setUiFontFamily(const QString &family);
void applyPendingUiFont();
bool isUiFontInUse(const QString &family);
void setTerminalFontFamily(const QString &family);
void setTerminalFontPointSize(int pointSize);
void applyConfiguredUiFont();

QFont terminalFont(const QString &family = QString());
QFont inputMonospaceFont();

} // namespace AppFontPreferences
