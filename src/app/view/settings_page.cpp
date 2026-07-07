#include "app/view/settings_page.h"

#include <FluentQtWidgets/Dialogs/FolderListDialog.h>
#include <FluentQtWidgets/Settings/SettingCard.h>

#include <QtCore/QDir>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtGui/QColor>
#include <QtWidgets/QLabel>

using namespace FluentQt;

namespace {

int themeToIndex(Theme theme)
{
    switch(theme) {
    case Theme::Light:
        return 0;
    case Theme::Dark:
        return 1;
    case Theme::Auto:
        return 2;
    }
    return 0;
}

Theme indexToTheme(int index)
{
    if(index == 1) {
        return Theme::Dark;
    }
    if(index == 2) {
        return Theme::Auto;
    }
    return Theme::Light;
}

int displayModeToIndex(const QString &mode)
{
    if(mode == QStringLiteral("hex")) {
        return 1;
    }
    if(mode == QStringLiteral("mixed")) {
        return 2;
    }
    return 0;
}

QString indexToDisplayMode(int index)
{
    if(index == 1) {
        return QStringLiteral("hex");
    }
    if(index == 2) {
        return QStringLiteral("mixed");
    }
    return QStringLiteral("text");
}

int lineEndingToIndex(const QString &lineEnding)
{
    if(lineEnding == QStringLiteral("cr")) {
        return 1;
    }
    if(lineEnding == QStringLiteral("lf")) {
        return 2;
    }
    if(lineEnding == QStringLiteral("crlf")) {
        return 3;
    }
    return 0;
}

QString indexToLineEnding(int index)
{
    if(index == 1) {
        return QStringLiteral("cr");
    }
    if(index == 2) {
        return QStringLiteral("lf");
    }
    if(index == 3) {
        return QStringLiteral("crlf");
    }
    return QStringLiteral("none");
}

QString defaultExportFolder()
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QDir::homePath() : documents;
}

void hideGroupTitle(SettingCardGroup *group)
{
    if(group && group->titleLabel()) {
        group->titleLabel()->hide();
    }
}

} // namespace

SettingsPage::SettingsPage(QWidget *parent)
    : AppPage(QStringLiteral("设置"),
              QStringLiteral("配置应用外观、终端显示和会话导出行为。"),
              parent)
{
    QSettings settings;

    auto *backButton = new TransparentToolButton(icon(FluentIcon::Return), this);
    backButton->setToolTip(QStringLiteral("返回终端"));
    addHeaderAction(backButton);
    connect(backButton, &TransparentToolButton::clicked, this, &SettingsPage::terminalRequested);

    auto *personalization = new SettingCardGroup(QString(), this);
    hideGroupTitle(personalization);

    auto *themeModeCard =
        new ComboBoxSettingCard(QStringList{QStringLiteral("浅色"), QStringLiteral("深色"), QStringLiteral("跟随系统")},
                                FluentIcon::Brush,
                                QStringLiteral("应用主题"),
                                QStringLiteral("切换并保存应用的明暗主题模式"),
                                personalization);
    themeModeCard->setCurrentIndex(themeToIndex(FluentConfig::instance()->themeMode()));

    const QColor defaultAccent(QStringLiteral("#009faa"));
    auto *themeColorCard = new CustomColorSettingCard(defaultAccent,
                                                      ThemeManager::instance()->accentColor(),
                                                      FluentIcon::Palette,
                                                      QStringLiteral("主题色"),
                                                      QStringLiteral("选择 Fluent 控件的强调色"),
                                                      personalization);

    connect(themeModeCard, &ComboBoxSettingCard::currentIndexChanged, this, [](int index) {
        const Theme theme = indexToTheme(index);
        FluentConfig::instance()->setThemeMode(theme);
        FluentConfig::instance()->save();
        ThemeManager::instance()->setTheme(theme);
    });
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [themeModeCard](Theme theme) {
        const int index = themeToIndex(theme);
        if(themeModeCard->currentIndex() != index) {
            themeModeCard->setCurrentIndex(index);
        }
    });

    connect(themeColorCard, &CustomColorSettingCard::colorChanged, this, [](const QColor &color) {
        FluentConfig::instance()->setThemeColor(color);
        FluentConfig::instance()->save();
        ThemeManager::instance()->setAccentColor(color);
    });
    connect(ThemeManager::instance(), &ThemeManager::accentColorChanged, this, [themeColorCard](const QColor &color) {
        if(themeColorCard->color() != color) {
            themeColorCard->setColor(color);
        }
    });

    personalization->addSettingCards({themeModeCard, themeColorCard});
    addSection(QString(), personalization);

    auto *terminalGroup = new SettingCardGroup(QString(), this);
    hideGroupTitle(terminalGroup);

    auto *displayModeCard =
        new ComboBoxSettingCard(QStringList{QStringLiteral("文本"), QStringLiteral("HEX"), QStringLiteral("混合")},
                                FluentIcon::View,
                                QStringLiteral("默认显示模式"),
                                QStringLiteral("设置终端记录的默认显示方式"),
                                terminalGroup);
    displayModeCard->setCurrentIndex(
        displayModeToIndex(settings.value(QStringLiteral("terminal/displayMode"), QStringLiteral("text")).toString()));

    auto *lineEndingCard =
        new ComboBoxSettingCard(QStringList{QStringLiteral("None"), QStringLiteral("CR"), QStringLiteral("LF"),
                                            QStringLiteral("CRLF")},
                                FluentIcon::Return,
                                QStringLiteral("默认换行"),
                                QStringLiteral("设置发送数据时追加的行结束符"),
                                terminalGroup);
    lineEndingCard->setCurrentIndex(
        lineEndingToIndex(settings.value(QStringLiteral("send/lineEnding"), QStringLiteral("none")).toString()));

    auto *maxRecordsCard =
        new RangeSettingCard(1000,
                             50000,
                             qBound(1000,
                                    settings.value(QStringLiteral("terminal/maxRecords"), 20000).toInt(),
                                    50000),
                             FluentIcon::Document,
                             QStringLiteral("最大显示记录"),
                             QStringLiteral("限制终端视图保留的记录数量"),
                             terminalGroup);

    const QString exportFolder =
        settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
    auto *exportFolderCard = new PushSettingCard(QStringLiteral("选择目录"),
                                                 FluentIcon::Folder,
                                                 QStringLiteral("默认导出目录"),
                                                 exportFolder,
                                                 terminalGroup);

    connect(displayModeCard, &ComboBoxSettingCard::currentIndexChanged, this, [](int index) {
        QSettings settings;
        settings.setValue(QStringLiteral("terminal/displayMode"), indexToDisplayMode(index));
    });
    connect(lineEndingCard, &ComboBoxSettingCard::currentIndexChanged, this, [](int index) {
        QSettings settings;
        settings.setValue(QStringLiteral("send/lineEnding"), indexToLineEnding(index));
    });
    connect(maxRecordsCard, &RangeSettingCard::valueChanged, this, [](int value) {
        QSettings settings;
        settings.setValue(QStringLiteral("terminal/maxRecords"), value);
    });
    connect(exportFolderCard, &PushSettingCard::clicked, this, [exportFolderCard]() {
        QSettings settings;
        const QString current = settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
        FolderPickerDialog dialog(current, exportFolderCard->window());
        if(dialog.exec() != QDialog::Accepted) {
            return;
        }
        const QString folder = dialog.selectedFolder();
        if(folder.isEmpty()) {
            return;
        }
        settings.setValue(QStringLiteral("export/folder"), folder);
        exportFolderCard->setContent(folder);
    });

    terminalGroup->addSettingCards({displayModeCard, lineEndingCard, maxRecordsCard, exportFolderCard});
    addSection(QString(), terminalGroup);
}
