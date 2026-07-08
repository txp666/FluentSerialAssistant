#include "app/view/settings_page.h"

#include "app/core/font_preferences.h"
#include "app/core/update_checker.h"

#include <FluentQtWidgets/Dialogs/FolderListDialog.h>
#include <FluentQtWidgets/Settings/SettingCard.h>
#include <FluentQtWidgets/Widgets/ComboBox.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtGui/QColor>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>

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

int fontIndex(const QStringList &families, const QString &family)
{
    const int index = families.indexOf(family);
    return index >= 0 ? index : 0;
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

void selectFont(ComboBoxSettingCard *card, const QString &family)
{
    if(!card || !card->comboBox()) {
        return;
    }

    auto *comboBox = card->comboBox();
    if(comboBox->findText(family) < 0) {
        comboBox->addItem(family);
    }
    comboBox->setCurrentText(family);
}

QString currentVersionText()
{
    const QString version = QCoreApplication::applicationVersion();
    return QStringLiteral("当前版本 %1").arg(version.isEmpty() ? QStringLiteral("未知") : version);
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
    const QStringList uiFonts = AppFontPreferences::uiFontFamilies();
    auto *uiFontCard = new ComboBoxSettingCard(uiFonts,
                                               FluentIcon::Font,
                                               QStringLiteral("界面字体"),
                                               QStringLiteral("设置按钮、标签和页面文本的字体"),
                                               personalization);
    uiFontCard->setCurrentIndex(fontIndex(uiFonts, AppFontPreferences::currentUiFontFamily()));
    auto *importFontCard = new PushSettingCard(QStringLiteral("选择文件"),
                                               FluentIcon::Download,
                                               QStringLiteral("导入字体"),
                                               QStringLiteral("支持 TTF、OTF 和 TTC 字体文件"),
                                               personalization);
    m_updateCard = new PushSettingCard(QStringLiteral("检查更新"),
                                       FluentIcon::Update,
                                       QStringLiteral("应用更新"),
                                       currentVersionText(),
                                       personalization);
    m_updateChecker = new AppUpdate::UpdateChecker(this);

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
    connect(uiFontCard, &ComboBoxSettingCard::currentTextChanged, this, [](const QString &family) {
        AppFontPreferences::setUiFontFamily(family);
    });
    connect(m_updateCard, &PushSettingCard::clicked, this, &SettingsPage::checkForUpdates);
    connect(m_updateChecker, &AppUpdate::UpdateChecker::checkStarted, this, &SettingsPage::handleUpdateCheckStarted);
    connect(m_updateChecker,
            &AppUpdate::UpdateChecker::checkFinished,
            this,
            &SettingsPage::handleUpdateCheckFinished);

    personalization->addSettingCards({themeModeCard, themeColorCard, uiFontCard, importFontCard, m_updateCard});
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
    const QStringList terminalFonts = AppFontPreferences::terminalFontFamilies();
    auto *terminalFontCard = new ComboBoxSettingCard(terminalFonts,
                                                    FluentIcon::CommandPrompt,
                                                    QStringLiteral("终端字体"),
                                                    QStringLiteral("设置接收和发送记录的等宽字体"),
                                                    terminalGroup);
    terminalFontCard->setCurrentIndex(fontIndex(terminalFonts, AppFontPreferences::currentTerminalFontFamily()));
    auto *terminalFontSizeCard = new RangeSettingCard(8,
                                                      28,
                                                      AppFontPreferences::currentTerminalFontPointSize(),
                                                      FluentIcon::FontSize,
                                                      QStringLiteral("终端字号"),
                                                      QStringLiteral("设置终端记录的字体大小"),
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
    connect(terminalFontCard, &ComboBoxSettingCard::currentTextChanged, this, [this](const QString &family) {
        AppFontPreferences::setTerminalFontFamily(family);
        emit terminalFontChanged(AppFontPreferences::currentTerminalFontFamily());
    });
    connect(terminalFontSizeCard, &RangeSettingCard::valueChanged, this, [this](int value) {
        AppFontPreferences::setTerminalFontPointSize(value);
        emit terminalFontChanged(AppFontPreferences::currentTerminalFontFamily());
    });
    connect(importFontCard, &PushSettingCard::clicked, this, [this, importFontCard, uiFontCard, terminalFontCard]() {
        const QString filePath =
            QFileDialog::getOpenFileName(importFontCard->window(),
                                         QStringLiteral("选择字体文件"),
                                         QDir::homePath(),
                                         QStringLiteral("字体文件 (*.ttf *.otf *.ttc);;所有文件 (*)"));
        if(filePath.isEmpty()) {
            return;
        }

        const AppFontPreferences::FontImportResult result = AppFontPreferences::importFontFile(filePath);
        if(!result.ok || result.families.isEmpty()) {
            QMessageBox::warning(importFontCard->window(),
                                 QStringLiteral("导入字体失败"),
                                 result.errorMessage.isEmpty() ? QStringLiteral("无法加载该字体文件。")
                                                               : result.errorMessage);
            return;
        }

        const QString family = result.families.first();
        importFontCard->setContent(family);
        selectFont(uiFontCard, family);
        selectFont(terminalFontCard, family);
        emit terminalFontChanged(family);
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

    terminalGroup->addSettingCards({displayModeCard,
                                    lineEndingCard,
                                    maxRecordsCard,
                                    terminalFontCard,
                                    terminalFontSizeCard,
                                    exportFolderCard});
    addSection(QString(), terminalGroup);
}

void SettingsPage::checkForUpdates()
{
    if(m_updateChecker && !m_updateChecker->isChecking()) {
        m_updateChecker->checkLatestRelease();
    }
}

void SettingsPage::handleUpdateCheckStarted()
{
    if(!m_updateCard) {
        return;
    }
    m_updateCard->setContent(QStringLiteral("正在检查更新..."));
    if(m_updateCard->button()) {
        m_updateCard->button()->setEnabled(false);
    }
}

void SettingsPage::handleUpdateCheckFinished(bool ok,
                                             bool updateAvailable,
                                             const QString &currentVersion,
                                             const QString &latestVersion,
                                             const QUrl &releaseUrl,
                                             const QString &message)
{
    if(m_updateCard && m_updateCard->button()) {
        m_updateCard->button()->setEnabled(true);
    }

    if(!ok) {
        if(m_updateCard) {
            m_updateCard->setContent(QStringLiteral("检查失败：%1").arg(message));
        }
        QMessageBox::warning(this, QStringLiteral("检查更新失败"), message);
        return;
    }

    if(!updateAvailable) {
        if(m_updateCard) {
            m_updateCard->setContent(QStringLiteral("当前已是最新版本 %1").arg(currentVersion));
        }
        QMessageBox::information(this,
                                 QStringLiteral("检查更新"),
                                 QStringLiteral("当前已是最新版本：%1").arg(currentVersion));
        return;
    }

    if(m_updateCard) {
        m_updateCard->setContent(QStringLiteral("发现新版本 %1").arg(latestVersion));
    }

    QMessageBox messageBox(this);
    messageBox.setIcon(QMessageBox::Information);
    messageBox.setWindowTitle(QStringLiteral("发现新版本"));
    messageBox.setText(QStringLiteral("当前版本：%1\n最新版本：%2").arg(currentVersion, latestVersion));
    messageBox.setInformativeText(QStringLiteral("可以打开 GitHub Releases 下载最新版本。"));
    QPushButton *openButton = messageBox.addButton(QStringLiteral("打开发布页"), QMessageBox::AcceptRole);
    messageBox.addButton(QStringLiteral("稍后"), QMessageBox::RejectRole);
    messageBox.exec();

    if(messageBox.clickedButton() == openButton && releaseUrl.isValid()) {
        QDesktopServices::openUrl(releaseUrl);
    }
}
