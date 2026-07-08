#include "app/core/font_preferences.h"

#include <FluentQtWidgets/Config.h>
#include <FluentQtWidgets/Theme.h>

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QtGlobal>
#include <QtGui/QFontDatabase>
#include <QtWidgets/QApplication>

#include <algorithm>

namespace {

constexpr int DefaultTerminalFontPointSize = 10;
constexpr int MinTerminalFontPointSize = 8;
constexpr int MaxTerminalFontPointSize = 28;

QStringList &customFontFamilies()
{
    static QStringList families;
    return families;
}

void appendUnique(QStringList *items, const QString &value)
{
    if(!value.isEmpty() && !items->contains(value)) {
        items->append(value);
    }
}

void prependUnique(QStringList *items, const QString &value)
{
    if(!value.isEmpty() && !items->contains(value)) {
        items->prepend(value);
    }
}

QStringList installedFontFamilies()
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    return QFontDatabase().families();
#else
    return QFontDatabase::families();
#endif
}

bool hasFontFamily(const QStringList &installed, const QString &family)
{
    return std::any_of(installed.cbegin(), installed.cend(), [&family](const QString &installedFamily) {
        return installedFamily.compare(family, Qt::CaseInsensitive) == 0;
    });
}

QString canonicalFontFamily(const QStringList &installed, const QString &family)
{
    for(const QString &installedFamily : installed) {
        if(installedFamily.compare(family, Qt::CaseInsensitive) == 0) {
            return installedFamily;
        }
    }
    return QString();
}

void appendAvailableFont(QStringList *fonts, const QStringList &installed, const QString &family)
{
    const QString canonical = canonicalFontFamily(installed, family);
    appendUnique(fonts, canonical);
}

void appendCurrentFont(QStringList *fonts, const QString &family)
{
    prependUnique(fonts, family);
}

QString appDefaultFontFamily()
{
    return qApp ? qApp->font().family() : QString();
}

QStringList preferredUiFonts()
{
    return {
        QStringLiteral("Segoe UI"),
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("PingFang SC"),
        QStringLiteral(".AppleSystemUIFont"),
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Noto Sans SC"),
        QStringLiteral("Noto Sans"),
        QStringLiteral("Arial")
    };
}

QStringList preferredTerminalFonts()
{
    return {
        QStringLiteral("Cascadia Mono"),
        QStringLiteral("Consolas"),
        QStringLiteral("Menlo"),
        QStringLiteral("SF Mono"),
        QStringLiteral("Monaco"),
        QStringLiteral("Noto Sans Mono CJK SC"),
        QStringLiteral("Noto Sans Mono"),
        QStringLiteral("DejaVu Sans Mono"),
        QStringLiteral("Monospace")
    };
}

QString firstAvailableFont(const QStringList &candidates, const QString &fallback)
{
    const QStringList installed = installedFontFamilies();
    for(const QString &candidate : candidates) {
        const QString canonical = canonicalFontFamily(installed, candidate);
        if(!canonical.isEmpty()) {
            return canonical;
        }
    }
    return fallback;
}

QString defaultUiFontFamily()
{
    return firstAvailableFont(preferredUiFonts(), appDefaultFontFamily());
}

QString defaultTerminalFontFamily()
{
    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    return firstAvailableFont(preferredTerminalFonts(), fixedFont.family());
}

QString resolvedFontFamily(const QString &family, const QString &fallback)
{
    const QString trimmed = family.trimmed();
    if(trimmed.isEmpty()) {
        return fallback;
    }

    const QStringList installed = installedFontFamilies();
    if(hasFontFamily(installed, trimmed)) {
        return canonicalFontFamily(installed, trimmed);
    }

    const QString custom = canonicalFontFamily(customFontFamilies(), trimmed);
    if(!custom.isEmpty()) {
        return custom;
    }
    return fallback;
}

QStringList uiFallbackFamilies(const QString &primary)
{
    QStringList families;
    appendCurrentFont(&families, primary);

    const QStringList installed = installedFontFamilies();
    for(const QString &family : preferredUiFonts()) {
        appendAvailableFont(&families, installed, family);
    }

    return families;
}

QString customFontDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base.isEmpty() ? QDir::currentPath() : base).filePath(QStringLiteral("fonts"));
}

QString fontFileHash(const QString &filePath, QString *errorMessage)
{
    QFile file(filePath);
    if(!file.open(QIODevice::ReadOnly)) {
        if(errorMessage) {
            *errorMessage = QStringLiteral("无法读取字体文件。");
        }
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if(chunk.isEmpty() && file.error() != QFile::NoError) {
            if(errorMessage) {
                *errorMessage = QStringLiteral("读取字体文件失败。");
            }
            return QString();
        }
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

QStringList loadFontFile(const QString &filePath)
{
    const int fontId = QFontDatabase::addApplicationFont(filePath);
    if(fontId < 0) {
        return {};
    }

    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    for(const QString &family : families) {
        appendUnique(&customFontFamilies(), family);
    }
    return families;
}

bool isSupportedFontFile(const QFileInfo &fileInfo)
{
    const QString suffix = fileInfo.suffix().toLower();
    return suffix == QStringLiteral("ttf") || suffix == QStringLiteral("otf") || suffix == QStringLiteral("ttc");
}

} // namespace

namespace AppFontPreferences {

QStringList uiFontFamilies()
{
    QStringList families;
    const QStringList installed = installedFontFamilies();
    for(const QString &family : preferredUiFonts()) {
        appendAvailableFont(&families, installed, family);
    }
    for(const QString &family : customFontFamilies()) {
        appendUnique(&families, family);
    }
    appendCurrentFont(&families, currentUiFontFamily());
    return families;
}

QStringList terminalFontFamilies()
{
    QStringList families;
    const QStringList installed = installedFontFamilies();
    for(const QString &family : preferredTerminalFonts()) {
        appendAvailableFont(&families, installed, family);
    }
    for(const QString &family : customFontFamilies()) {
        appendUnique(&families, family);
    }
    appendCurrentFont(&families, currentTerminalFontFamily());
    return families;
}

QString currentUiFontFamily()
{
    const QStringList installed = installedFontFamilies();
    for(const QString &family : FluentQt::FluentConfig::instance()->fontFamilies()) {
        const QString canonical = canonicalFontFamily(installed, family);
        if(!canonical.isEmpty()) {
            return canonical;
        }
        const QString custom = canonicalFontFamily(customFontFamilies(), family);
        if(!custom.isEmpty()) {
            return custom;
        }
    }
    return defaultUiFontFamily();
}

QString currentTerminalFontFamily()
{
    QSettings settings;
    return resolvedFontFamily(settings.value(QStringLiteral("terminal/fontFamily")).toString(),
                              defaultTerminalFontFamily());
}

int currentTerminalFontPointSize()
{
    QSettings settings;
    return qBound(MinTerminalFontPointSize,
                  settings.value(QStringLiteral("terminal/fontPointSize"), DefaultTerminalFontPointSize).toInt(),
                  MaxTerminalFontPointSize);
}

void loadCustomFonts()
{
    QDir dir(customFontDirectory());
    if(!dir.exists()) {
        return;
    }

    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.ttf"),
                                                   QStringLiteral("*.otf"),
                                                   QStringLiteral("*.ttc")},
                                                  QDir::Files | QDir::Readable,
                                                  QDir::Name);
    for(const QFileInfo &file : files) {
        loadFontFile(file.absoluteFilePath());
    }
}

FontImportResult importFontFile(const QString &filePath)
{
    FontImportResult result;
    const QFileInfo sourceInfo(filePath);
    if(!sourceInfo.exists() || !sourceInfo.isFile()) {
        result.errorMessage = QStringLiteral("字体文件不存在。");
        return result;
    }
    if(!isSupportedFontFile(sourceInfo)) {
        result.errorMessage = QStringLiteral("请选择 TTF、OTF 或 TTC 字体文件。");
        return result;
    }

    QString hashError;
    const QString digest = fontFileHash(sourceInfo.absoluteFilePath(), &hashError);
    if(digest.isEmpty()) {
        result.errorMessage = hashError.isEmpty() ? QStringLiteral("无法识别字体文件。") : hashError;
        return result;
    }

    QDir fontDir(customFontDirectory());
    if(!fontDir.exists() && !fontDir.mkpath(QStringLiteral("."))) {
        result.errorMessage = QStringLiteral("无法创建字体保存目录。");
        return result;
    }

    const QString suffix = sourceInfo.suffix().toLower();
    const QString storedName = QStringLiteral("%1.%2").arg(digest.left(16), suffix);
    const QString storedPath = fontDir.filePath(storedName);
    bool copied = false;
    if(!QFileInfo::exists(storedPath)) {
        if(!QFile::copy(sourceInfo.absoluteFilePath(), storedPath)) {
            result.errorMessage = QStringLiteral("复制字体文件失败。");
            return result;
        }
        copied = true;
    }

    const QStringList families = loadFontFile(storedPath);
    if(families.isEmpty()) {
        if(copied) {
            QFile::remove(storedPath);
        }
        result.errorMessage = QStringLiteral("无法加载该字体文件。");
        return result;
    }

    result.ok = true;
    result.families = families;
    result.storedFilePath = storedPath;
    return result;
}

void setUiFontFamily(const QString &family)
{
    const QString resolved = resolvedFontFamily(family, defaultUiFontFamily());
    FluentQt::FluentConfig::instance()->setFontFamilies(uiFallbackFamilies(resolved));
    FluentQt::FluentConfig::instance()->save();
    applyConfiguredUiFont();
}

void setTerminalFontFamily(const QString &family)
{
    QSettings settings;
    settings.setValue(QStringLiteral("terminal/fontFamily"),
                      resolvedFontFamily(family, defaultTerminalFontFamily()));
}

void setTerminalFontPointSize(int pointSize)
{
    QSettings settings;
    settings.setValue(QStringLiteral("terminal/fontPointSize"),
                      qBound(MinTerminalFontPointSize, pointSize, MaxTerminalFontPointSize));
}

void applyConfiguredUiFont()
{
    FluentQt::ThemeManager::instance()->setTheme(FluentQt::FluentConfig::instance()->themeMode());
}

QFont terminalFont(const QString &family)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(currentTerminalFontPointSize());

    const QString resolved = resolvedFontFamily(family.isEmpty() ? currentTerminalFontFamily() : family,
                                                defaultTerminalFontFamily());
    if(!resolved.isEmpty()) {
        font.setFamily(resolved);
    }
    return font;
}

} // namespace AppFontPreferences
