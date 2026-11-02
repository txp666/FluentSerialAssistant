#include "app/view/settings_page.h"

#include "app/core/app_i18n.h"
#include "app/core/font_preferences.h"
#include "app/core/terminal_color_rules.h"
#include "app/core/update_manager.h"
#include "app/serial/virtual_serial_pair.h"
#include "app/view/fluent_tooltip_helper.h"
#include "app/view/terminal_color_dialog.h"

#include <FluentQtWidgets/ConfigItem.h>
#include <FluentQtWidgets/Dialogs/Dialog.h>
#include <FluentQtWidgets/Settings/SettingCard.h>
#include <FluentQtWidgets/Widgets/Button.h>
#include <FluentQtWidgets/Widgets/ComboBox.h>
#include <FluentQtWidgets/Widgets/InfoBar.h>

#include "app/core/app_settings.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QStandardPaths>
#include <QtGui/QColor>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QLabel>

using namespace FluentQt;

namespace {

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

SettingsPage::SettingsPage(QWidget *parent, AppUpdate::UpdateManager *updateManager)
    : AppPage(AppI18n::text("设置"), AppI18n::text("配置应用外观、终端显示和会话导出行为。"), parent)
{
    AppSettings settings;

    auto *backButton = new TransparentToolButton(icon(FluentIcon::Return), this);
    AppUi::setFluentToolTip(backButton, AppI18n::text("返回终端"));
    addHeaderAction(backButton);
    connect(backButton, &TransparentToolButton::clicked, this, &SettingsPage::terminalRequested);

    auto *personalization = new SettingCardGroup(QString(), this);
    hideGroupTitle(personalization);

    auto *themeModeItem = new OptionsConfigItem(
        QStringLiteral("Personalization"), QStringLiteral("ThemeMode"),
        QVariant::fromValue(FluentConfig::instance()->themeMode()),
        {QVariant::fromValue(Theme::Light), QVariant::fromValue(Theme::Dark), QVariant::fromValue(Theme::Auto)}, this);
    auto *themeModeCard = new ComboBoxSettingCard(
        themeModeItem, FluentIcon::Brush, AppI18n::text("应用主题"), AppI18n::text("切换并保存应用的明暗主题模式"),
        {AppI18n::text("浅色"), AppI18n::text("深色"), AppI18n::text("跟随系统")}, personalization);
    auto *languageItem =
        new OptionsConfigItem(QStringLiteral("Personalization"), QStringLiteral("Language"),
                              AppI18n::normalizedLocaleName(FluentConfig::instance()->localeName()),
                              {QStringLiteral("Auto"), QStringLiteral("zh_CN"), QStringLiteral("en_US")}, this);
    auto *languageCard = new ComboBoxSettingCard(
        languageItem, FluentIcon::Language, AppI18n::text("界面语言"),
        AppI18n::text("切换 Fluent 控件和应用翻译资源的显示语言"),
        {AppI18n::text("跟随系统"), AppI18n::text("简体中文"), QStringLiteral("English")}, personalization);
    languageCard->setContent(
        AppI18n::text("当前：%1").arg(AppI18n::localeDisplayName(FluentConfig::instance()->localeName())));

    const QColor defaultAccent(QStringLiteral("#009faa"));
    auto *themeColorCard =
        new CustomColorSettingCard(defaultAccent, ThemeManager::instance()->accentColor(), FluentIcon::Palette,
                                   AppI18n::text("主题色"), AppI18n::text("选择 Fluent 控件的强调色"), personalization);
    const QStringList uiFonts = AppFontPreferences::uiFontFamilies();
    // The font cards have no config item: an imported font joins their lists, and an
    // OptionsConfigItem's options are fixed.
    auto *uiFontCard =
        new ComboBoxSettingCard(nullptr, FluentIcon::Font, AppI18n::text("界面字体"),
                                AppI18n::text("设置按钮、标签和页面文本的字体"), QStringList(), personalization);
    uiFontCard->comboBox()->addItems(uiFonts);
    uiFontCard->comboBox()->setCurrentIndex(fontIndex(uiFonts, AppFontPreferences::currentUiFontFamily()));
    auto *importFontCard =
        new PushSettingCard(AppI18n::text("选择文件"), FluentIcon::Download, AppI18n::text("导入字体"),
                            AppI18n::text("支持 TTF、OTF 和 TTC 字体文件"), personalization);
    m_updateCard = new PushSettingCard(AppI18n::text("检查更新"), FluentIcon::Update, AppI18n::text("应用更新"),
                                       currentVersionText(), personalization);
    m_updateCard->setObjectName(QStringLiteral("applicationUpdateCard"));
    m_updateManager = updateManager ? updateManager : new AppUpdate::UpdateManager(this);
    m_lastUpdateState = m_updateManager->state();

    connect(themeModeItem, &ConfigItem::valueChanged, this, [](const QVariant &value) {
        const Theme theme = value.value<Theme>();
        FluentConfig::instance()->setThemeMode(theme);
        FluentConfig::instance()->save();
        ThemeManager::instance()->setTheme(theme);
    });
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [themeModeItem](Theme theme) { themeModeItem->setValue(QVariant::fromValue(theme)); });
    connect(languageItem, &ConfigItem::valueChanged, this, [languageCard](const QVariant &value) {
        const QString localeName = AppI18n::applyLocale(value.toString());
        languageCard->setContent(AppI18n::text("当前：%1").arg(AppI18n::localeDisplayName(localeName)));
    });
    connect(FluentConfig::instance(), &FluentConfig::localeNameChanged, this,
            [languageItem, languageCard](const QString &localeName) {
                languageItem->setValue(AppI18n::normalizedLocaleName(localeName));
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
    connect(uiFontCard->comboBox(), &ComboBox::currentTextChanged, this,
            [](const QString &family) { AppFontPreferences::setUiFontFamily(family); });
    connect(m_updateCard, &PushSettingCard::clicked, this, &SettingsPage::checkForUpdates);
    connect(m_updateManager, &AppUpdate::UpdateManager::stateChanged, this, &SettingsPage::handleUpdateStateChanged);
    connect(m_updateManager, &AppUpdate::UpdateManager::updateFound, this, &SettingsPage::updateDialogRequested);
    connect(FluentConfig::instance(), &FluentConfig::localeNameChanged, this, &SettingsPage::refreshUpdateCard);
    refreshUpdateCard();

    personalization->addSettingCards(
        {themeModeCard, languageCard, themeColorCard, uiFontCard, importFontCard, m_updateCard});
    addSection(QString(), personalization);

    auto *testGroup = new SettingCardGroup(QString(), this);
    hideGroupTitle(testGroup);
    auto *virtualPair = VirtualSerialPair::instance();
    auto *virtualPairCard =
        new SwitchSettingCard(FluentIcon::Connect, AppI18n::text("内置虚拟串口对"),
                              AppI18n::text("两个会话分别连接 VIRTUAL-A 和 VIRTUAL-B，即可双向收发。\n"
                                            "仅在本应用内可用，不模拟波特率和流控；关闭后断开虚拟连接。"),
                              testGroup);
    virtualPairCard->setObjectName(QStringLiteral("virtualSerialPairSwitch"));
    virtualPairCard->setFixedHeight(92);
    connect(virtualPairCard, &SettingCard::contentChanged, virtualPairCard,
            [virtualPairCard]() { virtualPairCard->setFixedHeight(92); });
    virtualPairCard->setChecked(virtualPair->isEnabled());
    connect(virtualPairCard, &SwitchSettingCard::checkedChanged, virtualPair, &VirtualSerialPair::setEnabled);
    connect(virtualPair, &VirtualSerialPair::enabledChanged, virtualPairCard, &SwitchSettingCard::setChecked);
    testGroup->addSettingCard(virtualPairCard);
    addSection(QString(), testGroup);

    auto *terminalGroup = new SettingCardGroup(QString(), this);
    hideGroupTitle(terminalGroup);

    auto *displayModeItem =
        new OptionsConfigItem(QStringLiteral("Terminal"), QStringLiteral("DisplayMode"),
                              settings.value(QStringLiteral("terminal/displayMode"), QStringLiteral("text")).toString(),
                              {QStringLiteral("text"), QStringLiteral("hex"), QStringLiteral("mixed")}, this);
    auto *displayModeCard = new ComboBoxSettingCard(
        displayModeItem, FluentIcon::View, AppI18n::text("默认显示模式"), AppI18n::text("设置终端记录的默认显示方式"),
        {AppI18n::text("文本"), QStringLiteral("HEX"), AppI18n::text("混合")}, terminalGroup);

    auto *lineEndingItem = new OptionsConfigItem(
        QStringLiteral("Send"), QStringLiteral("LineEnding"),
        settings.value(QStringLiteral("send/lineEnding"), QStringLiteral("none")).toString(),
        {QStringLiteral("none"), QStringLiteral("cr"), QStringLiteral("lf"), QStringLiteral("crlf")}, this);
    auto *lineEndingCard = new ComboBoxSettingCard(
        lineEndingItem, FluentIcon::Return, AppI18n::text("默认换行"), AppI18n::text("设置发送数据时追加的行结束符"),
        {QStringLiteral("None"), QStringLiteral("CR"), QStringLiteral("LF"), QStringLiteral("CRLF")}, terminalGroup);

    auto *maxRecordsCard = new RangeSettingCard(
        1000, 50000, qBound(1000, settings.value(QStringLiteral("terminal/maxRecords"), 20000).toInt(), 50000),
        FluentIcon::Document, AppI18n::text("最大显示记录"), AppI18n::text("限制终端视图保留的记录数量"),
        terminalGroup);
    maxRecordsCard->setObjectName(QStringLiteral("terminalMaxRecordsCard"));
    const QStringList terminalFonts = AppFontPreferences::terminalFontFamilies();
    auto *terminalFontCard =
        new ComboBoxSettingCard(nullptr, FluentIcon::CommandPrompt, AppI18n::text("终端字体"),
                                AppI18n::text("设置接收和发送记录的等宽字体"), QStringList(), terminalGroup);
    terminalFontCard->comboBox()->addItems(terminalFonts);
    terminalFontCard->comboBox()->setCurrentIndex(
        fontIndex(terminalFonts, AppFontPreferences::currentTerminalFontFamily()));
    auto *terminalFontSizeCard =
        new RangeSettingCard(8, 28, AppFontPreferences::currentTerminalFontPointSize(), FluentIcon::FontSize,
                             AppI18n::text("终端字号"), AppI18n::text("设置终端记录的字体大小"), terminalGroup);
    auto *terminalColorCard =
        new PushSettingCard(AppI18n::text("配置"), FluentIcon::Palette, AppI18n::text("内容着色"),
                            AppI18n::text("选择着色示例或自定义规则，适用于所有终端会话"), terminalGroup);
    terminalColorCard->setObjectName(QStringLiteral("terminalColorSettingsCard"));

    const QString exportFolder = settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
    auto *exportFolderCard = new PushSettingCard(AppI18n::text("选择目录"), FluentIcon::Folder,
                                                 AppI18n::text("默认导出目录"), exportFolder, terminalGroup);

    connect(displayModeItem, &ConfigItem::valueChanged, this, [](const QVariant &value) {
        AppSettings settings;
        settings.setValue(QStringLiteral("terminal/displayMode"), value.toString());
    });
    connect(lineEndingItem, &ConfigItem::valueChanged, this, [](const QVariant &value) {
        AppSettings settings;
        settings.setValue(QStringLiteral("send/lineEnding"), value.toString());
    });
    connect(maxRecordsCard, &RangeSettingCard::valueChanged, this, [this](int value) {
        AppSettings settings;
        settings.setValue(QStringLiteral("terminal/maxRecords"), value);
        emit terminalRecordLimitChanged(value);
    });
    connect(terminalFontCard->comboBox(), &ComboBox::currentTextChanged, this, [this](const QString &family) {
        AppFontPreferences::setTerminalFontFamily(family);
        emit terminalFontChanged(AppFontPreferences::currentTerminalFontFamily());
    });
    connect(terminalFontSizeCard, &RangeSettingCard::valueChanged, this, [this](int value) {
        AppFontPreferences::setTerminalFontPointSize(value);
        emit terminalFontChanged(AppFontPreferences::currentTerminalFontFamily());
    });
    connect(terminalColorCard, &PushSettingCard::clicked, this, [this]() {
        TerminalColorDialog dialog(AppTerminal::loadColorConfig(), window());
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        AppTerminal::saveColorConfig(dialog.config());
        emit terminalColorsChanged();
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
        const QString folder = QDir::cleanPath(
            QFileDialog::getExistingDirectory(exportFolderCard->window(), AppI18n::text("选择目录"), current));
        if (folder.isEmpty()) {
            return;
        }
        settings.setValue(QStringLiteral("export/folder"), folder);
        exportFolderCard->setContent(folder);
    });

    terminalGroup->addSettingCards({displayModeCard, lineEndingCard, maxRecordsCard, terminalFontCard,
                                    terminalFontSizeCard, terminalColorCard, exportFolderCard});
    addSection(QString(), terminalGroup);
}

void SettingsPage::checkForUpdates()
{
    if (!m_updateManager) {
        return;
    }
    if (!m_updateManager->release().version.isEmpty() &&
        m_updateManager->state() != AppUpdate::UpdateManager::State::Checking &&
        m_updateManager->state() != AppUpdate::UpdateManager::State::UpToDate) {
        emit updateDialogRequested();
        return;
    }
    if (!m_updateManager->isBusy()) {
        m_updateManager->checkForUpdates(false);
    }
}

void SettingsPage::handleUpdateStateChanged(AppUpdate::UpdateManager::State state)
{
    const auto previousState = m_lastUpdateState;
    m_lastUpdateState = state;
    refreshUpdateCard();

    if (m_updateManager->isAutomaticCheck() || previousState != AppUpdate::UpdateManager::State::Checking) {
        return;
    }
    if (state == AppUpdate::UpdateManager::State::Failed) {
        InfoBar::error(AppI18n::text("检查更新失败"), m_updateManager->statusMessage(), Qt::Horizontal, true, 3500,
                       InfoBarPosition::Top, window());
    } else if (state == AppUpdate::UpdateManager::State::UpToDate) {
        InfoBar::success(AppI18n::text("当前已是最新版本"),
                         AppI18n::text("版本 %1").arg(QCoreApplication::applicationVersion()), Qt::Horizontal, true,
                         2200, InfoBarPosition::Top, window());
    }
}

void SettingsPage::refreshUpdateCard()
{
    if (!m_updateCard || !m_updateManager) {
        return;
    }
    using State = AppUpdate::UpdateManager::State;
    const auto state = m_updateManager->state();
    QString content = m_updateManager->statusMessage();
    if (state == State::Idle) {
        content = currentVersionText();
    } else if (state == State::Checking) {
        content = AppI18n::text("正在检查更新...");
    } else if (state == State::Available) {
        content = AppI18n::text("发现新版本 %1").arg(m_updateManager->release().version);
    } else if (state == State::UpToDate) {
        content = AppI18n::text("当前已是最新版本 %1").arg(QCoreApplication::applicationVersion());
    }
    m_updateCard->setContent(content);
    if (auto *button = m_updateCard->button()) {
        button->setEnabled(state != State::Checking && state != State::Installing);
        button->setText(state == State::Downloading || state == State::Verifying ? AppI18n::text("查看进度")
                        : !m_updateManager->release().version.isEmpty() && state != State::UpToDate
                            ? AppI18n::text("查看更新")
                            : AppI18n::text("检查更新"));
    }
}
