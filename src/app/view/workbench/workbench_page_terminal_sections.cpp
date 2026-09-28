#include "app/view/workbench/workbench_page_internal.h"

#include "app/core/app_i18n.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QShortcut>

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

namespace {

bool isReceiveHexMode(const QString &mode) { return mode == QStringLiteral("hex"); }

QString shortcutReceiveDisplayModeLabel(bool hexMode)
{
    return hexMode ? QStringLiteral("HEX") : AppI18n::text("文本");
}

QString nextReceiveDisplayMode(bool hexMode) { return hexMode ? QStringLiteral("text") : QStringLiteral("hex"); }

} // namespace

QWidget *WorkbenchPage::createTerminalSection()
{
    auto *section = new HeaderCardWidget(this);
    hideCardTitle(section);
    auto *root = cardBody(section, 10);
    section->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    section->headerLayout()->addWidget(new BodyLabel(QStringLiteral("RX"), section), 0, Qt::AlignVCenter);
    m_rxCounterLabel = new StrongBodyLabel(QStringLiteral("0 B"), section);
    section->headerLayout()->addWidget(m_rxCounterLabel, 0, Qt::AlignVCenter);
    section->headerLayout()->addWidget(new BodyLabel(QStringLiteral("TX"), section), 0, Qt::AlignVCenter);
    m_txCounterLabel = new StrongBodyLabel(QStringLiteral("0 B"), section);
    section->headerLayout()->addWidget(m_txCounterLabel, 0, Qt::AlignVCenter);
    section->headerLayout()->addWidget(new BodyLabel(QStringLiteral("RX/s"), section), 0, Qt::AlignVCenter);
    m_rxRateLabel = new StrongBodyLabel(QStringLiteral("0 B/s"), section);
    section->headerLayout()->addWidget(m_rxRateLabel, 0, Qt::AlignVCenter);
    section->headerLayout()->addWidget(new BodyLabel(QStringLiteral("TX/s"), section), 0, Qt::AlignVCenter);
    m_txRateLabel = new StrongBodyLabel(QStringLiteral("0 B/s"), section);
    section->headerLayout()->addWidget(m_txRateLabel, 0, Qt::AlignVCenter);
    m_connectionTimeLabel = new CaptionLabel(AppI18n::text("未连接"), section);
    section->headerLayout()->addWidget(m_connectionTimeLabel, 0, Qt::AlignVCenter);
    section->headerLayout()->addStretch(1);

    auto *searchButton = new TransparentToolButton(icon(FluentIcon::Search), section);
    searchButton->setObjectName(QStringLiteral("terminalSearchButton"));
    AppUi::setFluentToolTip(searchButton, AppI18n::text("搜索"));
    auto *plotButton = new TransparentToolButton(icon(FluentIcon::PieSingle), section);
    AppUi::setFluentToolTip(plotButton, AppI18n::text("快速绘图"));
    auto *dataTableButton = new TransparentToolButton(icon(FluentIcon::View), section);
    AppUi::setFluentToolTip(dataTableButton, AppI18n::text("数据表格"));
    auto *themeButton = new TransparentToolButton(icon(FluentIcon::Constract), section);
    AppUi::setFluentToolTip(themeButton, AppI18n::text("切换主题"));
    auto *languageButton = new TransparentToolButton(icon(FluentIcon::Language), section);
    AppUi::setFluentToolTip(languageButton, AppI18n::text("切换语言"));
    m_receiveModeButton = new TransparentToolButton(icon(FluentIcon::Font), section);
    AppUi::installFluentToolTip(m_receiveModeButton);
    auto *settingsButton = new TransparentToolButton(icon(FluentIcon::Setting), section);
    AppUi::setFluentToolTip(settingsButton, AppI18n::text("设置"));
    for (ToolButton *button : {searchButton, plotButton, dataTableButton, themeButton, languageButton,
                               m_receiveModeButton, settingsButton}) {
        button->setFixedSize(CompactControlHeight, CompactControlHeight);
        button->setIconSize(QSize(16, 16));
    }
    section->headerLayout()->addSpacing(4);
    section->headerLayout()->addWidget(searchButton, 0, Qt::AlignVCenter);
    section->headerLayout()->addWidget(plotButton, 0, Qt::AlignVCenter);
    section->headerLayout()->addWidget(dataTableButton, 0, Qt::AlignVCenter);
    section->headerLayout()->addWidget(themeButton, 0, Qt::AlignVCenter);
    section->headerLayout()->addWidget(languageButton, 0, Qt::AlignVCenter);
    section->headerLayout()->addWidget(m_receiveModeButton, 0, Qt::AlignVCenter);
    section->headerLayout()->addWidget(settingsButton, 0, Qt::AlignVCenter);

    // A search stays open while the user reads the terminal or changes focus.
    // The native tool-window title bar also provides dragging and an explicit close action.
    auto *searchPanel = new QWidget(this, Qt::Tool | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    m_terminalSearchWindow = searchPanel;
    searchPanel->setObjectName(QStringLiteral("terminalSearchWindow"));
    searchPanel->setWindowTitle(AppI18n::text("终端搜索"));
    searchPanel->setWindowIcon(icon(FluentIcon::Search));
    searchPanel->setAttribute(Qt::WA_QuitOnClose, false);
    searchPanel->setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
    FluentStyleSheet::setCustomStyleSheet(
        searchPanel, QStringLiteral("QWidget#terminalSearchWindow { background-color: #f9f9f9; }"),
        QStringLiteral("QWidget#terminalSearchWindow { background-color: #202020; }"));
    auto *searchLayout = new QVBoxLayout(searchPanel);
    searchLayout->setContentsMargins(12, 12, 12, 12);
    searchLayout->setSpacing(10);
    searchLayout->setSizeConstraint(QLayout::SetFixedSize);

    auto *closeSearch = new QShortcut(QKeySequence(Qt::Key_Escape), searchPanel);
    connect(closeSearch, &QShortcut::activated, searchPanel, &QWidget::close);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(8);
    m_terminalSearchEdit = new SearchLineEdit(searchPanel);
    m_terminalSearchEdit->setObjectName(QStringLiteral("terminalSearchEdit"));
    m_terminalSearchEdit->setPlaceholderText(AppI18n::text("搜索终端内容"));
    m_terminalSearchEdit->setClearButtonEnabled(true);
    m_terminalSearchEdit->setFixedHeight(CompactControlHeight);
    m_terminalSearchEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const QString searchEditStyle =
        QStringLiteral("QLineEdit#terminalSearchEdit { min-height: 30px; max-height: 30px; }");
    FluentStyleSheet::setCustomStyleSheet(m_terminalSearchEdit, searchEditStyle, searchEditStyle);
    m_terminalSearchEdit->hBoxLayout()->setContentsMargins(4, 0, 4, 0);
    for (auto *button : {m_terminalSearchEdit->clearButton(), m_terminalSearchEdit->searchButton()}) {
        button->setFixedSize(28, 24);
        m_terminalSearchEdit->hBoxLayout()->setAlignment(button, Qt::AlignRight | Qt::AlignVCenter);
    }
    m_terminalSearchPrevButton = new TransparentToolButton(icon(FluentIcon::Up), searchPanel);
    m_terminalSearchPrevButton->setObjectName(QStringLiteral("terminalSearchPrevious"));
    m_terminalSearchPrevButton->setAccessibleName(AppI18n::text("上一个匹配"));
    m_terminalSearchNextButton = new TransparentToolButton(icon(FluentIcon::Down), searchPanel);
    m_terminalSearchNextButton->setObjectName(QStringLiteral("terminalSearchNext"));
    m_terminalSearchNextButton->setAccessibleName(AppI18n::text("下一个匹配"));
    for (ToolButton *button : {m_terminalSearchPrevButton, m_terminalSearchNextButton}) {
        button->setEnabled(false);
        button->setFixedSize(CompactControlHeight, CompactControlHeight);
        button->setIconSize(QSize(16, 16));
        const QString buttonStyle =
            QStringLiteral("QToolButton#%1 { min-width: 32px; max-width: 32px; min-height: 32px; "
                           "max-height: 32px; padding: 0px; }")
                .arg(button->objectName());
        FluentStyleSheet::setCustomStyleSheet(button, buttonStyle, buttonStyle);
    }
    searchRow->addWidget(m_terminalSearchEdit, 1, Qt::AlignVCenter);
    searchRow->addWidget(m_terminalSearchPrevButton, 0, Qt::AlignVCenter);
    searchRow->addWidget(m_terminalSearchNextButton, 0, Qt::AlignVCenter);
    searchLayout->addLayout(searchRow);

    auto *searchOptionsRow = new QHBoxLayout;
    searchOptionsRow->setSpacing(8);
    m_terminalSearchCaseCheck = new CheckBox(QStringLiteral("Aa"), searchPanel);
    m_terminalSearchCaseCheck->setObjectName(QStringLiteral("terminalSearchCase"));
    m_terminalSearchCaseCheck->setAccessibleName(AppI18n::text("大小写敏感"));
    m_terminalSearchCaseCheck->setFixedHeight(CompactControlHeight);
    setFixedControlWidth(m_terminalSearchCaseCheck, 54);
    m_terminalSearchRegexCheck = new CheckBox(QStringLiteral(".*"), searchPanel);
    m_terminalSearchRegexCheck->setObjectName(QStringLiteral("terminalSearchRegex"));
    m_terminalSearchRegexCheck->setAccessibleName(AppI18n::text("正则搜索"));
    m_terminalSearchRegexCheck->setFixedHeight(CompactControlHeight);
    setFixedControlWidth(m_terminalSearchRegexCheck, 54);
    m_terminalFilterCombo = new ComboBox(searchPanel);
    m_terminalFilterCombo->setObjectName(QStringLiteral("terminalSearchFilter"));
    m_terminalFilterCombo->addItem(AppI18n::text("全部"), QIcon(), QStringLiteral("all"));
    m_terminalFilterCombo->addItem(AppI18n::text("仅接收"), QIcon(), QStringLiteral("rx"));
    m_terminalFilterCombo->addItem(AppI18n::text("仅发送"), QIcon(), QStringLiteral("tx"));
    m_terminalFilterCombo->setFixedHeight(CompactControlHeight);
    setFixedControlWidth(m_terminalFilterCombo, 104);
    const QString filterStyle =
        QStringLiteral("QPushButton#terminalSearchFilter { min-height: 30px; max-height: 30px; "
                       "padding: 0px 31px 0px 11px; }");
    FluentStyleSheet::setCustomStyleSheet(m_terminalFilterCombo, filterStyle, filterStyle);
    m_terminalSummaryLabel = new CaptionLabel(AppI18n::text("显示 0 条"), searchPanel);
    m_terminalSummaryLabel->setObjectName(QStringLiteral("terminalSearchSummary"));
    m_terminalSummaryLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_terminalSummaryLabel->setFixedHeight(CompactControlHeight);
    m_terminalSummaryLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setFixedControlWidth(m_terminalSummaryLabel, 148);
    searchOptionsRow->addWidget(m_terminalSearchCaseCheck, 0, Qt::AlignVCenter);
    searchOptionsRow->addWidget(m_terminalSearchRegexCheck, 0, Qt::AlignVCenter);
    searchOptionsRow->addWidget(m_terminalFilterCombo, 0, Qt::AlignVCenter);
    searchOptionsRow->addWidget(m_terminalSummaryLabel, 1, Qt::AlignVCenter);
    searchLayout->addLayout(searchOptionsRow);

    m_terminalView = new TextBrowser(section);
    m_terminalView->setReadOnly(true);
    m_terminalView->setLineWrapMode(QTextEdit::WidgetWidth);
    m_terminalView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_terminalView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_terminalView->document()->setMaximumBlockCount(maxRecordCount());
    m_terminalView->setMinimumHeight(420);
    applyTerminalFont();

    root->addWidget(m_terminalView, 1);

    connect(searchButton, &TransparentToolButton::clicked, this, [this, searchButton]() {
        m_terminalSearchWindow->adjustSize();
        if (!m_terminalSearchWindowPositioned) {
            QPoint position = searchButton->mapToGlobal(
                QPoint(searchButton->width() - m_terminalSearchWindow->width(), searchButton->height() + 8));
            if (QScreen *screen = QGuiApplication::screenAt(searchButton->mapToGlobal(QPoint(0, 0)))) {
                const QRect available = screen->availableGeometry();
                position.setX(qBound(available.left(), position.x(),
                                     qMax(available.left(), available.right() - m_terminalSearchWindow->width())));
                position.setY(qBound(available.top(), position.y(),
                                     qMax(available.top(), available.bottom() - m_terminalSearchWindow->height())));
            }
            m_terminalSearchWindow->move(position);
            m_terminalSearchWindowPositioned = true;
        }
        m_terminalSearchWindow->show();
        m_terminalSearchWindow->raise();
        m_terminalSearchWindow->activateWindow();
        if (m_terminalSearchEdit) {
            m_terminalSearchEdit->setFocus(Qt::OtherFocusReason);
            m_terminalSearchEdit->selectAll();
        }
    });
    connect(plotButton, &TransparentToolButton::clicked, this, &WorkbenchPage::showQuickPlotWindow);
    connect(dataTableButton, &TransparentToolButton::clicked, this, &WorkbenchPage::showDataTableWindow);
    connect(themeButton, &TransparentToolButton::clicked, this, []() {
        const Theme current = ThemeManager::instance()->effectiveTheme();
        const Theme next = current == Theme::Dark ? Theme::Light : Theme::Dark;
        FluentConfig::instance()->setThemeMode(next);
        FluentConfig::instance()->save();
        ThemeManager::instance()->setTheme(next);
    });
    connect(languageButton, &TransparentToolButton::clicked, this,
            []() { AppI18n::applyLocale(AppI18n::toggledChineseEnglishLocaleName()); });
    connect(m_receiveModeButton, &TransparentToolButton::clicked, this, [this]() {
        if (m_displayModeSegment) {
            m_displayModeSegment->setCurrentItem(nextReceiveDisplayMode(isReceiveHexMode(currentDisplayMode())));
        }
    });
    connect(settingsButton, &TransparentToolButton::clicked, this, &WorkbenchPage::settingsRequested);
    connect(m_terminalSearchEdit, &SearchLineEdit::textChanged, this, [this]() {
        resetTerminalSearchNavigation();
        renderTerminal(true);
    });
    connect(m_terminalSearchEdit, &SearchLineEdit::searchSignal, this,
            [this](const QString &) { moveTerminalSearchMatch(1); });
    connect(m_terminalSearchEdit, &SearchLineEdit::clearSignal, this, [this]() {
        resetTerminalSearchNavigation();
        renderTerminal();
    });
    connect(m_terminalSearchPrevButton, &ToolButton::clicked, this, [this]() { moveTerminalSearchMatch(-1); });
    connect(m_terminalSearchNextButton, &ToolButton::clicked, this, [this]() { moveTerminalSearchMatch(1); });
    connect(m_terminalSearchCaseCheck, &CheckBox::toggled, this, [this](bool) {
        resetTerminalSearchNavigation();
        renderTerminal(true);
    });
    connect(m_terminalSearchRegexCheck, &CheckBox::toggled, this, [this](bool) {
        resetTerminalSearchNavigation();
        renderTerminal(true);
    });
    connect(m_terminalFilterCombo, &ComboBox::currentIndexChanged, this, [this](int) { renderTerminal(); });
    updateReceiveModeButton();

    return section;
}

void WorkbenchPage::updateReceiveModeButton()
{
    if (!m_receiveModeButton) {
        return;
    }

    const bool hexMode = isReceiveHexMode(currentDisplayMode());
    const QString currentLabel = shortcutReceiveDisplayModeLabel(hexMode);
    const QString nextLabel = shortcutReceiveDisplayModeLabel(!hexMode);
    m_receiveModeButton->setIcon(icon(hexMode ? FluentIcon::Code : FluentIcon::Font));
    m_receiveModeButton->setToolTip(AppI18n::text("当前接收显示为 %1，点击切换为 %2").arg(currentLabel, nextLabel));
}

QWidget *WorkbenchPage::createSendSection()
{
    auto *section = new HeaderCardWidget(this);
    hideCardHeader(section);
    auto *root = cardBody(section, 10);

    auto *sendRow = new QHBoxLayout;
    sendRow->setSpacing(10);
    m_sendEdit = new PlainTextEdit(section);
    m_sendEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_sendEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_sendEdit->setMinimumHeight(112);
    m_sendEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_sendEdit->installEventFilter(this);
    sendRow->addWidget(m_sendEdit, 1);

    m_sendModeButton = new PushButton(section);
    setFixedControlWidth(m_sendModeButton, 72);
    m_sendModeButton->setMinimumHeight(112);
    m_sendModeButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    AppUi::installFluentToolTip(m_sendModeButton, ToolTipPosition::Left);
    sendRow->addWidget(m_sendModeButton);

    m_sendButton = new PrimaryPushButton(icon(FluentIcon::Send), QString(), section);
    AppUi::setFluentToolTip(m_sendButton, AppI18n::text("发送"), ToolTipPosition::Left);
    m_sendButton->setIconSize(QSize(40, 40));
    setFixedControlWidth(m_sendButton, 112);
    m_sendButton->setMinimumHeight(112);
    sendRow->addWidget(m_sendButton);

    root->addLayout(sendRow);

    connect(m_sendModeButton, &PushButton::clicked, this, [this]() {
        if (m_hexSendCheck) {
            m_hexSendCheck->setChecked(!m_hexSendCheck->isChecked());
        }
    });
    connect(m_sendButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::sendCurrentPayload);
    updateSendModeButton();

    return section;
}

void WorkbenchPage::updateSendModeButton()
{
    if (!m_sendModeButton) {
        return;
    }

    const bool hexMode = m_hexSendCheck && m_hexSendCheck->isChecked();
    m_sendModeButton->setText(hexMode ? QStringLiteral("HEX") : AppI18n::text("文本"));
    m_sendModeButton->setToolTip(hexMode ? AppI18n::text("当前为 HEX 发送，点击切换为文本")
                                         : AppI18n::text("当前为文本发送，点击切换为 HEX"));
}
