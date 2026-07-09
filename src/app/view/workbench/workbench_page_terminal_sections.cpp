#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

QWidget *WorkbenchPage::createTerminalSection()
{
    auto *section = new HeaderCardWidget(this);
    hideCardTitle(section);
    auto *root = cardBody(section, 10);
    section->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    section->headerLayout()->addStretch(1);
    section->headerLayout()->addWidget(new BodyLabel(QStringLiteral("RX"), section));
    m_rxCounterLabel = new StrongBodyLabel(QStringLiteral("0 B"), section);
    section->headerLayout()->addWidget(m_rxCounterLabel);
    section->headerLayout()->addWidget(new BodyLabel(QStringLiteral("TX"), section));
    m_txCounterLabel = new StrongBodyLabel(QStringLiteral("0 B"), section);
    section->headerLayout()->addWidget(m_txCounterLabel);
    section->headerLayout()->addWidget(new BodyLabel(QStringLiteral("RX/s"), section));
    m_rxRateLabel = new StrongBodyLabel(QStringLiteral("0 B/s"), section);
    section->headerLayout()->addWidget(m_rxRateLabel);
    section->headerLayout()->addWidget(new BodyLabel(QStringLiteral("TX/s"), section));
    m_txRateLabel = new StrongBodyLabel(QStringLiteral("0 B/s"), section);
    section->headerLayout()->addWidget(m_txRateLabel);
    m_connectionTimeLabel = new CaptionLabel(QStringLiteral("未连接"), section);
    section->headerLayout()->addWidget(m_connectionTimeLabel);

    auto *searchButton = new TransparentToolButton(icon(FluentIcon::Search), section);
    searchButton->setToolTip(QStringLiteral("搜索"));
    auto *themeButton = new TransparentToolButton(icon(FluentIcon::Constract), section);
    themeButton->setToolTip(QStringLiteral("切换主题"));
    auto *settingsButton = new TransparentToolButton(icon(FluentIcon::Setting), section);
    settingsButton->setToolTip(QStringLiteral("设置"));
    for (ToolButton *button : {searchButton, themeButton, settingsButton}) {
        button->setFixedSize(CompactControlHeight, CompactControlHeight);
        button->setIconSize(QSize(16, 16));
    }
    section->headerLayout()->addSpacing(4);
    section->headerLayout()->addWidget(searchButton);
    section->headerLayout()->addWidget(themeButton);
    section->headerLayout()->addWidget(settingsButton);

    auto *searchView = new FlyoutView(QStringLiteral("终端搜索"), QString(), icon(FluentIcon::Search), QPixmap(), true);
    auto *searchPanel = new QWidget(searchView);
    searchPanel->setFixedWidth(420);
    auto *searchLayout = new QVBoxLayout(searchPanel);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(10);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(8);
    m_terminalSearchEdit = new SearchLineEdit(searchPanel);
    m_terminalSearchEdit->setPlaceholderText(QStringLiteral("搜索终端内容"));
    m_terminalSearchEdit->setClearButtonEnabled(true);
    m_terminalSearchEdit->setFixedHeight(CompactControlHeight);
    m_terminalSearchEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_terminalSearchPrevButton = new TransparentToolButton(icon(FluentIcon::Up), searchPanel);
    m_terminalSearchPrevButton->setToolTip(QStringLiteral("上一个匹配"));
    m_terminalSearchNextButton = new TransparentToolButton(icon(FluentIcon::Down), searchPanel);
    m_terminalSearchNextButton->setToolTip(QStringLiteral("下一个匹配"));
    for (ToolButton *button : {m_terminalSearchPrevButton, m_terminalSearchNextButton}) {
        button->setEnabled(false);
        button->setFixedSize(CompactControlHeight, CompactControlHeight);
        button->setIconSize(QSize(16, 16));
    }
    searchRow->addWidget(m_terminalSearchEdit, 1);
    searchRow->addWidget(m_terminalSearchPrevButton);
    searchRow->addWidget(m_terminalSearchNextButton);
    searchLayout->addLayout(searchRow);

    auto *searchOptionsRow = new QHBoxLayout;
    searchOptionsRow->setSpacing(8);
    m_terminalSearchCaseCheck = new CheckBox(QStringLiteral("Aa"), searchPanel);
    m_terminalSearchCaseCheck->setToolTip(QStringLiteral("大小写敏感"));
    m_terminalSearchCaseCheck->setFixedHeight(CompactControlHeight);
    setFixedControlWidth(m_terminalSearchCaseCheck, 54);
    m_terminalSearchRegexCheck = new CheckBox(QStringLiteral(".*"), searchPanel);
    m_terminalSearchRegexCheck->setToolTip(QStringLiteral("正则搜索"));
    m_terminalSearchRegexCheck->setFixedHeight(CompactControlHeight);
    setFixedControlWidth(m_terminalSearchRegexCheck, 54);
    m_terminalFilterCombo = new ComboBox(searchPanel);
    m_terminalFilterCombo->addItem(QStringLiteral("全部"), QIcon(), QStringLiteral("all"));
    m_terminalFilterCombo->addItem(QStringLiteral("仅接收"), QIcon(), QStringLiteral("rx"));
    m_terminalFilterCombo->addItem(QStringLiteral("仅发送"), QIcon(), QStringLiteral("tx"));
    m_terminalFilterCombo->setFixedHeight(CompactControlHeight);
    setFixedControlWidth(m_terminalFilterCombo, 104);
    m_terminalSummaryLabel = new CaptionLabel(QStringLiteral("显示 0 条"), searchPanel);
    m_terminalSummaryLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_terminalSummaryLabel->setFixedHeight(CompactControlHeight);
    m_terminalSummaryLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setFixedControlWidth(m_terminalSummaryLabel, 148);
    searchOptionsRow->addWidget(m_terminalSearchCaseCheck);
    searchOptionsRow->addWidget(m_terminalSearchRegexCheck);
    searchOptionsRow->addWidget(m_terminalFilterCombo);
    searchOptionsRow->addWidget(m_terminalSummaryLabel, 1);
    searchLayout->addLayout(searchOptionsRow);
    searchView->addWidget(searchPanel);
    auto *searchFlyout = Flyout::make(searchView, nullptr, section, FlyoutAnimationType::DropDown, false);

    m_terminalView = new TextBrowser(section);
    m_terminalView->setReadOnly(true);
    m_terminalView->setLineWrapMode(QTextEdit::WidgetWidth);
    m_terminalView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_terminalView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_terminalView->document()->setMaximumBlockCount(maxRecordCount());
    m_terminalView->setMinimumHeight(420);
    applyTerminalFont();

    root->addWidget(m_terminalView, 1);

    connect(searchButton, &TransparentToolButton::clicked, this, [this, searchButton, searchFlyout]() {
        searchFlyout->exec(searchButton);
        if (m_terminalSearchEdit) {
            m_terminalSearchEdit->setFocus(Qt::PopupFocusReason);
            m_terminalSearchEdit->selectAll();
        }
    });
    connect(themeButton, &TransparentToolButton::clicked, this, []() {
        const Theme current = ThemeManager::instance()->effectiveTheme();
        const Theme next = current == Theme::Dark ? Theme::Light : Theme::Dark;
        FluentConfig::instance()->setThemeMode(next);
        FluentConfig::instance()->save();
        ThemeManager::instance()->setTheme(next);
    });
    connect(settingsButton, &TransparentToolButton::clicked, this, &WorkbenchPage::settingsRequested);
    connect(m_terminalSearchEdit, &SearchLineEdit::textChanged, this, [this]() {
        resetTerminalSearchNavigation();
        renderTerminal();
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
        renderTerminal();
    });
    connect(m_terminalSearchRegexCheck, &CheckBox::toggled, this, [this](bool) {
        resetTerminalSearchNavigation();
        renderTerminal();
    });
    connect(m_terminalFilterCombo, &ComboBox::currentIndexChanged, this, [this](int) { renderTerminal(); });

    return section;
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

    m_sendButton = new PrimaryPushButton(icon(FluentIcon::Send), QString(), section);
    m_sendButton->setToolTip(QStringLiteral("发送"));
    m_sendButton->setIconSize(QSize(40, 40));
    setFixedControlWidth(m_sendButton, 112);
    m_sendButton->setMinimumHeight(112);
    sendRow->addWidget(m_sendButton);

    root->addLayout(sendRow);

    connect(m_sendButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::sendCurrentPayload);

    return section;
}
