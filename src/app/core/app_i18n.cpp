#include "app/core/app_i18n.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QMetaProperty>
#include <QtCore/QCoreApplication>
#include <QtCore/QLocale>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTranslator>
#include <QtCore/QXmlStreamReader>
#include <QtGui/QAction>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QTreeWidget>

#include <utility>

namespace AppI18n {

namespace {

QPointer<FluentQt::FluentTranslator> s_fluentTranslator;
QPointer<QTranslator> s_appTranslator;

struct Pattern
{
    QRegularExpression regex;
    QVector<int> placeholderOrder;
};

struct CatalogEntry
{
    QString source;
    QString zh;
    QString en;
    Pattern sourcePattern;
    Pattern zhPattern;
    Pattern enPattern;
};

QVector<CatalogEntry> s_catalog;
bool s_catalogLoaded = false;

void removeTranslator(QCoreApplication *application, QTranslator *translator)
{
    if (!application || !translator) {
        return;
    }
    application->removeTranslator(translator);
    translator->deleteLater();
}

QLocale localeForName(const QString &localeName)
{
    const QString normalized = normalizedLocaleName(localeName);
    if (normalized == QStringLiteral("Auto")) {
        return QLocale::system();
    }
    return QLocale(normalized);
}

bool isChineseLocale(const QLocale &locale)
{
    return locale.language() == QLocale::Chinese;
}

QString translationNameForLocale(const QLocale &locale)
{
    return isChineseLocale(locale) ? QStringLiteral("zh_CN") : QStringLiteral("en_US");
}

QString catalogResourcePath(const QString &localeName)
{
    return QStringLiteral(":/app/i18n/fluentserialassistant.%1.ts").arg(localeName);
}

QHash<QString, QString> readTsCatalog(const QString &localeName)
{
    QHash<QString, QString> result;
    QFile file(catalogResourcePath(localeName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return result;
    }

    QXmlStreamReader xml(&file);
    QString contextName;
    QString source;
    QString translation;
    bool inMessage = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QStringView name = xml.name();
            if (name == QLatin1String("context")) {
                contextName.clear();
            } else if (name == QLatin1String("message")) {
                inMessage = true;
                source.clear();
                translation.clear();
            } else if (name == QLatin1String("name")) {
                contextName = xml.readElementText();
            } else if (inMessage && name == QLatin1String("source")) {
                source = xml.readElementText();
            } else if (inMessage && name == QLatin1String("translation")) {
                translation = xml.readElementText();
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("message")) {
            if ((contextName == QLatin1String("AppText") ||
                 contextName == QLatin1String("FluentQt::RealtimePlotWidget")) &&
                !source.isEmpty()) {
                result.insert(source, translation);
            }
            inMessage = false;
        }
    }
    return result;
}

Pattern patternForText(const QString &text)
{
    Pattern pattern;
    static const QRegularExpression placeholderRe(QStringLiteral("%(\\d+)"));

    qsizetype cursor = 0;
    QString regexText = QStringLiteral("^");
    auto it = placeholderRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const qsizetype start = match.capturedStart();
        regexText += QRegularExpression::escape(text.mid(cursor, start - cursor));
        regexText += QStringLiteral("(.*?)");
        pattern.placeholderOrder.append(match.captured(1).toInt());
        cursor = match.capturedEnd();
    }

    if (pattern.placeholderOrder.isEmpty()) {
        return pattern;
    }

    regexText += QRegularExpression::escape(text.mid(cursor));
    regexText += QStringLiteral("$");
    pattern.regex = QRegularExpression(regexText, QRegularExpression::DotMatchesEverythingOption);
    return pattern;
}

void ensureCatalog()
{
    if (s_catalogLoaded) {
        return;
    }
    s_catalogLoaded = true;

    const QHash<QString, QString> zh = readTsCatalog(QStringLiteral("zh_CN"));
    const QHash<QString, QString> en = readTsCatalog(QStringLiteral("en_US"));
    QSet<QString> sources;
    for (auto it = zh.constBegin(); it != zh.constEnd(); ++it) {
        sources.insert(it.key());
    }
    for (auto it = en.constBegin(); it != en.constEnd(); ++it) {
        sources.insert(it.key());
    }

    s_catalog.reserve(sources.size());
    for (const QString &source : sources) {
        CatalogEntry entry;
        entry.source = source;
        entry.zh = zh.value(source, source);
        entry.en = en.value(source, source);
        entry.sourcePattern = patternForText(entry.source);
        entry.zhPattern = patternForText(entry.zh);
        entry.enPattern = patternForText(entry.en);
        s_catalog.append(entry);
    }
}

QString targetText(const CatalogEntry &entry, const QString &localeName)
{
    return localeName == QLatin1String("zh_CN") ? entry.zh : entry.en;
}

QString replacePlaceholders(QString pattern, const QHash<int, QString> &arguments)
{
    static const QRegularExpression placeholderRe(QStringLiteral("%(\\d+)"));
    QString result;
    qsizetype cursor = 0;
    auto it = placeholderRe.globalMatch(pattern);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += pattern.mid(cursor, match.capturedStart() - cursor);
        const int index = match.captured(1).toInt();
        result += arguments.value(index, match.captured(0));
        cursor = match.capturedEnd();
    }
    result += pattern.mid(cursor);
    return result;
}

bool matchPattern(const QString &value, const Pattern &pattern, QHash<int, QString> *arguments)
{
    if (!pattern.regex.isValid() || pattern.placeholderOrder.isEmpty()) {
        return false;
    }
    const QRegularExpressionMatch match = pattern.regex.match(value);
    if (!match.hasMatch()) {
        return false;
    }
    if (arguments) {
        for (int i = 0; i < pattern.placeholderOrder.size(); ++i) {
            arguments->insert(pattern.placeholderOrder.at(i), match.captured(i + 1));
        }
    }
    return true;
}

QString translateValue(const QString &value, const QString &localeName, bool allowPatterns = true)
{
    if (value.isEmpty()) {
        return value;
    }

    ensureCatalog();
    const QString targetLocale = effectiveLocaleName(localeName);
    for (const CatalogEntry &entry : std::as_const(s_catalog)) {
        if (value == entry.source || value == entry.zh || value == entry.en) {
            return targetText(entry, targetLocale);
        }
    }

    if (!allowPatterns) {
        return value;
    }

    for (const CatalogEntry &entry : std::as_const(s_catalog)) {
        QHash<int, QString> arguments;
        if (!matchPattern(value, entry.sourcePattern, &arguments) && !matchPattern(value, entry.zhPattern, &arguments) &&
            !matchPattern(value, entry.enPattern, &arguments)) {
            continue;
        }

        for (auto it = arguments.begin(); it != arguments.end(); ++it) {
            it.value() = translateValue(it.value(), targetLocale, false);
        }
        return replacePlaceholders(targetText(entry, targetLocale), arguments);
    }

    return value;
}

bool setTranslatedProperty(QObject *object, const char *propertyName, const QString &localeName)
{
    const QVariant value = object->property(propertyName);
    if (!value.isValid() || value.metaType().id() != QMetaType::QString) {
        return false;
    }

    const QString oldText = value.toString();
    const QString newText = translateValue(oldText, localeName);
    if (newText == oldText) {
        return false;
    }

    const QSignalBlocker blocker(object);
    object->setProperty(propertyName, newText);
    return true;
}

void retranslateObjectProperties(QObject *object, const QString &localeName)
{
    static constexpr const char *kProperties[] = {"title",       "content", "windowTitle", "toolTip",
                                                  "statusTip",   "whatsThis", "placeholderText",
                                                  "accessibleName", "accessibleDescription"};
    for (const char *propertyName : kProperties) {
        setTranslatedProperty(object, propertyName, localeName);
    }

    if (auto *label = qobject_cast<QLabel *>(object)) {
        const QString text = label->text();
        const QString translated = translateValue(text, localeName);
        if (translated != text) {
            const QSignalBlocker blocker(label);
            label->setText(translated);
        }
    } else if (auto *button = qobject_cast<QAbstractButton *>(object)) {
        const QString text = button->text();
        const QString translated = translateValue(text, localeName);
        if (translated != text) {
            const QSignalBlocker blocker(button);
            button->setText(translated);
        }
    } else if (auto *groupBox = qobject_cast<QGroupBox *>(object)) {
        const QString title = groupBox->title();
        const QString translated = translateValue(title, localeName);
        if (translated != title) {
            const QSignalBlocker blocker(groupBox);
            groupBox->setTitle(translated);
        }
    }

    if (auto *action = qobject_cast<QAction *>(object)) {
        const QSignalBlocker blocker(action);
        action->setText(translateValue(action->text(), localeName));
        action->setToolTip(translateValue(action->toolTip(), localeName));
        action->setStatusTip(translateValue(action->statusTip(), localeName));
        action->setWhatsThis(translateValue(action->whatsThis(), localeName));
    }
}

template <typename Combo>
void retranslateComboItems(Combo *combo, const QString &localeName)
{
    if (!combo) {
        return;
    }
    const QSignalBlocker blocker(combo);
    for (int i = 0; i < combo->count(); ++i) {
        const QString text = combo->itemText(i);
        const QString translated = translateValue(text, localeName);
        if (translated != text) {
            combo->setItemText(i, translated);
        }
    }
}

void retranslateListWidget(QListWidget *list, const QString &localeName)
{
    if (!list) {
        return;
    }
    const QSignalBlocker blocker(list);
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        if (!item) {
            continue;
        }
        item->setText(translateValue(item->text(), localeName));
        item->setToolTip(translateValue(item->toolTip(), localeName));
        item->setStatusTip(translateValue(item->statusTip(), localeName));
        item->setWhatsThis(translateValue(item->whatsThis(), localeName));
    }
}

void retranslateTableWidget(QTableWidget *table, const QString &localeName)
{
    if (!table) {
        return;
    }
    const QSignalBlocker blocker(table);
    for (int row = 0; row < table->rowCount(); ++row) {
        for (int column = 0; column < table->columnCount(); ++column) {
            QTableWidgetItem *item = table->item(row, column);
            if (item) {
                item->setText(translateValue(item->text(), localeName));
                item->setToolTip(translateValue(item->toolTip(), localeName));
            }
        }
    }
    for (int column = 0; column < table->columnCount(); ++column) {
        if (QTableWidgetItem *item = table->horizontalHeaderItem(column)) {
            item->setText(translateValue(item->text(), localeName));
        }
    }
}

void retranslateTreeItem(QTreeWidgetItem *item, const QString &localeName)
{
    if (!item) {
        return;
    }
    for (int column = 0; column < item->columnCount(); ++column) {
        item->setText(column, translateValue(item->text(column), localeName));
        item->setToolTip(column, translateValue(item->toolTip(column), localeName));
    }
    for (int i = 0; i < item->childCount(); ++i) {
        retranslateTreeItem(item->child(i), localeName);
    }
}

void retranslateTreeWidget(QTreeWidget *tree, const QString &localeName)
{
    if (!tree) {
        return;
    }
    const QSignalBlocker blocker(tree);
    for (int column = 0; column < tree->columnCount(); ++column) {
        if (QTreeWidgetItem *item = tree->headerItem()) {
            item->setText(column, translateValue(item->text(column), localeName));
        }
    }
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        retranslateTreeItem(tree->topLevelItem(i), localeName);
    }
}

void retranslateTabBar(FluentQt::TabBar *tabBar, const QString &localeName)
{
    if (!tabBar) {
        return;
    }
    const QSignalBlocker blocker(tabBar);
    for (int i = 0; i < tabBar->count(); ++i) {
        const QString text = tabBar->tabText(i);
        const QString translated = translateValue(text, localeName);
        if (translated != text) {
            tabBar->setTabText(i, translated);
        }
    }
    if (tabBar->addButton()) {
        tabBar->addButton()->setToolTip(translateValue(tabBar->addButton()->toolTip(), localeName));
    }
}

void retranslateQtTabBar(QTabBar *tabBar, const QString &localeName)
{
    if (!tabBar) {
        return;
    }
    const QSignalBlocker blocker(tabBar);
    for (int i = 0; i < tabBar->count(); ++i) {
        tabBar->setTabText(i, translateValue(tabBar->tabText(i), localeName));
        tabBar->setTabToolTip(i, translateValue(tabBar->tabToolTip(i), localeName));
    }
}

void retranslateSpecialObject(QObject *object, const QString &localeName)
{
    if (auto *combo = qobject_cast<FluentQt::ComboBox *>(object)) {
        retranslateComboItems(combo, localeName);
    } else if (auto *combo = qobject_cast<FluentQt::EditableComboBox *>(object)) {
        retranslateComboItems(combo, localeName);
    } else if (auto *combo = qobject_cast<FluentQt::ModelComboBox *>(object)) {
        retranslateComboItems(combo, localeName);
    } else if (auto *combo = qobject_cast<FluentQt::EditableModelComboBox *>(object)) {
        retranslateComboItems(combo, localeName);
    } else if (auto *combo = qobject_cast<QComboBox *>(object)) {
        retranslateComboItems(combo, localeName);
    } else if (auto *tabWidget = qobject_cast<FluentQt::TabWidget *>(object)) {
        const QSignalBlocker blocker(tabWidget);
        for (int i = 0; i < tabWidget->count(); ++i) {
            tabWidget->setTabText(i, translateValue(tabWidget->tabText(i), localeName));
        }
    } else if (auto *tabBar = qobject_cast<FluentQt::TabBar *>(object)) {
        retranslateTabBar(tabBar, localeName);
    } else if (auto *tabWidget = qobject_cast<QTabWidget *>(object)) {
        const QSignalBlocker blocker(tabWidget);
        for (int i = 0; i < tabWidget->count(); ++i) {
            tabWidget->setTabText(i, translateValue(tabWidget->tabText(i), localeName));
            tabWidget->setTabToolTip(i, translateValue(tabWidget->tabToolTip(i), localeName));
        }
    } else if (auto *tabBar = qobject_cast<QTabBar *>(object)) {
        retranslateQtTabBar(tabBar, localeName);
    } else if (auto *list = qobject_cast<QListWidget *>(object)) {
        retranslateListWidget(list, localeName);
    } else if (auto *table = qobject_cast<QTableWidget *>(object)) {
        retranslateTableWidget(table, localeName);
    } else if (auto *tree = qobject_cast<QTreeWidget *>(object)) {
        retranslateTreeWidget(tree, localeName);
    } else if (auto *menu = qobject_cast<QMenu *>(object)) {
        menu->setTitle(translateValue(menu->title(), localeName));
    }
}

} // namespace

QString normalizedLocaleName(const QString &localeName)
{
    const QString trimmed = localeName.trimmed();
    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Auto");
    }
    if (trimmed.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
        return QStringLiteral("zh_CN");
    }
    if (trimmed.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)) {
        return QStringLiteral("en_US");
    }
    return QStringLiteral("Auto");
}

QString effectiveLocaleName(const QString &localeName)
{
    return translationNameForLocale(localeForName(localeName.isEmpty() ? FluentQt::FluentConfig::instance()->localeName()
                                                                      : localeName));
}

QString localeDisplayName(const QString &localeName)
{
    const QString normalized = normalizedLocaleName(localeName);
    if (normalized == QStringLiteral("zh_CN")) {
        return AppI18n::text("简体中文");
    }
    if (normalized == QStringLiteral("en_US")) {
        return QStringLiteral("English");
    }
    return AppI18n::text("跟随系统");
}

int localeIndex(const QString &localeName)
{
    const QString normalized = normalizedLocaleName(localeName);
    if (normalized == QStringLiteral("zh_CN")) {
        return 1;
    }
    if (normalized == QStringLiteral("en_US")) {
        return 2;
    }
    return 0;
}

QString localeNameForIndex(int index)
{
    if (index == 1) {
        return QStringLiteral("zh_CN");
    }
    if (index == 2) {
        return QStringLiteral("en_US");
    }
    return QStringLiteral("Auto");
}

QString toggledChineseEnglishLocaleName(const QString &localeName)
{
    const QString current = localeName.isEmpty() ? FluentQt::FluentConfig::instance()->localeName() : localeName;
    return effectiveLocaleName(current) == QStringLiteral("zh_CN") ? QStringLiteral("en_US") : QStringLiteral("zh_CN");
}

QString text(const char *sourceText)
{
    return QCoreApplication::translate("AppText", sourceText);
}

bool installTranslators(QCoreApplication *application, const QString &localeName)
{
    if (!application) {
        return false;
    }

    removeTranslator(application, s_appTranslator);
    removeTranslator(application, s_fluentTranslator);

    const QLocale locale = localeForName(localeName.isEmpty() ? FluentQt::FluentConfig::instance()->localeName()
                                                              : localeName);
    bool installed = false;

    auto *fluentTranslator = new FluentQt::FluentTranslator(locale, application);
    if (!fluentTranslator->isEmpty() && application->installTranslator(fluentTranslator)) {
        s_fluentTranslator = fluentTranslator;
        installed = true;
    } else {
        fluentTranslator->deleteLater();
    }

    auto *appTranslator = new QTranslator(application);
    const QString appTranslation = translationNameForLocale(locale);
    if (appTranslator->load(QStringLiteral(":/app/i18n/fluentserialassistant.%1.qm").arg(appTranslation)) &&
        application->installTranslator(appTranslator)) {
        s_appTranslator = appTranslator;
        installed = true;
    } else {
        appTranslator->deleteLater();
    }

    return installed;
}

void retranslateObjectTree(QObject *root, const QString &localeName)
{
    if (!root) {
        return;
    }

    const QString targetLocale = effectiveLocaleName(localeName);
    retranslateObjectProperties(root, targetLocale);
    retranslateSpecialObject(root, targetLocale);
    for (QObject *child : root->children()) {
        retranslateObjectTree(child, targetLocale);
    }
}

void retranslateApplication(const QString &localeName)
{
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!application) {
        return;
    }

    const QString targetLocale = effectiveLocaleName(localeName);
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        retranslateObjectTree(widget, targetLocale);
    }
}

QString applyLocale(const QString &localeName, QCoreApplication *application)
{
    const QString normalized = normalizedLocaleName(localeName);
    installTranslators(application ? application : QCoreApplication::instance(), normalized);
    FluentQt::FluentConfig::instance()->setLocaleName(normalized);
    FluentQt::FluentConfig::instance()->save();
    retranslateApplication(normalized);
    return normalized;
}

} // namespace AppI18n
