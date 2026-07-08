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
    auto *themeButton = new TransparentToolButton(icon(FluentIcon::Constract), section);
    themeButton->setToolTip(QStringLiteral("切换主题"));
    auto *settingsButton = new TransparentToolButton(icon(FluentIcon::Setting), section);
    settingsButton->setToolTip(QStringLiteral("设置"));
    section->headerLayout()->addWidget(themeButton);
    section->headerLayout()->addWidget(settingsButton);
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

    auto *toolsRow = new QHBoxLayout;
    toolsRow->setSpacing(8);
    m_terminalSearchEdit = new SearchLineEdit(section);
    m_terminalSearchEdit->setPlaceholderText(QStringLiteral("搜索终端内容"));
    m_terminalSearchEdit->setClearButtonEnabled(true);
    m_terminalSearchEdit->setFixedHeight(CompactControlHeight);
    m_terminalSearchEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_terminalFilterCombo = new ComboBox(section);
    m_terminalFilterCombo->addItem(QStringLiteral("全部"), QIcon(), QStringLiteral("all"));
    m_terminalFilterCombo->addItem(QStringLiteral("仅接收"), QIcon(), QStringLiteral("rx"));
    m_terminalFilterCombo->addItem(QStringLiteral("仅发送"), QIcon(), QStringLiteral("tx"));
    m_terminalFilterCombo->setFixedHeight(CompactControlHeight);
    setFixedControlWidth(m_terminalFilterCombo, 104);
    m_terminalSummaryLabel = new CaptionLabel(QStringLiteral("显示 0 条"), section);
    m_terminalSummaryLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_terminalSummaryLabel->setFixedHeight(CompactControlHeight);
    m_terminalSummaryLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setFixedControlWidth(m_terminalSummaryLabel, 96);
    toolsRow->addWidget(m_terminalSearchEdit, 1);
    toolsRow->addWidget(m_terminalFilterCombo);
    toolsRow->addWidget(m_terminalSummaryLabel);
    root->addLayout(toolsRow);

    m_terminalView = new TextBrowser(section);
    m_terminalView->setReadOnly(true);
    m_terminalView->setLineWrapMode(QTextEdit::WidgetWidth);
    m_terminalView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_terminalView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_terminalView->document()->setMaximumBlockCount(maxRecordCount());
    m_terminalView->setMinimumHeight(420);
    applyTerminalFont();

    root->addWidget(m_terminalView, 1);

    connect(themeButton, &TransparentToolButton::clicked, this, []() {
        const Theme current = ThemeManager::instance()->effectiveTheme();
        const Theme next = current == Theme::Dark ? Theme::Light : Theme::Dark;
        FluentConfig::instance()->setThemeMode(next);
        FluentConfig::instance()->save();
        ThemeManager::instance()->setTheme(next);
    });
    connect(settingsButton, &TransparentToolButton::clicked, this, &WorkbenchPage::settingsRequested);
    connect(m_terminalSearchEdit, &SearchLineEdit::textChanged, this, [this]() { renderTerminal(); });
    connect(m_terminalSearchEdit, &SearchLineEdit::searchSignal, this, [this](const QString &) { renderTerminal(); });
    connect(m_terminalSearchEdit, &SearchLineEdit::clearSignal, this, [this]() { renderTerminal(); });
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
