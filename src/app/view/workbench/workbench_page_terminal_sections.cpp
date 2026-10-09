#include "app/view/workbench/workbench_page_internal.h"

#include "app/core/app_i18n.h"

#include <QtGui/QFontDatabase>
#include <QtGui/QPainter>
#include <QtGui/QShortcut>

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

namespace {

// Keep the full value for accessibility when the header has to elide it.
template <typename Label> class TerminalStatLabel : public Label
{
  public:
    TerminalStatLabel(const QString &text, QWidget *parent) : Label(text, parent) {}

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (this->fontMetrics().horizontalAdvance(this->text()) <= this->contentsRect().width()) {
            Label::paintEvent(event);
            return;
        }
        QPainter painter(this);
        painter.setPen(this->palette().color(QPalette::WindowText));
        painter.drawText(this->contentsRect(), this->alignment(),
                         this->fontMetrics().elidedText(this->text(), Qt::ElideRight, this->contentsRect().width()));
    }
};

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

    m_terminalStatsWidget = new QWidget(section);
    m_terminalStatsWidget->setObjectName(QStringLiteral("terminalStatistics"));
    m_terminalStatsWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_terminalStatsWidget->setMinimumWidth(0);
    m_terminalStatsWidget->setFixedHeight(38);
    m_terminalStatsWidget->installEventFilter(this);
    const auto createGroup = [this](const QString &name, const QString &description, const QString &lightBackground,
                                    const QString &darkBackground) {
        auto *group = new QWidget(m_terminalStatsWidget);
        group->setObjectName(name);
        group->setAccessibleName(description);
        group->setAttribute(Qt::WA_StyledBackground);
        const QString style = QStringLiteral("QWidget#%1 { background-color: %2; border-radius: 5px; }");
        FluentStyleSheet::setCustomStyleSheet(group, style.arg(name, lightBackground), style.arg(name, darkBackground));
        m_terminalStatsGroups.append(group);
        return group;
    };
    auto *receiveGroup = createGroup(QStringLiteral("terminalReceiveStats"), AppI18n::text("接收"),
                                     QStringLiteral("rgba(0, 121, 107, 12)"), QStringLiteral("rgba(79, 214, 191, 14)"));
    auto *sendGroup = createGroup(QStringLiteral("terminalSendStats"), AppI18n::text("发送"),
                                  QStringLiteral("rgba(177, 86, 15, 12)"), QStringLiteral("rgba(255, 185, 115, 14)"));
    auto *connectionGroup = createGroup(QStringLiteral("terminalConnectionStats"), AppI18n::text("连接状态"),
                                        QStringLiteral("rgba(0, 0, 0, 5)"), QStringLiteral("rgba(255, 255, 255, 5)"));
    auto *receiveDirection = new StrongBodyLabel(QStringLiteral("R↓"), receiveGroup);
    receiveDirection->setAccessibleName(AppI18n::text("接收"));
    receiveDirection->setTextColor(QColor(0, 121, 107), QColor(79, 214, 191));
    auto *sendDirection = new StrongBodyLabel(QStringLiteral("T↑"), sendGroup);
    sendDirection->setAccessibleName(AppI18n::text("发送"));
    sendDirection->setTextColor(QColor(177, 86, 15), QColor(255, 185, 115));
    m_rxCounterLabel = new TerminalStatLabel<StrongBodyLabel>(QStringLiteral("0 B"), receiveGroup);
    m_txCounterLabel = new TerminalStatLabel<StrongBodyLabel>(QStringLiteral("0 B"), sendGroup);
    m_rxRateLabel = new TerminalStatLabel<StrongBodyLabel>(QStringLiteral("0 B/s"), receiveGroup);
    m_txRateLabel = new TerminalStatLabel<StrongBodyLabel>(QStringLiteral("0 B/s"), sendGroup);
    m_connectionStatusLabel = new TerminalStatLabel<CaptionLabel>(AppI18n::text("未连接"), connectionGroup);
    m_connectionTimeLabel = new TerminalStatLabel<CaptionLabel>(QStringLiteral("—"), connectionGroup);
    m_connectionTimeLabel->setAccessibleName(AppI18n::text("连接时长"));
    auto *receiveTotal = new CaptionLabel(AppI18n::text("累计"), receiveGroup);
    auto *sendTotal = new CaptionLabel(AppI18n::text("累计"), sendGroup);
    QFont valueFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    valueFont.setPixelSize(14);
    valueFont.setWeight(QFont::DemiBold);
    for (auto *label : {m_rxCounterLabel, m_txCounterLabel, m_rxRateLabel, m_txRateLabel}) {
        label->setFont(valueFont);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    valueFont.setPixelSize(11);
    valueFont.setWeight(QFont::Normal);
    for (FluentLabelBase *label : QList<FluentLabelBase *>{m_rxCounterLabel, m_txCounterLabel, m_connectionTimeLabel}) {
        label->setFont(valueFont);
    }
    for (FluentLabelBase *label :
         QList<FluentLabelBase *>{receiveTotal, sendTotal, m_rxCounterLabel, m_txCounterLabel, m_connectionTimeLabel}) {
        label->setPixelFontSize(11);
        label->setTextColor(QColor(100, 100, 100), QColor(170, 170, 170));
    }
    m_terminalStatsLabels = {receiveDirection,        m_rxRateLabel,        receiveTotal, m_rxCounterLabel,
                             sendDirection,           m_txRateLabel,        sendTotal,    m_txCounterLabel,
                             m_connectionStatusLabel, m_connectionTimeLabel};
    section->headerLayout()->addWidget(m_terminalStatsWidget, 1, Qt::AlignVCenter);

    auto *actions = new QWidget(section);
    actions->setObjectName(QStringLiteral("terminalHeaderActions"));
    auto *actionLayout = new QHBoxLayout(actions);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(4);
    actionLayout->setSizeConstraint(QLayout::SetFixedSize);
    section->headerLayout()->addWidget(actions, 0, Qt::AlignVCenter);

    auto *clearButton = new TransparentToolButton(icon(FluentIcon::Broom), section);
    clearButton->setObjectName(QStringLiteral("terminalClearButton"));
    clearButton->setAccessibleName(AppI18n::text("清空终端和计数"));
    AppUi::setFluentToolTip(clearButton, AppI18n::text("清空终端和计数"));
    section->headerLayout()->insertWidget(0, clearButton, 0, Qt::AlignVCenter);
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
    for (ToolButton *button : {clearButton, searchButton, plotButton, dataTableButton, themeButton, languageButton,
                               m_receiveModeButton, settingsButton}) {
        button->setProperty("terminalHeaderAction", true);
        button->setFixedSize(CompactControlHeight, CompactControlHeight);
        button->setIconSize(QSize(16, 16));
        const QString buttonStyle =
            QStringLiteral("QToolButton[terminalHeaderAction=\"true\"] { min-width: 32px; max-width: 32px; "
                           "min-height: 32px; max-height: 32px; padding: 0px; }");
        FluentStyleSheet::setCustomStyleSheet(button, buttonStyle, buttonStyle);
    }
    for (ToolButton *button : {searchButton, plotButton, dataTableButton, themeButton, languageButton,
                               m_receiveModeButton, settingsButton}) {
        actionLayout->addWidget(button);
    }

    m_terminalView = new TextBrowser(section);
    m_terminalView->setReadOnly(true);
    m_terminalView->setLineWrapMode(QTextEdit::WidgetWidth);
    m_terminalView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_terminalView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_terminalView->document()->setMaximumBlockCount(maxRecordCount());
    m_terminalView->setMinimumHeight(420);
    applyTerminalFont();
    root->addWidget(m_terminalView, 1);

    // Keep find controls anchored to the terminal without changing its viewport size.
    auto *searchPanel = new QWidget(m_terminalView->viewport());
    m_terminalSearchBar = searchPanel;
    searchPanel->setObjectName(QStringLiteral("terminalSearchBar"));
    searchPanel->setAccessibleName(AppI18n::text("终端搜索"));
    searchPanel->setAttribute(Qt::WA_StyledBackground);
    searchPanel->setAttribute(Qt::WA_NoMousePropagation);
    searchPanel->setFixedHeight(44);
    FluentStyleSheet::setCustomStyleSheet(searchPanel,
                                          QStringLiteral("QWidget#terminalSearchBar { background-color: rgba(243, 243, "
                                                         "243, 210); border: 1px solid rgba(180, 180, 180, 150); "
                                                         "border-radius: 6px; }"),
                                          QStringLiteral("QWidget#terminalSearchBar { background-color: rgba(37, 37, "
                                                         "38, 210); border: 1px solid rgba(100, 100, 100, 150); "
                                                         "border-radius: 6px; }"));
    auto *searchRow = new QHBoxLayout(searchPanel);
    searchRow->setContentsMargins(8, 6, 8, 6);
    searchRow->setSpacing(4);

    m_terminalSearchEdit = new LineEdit(searchPanel);
    m_terminalSearchEdit->setObjectName(QStringLiteral("terminalSearchEdit"));
    m_terminalSearchEdit->setPlaceholderText(AppI18n::text("搜索终端内容"));
    m_terminalSearchEdit->setAccessibleName(AppI18n::text("搜索终端内容"));
    m_terminalSearchEdit->setFixedHeight(CompactControlHeight);
    m_terminalSearchEdit->setMinimumWidth(140);
    m_terminalSearchEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const QString searchEditStyle =
        QStringLiteral("QLineEdit#terminalSearchEdit { min-height: 30px; max-height: 30px; padding: 0px 8px; }");
    FluentStyleSheet::setCustomStyleSheet(
        m_terminalSearchEdit,
        searchEditStyle + QStringLiteral("QLineEdit#terminalSearchEdit { background-color: #ffffff; }"),
        searchEditStyle + QStringLiteral("QLineEdit#terminalSearchEdit { background-color: #202020; }"));
    m_terminalSearchEdit->hBoxLayout()->setContentsMargins(4, 0, 4, 0);
    m_terminalSearchEdit->hBoxLayout()->setSpacing(2);
    m_terminalSearchEdit->hBoxLayout()->insertStretch(0, 1);
    m_terminalSearchEdit->installEventFilter(this);

    const auto createOption = [this](const QString &name, const QString &text, const QString &description) {
        auto *button = new TransparentToolButton(m_terminalSearchEdit);
        button->setObjectName(name);
        button->setText(text);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setCheckable(true);
        button->setAccessibleName(description);
        button->setFixedSize(26, 24);
        const QString base = QStringLiteral("QToolButton#%1 { min-width: 26px; max-width: 26px; "
                                            "min-height: 24px; max-height: 24px; padding: 0px; "
                                            "border: 1px solid transparent; border-radius: 3px; }")
                                 .arg(name);
        FluentStyleSheet::setCustomStyleSheet(
            button,
            base + QStringLiteral("QToolButton#%1:checked { background-color: #d6eaff; "
                                  "border-color: #0078d4; color: #005a9e; }")
                       .arg(name),
            base + QStringLiteral("QToolButton#%1:checked { background-color: #264f78; "
                                  "border-color: #75beff; color: #ffffff; }")
                       .arg(name));
        m_terminalSearchEdit->hBoxLayout()->addWidget(button, 0, Qt::AlignVCenter);
        return button;
    };
    m_terminalSearchCaseCheck =
        createOption(QStringLiteral("terminalSearchCase"), QStringLiteral("Aa"), AppI18n::text("大小写敏感"));
    m_terminalSearchRegexCheck =
        createOption(QStringLiteral("terminalSearchRegex"), QStringLiteral(".*"), AppI18n::text("正则搜索"));
    m_terminalSearchEdit->setTextMargins(0, 0, 60, 0);

    m_terminalSummaryLabel = new CaptionLabel(searchPanel);
    m_terminalSummaryLabel->setObjectName(QStringLiteral("terminalSearchSummary"));
    m_terminalSummaryLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_terminalSummaryLabel->setFixedSize(104, CompactControlHeight);
    m_terminalSummaryLabel->setAlignment(Qt::AlignCenter);
    m_terminalSearchPrevButton = new TransparentToolButton(icon(FluentIcon::Up), searchPanel);
    m_terminalSearchPrevButton->setObjectName(QStringLiteral("terminalSearchPrevious"));
    m_terminalSearchPrevButton->setAccessibleName(AppI18n::text("上一个匹配"));
    m_terminalSearchPrevButton->setEnabled(false);
    m_terminalSearchNextButton = new TransparentToolButton(icon(FluentIcon::Down), searchPanel);
    m_terminalSearchNextButton->setObjectName(QStringLiteral("terminalSearchNext"));
    m_terminalSearchNextButton->setAccessibleName(AppI18n::text("下一个匹配"));
    m_terminalSearchNextButton->setEnabled(false);
    ToolButton *closeButton = new TransparentToolButton(icon(FluentIcon::Close), searchPanel);
    closeButton->setObjectName(QStringLiteral("terminalSearchClose"));
    closeButton->setAccessibleName(AppI18n::text("关闭搜索"));
    for (ToolButton *button : {m_terminalSearchPrevButton, m_terminalSearchNextButton, closeButton}) {
        button->setFixedSize(28, 28);
        button->setIconSize(QSize(16, 16));
        const QString buttonStyle =
            QStringLiteral("QToolButton#%1 { min-width: 28px; max-width: 28px; min-height: 28px; "
                           "max-height: 28px; padding: 0px; }")
                .arg(button->objectName());
        FluentStyleSheet::setCustomStyleSheet(button, buttonStyle, buttonStyle);
    }

    m_terminalFilterCombo = new ComboBox(searchPanel);
    m_terminalFilterCombo->setObjectName(QStringLiteral("terminalSearchFilter"));
    m_terminalFilterCombo->setAccessibleName(AppI18n::text("收发筛选"));
    m_terminalFilterCombo->addItem(AppI18n::text("全部"), QIcon(), QStringLiteral("all"));
    m_terminalFilterCombo->addItem(AppI18n::text("仅接收"), QIcon(), QStringLiteral("rx"));
    m_terminalFilterCombo->addItem(AppI18n::text("仅发送"), QIcon(), QStringLiteral("tx"));
    m_terminalFilterCombo->setFixedSize(88, CompactControlHeight);
    const QString filterStyle = QStringLiteral("QPushButton#terminalSearchFilter { min-height: 30px; max-height: 30px; "
                                               "padding: 0px 25px 0px 7px; }");
    FluentStyleSheet::setCustomStyleSheet(m_terminalFilterCombo, filterStyle, filterStyle);
    searchRow->addWidget(m_terminalSearchEdit, 1, Qt::AlignVCenter);
    searchRow->addWidget(m_terminalSummaryLabel, 0, Qt::AlignVCenter);
    searchRow->addWidget(m_terminalSearchPrevButton, 0, Qt::AlignVCenter);
    searchRow->addWidget(m_terminalSearchNextButton, 0, Qt::AlignVCenter);
    searchRow->addWidget(m_terminalFilterCombo, 0, Qt::AlignVCenter);
    searchRow->addWidget(closeButton, 0, Qt::AlignVCenter);
    searchPanel->hide();
    m_terminalView->viewport()->installEventFilter(this);

    connect(clearButton, &ToolButton::clicked, this, [this]() {
        clearTerminal();
        resetCounters();
    });
    connect(searchButton, &ToolButton::clicked, this, &WorkbenchPage::showTerminalSearchBar);
    connect(closeButton, &ToolButton::clicked, this, &WorkbenchPage::hideTerminalSearchBar);
    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this, &WorkbenchPage::showTerminalSearchBar);
    auto *closeSearch = new QShortcut(QKeySequence(Qt::Key_Escape), searchPanel);
    closeSearch->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closeSearch, &QShortcut::activated, this, &WorkbenchPage::hideTerminalSearchBar);
    m_terminalView->installEventFilter(this);
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
    connect(m_terminalSearchEdit, &LineEdit::textChanged, this, [this]() {
        resetTerminalSearchNavigation();
        renderTerminal(true);
    });
    connect(m_terminalSearchPrevButton, &ToolButton::clicked, this, [this]() { moveTerminalSearchMatch(-1); });
    connect(m_terminalSearchNextButton, &ToolButton::clicked, this, [this]() { moveTerminalSearchMatch(1); });
    connect(m_terminalSearchCaseCheck, &ToolButton::toggled, this, [this](bool) {
        resetTerminalSearchNavigation();
        renderTerminal(true);
    });
    connect(m_terminalSearchRegexCheck, &ToolButton::toggled, this, [this](bool) {
        resetTerminalSearchNavigation();
        renderTerminal(true);
    });
    connect(m_terminalFilterCombo, &ComboBox::currentIndexChanged, this, [this](int) { renderTerminal(); });
    updateReceiveModeButton();

    return section;
}

void WorkbenchPage::updateTerminalHeaderLayout()
{
    if (!m_terminalStatsWidget || m_terminalStatsLabels.size() != 10) {
        return;
    }

    constexpr int padding = 6;
    constexpr int labelGap = 4;
    const int available = m_terminalStatsWidget->width();
    const auto sampleWidth = [](FluentLabelBase *label, const QString &sample, int pixels) {
        QFont font = label->font();
        font.setPixelSize(pixels);
        return QFontMetrics(font).horizontalAdvance(sample) + 2;
    };
    const int totalCaptionWidth = qMax(sampleWidth(m_terminalStatsLabels.at(2), AppI18n::text("累计"), 11),
                                       sampleWidth(m_terminalStatsLabels.at(6), AppI18n::text("累计"), 11));
    const int counterWidth = sampleWidth(m_rxCounterLabel, QStringLiteral("999.9 MB"), 11);
    const int statusWidth = qMax(sampleWidth(m_connectionStatusLabel, AppI18n::text("已连接"), 12),
                                 sampleWidth(m_connectionStatusLabel, AppI18n::text("未连接"), 12));
    const int timeWidth = sampleWidth(m_connectionTimeLabel, QStringLiteral("999:59:59"), 11);
    const int desiredConnectionWidth = 2 * padding + qMax(statusWidth, timeWidth);
    int fontSize = 14;
    int groupGap = 8;
    int directionWidth = 0;
    int rateWidth = 0;
    int trafficWidth = 0;
    // The same fixed templates determine all three groups, regardless of live data.
    for (int mode = 0; mode < 2; ++mode) {
        fontSize = mode == 0 ? 14 : 12;
        groupGap = mode == 0 ? 8 : 6;
        directionWidth = qMax(sampleWidth(m_terminalStatsLabels.at(0), QStringLiteral("R↓"), fontSize),
                              sampleWidth(m_terminalStatsLabels.at(4), QStringLiteral("T↑"), fontSize));
        rateWidth = sampleWidth(m_rxRateLabel, QStringLiteral("999.9 MB/s"), fontSize);
        trafficWidth =
            2 * padding + qMax(directionWidth + labelGap + rateWidth, totalCaptionWidth + labelGap + counterWidth);
        if (2 * trafficWidth + 2 * groupGap + desiredConnectionWidth <= available) {
            break;
        }
    }
    int connectionWidth = qMin(desiredConnectionWidth, qMax(0, available - 2 * trafficWidth - 2 * groupGap));
    if (connectionWidth < 32) {
        connectionWidth = 0;
    }
    const int connectionSpace = connectionWidth > 0 ? connectionWidth + groupGap : 0;
    trafficWidth = qMin(trafficWidth, qMax(0, (available - groupGap - connectionSpace) / 2));
    const int groupWidths[] = {trafficWidth, trafficWidth, connectionWidth};
    int groupX = 0;
    for (int index = 0; index < m_terminalStatsGroups.size(); ++index) {
        auto *group = m_terminalStatsGroups.at(index);
        group->setGeometry(groupX, 0, groupWidths[index], 38);
        group->setVisible(groupWidths[index] > 0);
        groupX += groupWidths[index] + groupGap;
    }

    const auto place = [](FluentLabelBase *label, const QRect &bounds, int pixels) {
        if (label->pixelFontSize() != pixels) {
            label->setPixelFontSize(pixels);
        }
        label->setGeometry(bounds);
        label->setVisible(bounds.width() > 0);
    };
    for (int offset : {0, 4}) {
        const int innerWidth = qMax(0, trafficWidth - 2 * padding);
        const int actualDirectionWidth = qMin(directionWidth, innerWidth);
        const int actualRateWidth = qMin(rateWidth, qMax(0, innerWidth - directionWidth - labelGap));
        const int actualCaptionWidth = qMin(totalCaptionWidth, innerWidth);
        const int actualCounterWidth = qMin(counterWidth, qMax(0, innerWidth - totalCaptionWidth - labelGap));
        place(m_terminalStatsLabels.at(offset), QRect(padding, 2, actualDirectionWidth, 18), fontSize);
        place(m_terminalStatsLabels.at(offset + 1),
              QRect(trafficWidth - padding - actualRateWidth, 2, actualRateWidth, 18), fontSize);
        place(m_terminalStatsLabels.at(offset + 2), QRect(padding, 21, actualCaptionWidth, 14), 11);
        place(m_terminalStatsLabels.at(offset + 3),
              QRect(trafficWidth - padding - actualCounterWidth, 21, actualCounterWidth, 14), 11);
    }
    const int connectionInnerWidth = qMax(0, connectionWidth - 2 * padding);
    place(m_connectionStatusLabel, QRect(padding, 2, connectionInnerWidth, 18), 12);
    place(m_connectionTimeLabel, QRect(padding, 21, connectionInnerWidth, 14), 11);
}

void WorkbenchPage::positionTerminalSearchBar()
{
    if (!m_terminalSearchBar || !m_terminalView) {
        return;
    }
    const int margin = 8;
    const int availableWidth = qMax(0, m_terminalView->viewport()->width() - 2 * margin);
    m_terminalSearchBar->resize(qMin(560, availableWidth), 44);
    m_terminalSearchBar->move(qMax(margin, m_terminalView->viewport()->width() - m_terminalSearchBar->width() - margin),
                              margin);
}

void WorkbenchPage::showTerminalSearchBar()
{
    positionTerminalSearchBar();
    m_terminalSearchBar->show();
    m_terminalSearchBar->raise();
    m_terminalSearchEdit->setFocus(Qt::ShortcutFocusReason);
    m_terminalSearchEdit->selectAll();
}

void WorkbenchPage::hideTerminalSearchBar()
{
    if (m_terminalSearchBar->isVisible()) {
        m_terminalSearchBar->hide();
        m_terminalView->setFocus(Qt::ShortcutFocusReason);
    }
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
    m_sendModeButton->setAccessibleDescription(hexMode ? AppI18n::text("当前为 HEX 发送，点击切换为文本")
                                                       : AppI18n::text("当前为文本发送，点击切换为 HEX"));
}
