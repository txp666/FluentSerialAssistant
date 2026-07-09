#include "app/view/settings_page.h"

#include "app/core/app_i18n.h"
#include "app/core/font_preferences.h"
#include "app/core/update_checker.h"
#include "app/view/fluent_tooltip_helper.h"

#include <FluentQtWidgets/Dialogs/Dialog.h>
#include <FluentQtWidgets/Dialogs/FolderListDialog.h>
#include <FluentQtWidgets/Settings/SettingCard.h>
#include <FluentQtWidgets/Widgets/Button.h>
#include <FluentQtWidgets/Widgets/ComboBox.h>
#include <FluentQtWidgets/Widgets/InfoBar.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include "app/core/app_settings.h"
#include <QtCore/QStandardPaths>
#include <QtGui/QColor>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QLabel>

using namespace FluentQt;

namespace {

int themeToIndex(Theme theme)
{
    switch (theme) {
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
    if (index == 1) {
        return Theme::Dark;
    }
    if (index == 2) {
        return Theme::Auto;
    }
    return Theme::Light;
}

int displayModeToIndex(const QString &mode)
{
    if (mode == QStringLiteral("hex")) {
        return 1;
    }
    if (mode == QStringLiteral("mixed")) {
        return 2;
    }
    return 0;
}

QString indexToDisplayMode(int index)
{
    if (index == 1) {
        return QStringLiteral("hex");
    }
    if (index == 2) {
        return QStringLiteral("mixed");
    }
    return QStringLiteral("text");
}

int lineEndingToIndex(const QString &lineEnding)
{
    if (lineEnding == QStringLiteral("cr")) {
        return 1;
    }
    if (lineEnding == QStringLiteral("lf")) {
        return 2;
    }
    if (lineEnding == QStringLiteral("crlf")) {
        return 3;
    }
    return 0;
}

QString indexToLineEnding(int index)
{
    if (index == 1) {
        return QStringLiteral("cr");
    }
    if (index == 2) {
        return QStringLiteral("lf");
    }
    if (index == 3) {
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
    if (group && group->titleLabel()) {
        group->titleLabel()->hide();
    }
}

void selectFont(ComboBoxSettingCard *card, const QString &family)
{
    if (!card || !card->comboBox()) {
        return;
    }

    auto *comboBox = card->comboBox();
    if (comboBox->findText(family) < 0) {
        comboBox->addItem(family);
    }
    comboBox->setCurrentText(family);
}

QString currentVersionText()
{
    const QString version = QCoreApplication::applicationVersion();
    return AppI18n::text("当前版本 %1").arg(version.isEmpty() ? AppI18n::text("未知") : version);
}

} // namespace

SettingsPage::SettingsPage(QWidget *parent)
    : AppPage(AppI18n::text("设置"), AppI18n::text("配置应用外观、终端显示和会话导出行为。"), parent)
{
    AppSettings settings;

    auto *backButton = new TransparentToolButton(icon(FluentIcon::Return), this);
    AppUi::setFluentToolTip(backButton, AppI18n::text("返回终端"));
    addHeaderAction(backButton);
    connect(backButton, &TransparentToolButton::clicked, this, &SettingsPage::terminalRequested);

    auto *personalization = new SettingCardGroup(QString(), this);
    hideGroupTitle(personalization);

    auto *themeModeCard = new ComboBoxSettingCard(
        QStringList{AppI18n::text("浅色"), AppI18n::text("深色"), AppI18n::text("跟随系统")}, FluentIcon::Brush,
        AppI18n::text("应用主题"), AppI18n::text("切换并保存应用的明暗主题模式"), personalization);
    themeModeCard->setCurrentIndex(themeToIndex(FluentConfig::instance()->themeMode()));
    auto *languageCard = new ComboBoxSettingCard(
        QStringList{AppI18n::text("跟随系统"), AppI18n::text("简体中文"), QStringLiteral("English")},
        FluentIcon::Language, AppI18n::text("界面语言"),
        AppI18n::text("切换 Fluent 控件和应用翻译资源的显示语言"), personalization);
    languageCard->setCurrentIndex(AppI18n::localeIndex(FluentConfig::instance()->localeName()));
    languageCard->setContent(
        AppI18n::text("当前：%1").arg(AppI18n::localeDisplayName(FluentConfig::instance()->localeName())));

    const QColor defaultAccent(QStringLiteral("#009faa"));
    auto *themeColorCard = new CustomColorSettingCard(defaultAccent, ThemeManager::instance()->accentColor(),
                                                      FluentIcon::Palette, AppI18n::text("主题色"),
                                                      AppI18n::text("选择 Fluent 控件的强调色"), personalization);
    const QStringList uiFonts = AppFontPreferences::uiFontFamilies();
    auto *uiFontCard = new ComboBoxSettingCard(uiFonts, FluentIcon::Font, AppI18n::text("界面字体"),
                                               AppI18n::text("设置按钮、标签和页面文本的字体"), personalization);
    uiFontCard->setCurrentIndex(fontIndex(uiFonts, AppFontPreferences::currentUiFontFamily()));
    auto *importFontCard =
        new PushSettingCard(AppI18n::text("选择文件"), FluentIcon::Download, AppI18n::text("导入字体"),
                            AppI18n::text("支持 TTF、OTF 和 TTC 字体文件"), personalization);
    m_updateCard = new PushSettingCard(AppI18n::text("检查更新"), FluentIcon::Update, AppI18n::text("应用更新"),
                                       currentVersionText(), personalization);
    m_updateChecker = new AppUpdate::UpdateChecker(this);

    connect(themeModeCard, &ComboBoxSettingCard::currentIndexChanged, this, [](int index) {
        const Theme theme = indexToTheme(index);
        FluentConfig::instance()->setThemeMode(theme);
        FluentConfig::instance()->save();
        ThemeManager::instance()->setTheme(theme);
    });
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [themeModeCard](Theme theme) {
        const int index = themeToIndex(theme);
        if (themeModeCard->currentIndex() != index) {
            themeModeCard->setCurrentIndex(index);
        }
    });
    connect(languageCard, &ComboBoxSettingCard::currentIndexChanged, this, [this, languageCard](int index) {
        const QString localeName = AppI18n::applyLocale(AppI18n::localeNameForIndex(index));
        languageCard->setContent(AppI18n::text("当前：%1").arg(AppI18n::localeDisplayName(localeName)));
    });
    connect(FluentConfig::instance(), &FluentConfig::localeNameChanged, this, [languageCard](const QString &localeName) {
        const int index = AppI18n::localeIndex(localeName);
        if (languageCard->currentIndex() != index) {
            languageCard->setCurrentIndex(index);
        }
        languageCard->setContent(AppI18n::text("当前：%1").arg(AppI18n::localeDisplayName(localeName)));
    });

    connect(themeColorCard, &CustomColorSettingCard::colorChanged, this, [](const QColor &color) {
        FluentConfig::instance()->setThemeColor(color);
        FluentConfig::instance()->save();
        ThemeManager::instance()->setAccentColor(color);
    });
    connect(ThemeManager::instance(), &ThemeManager::accentColorChanged, this, [themeColorCard](const QColor &color) {
        if (themeColorCard->color() != color) {
            themeColorCard->setColor(color);
        }
    });
    connect(uiFontCard, &ComboBoxSettingCard::currentTextChanged, this,
            [](const QString &family) { AppFontPreferences::setUiFontFamily(family); });
    connect(m_updateCard, &PushSettingCard::clicked, this, &SettingsPage::checkForUpdates);
    connect(m_updateChecker, &AppUpdate::UpdateChecker::checkStarted, this, &SettingsPage::handleUpdateCheckStarted);
    connect(m_updateChecker, &AppUpdate::UpdateChecker::checkFinished, this, &SettingsPage::handleUpdateCheckFinished);

    personalization->addSettingCards({themeModeCard, languageCard, themeColorCard, uiFontCard, importFontCard,
                                      m_updateCard});
    addSection(QString(), personalization);

    auto *terminalGroup = new SettingCardGroup(QString(), this);
    hideGroupTitle(terminalGroup);

    auto *displayModeCard = new ComboBoxSettingCard(
        QStringList{AppI18n::text("文本"), QStringLiteral("HEX"), AppI18n::text("混合")}, FluentIcon::View,
        AppI18n::text("默认显示模式"), AppI18n::text("设置终端记录的默认显示方式"), terminalGroup);
    displayModeCard->setCurrentIndex(
        displayModeToIndex(settings.value(QStringLiteral("terminal/displayMode"), QStringLiteral("text")).toString()));

    auto *lineEndingCard = new ComboBoxSettingCard(
        QStringList{QStringLiteral("None"), QStringLiteral("CR"), QStringLiteral("LF"), QStringLiteral("CRLF")},
        FluentIcon::Return, AppI18n::text("默认换行"), AppI18n::text("设置发送数据时追加的行结束符"), terminalGroup);
    lineEndingCard->setCurrentIndex(
        lineEndingToIndex(settings.value(QStringLiteral("send/lineEnding"), QStringLiteral("none")).toString()));

    auto *maxRecordsCard = new RangeSettingCard(
        1000, 50000, qBound(1000, settings.value(QStringLiteral("terminal/maxRecords"), 20000).toInt(), 50000),
        FluentIcon::Document, AppI18n::text("最大显示记录"), AppI18n::text("限制终端视图保留的记录数量"),
        terminalGroup);
    const QStringList terminalFonts = AppFontPreferences::terminalFontFamilies();
    auto *terminalFontCard =
        new ComboBoxSettingCard(terminalFonts, FluentIcon::CommandPrompt, AppI18n::text("终端字体"),
                                AppI18n::text("设置接收和发送记录的等宽字体"), terminalGroup);
    terminalFontCard->setCurrentIndex(fontIndex(terminalFonts, AppFontPreferences::currentTerminalFontFamily()));
    auto *terminalFontSizeCard =
        new RangeSettingCard(8, 28, AppFontPreferences::currentTerminalFontPointSize(), FluentIcon::FontSize,
                             AppI18n::text("终端字号"), AppI18n::text("设置终端记录的字体大小"), terminalGroup);

    const QString exportFolder = settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
    auto *exportFolderCard = new PushSettingCard(AppI18n::text("选择目录"), FluentIcon::Folder,
                                                 AppI18n::text("默认导出目录"), exportFolder, terminalGroup);

    connect(displayModeCard, &ComboBoxSettingCard::currentIndexChanged, this, [](int index) {
        AppSettings settings;
        settings.setValue(QStringLiteral("terminal/displayMode"), indexToDisplayMode(index));
    });
    connect(lineEndingCard, &ComboBoxSettingCard::currentIndexChanged, this, [](int index) {
        AppSettings settings;
        settings.setValue(QStringLiteral("send/lineEnding"), indexToLineEnding(index));
    });
    connect(maxRecordsCard, &RangeSettingCard::valueChanged, this, [](int value) {
        AppSettings settings;
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
            QFileDialog::getOpenFileName(importFontCard->window(), AppI18n::text("选择字体文件"), QDir::homePath(),
                                         AppI18n::text("字体文件 (*.ttf *.otf *.ttc);;所有文件 (*)"));
        if (filePath.isEmpty()) {
            return;
        }

        const AppFontPreferences::FontImportResult result = AppFontPreferences::importFontFile(filePath);
        if (!result.ok || result.families.isEmpty()) {
            MessageBox dialog(AppI18n::text("导入字体失败"),
                              result.errorMessage.isEmpty() ? AppI18n::text("无法加载该字体文件。")
                                                            : result.errorMessage,
                              importFontCard->window());
            dialog.hideCancelButton();
            dialog.setClosableOnMaskClicked(true);
            dialog.setDraggable(true);
            dialog.exec();
            return;
        }

        const QString family = result.families.first();
        importFontCard->setContent(family);
        selectFont(uiFontCard, family);
        selectFont(terminalFontCard, family);
        emit terminalFontChanged(family);
    });
    connect(exportFolderCard, &PushSettingCard::clicked, this, [exportFolderCard]() {
        AppSettings settings;
        const QString current = settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
        FolderPickerDialog dialog(current, exportFolderCard->window());
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const QString folder = dialog.selectedFolder();
        if (folder.isEmpty()) {
            return;
        }
        settings.setValue(QStringLiteral("export/folder"), folder);
        exportFolderCard->setContent(folder);
    });

    terminalGroup->addSettingCards(
        {displayModeCard, lineEndingCard, maxRecordsCard, terminalFontCard, terminalFontSizeCard, exportFolderCard});
    addSection(QString(), terminalGroup);
}

void SettingsPage::checkForUpdates()
{
    if (m_updateChecker && !m_updateChecker->isChecking()) {
        m_updateChecker->checkLatestRelease();
    }
}

void SettingsPage::handleUpdateCheckStarted()
{
    if (!m_updateCard) {
        return;
    }
    m_updateCard->setContent(AppI18n::text("正在检查更新..."));
    if (m_updateCard->button()) {
        m_updateCard->button()->setEnabled(false);
    }
}

void SettingsPage::handleUpdateCheckFinished(bool ok, bool updateAvailable, const QString &currentVersion,
                                             const QString &latestVersion, const QUrl &releaseUrl,
                                             const QString &message)
{
    if (m_updateCard && m_updateCard->button()) {
        m_updateCard->button()->setEnabled(true);
    }

    if (!ok) {
        if (m_updateCard) {
            m_updateCard->setContent(AppI18n::text("检查失败：%1").arg(message));
        }
        InfoBar::error(AppI18n::text("检查更新失败"), message, Qt::Horizontal, true, 3500, InfoBarPosition::Top,
                       window());
        return;
    }

    if (!updateAvailable) {
        if (m_updateCard) {
            m_updateCard->setContent(AppI18n::text("当前已是最新版本 %1").arg(currentVersion));
        }
        InfoBar::success(AppI18n::text("当前已是最新版本"), AppI18n::text("版本 %1").arg(currentVersion),
                         Qt::Horizontal, true, 2200, InfoBarPosition::Top, window());
        return;
    }

    if (m_updateCard) {
        m_updateCard->setContent(AppI18n::text("发现新版本 %1").arg(latestVersion));
    }

    auto *bar = InfoBar::info(AppI18n::text("发现新版本"),
                              AppI18n::text("当前 %1，最新 %2").arg(currentVersion, latestVersion), Qt::Horizontal,
                              true, 10000, InfoBarPosition::Top, window());
    auto *openButton = new PushButton(icon(FluentIcon::Link), AppI18n::text("打开发布页"), bar);
    openButton->setEnabled(releaseUrl.isValid());
    bar->addWidget(openButton);
    connect(openButton, &PushButton::clicked, this, [releaseUrl]() { QDesktopServices::openUrl(releaseUrl); });
}
