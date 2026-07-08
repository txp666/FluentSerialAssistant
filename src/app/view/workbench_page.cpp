#include "app/view/workbench_page.h"

#include "app/core/font_preferences.h"
#include "app/core/hex_utils.h"

#include <FluentQtWidgets/Dialogs/FolderListDialog.h>
#include <FluentQtWidgets/Settings/SettingCard.h>
#include <FluentQtWidgets/StyleSheet.h>
#include <FluentQtWidgets/Theme.h>

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QEvent>
#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStandardPaths>
#include <QtGui/QIcon>
#include <QtGui/QKeyEvent>
#include <QtGui/QTextCharFormat>
#include <QtGui/QTextCursor>
#include <QtGui/QTextOption>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtSerialPort/QSerialPortInfo>

#include <algorithm>

using namespace FluentQt;

namespace {

constexpr int FlushIntervalMs = 33;
constexpr int DefaultMaxTerminalRecords = 20000;
constexpr int MaxSendHistoryItems = 20;
constexpr int MaxSendPacketItems = 100;
constexpr int SidePanelWidth = 324;
constexpr int TerminalPanelMinWidth = 560;
constexpr int ReconnectIntervalMs = 2000;
constexpr int CompactControlHeight = 32;
constexpr int DefaultFileChunkSize = 256;
constexpr int DefaultFileChunkIntervalMs = 10;

QColor terminalTimestampColor()
{
    return ThemeManager::instance()->effectiveTheme() == Theme::Dark ? QColor(132, 192, 214) : QColor(75, 103, 128);
}

QColor terminalDirectionMarkerColor(bool tx)
{
    const bool dark = ThemeManager::instance()->effectiveTheme() == Theme::Dark;
    if(tx) {
        return dark ? QColor(255, 185, 115) : QColor(177, 86, 15);
    }
    return dark ? QColor(79, 214, 191) : QColor(0, 121, 107);
}

QColor terminalEspIdfLogLevelColor(QChar level)
{
    const bool dark = ThemeManager::instance()->effectiveTheme() == Theme::Dark;
    switch(level.toUpper().toLatin1()) {
    case 'V':
        return dark ? QColor(187, 154, 255) : QColor(108, 84, 190);
    case 'D':
        return dark ? QColor(117, 190, 255) : QColor(33, 118, 188);
    case 'I':
        return dark ? QColor(103, 219, 123) : QColor(0, 126, 67);
    case 'W':
        return dark ? QColor(255, 209, 102) : QColor(171, 103, 0);
    case 'E':
        return dark ? QColor(255, 132, 132) : QColor(190, 35, 35);
    default:
        return QColor();
    }
}

int espIdfLogPrefixEnd(const QString &line, int prefixStart)
{
    if(prefixStart + 4 >= line.size()) {
        return -1;
    }

    const QColor levelColor = terminalEspIdfLogLevelColor(line.at(prefixStart));
    if(!levelColor.isValid() || line.at(prefixStart + 1) != QLatin1Char(' ') ||
       line.at(prefixStart + 2) != QLatin1Char('(')) {
        return -1;
    }

    int cursor = prefixStart + 3;
    const int digitStart = cursor;
    while(cursor < line.size() && line.at(cursor).isDigit()) {
        ++cursor;
    }

    if(cursor == digitStart || cursor >= line.size() || line.at(cursor) != QLatin1Char(')')) {
        return -1;
    }

    return cursor + 1;
}

void setFixedControlWidth(QWidget *widget, int width)
{
    widget->setMinimumWidth(width);
    widget->setMaximumWidth(width);
}

void hideCardHeader(HeaderCardWidget *card)
{
    card->setTitle(QString());
    if(card->titleLabel()) {
        card->titleLabel()->hide();
    }
    if(card->headerView()) {
        card->headerView()->hide();
    }
    if(card->separator()) {
        card->separator()->hide();
    }
}

void hideCardTitle(HeaderCardWidget *card)
{
    card->setTitle(QString());
    if(card->titleLabel()) {
        card->titleLabel()->hide();
    }
}

QVBoxLayout *cardBody(HeaderCardWidget *card, int margin = 12)
{
    card->setBorderRadius(8);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    card->viewLayout()->setContentsMargins(0, 0, 0, 0);

    auto *body = new QWidget(card->view());
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->setSpacing(8);
    card->viewLayout()->addWidget(body, 1);
    return layout;
}

void makeCompactControl(QWidget *control)
{
    control->setMinimumWidth(0);
    control->setFixedHeight(CompactControlHeight);
    control->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

void setButtonRowControlPolicy(QWidget *control)
{
    control->setMinimumWidth(0);
    control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void addFormRow(QVBoxLayout *root, const QString &labelText, QWidget *control, QWidget *extra = nullptr)
{
    auto *parent = root->parentWidget();
    auto *row = new QHBoxLayout;
    row->setSpacing(8);

    auto *label = new BodyLabel(labelText, parent);
    setFixedControlWidth(label, 52);
    row->addWidget(label, 0, Qt::AlignVCenter);

    control->setMinimumWidth(0);
    control->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    row->addWidget(control, 1, Qt::AlignVCenter);

    if(extra) {
        row->addWidget(extra, 0, Qt::AlignVCenter);
    }

    root->addLayout(row);
}

QSerialPort::DataBits dataBitsFromText(const QString &text)
{
    if(text == QStringLiteral("5")) {
        return QSerialPort::Data5;
    }
    if(text == QStringLiteral("6")) {
        return QSerialPort::Data6;
    }
    if(text == QStringLiteral("7")) {
        return QSerialPort::Data7;
    }
    return QSerialPort::Data8;
}

QSerialPort::Parity parityFromData(const QVariant &data)
{
    return static_cast<QSerialPort::Parity>(data.toInt());
}

QSerialPort::StopBits stopBitsFromData(const QVariant &data)
{
    return static_cast<QSerialPort::StopBits>(data.toInt());
}

QSerialPort::FlowControl flowControlFromData(const QVariant &data)
{
    return static_cast<QSerialPort::FlowControl>(data.toInt());
}

QString defaultExportFolder()
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QDir::homePath() : documents;
}

QString modeLabel(const QString &mode)
{
    return mode == QStringLiteral("hex") ? QStringLiteral("HEX") : QStringLiteral("文本");
}

QString lineEndingLabel(const QString &lineEnding)
{
    if(lineEnding == QStringLiteral("cr")) {
        return QStringLiteral("CR");
    }
    if(lineEnding == QStringLiteral("lf")) {
        return QStringLiteral("LF");
    }
    if(lineEnding == QStringLiteral("crlf")) {
        return QStringLiteral("CRLF");
    }
    return QStringLiteral("None");
}

QByteArray lineEndingBytes(const QString &lineEnding)
{
    if(lineEnding == QStringLiteral("cr")) {
        return QByteArray("\r", 1);
    }
    if(lineEnding == QStringLiteral("lf")) {
        return QByteArray("\n", 1);
    }
    if(lineEnding == QStringLiteral("crlf")) {
        return QByteArray("\r\n", 2);
    }
    return {};
}

QString formatBytes(qint64 bytes)
{
    const double value = static_cast<double>(bytes);
    if(bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if(bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(value / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(value / (1024.0 * 1024.0), 0, 'f', 1);
}

QString formatBytesPerSecond(qint64 bytes)
{
    return QStringLiteral("%1/s").arg(formatBytes(bytes));
}

QString formatDuration(qint64 seconds)
{
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 secs = seconds % 60;
    if(hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
}

QStringList commonBaudRateTexts()
{
    QList<qint32> rates = QSerialPortInfo::standardBaudRates();
    const QList<qint32> additionalRates = {
        110,     300,     600,     1200,    2400,    4800,    9600,    14400,
        19200,   38400,   56000,   57600,   74880,   115200,  128000,  230400,
        250000,  256000,  460800,  500000,  921600,  1000000, 1500000, 2000000,
        3000000, 4000000
    };
    for(qint32 rate : additionalRates) {
        if(!rates.contains(rate)) {
            rates.append(rate);
        }
    }

    std::sort(rates.begin(), rates.end());
    QStringList texts;
    qint32 previous = 0;
    for(qint32 rate : rates) {
        if(rate <= 0 || rate == previous) {
            continue;
        }
        texts.append(QString::number(rate));
        previous = rate;
    }
    return texts;
}

QColor defaultTxColor()
{
    return QColor(QStringLiteral("#ff9f0a"));
}

QString qssString(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(value);
}

QString bytesToTerminalText(const QByteArray &data)
{
    QString text = QString::fromUtf8(data);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    for(QChar &ch : text) {
        if(ch == QLatin1Char('\n') || ch == QLatin1Char('\t')) {
            continue;
        }
        if(ch.unicode() < 0x20) {
            ch = QLatin1Char('.');
        }
    }
    while(text.endsWith(QLatin1Char('\n'))) {
        text.chop(1);
    }
    return text;
}

QString csvEscape(QString text)
{
    text.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(text);
}

} // namespace

WorkbenchPage::WorkbenchPage(QWidget *parent)
    : AppPage(QStringLiteral("终端"),
              QStringLiteral("连接串口设备，查看收发记录，并执行文本或 HEX 数据发送。"),
              parent,
              false)
{
    contentLayout()->setAlignment(Qt::Alignment());
    contentLayout()->addWidget(createWorkbench(), 1);

    setupSerialSignals();

    connect(&m_flushTimer, &QTimer::timeout, this, &WorkbenchPage::flushPendingLines);
    m_flushTimer.start(FlushIntervalMs);
    connect(&m_loopTimer, &QTimer::timeout, this, &WorkbenchPage::sendCurrentPayload);
    m_reconnectTimer.setInterval(ReconnectIntervalMs);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &WorkbenchPage::attemptReconnect);
    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, &WorkbenchPage::updateRateStats);
    m_fileSendTimer.setSingleShot(true);
    connect(&m_fileSendTimer, &QTimer::timeout, this, &WorkbenchPage::sendNextFileChunk);
    connect(ThemeManager::instance(), &ThemeManager::effectiveThemeChanged, this, [this]() {
        renderTerminal();
    });

    refreshPorts();
    loadSendHistory();
    loadSendPackets();
    restoreSettings();
    updateConnectionUi(false);
    updateCounters();
    updateRateStats();
    updateHistoryCombo();

    if(m_autoOpenCheck && m_autoOpenCheck->isChecked() && !currentSerialConfig().portName.isEmpty()) {
        QTimer::singleShot(250, this, &WorkbenchPage::onConnectClicked);
    }
}

WorkbenchPage::~WorkbenchPage()
{
    m_fileSendTimer.stop();
    if(m_fileSendFile.isOpen()) {
        m_fileSendFile.close();
    }
    m_reconnectTimer.stop();
    closeReceiveCapture();
    m_serial.closePort();
}

bool WorkbenchPage::eventFilter(QObject *watched, QEvent *event)
{
    if(watched == m_sendEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const bool enterKey = keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter;
        if(enterKey && keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
            sendCurrentPayload();
            return true;
        }
    }
    return AppPage::eventFilter(watched, event);
}

QWidget *WorkbenchPage::createWorkbench()
{
    auto *workbench = new QWidget(this);
    auto *root = new QHBoxLayout(workbench);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    auto *sideScroll = new ScrollArea(workbench);
    sideScroll->setFixedWidth(SidePanelWidth);
    sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if(sideScroll->horizontalFluentScrollBar()) {
        sideScroll->horizontalFluentScrollBar()->setForceHidden(true);
    }

    auto *sidePanel = new QWidget(sideScroll);
    auto *sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(10);
    sideLayout->addWidget(createConnectionSection());
    sideLayout->addWidget(createReceiveSettingsSection());
    sideLayout->addWidget(createSendSettingsSection());
    sideLayout->addWidget(createPacketSection());
    sideLayout->addWidget(createFileSendSection());
    sideLayout->addStretch(1);
    sideScroll->setWidget(sidePanel);
    sideScroll->setWidgetResizable(true);

    auto *terminalPanel = new QWidget(workbench);
    auto *terminalLayout = new QVBoxLayout(terminalPanel);
    terminalLayout->setContentsMargins(0, 0, 0, 0);
    terminalLayout->setSpacing(10);
    terminalLayout->addWidget(createTerminalSection(), 1);
    terminalLayout->addWidget(createSendSection());

    root->addWidget(sideScroll);
    root->addWidget(terminalPanel, 1);
    return workbench;
}

QWidget *WorkbenchPage::createCheckRow(const QStringList &labels,
                                       const QList<CheckBox **> &targets,
                                       QWidget *parent)
{
    auto *rowWidget = new QWidget(parent);
    auto *row = new QHBoxLayout(rowWidget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    const int count = qMin(labels.size(), targets.size());
    for(int i = 0; i < count; ++i) {
        auto *check = new CheckBox(labels.at(i), rowWidget);
        check->setMinimumWidth(0);
        check->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        *targets.at(i) = check;
        row->addWidget(check);
    }
    row->addStretch(1);
    return rowWidget;
}

QWidget *WorkbenchPage::createConnectionSection()
{
    auto *section = new HeaderCardWidget(this);
    hideCardHeader(section);
    auto *root = cardBody(section);

    m_portCombo = new ComboBox(section);
    makeCompactControl(m_portCombo);
    m_portCombo->setPlaceholderText(QStringLiteral("选择端口"));
    m_refreshButton = new TransparentToolButton(icon(FluentIcon::Sync), section);
    m_refreshButton->setToolTip(QStringLiteral("刷新端口"));
    m_refreshButton->setIconSize(QSize(16, 16));
    m_refreshButton->setFixedSize(CompactControlHeight, CompactControlHeight);
    addFormRow(root, QStringLiteral("端口"), m_portCombo, m_refreshButton);

    m_baudCombo = new EditableComboBox(section);
    makeCompactControl(m_baudCombo);
    m_baudCombo->addItems(commonBaudRateTexts());
    m_baudCombo->setCurrentText(QStringLiteral("115200"));
    addFormRow(root, QStringLiteral("波特率"), m_baudCombo);

    m_dataBitsCombo = new ComboBox(section);
    makeCompactControl(m_dataBitsCombo);
    m_dataBitsCombo->addItems({QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8")});
    m_dataBitsCombo->setCurrentText(QStringLiteral("8"));
    addFormRow(root, QStringLiteral("数据位"), m_dataBitsCombo);

    m_parityCombo = new ComboBox(section);
    makeCompactControl(m_parityCombo);
    m_parityCombo->addItem(QStringLiteral("无校验"), QIcon(), static_cast<int>(QSerialPort::NoParity));
    m_parityCombo->addItem(QStringLiteral("偶校验"), QIcon(), static_cast<int>(QSerialPort::EvenParity));
    m_parityCombo->addItem(QStringLiteral("奇校验"), QIcon(), static_cast<int>(QSerialPort::OddParity));
    m_parityCombo->addItem(QStringLiteral("空格"), QIcon(), static_cast<int>(QSerialPort::SpaceParity));
    m_parityCombo->addItem(QStringLiteral("标记"), QIcon(), static_cast<int>(QSerialPort::MarkParity));
    m_parityCombo->setCurrentIndex(0);
    addFormRow(root, QStringLiteral("校验位"), m_parityCombo);

    m_stopBitsCombo = new ComboBox(section);
    makeCompactControl(m_stopBitsCombo);
    m_stopBitsCombo->addItem(QStringLiteral("1 停止位"), QIcon(), static_cast<int>(QSerialPort::OneStop));
    m_stopBitsCombo->addItem(QStringLiteral("1.5 停止位"), QIcon(), static_cast<int>(QSerialPort::OneAndHalfStop));
    m_stopBitsCombo->addItem(QStringLiteral("2 停止位"), QIcon(), static_cast<int>(QSerialPort::TwoStop));
    m_stopBitsCombo->setCurrentIndex(0);
    addFormRow(root, QStringLiteral("停止位"), m_stopBitsCombo);

    m_flowControlCombo = new ComboBox(section);
    makeCompactControl(m_flowControlCombo);
    m_flowControlCombo->addItem(QStringLiteral("无流控"), QIcon(), static_cast<int>(QSerialPort::NoFlowControl));
    m_flowControlCombo->addItem(QStringLiteral("硬件流控"), QIcon(), static_cast<int>(QSerialPort::HardwareControl));
    m_flowControlCombo->addItem(QStringLiteral("软件流控"), QIcon(), static_cast<int>(QSerialPort::SoftwareControl));
    m_flowControlCombo->setCurrentIndex(0);
    addFormRow(root, QStringLiteral("流控"), m_flowControlCombo);

    root->addWidget(createCheckRow({QStringLiteral("RTS"), QStringLiteral("DTR"), QStringLiteral("启动连接")},
                                   {&m_rtsCheck, &m_dtrCheck, &m_autoOpenCheck},
                                   section));

    m_connectButton = new PrimaryPushButton(icon(FluentIcon::Connect), QStringLiteral("连接"), section);
    root->addWidget(m_connectButton);

    connect(m_refreshButton, &ToolButton::clicked, this, &WorkbenchPage::refreshPorts);
    connect(m_connectButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::onConnectClicked);
    connect(m_rtsCheck, &CheckBox::toggled, this, [this](bool checked) {
        if(m_serial.isOpen()) {
            m_serial.setRequestToSend(checked);
        }
    });
    connect(m_dtrCheck, &CheckBox::toggled, this, [this](bool checked) {
        if(m_serial.isOpen()) {
            m_serial.setDataTerminalReady(checked);
        }
    });
    connect(m_autoOpenCheck, &CheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("serial/autoOpen"), checked);
    });

    return section;
}

QWidget *WorkbenchPage::createReceiveSettingsSection()
{
    auto *section = new HeaderCardWidget(this);
    hideCardHeader(section);
    auto *root = cardBody(section);

    m_displayModeSegment = new SegmentedWidget(section);
    m_displayModeSegment->addItem(QStringLiteral("text"), QStringLiteral("文本"));
    m_displayModeSegment->addItem(QStringLiteral("hex"), QStringLiteral("HEX"));
    m_displayModeSegment->addItem(QStringLiteral("mixed"), QStringLiteral("混合"));
    m_displayModeSegment->setCurrentItem(QStringLiteral("text"));
    root->addWidget(m_displayModeSegment);

    auto *receiveOptions = new QWidget(section);
    auto *receiveOptionsGrid = new QGridLayout(receiveOptions);
    receiveOptionsGrid->setContentsMargins(0, 0, 0, 0);
    receiveOptionsGrid->setHorizontalSpacing(12);
    receiveOptionsGrid->setVerticalSpacing(6);
    receiveOptionsGrid->setColumnStretch(0, 1);
    receiveOptionsGrid->setColumnStretch(1, 1);
    m_saveReceiveCheck = new CheckBox(QStringLiteral("保存接收"), receiveOptions);
    m_autoScrollCheck = new CheckBox(QStringLiteral("自动滚动"), receiveOptions);
    m_timestampCheck = new CheckBox(QStringLiteral("时间戳"), receiveOptions);
    m_pauseCheck = new CheckBox(QStringLiteral("暂停显示"), receiveOptions);
    for(CheckBox *check : {m_saveReceiveCheck, m_autoScrollCheck, m_timestampCheck, m_pauseCheck}) {
        check->setMinimumWidth(0);
        check->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    receiveOptionsGrid->addWidget(m_saveReceiveCheck, 0, 0);
    receiveOptionsGrid->addWidget(m_autoScrollCheck, 0, 1);
    receiveOptionsGrid->addWidget(m_timestampCheck, 1, 0);
    receiveOptionsGrid->addWidget(m_pauseCheck, 1, 1);
    root->addWidget(receiveOptions);

    auto *frameRowWidget = new QWidget(section);
    auto *frameRow = new QHBoxLayout(frameRowWidget);
    frameRow->setContentsMargins(0, 0, 0, 0);
    frameRow->setSpacing(6);
    m_autoFrameBreakCheck = new CheckBox(QStringLiteral("自动断帧"), frameRowWidget);
    m_autoFrameBreakCheck->setMinimumWidth(0);
    m_autoFrameBreakCheck->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_frameBreakIntervalSpin = new SpinBox(frameRowWidget);
    m_frameBreakIntervalSpin->setRange(1, 60000);
    m_frameBreakIntervalSpin->setValue(20);
    m_frameBreakIntervalSpin->setSymbolVisible(false);
    m_frameBreakIntervalSpin->setFixedHeight(CompactControlHeight);
    setFixedControlWidth(m_frameBreakIntervalSpin, 92);
    auto *frameBreakUnitLabel = new CaptionLabel(QStringLiteral("ms"), frameRowWidget);
    frameBreakUnitLabel->setFixedHeight(CompactControlHeight);
    frameBreakUnitLabel->setFixedWidth(22);
    frameBreakUnitLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    frameRow->addWidget(m_autoFrameBreakCheck);
    frameRow->addWidget(m_frameBreakIntervalSpin);
    frameRow->addWidget(frameBreakUnitLabel);
    root->addWidget(frameRowWidget);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    m_clearButton = new PushButton(icon(FluentIcon::Broom), QStringLiteral("清空"), section);
    m_resetCountersButton = new PushButton(icon(FluentIcon::Cancel), QStringLiteral("计数"), section);
    setButtonRowControlPolicy(m_clearButton);
    setButtonRowControlPolicy(m_resetCountersButton);
    actionRow->addWidget(m_clearButton);
    actionRow->addWidget(m_resetCountersButton);
    root->addLayout(actionRow);

    auto *exportRow = new QHBoxLayout;
    exportRow->setSpacing(8);
    m_exportTxtButton = new PushButton(icon(FluentIcon::Document), QStringLiteral("TXT"), section);
    m_exportCsvButton = new PushButton(icon(FluentIcon::SaveAs), QStringLiteral("CSV"), section);
    m_exportBinButton = new PushButton(icon(FluentIcon::Save), QStringLiteral("BIN"), section);
    setButtonRowControlPolicy(m_exportTxtButton);
    setButtonRowControlPolicy(m_exportCsvButton);
    setButtonRowControlPolicy(m_exportBinButton);
    exportRow->addWidget(m_exportTxtButton);
    exportRow->addWidget(m_exportCsvButton);
    exportRow->addWidget(m_exportBinButton);
    root->addLayout(exportRow);

    m_receiveCaptureLabel = new CaptionLabel(QStringLiteral("接收保存未启用"), section);
    m_receiveCaptureLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_receiveCaptureLabel->setWordWrap(true);
    root->addWidget(m_receiveCaptureLabel);

    connect(m_displayModeSegment, &SegmentedWidget::currentItemChanged, this, [this](const QString &routeKey) {
        QSettings settings;
        settings.setValue(QStringLiteral("terminal/displayMode"), routeKey);
        renderTerminal();
    });
    connect(m_saveReceiveCheck, &CheckBox::toggled, this, &WorkbenchPage::updateReceiveCapture);
    connect(m_autoScrollCheck, &CheckBox::toggled, this, [this](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("receive/autoScroll"), checked);
        if(checked) {
            renderTerminal();
        }
    });
    connect(m_timestampCheck, &CheckBox::toggled, this, [this](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("receive/timestamp"), checked);
        renderTerminal();
    });
    connect(m_pauseCheck, &CheckBox::toggled, this, [this](bool checked) {
        if(!checked) {
            flushPendingLines();
        }
    });
    connect(m_autoFrameBreakCheck, &CheckBox::toggled, this, [this](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("receive/autoFrameBreak"), checked);
        if(!checked) {
            m_lastRxTimestamp = QDateTime();
        }
    });
    connect(m_frameBreakIntervalSpin, &SpinBox::valueChanged, this, [](int value) {
        QSettings settings;
        settings.setValue(QStringLiteral("receive/frameBreakMs"), value);
    });
    connect(m_clearButton, &PushButton::clicked, this, [this]() {
        m_terminalStartRecord = m_records.size();
        m_pendingRecordIndexes.clear();
        m_terminalView->clear();
    });
    connect(m_resetCountersButton, &PushButton::clicked, this, [this]() {
        m_rxCount = 0;
        m_txCount = 0;
        updateCounters();
    });
    connect(m_exportTxtButton, &PushButton::clicked, this, [this]() { exportRecords(ExportFormat::Txt); });
    connect(m_exportCsvButton, &PushButton::clicked, this, [this]() { exportRecords(ExportFormat::Csv); });
    connect(m_exportBinButton, &PushButton::clicked, this, [this]() { exportRecords(ExportFormat::Bin); });

    return section;
}

QWidget *WorkbenchPage::createSendSettingsSection()
{
    auto *section = new HeaderCardWidget(this);
    hideCardHeader(section);
    auto *root = cardBody(section);

    m_lineEndingCombo = new ComboBox(section);
    makeCompactControl(m_lineEndingCombo);
    m_lineEndingCombo->addItem(QStringLiteral("None"), QIcon(), QStringLiteral("none"));
    m_lineEndingCombo->addItem(QStringLiteral("CR"), QIcon(), QStringLiteral("cr"));
    m_lineEndingCombo->addItem(QStringLiteral("LF"), QIcon(), QStringLiteral("lf"));
    m_lineEndingCombo->addItem(QStringLiteral("CRLF"), QIcon(), QStringLiteral("crlf"));
    m_lineEndingCombo->setCurrentIndex(0);
    addFormRow(root, QStringLiteral("换行"), m_lineEndingCombo);

    auto *sendOptions = new QWidget(section);
    auto *sendOptionsGrid = new QGridLayout(sendOptions);
    sendOptionsGrid->setContentsMargins(0, 0, 0, 0);
    sendOptionsGrid->setHorizontalSpacing(12);
    sendOptionsGrid->setVerticalSpacing(6);
    sendOptionsGrid->setColumnStretch(0, 1);
    sendOptionsGrid->setColumnStretch(1, 1);
    m_hexSendCheck = new CheckBox(QStringLiteral("HEX 发送"), sendOptions);
    m_showTxCheck = new CheckBox(QStringLiteral("显示发送字符串"), sendOptions);
    m_loopCheck = new CheckBox(QStringLiteral("定时发送"), sendOptions);
    m_autoReconnectCheck = new CheckBox(QStringLiteral("自动重连"), sendOptions);
    for(CheckBox *check : {m_hexSendCheck, m_showTxCheck, m_loopCheck, m_autoReconnectCheck}) {
        check->setMinimumWidth(0);
        check->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    m_txColorButton = new ColorPickerButton(defaultTxColor(), QStringLiteral("TX 颜色"), sendOptions);
    m_txColorButton->setFixedSize(64, CompactControlHeight);
    sendOptionsGrid->addWidget(m_hexSendCheck, 0, 0);
    sendOptionsGrid->addWidget(m_showTxCheck, 0, 1);
    sendOptionsGrid->addWidget(m_txColorButton, 0, 2);
    sendOptionsGrid->addWidget(m_loopCheck, 1, 0);
    sendOptionsGrid->addWidget(m_autoReconnectCheck, 1, 1);
    root->addWidget(sendOptions);

    m_historyCombo = new ComboBox(section);
    makeCompactControl(m_historyCombo);
    addFormRow(root, QStringLiteral("历史"), m_historyCombo);

    m_loopIntervalSpin = new SpinBox(section);
    m_loopIntervalSpin->setRange(10, 600000);
    m_loopIntervalSpin->setValue(1000);
    m_loopIntervalSpin->setSuffix(QStringLiteral(" ms"));
    m_loopIntervalSpin->setEnabled(false);
    addFormRow(root, QStringLiteral("间隔"), m_loopIntervalSpin);

    connect(m_historyCombo, &ComboBox::currentIndexChanged, this, &WorkbenchPage::applyHistoryItem);
    connect(m_hexSendCheck, &CheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("send/mode"), checked ? QStringLiteral("hex") : QStringLiteral("text"));
    });
    connect(m_showTxCheck, &CheckBox::toggled, this, [this](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("send/showTx"), checked);
        m_txColorButton->setEnabled(checked);
        renderTerminal();
    });
    connect(m_txColorButton, &ColorPickerButton::colorChanged, this, [this](const QColor &color) {
        QSettings settings;
        settings.setValue(QStringLiteral("send/txColor"), color.name(QColor::HexRgb));
        renderTerminal();
    });
    connect(m_loopCheck, &CheckBox::toggled, this, &WorkbenchPage::onLoopChanged);
    connect(m_autoReconnectCheck, &CheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("serial/autoReconnect"), checked);
    });
    connect(m_loopIntervalSpin, &SpinBox::valueChanged, this, [this](int value) {
        if(m_loopTimer.isActive()) {
            m_loopTimer.start(value);
        }
    });

    return section;
}

QWidget *WorkbenchPage::createPacketSection()
{
    auto *section = new HeaderCardWidget(QStringLiteral("常用包"), this);
    auto *root = cardBody(section);

    m_packetNameEdit = new LineEdit(section);
    m_packetNameEdit->setPlaceholderText(QStringLiteral("包名称"));
    makeCompactControl(m_packetNameEdit);
    addFormRow(root, QStringLiteral("名称"), m_packetNameEdit);

    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(8);
    m_packetModeCombo = new ComboBox(section);
    m_packetModeCombo->addItem(QStringLiteral("文本"), QIcon(), QStringLiteral("text"));
    m_packetModeCombo->addItem(QStringLiteral("HEX"), QIcon(), QStringLiteral("hex"));
    m_packetModeCombo->setCurrentIndex(0);
    makeCompactControl(m_packetModeCombo);
    m_packetLineEndingCombo = new ComboBox(section);
    m_packetLineEndingCombo->addItem(QStringLiteral("None"), QIcon(), QStringLiteral("none"));
    m_packetLineEndingCombo->addItem(QStringLiteral("CR"), QIcon(), QStringLiteral("cr"));
    m_packetLineEndingCombo->addItem(QStringLiteral("LF"), QIcon(), QStringLiteral("lf"));
    m_packetLineEndingCombo->addItem(QStringLiteral("CRLF"), QIcon(), QStringLiteral("crlf"));
    m_packetLineEndingCombo->setCurrentIndex(0);
    makeCompactControl(m_packetLineEndingCombo);
    modeRow->addWidget(m_packetModeCombo);
    modeRow->addWidget(m_packetLineEndingCombo);
    root->addLayout(modeRow);

    m_packetPayloadEdit = new PlainTextEdit(section);
    m_packetPayloadEdit->setPlaceholderText(QStringLiteral("发送内容"));
    m_packetPayloadEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_packetPayloadEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_packetPayloadEdit->setFixedHeight(72);
    m_packetPayloadEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_packetPayloadEdit);

    m_packetList = new ListWidget(section);
    m_packetList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_packetList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_packetList->setMinimumHeight(132);
    m_packetList->setMaximumHeight(220);
    m_packetList->setBorderRadius(8);
    root->addWidget(m_packetList);

    auto *moveRow = new QHBoxLayout;
    moveRow->setSpacing(8);
    m_packetUpButton = new TransparentToolButton(icon(FluentIcon::Up), section);
    m_packetUpButton->setToolTip(QStringLiteral("上移"));
    m_packetDownButton = new TransparentToolButton(icon(FluentIcon::Download), section);
    m_packetDownButton->setToolTip(QStringLiteral("下移"));
    for(ToolButton *button : {m_packetUpButton, m_packetDownButton}) {
        button->setFixedSize(CompactControlHeight, CompactControlHeight);
        button->setIconSize(QSize(16, 16));
    }
    m_packetSaveButton = new PushButton(icon(FluentIcon::Save), QStringLiteral("保存"), section);
    m_packetLoadButton = new PushButton(icon(FluentIcon::Edit), QStringLiteral("填入发送"), section);
    setButtonRowControlPolicy(m_packetSaveButton);
    setButtonRowControlPolicy(m_packetLoadButton);
    moveRow->addWidget(m_packetUpButton);
    moveRow->addWidget(m_packetDownButton);
    moveRow->addWidget(m_packetSaveButton);
    moveRow->addWidget(m_packetLoadButton);
    root->addLayout(moveRow);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    m_packetSendButton = new PrimaryPushButton(icon(FluentIcon::Send), QStringLiteral("发送"), section);
    m_packetDeleteButton = new PushButton(icon(FluentIcon::Delete), QStringLiteral("删除"), section);
    setButtonRowControlPolicy(m_packetSendButton);
    setButtonRowControlPolicy(m_packetDeleteButton);
    actionRow->addWidget(m_packetSendButton);
    actionRow->addWidget(m_packetDeleteButton);
    root->addLayout(actionRow);

    connect(m_packetList, &ListWidget::currentRowChanged, this, [this](int row) {
        applyPacket(row);
    });
    connect(m_packetSaveButton, &PushButton::clicked, this, &WorkbenchPage::saveCurrentPacket);
    connect(m_packetLoadButton, &PushButton::clicked, this, [this]() {
        applyPacket(m_packetList->currentRow());
    });
    connect(m_packetDeleteButton, &PushButton::clicked, this, &WorkbenchPage::removeSelectedPacket);
    connect(m_packetSendButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::sendSelectedPacket);
    connect(m_packetUpButton, &ToolButton::clicked, this, [this]() { moveSelectedPacket(-1); });
    connect(m_packetDownButton, &ToolButton::clicked, this, [this]() { moveSelectedPacket(1); });

    updatePacketTable();
    return section;
}

QWidget *WorkbenchPage::createFileSendSection()
{
    auto *section = new HeaderCardWidget(QStringLiteral("文件发送"), this);
    auto *root = cardBody(section);

    m_filePathEdit = new LineEdit(section);
    m_filePathEdit->setPlaceholderText(QStringLiteral("选择待发送文件"));
    m_filePathEdit->setReadOnly(true);
    makeCompactControl(m_filePathEdit);
    m_fileBrowseButton = new PushButton(icon(FluentIcon::Folder), QStringLiteral("浏览"), section);
    m_fileBrowseButton->setFixedHeight(CompactControlHeight);
    addFormRow(root, QStringLiteral("文件"), m_filePathEdit, m_fileBrowseButton);

    m_fileChunkSizeSpin = new SpinBox(section);
    m_fileChunkSizeSpin->setRange(1, 65536);
    m_fileChunkSizeSpin->setValue(DefaultFileChunkSize);
    m_fileChunkSizeSpin->setSuffix(QStringLiteral(" B"));
    addFormRow(root, QStringLiteral("块长"), m_fileChunkSizeSpin);

    m_fileIntervalSpin = new SpinBox(section);
    m_fileIntervalSpin->setRange(0, 60000);
    m_fileIntervalSpin->setValue(DefaultFileChunkIntervalMs);
    m_fileIntervalSpin->setSuffix(QStringLiteral(" ms"));
    addFormRow(root, QStringLiteral("间隔"), m_fileIntervalSpin);

    m_fileProgressBar = new ProgressBar(section);
    m_fileProgressBar->setRange(0, 100);
    m_fileProgressBar->setValue(0);
    m_fileProgressBar->setFixedHeight(18);
    root->addWidget(m_fileProgressBar);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    m_fileSendButton = new PrimaryPushButton(icon(FluentIcon::Send), QStringLiteral("发送文件"), section);
    m_fileCancelButton = new PushButton(icon(FluentIcon::Cancel), QStringLiteral("取消"), section);
    setButtonRowControlPolicy(m_fileSendButton);
    setButtonRowControlPolicy(m_fileCancelButton);
    actionRow->addWidget(m_fileSendButton);
    actionRow->addWidget(m_fileCancelButton);
    root->addLayout(actionRow);

    m_fileStatusLabel = new CaptionLabel(QStringLiteral("未选择文件"), section);
    m_fileStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_fileStatusLabel->setWordWrap(true);
    root->addWidget(m_fileStatusLabel);

    connect(m_fileBrowseButton, &PushButton::clicked, this, &WorkbenchPage::browseSendFile);
    connect(m_fileSendButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::startFileSend);
    connect(m_fileCancelButton, &PushButton::clicked, this, &WorkbenchPage::cancelFileSend);

    updateFileSendUi(false);
    return section;
}

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
    connect(m_terminalSearchEdit, &SearchLineEdit::textChanged, this, [this]() {
        renderTerminal();
    });
    connect(m_terminalSearchEdit, &SearchLineEdit::searchSignal, this, [this](const QString &) {
        renderTerminal();
    });
    connect(m_terminalSearchEdit, &SearchLineEdit::clearSignal, this, [this]() {
        renderTerminal();
    });
    connect(m_terminalFilterCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        renderTerminal();
    });

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

void WorkbenchPage::setupSerialSignals()
{
    connect(&m_serial, &SerialController::opened, this, [this](const QString &portName) {
        m_reconnectTimer.stop();
        m_manualDisconnect = false;
        m_lastRxTimestamp = QDateTime();
        m_connectionStartedAt = QDateTime::currentDateTime();
        m_lastStatsRxCount = m_rxCount;
        m_lastStatsTxCount = m_txCount;
        m_statsTimer.start();
        updateConnectionUi(true);
        updateRateStats();
        if(m_saveReceiveCheck->isChecked()) {
            updateReceiveCapture(true);
        }
        showSuccess(QStringLiteral("连接成功"), QStringLiteral("%1 已打开").arg(portName));
    });
    connect(&m_serial, &SerialController::closed, this, [this]() {
        if(m_fileSendFile.isOpen()) {
            m_fileSendTimer.stop();
            m_fileSendFile.close();
            updateFileSendUi(false);
            updateFileProgress();
        }
        m_statsTimer.stop();
        closeReceiveCapture();
        m_lastRxTimestamp = QDateTime();
        updateConnectionUi(false);
        updateRateStats();
        if(!m_manualDisconnect && m_autoReconnectCheck && m_autoReconnectCheck->isChecked()) {
            scheduleReconnect();
            return;
        }
        showInfo(QStringLiteral("连接已关闭"), QStringLiteral("串口已断开"));
    });
    connect(&m_serial, &SerialController::dataReceived, this, [this](const QByteArray &data) {
        appendRecord(RecordDirection::Rx, data);
    });
    connect(&m_serial, &SerialController::errorOccurred, this, [this](const QString &message) {
        if(!message.isEmpty()) {
            if(m_reconnectTimer.isActive()) {
                return;
            }
            showError(QStringLiteral("串口错误"), message);
        }
    });
}

void WorkbenchPage::refreshPorts()
{
    const QString previousPort = m_portCombo->currentData().toString();
    m_ports = SerialController::availablePorts();
    m_portCombo->clear();

    int selectedIndex = -1;
    for(int i = 0; i < m_ports.size(); ++i) {
        const SerialPortDescriptor &descriptor = m_ports.at(i);
        m_portCombo->addItem(descriptor.displayName(), QIcon(), descriptor.portName);
        if(descriptor.portName == previousPort) {
            selectedIndex = i;
        }
    }

    if(m_ports.isEmpty()) {
        m_portCombo->addItem(QStringLiteral("未发现串口"));
        m_portCombo->setItemEnabled(0, false);
        m_portCombo->setCurrentIndex(0);
        return;
    }

    m_portCombo->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
}

void WorkbenchPage::restoreSettings()
{
    QSettings settings;
    const QString portName = settings.value(QStringLiteral("serial/portName")).toString();
    const QString baudRate = settings.value(QStringLiteral("serial/baudRate"), QStringLiteral("115200")).toString();
    const QString dataBits = settings.value(QStringLiteral("serial/dataBits"), QStringLiteral("8")).toString();
    const int parity = settings.value(QStringLiteral("serial/parity"), static_cast<int>(QSerialPort::NoParity)).toInt();
    const int stopBits = settings.value(QStringLiteral("serial/stopBits"), static_cast<int>(QSerialPort::OneStop)).toInt();
    const int flow = settings.value(QStringLiteral("serial/flowControl"), static_cast<int>(QSerialPort::NoFlowControl)).toInt();
    const QString displayMode = settings.value(QStringLiteral("terminal/displayMode"), QStringLiteral("text")).toString();
    const QString sendMode = settings.value(QStringLiteral("send/mode"), QStringLiteral("text")).toString();
    const QString lineEnding = settings.value(QStringLiteral("send/lineEnding"), QStringLiteral("none")).toString();
    const int loopInterval = settings.value(QStringLiteral("send/loopIntervalMs"), 1000).toInt();
    const int frameBreakMs = settings.value(QStringLiteral("receive/frameBreakMs"), 20).toInt();
    const QString sendPayload = settings.value(QStringLiteral("send/currentPayload")).toString();
    const QString packetName = settings.value(QStringLiteral("send/currentPacketName")).toString();
    const QString packetPayload = settings.value(QStringLiteral("send/currentPacketPayload")).toString();
    const QString packetMode = settings.value(QStringLiteral("send/currentPacketMode"), QStringLiteral("text")).toString();
    const QString packetLineEnding =
        settings.value(QStringLiteral("send/currentPacketLineEnding"), QStringLiteral("none")).toString();
    const QString filePath = settings.value(QStringLiteral("fileSend/path")).toString();
    const int fileChunkSize = settings.value(QStringLiteral("fileSend/chunkSize"), DefaultFileChunkSize).toInt();
    const int fileInterval = settings.value(QStringLiteral("fileSend/intervalMs"), DefaultFileChunkIntervalMs).toInt();

    const int portIndex = m_portCombo->findData(portName);
    if(portIndex >= 0) {
        m_portCombo->setCurrentIndex(portIndex);
    }
    m_baudCombo->setCurrentText(baudRate);
    m_dataBitsCombo->setCurrentText(dataBits);
    const int parityIndex = m_parityCombo->findData(parity);
    if(parityIndex >= 0) {
        m_parityCombo->setCurrentIndex(parityIndex);
    }
    const int stopIndex = m_stopBitsCombo->findData(stopBits);
    if(stopIndex >= 0) {
        m_stopBitsCombo->setCurrentIndex(stopIndex);
    }
    const int flowIndex = m_flowControlCombo->findData(flow);
    if(flowIndex >= 0) {
        m_flowControlCombo->setCurrentIndex(flowIndex);
    }
    if(m_displayModeSegment->contains(displayMode)) {
        m_displayModeSegment->setCurrentItem(displayMode);
    }
    m_hexSendCheck->setChecked(sendMode == QStringLiteral("hex"));
    const int eolIndex = m_lineEndingCombo->findData(lineEnding);
    if(eolIndex >= 0) {
        m_lineEndingCombo->setCurrentIndex(eolIndex);
    }
    m_loopIntervalSpin->setValue(qBound(10, loopInterval, 600000));
    m_rtsCheck->setChecked(settings.value(QStringLiteral("serial/rts"), false).toBool());
    m_dtrCheck->setChecked(settings.value(QStringLiteral("serial/dtr"), false).toBool());
    m_autoOpenCheck->setChecked(settings.value(QStringLiteral("serial/autoOpen"), false).toBool());
    m_saveReceiveCheck->setChecked(settings.value(QStringLiteral("receive/saveToFile"), false).toBool());
    m_autoScrollCheck->setChecked(settings.value(QStringLiteral("receive/autoScroll"), true).toBool());
    m_timestampCheck->setChecked(settings.value(QStringLiteral("receive/timestamp"), false).toBool());
    m_autoFrameBreakCheck->setChecked(settings.value(QStringLiteral("receive/autoFrameBreak"), false).toBool());
    m_frameBreakIntervalSpin->setValue(qBound(1, frameBreakMs, 60000));
    m_showTxCheck->setChecked(settings.value(QStringLiteral("send/showTx"), true).toBool());
    const QColor txColor(settings.value(QStringLiteral("send/txColor"),
                                        defaultTxColor().name(QColor::HexRgb))
                             .toString());
    m_txColorButton->setColor(txColor.isValid() ? txColor : defaultTxColor());
    m_txColorButton->setEnabled(m_showTxCheck->isChecked());
    m_autoReconnectCheck->setChecked(settings.value(QStringLiteral("serial/autoReconnect"), true).toBool());
    m_sendEdit->setPlainText(sendPayload);
    m_packetNameEdit->setText(packetName);
    m_packetPayloadEdit->setPlainText(packetPayload);
    const int packetModeIndex = m_packetModeCombo->findData(packetMode);
    if(packetModeIndex >= 0) {
        m_packetModeCombo->setCurrentIndex(packetModeIndex);
    }
    const int packetEolIndex = m_packetLineEndingCombo->findData(packetLineEnding);
    if(packetEolIndex >= 0) {
        m_packetLineEndingCombo->setCurrentIndex(packetEolIndex);
    }
    m_filePathEdit->setText(filePath);
    m_fileChunkSizeSpin->setValue(qBound(1, fileChunkSize, 65536));
    m_fileIntervalSpin->setValue(qBound(0, fileInterval, 60000));
    if(!filePath.isEmpty()) {
        const QFileInfo info(filePath);
        m_fileStatusLabel->setText(info.exists() ? QStringLiteral("%1 · %2").arg(info.fileName(), formatBytes(info.size()))
                                                 : QStringLiteral("文件不存在：%1").arg(filePath));
    }
    m_terminalView->document()->setMaximumBlockCount(maxRecordCount());
    applyTerminalFont();
}

void WorkbenchPage::saveSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("serial/portName"), m_portCombo->currentData().toString());
    settings.setValue(QStringLiteral("serial/baudRate"), m_baudCombo->currentText());
    settings.setValue(QStringLiteral("serial/dataBits"), m_dataBitsCombo->currentText());
    settings.setValue(QStringLiteral("serial/parity"), m_parityCombo->currentData().toInt());
    settings.setValue(QStringLiteral("serial/stopBits"), m_stopBitsCombo->currentData().toInt());
    settings.setValue(QStringLiteral("serial/flowControl"), m_flowControlCombo->currentData().toInt());
    settings.setValue(QStringLiteral("serial/rts"), m_rtsCheck->isChecked());
    settings.setValue(QStringLiteral("serial/dtr"), m_dtrCheck->isChecked());
    settings.setValue(QStringLiteral("serial/autoOpen"), m_autoOpenCheck->isChecked());
    settings.setValue(QStringLiteral("receive/saveToFile"), m_saveReceiveCheck->isChecked());
    settings.setValue(QStringLiteral("receive/autoScroll"), m_autoScrollCheck->isChecked());
    settings.setValue(QStringLiteral("receive/timestamp"), m_timestampCheck->isChecked());
    settings.setValue(QStringLiteral("receive/autoFrameBreak"), m_autoFrameBreakCheck->isChecked());
    settings.setValue(QStringLiteral("receive/frameBreakMs"), m_frameBreakIntervalSpin->value());
    settings.setValue(QStringLiteral("terminal/displayMode"), currentDisplayMode());
    settings.setValue(QStringLiteral("send/mode"), m_hexSendCheck->isChecked() ? QStringLiteral("hex") : QStringLiteral("text"));
    settings.setValue(QStringLiteral("send/lineEnding"), selectedLineEndingKey());
    settings.setValue(QStringLiteral("send/showTx"), m_showTxCheck->isChecked());
    settings.setValue(QStringLiteral("send/txColor"), selectedTxColor().name(QColor::HexRgb));
    settings.setValue(QStringLiteral("send/loopIntervalMs"), m_loopIntervalSpin->value());
    settings.setValue(QStringLiteral("send/currentPayload"), m_sendEdit->toPlainText());
    settings.setValue(QStringLiteral("send/currentPacketName"), m_packetNameEdit->text());
    settings.setValue(QStringLiteral("send/currentPacketPayload"), m_packetPayloadEdit->toPlainText());
    settings.setValue(QStringLiteral("send/currentPacketMode"), m_packetModeCombo->currentData().toString());
    settings.setValue(QStringLiteral("send/currentPacketLineEnding"),
                      m_packetLineEndingCombo->currentData().toString());
    settings.setValue(QStringLiteral("serial/autoReconnect"), m_autoReconnectCheck->isChecked());
    settings.setValue(QStringLiteral("fileSend/path"), m_filePathEdit->text());
    settings.setValue(QStringLiteral("fileSend/chunkSize"), m_fileChunkSizeSpin->value());
    settings.setValue(QStringLiteral("fileSend/intervalMs"), m_fileIntervalSpin->value());
    saveSendHistory();
    saveSendPackets();
}

void WorkbenchPage::setTerminalFontFamily(const QString &family)
{
    applyTerminalFont(family);
}

void WorkbenchPage::applyTerminalFont(const QString &family)
{
    if(m_terminalView) {
        const QFont font = AppFontPreferences::terminalFont(family);
        m_terminalView->setFont(font);
        m_terminalView->setCurrentFont(font);
        m_terminalView->document()->setDefaultFont(font);

        const QString qss = QStringLiteral("QTextBrowser[fqw=\"TextBrowser\"] { font-family: %1; font-size: %2pt; }")
                                .arg(qssString(font.family()))
                                .arg(font.pointSize());
        FluentStyleSheet::setCustomStyleSheet(m_terminalView, qss, qss);
        m_terminalView->viewport()->update();
    }
}

void WorkbenchPage::updateConnectionUi(bool connected)
{
    m_connectButton->setText(connected ? QStringLiteral("断开") : QStringLiteral("连接"));
    m_connectButton->setIcon(icon(connected ? FluentIcon::PowerButton : FluentIcon::Connect));
    setControlsEnabledForConnection(connected);
}

void WorkbenchPage::updateCounters()
{
    m_rxCounterLabel->setText(formatBytes(m_rxCount));
    m_txCounterLabel->setText(formatBytes(m_txCount));
    if(m_terminalSummaryLabel) {
        int visibleRecords = 0;
        for(const SessionRecord &record : m_records) {
            if(record.direction == RecordDirection::Tx && m_showTxCheck && !m_showTxCheck->isChecked()) {
                continue;
            }
            if(record.direction != RecordDirection::FrameBreak && recordMatchesTerminalFilter(record)) {
                ++visibleRecords;
            }
        }
        m_terminalSummaryLabel->setText(QStringLiteral("显示 %1 条").arg(visibleRecords));
    }
}

void WorkbenchPage::updateRateStats()
{
    if(m_rxRateLabel) {
        m_rxRateLabel->setText(formatBytesPerSecond(qMax<qint64>(0, m_rxCount - m_lastStatsRxCount)));
    }
    if(m_txRateLabel) {
        m_txRateLabel->setText(formatBytesPerSecond(qMax<qint64>(0, m_txCount - m_lastStatsTxCount)));
    }
    m_lastStatsRxCount = m_rxCount;
    m_lastStatsTxCount = m_txCount;

    if(!m_connectionTimeLabel) {
        return;
    }
    if(m_serial.isOpen() && m_connectionStartedAt.isValid()) {
        const qint64 seconds = m_connectionStartedAt.secsTo(QDateTime::currentDateTime());
        m_connectionTimeLabel->setText(QStringLiteral("已连接 %1").arg(formatDuration(seconds)));
    } else {
        m_connectionTimeLabel->setText(QStringLiteral("未连接"));
    }
}

void WorkbenchPage::updateHistoryCombo()
{
    if(!m_historyCombo) {
        return;
    }

    const QSignalBlocker blocker(m_historyCombo);
    m_historyCombo->clear();
    m_historyCombo->addItem(QStringLiteral("发送历史"), QIcon(), QVariant());
    m_historyCombo->setItemEnabled(0, false);
    for(int i = 0; i < m_sendHistory.size(); ++i) {
        m_historyCombo->addItem(historyLabel(m_sendHistory.at(i)), QIcon(), i);
    }
    m_historyCombo->setCurrentIndex(0);
}

void WorkbenchPage::applyHistoryItem(int index)
{
    bool ok = false;
    const int historyIndex = m_historyCombo->itemData(index).toInt(&ok);
    if(!ok || historyIndex < 0 || historyIndex >= m_sendHistory.size()) {
        return;
    }

    const SendHistoryItem item = m_sendHistory.at(historyIndex);
    m_hexSendCheck->setChecked(item.mode == QStringLiteral("hex"));
    const int lineEndingIndex = m_lineEndingCombo->findData(item.lineEnding);
    if(lineEndingIndex >= 0) {
        m_lineEndingCombo->setCurrentIndex(lineEndingIndex);
    }
    m_sendEdit->setPlainText(item.payload);
}

void WorkbenchPage::updatePacketTable(int selectedRow)
{
    if(!m_packetList) {
        return;
    }

    const QSignalBlocker blocker(m_packetList);
    m_packetList->clear();
    for(int row = 0; row < m_sendPackets.size(); ++row) {
        const SendPacket &packet = m_sendPackets.at(row);
        QString payload = packet.payload.simplified();
        if(payload.size() > 38) {
            payload = payload.left(35) + QStringLiteral("...");
        }

        auto *item = new QListWidgetItem(icon(FluentIcon::CommandPrompt),
                                         QStringLiteral("%1\n%2 · %3 · %4")
                                             .arg(packet.name,
                                                  modeLabel(packet.mode),
                                                  lineEndingLabel(packet.lineEnding),
                                                  payload));
        item->setData(Qt::UserRole, row);
        item->setToolTip(packet.payload);
        item->setSizeHint(QSize(0, 52));
        m_packetList->addItem(item);
    }

    if(!m_sendPackets.isEmpty()) {
        const int row = qBound(0, selectedRow >= 0 ? selectedRow : m_packetList->currentRow(), m_sendPackets.size() - 1);
        m_packetList->setCurrentRow(row);
    }
    const bool hasPacket = !m_sendPackets.isEmpty();
    if(m_packetLoadButton) {
        m_packetLoadButton->setEnabled(hasPacket);
    }
    if(m_packetDeleteButton) {
        m_packetDeleteButton->setEnabled(hasPacket);
    }
    if(m_packetSendButton) {
        m_packetSendButton->setEnabled(hasPacket && m_serial.isOpen());
    }
    if(m_packetUpButton) {
        m_packetUpButton->setEnabled(hasPacket);
    }
    if(m_packetDownButton) {
        m_packetDownButton->setEnabled(hasPacket);
    }
}

void WorkbenchPage::applyPacket(int row)
{
    if(row < 0 || row >= m_sendPackets.size()) {
        return;
    }

    const SendPacket packet = m_sendPackets.at(row);
    m_packetNameEdit->setText(packet.name);
    m_packetPayloadEdit->setPlainText(packet.payload);
    const int packetModeIndex = m_packetModeCombo->findData(packet.mode);
    if(packetModeIndex >= 0) {
        m_packetModeCombo->setCurrentIndex(packetModeIndex);
    }
    const int packetLineEndingIndex = m_packetLineEndingCombo->findData(packet.lineEnding);
    if(packetLineEndingIndex >= 0) {
        m_packetLineEndingCombo->setCurrentIndex(packetLineEndingIndex);
    }

    m_hexSendCheck->setChecked(packet.mode == QStringLiteral("hex"));
    const int lineEndingIndex = m_lineEndingCombo->findData(packet.lineEnding);
    if(lineEndingIndex >= 0) {
        m_lineEndingCombo->setCurrentIndex(lineEndingIndex);
    }
    m_sendEdit->setPlainText(packet.payload);
}

void WorkbenchPage::saveCurrentPacket()
{
    const QString payload = m_packetPayloadEdit->toPlainText();
    if(payload.trimmed().isEmpty()) {
        showWarning(QStringLiteral("无法保存"), QStringLiteral("发送内容为空"));
        return;
    }

    QString name = m_packetNameEdit->text().trimmed();
    if(name.isEmpty()) {
        name = payload.simplified();
        if(name.size() > 18) {
            name = name.left(18) + QStringLiteral("...");
        }
        if(name.isEmpty()) {
            name = QStringLiteral("未命名包");
        }
        m_packetNameEdit->setText(name);
    }

    SendPacket packet;
    packet.name = name;
    packet.mode = m_packetModeCombo->currentData().toString();
    packet.payload = payload;
    packet.lineEnding = m_packetLineEndingCombo->currentData().toString();
    packet.enabled = true;

    int targetRow = -1;
    for(int i = 0; i < m_sendPackets.size(); ++i) {
        if(m_sendPackets.at(i).name == packet.name) {
            targetRow = i;
            break;
        }
    }
    if(targetRow >= 0) {
        m_sendPackets[targetRow] = packet;
    } else {
        if(m_sendPackets.size() >= MaxSendPacketItems) {
            m_sendPackets.removeLast();
        }
        m_sendPackets.append(packet);
        targetRow = m_sendPackets.size() - 1;
    }

    updatePacketTable(targetRow);
    applyPacket(targetRow);
    saveSendPackets();
    showSuccess(QStringLiteral("已保存常用包"), packet.name);
}

void WorkbenchPage::removeSelectedPacket()
{
    const int row = m_packetList ? m_packetList->currentRow() : -1;
    if(row < 0 || row >= m_sendPackets.size()) {
        return;
    }
    const QString name = m_sendPackets.at(row).name;
    m_sendPackets.removeAt(row);
    updatePacketTable(qMin(row, m_sendPackets.size() - 1));
    saveSendPackets();
    showInfo(QStringLiteral("已删除常用包"), name);
}

void WorkbenchPage::moveSelectedPacket(int direction)
{
    const int row = m_packetList ? m_packetList->currentRow() : -1;
    const int target = row + direction;
    if(row < 0 || row >= m_sendPackets.size() || target < 0 || target >= m_sendPackets.size()) {
        return;
    }

    m_sendPackets.swapItemsAt(row, target);
    updatePacketTable(target);
    saveSendPackets();
}

void WorkbenchPage::sendSelectedPacket()
{
    sendPacket(m_packetList ? m_packetList->currentRow() : -1);
}

void WorkbenchPage::sendPacket(int row)
{
    if(row < 0 || row >= m_sendPackets.size()) {
        return;
    }
    if(!m_serial.isOpen()) {
        showWarning(QStringLiteral("无法发送"), QStringLiteral("请先连接串口"));
        return;
    }

    const SendPacket packet = m_sendPackets.at(row);
    bool ok = false;
    const QByteArray payload = payloadFromText(packet.payload, packet.mode, packet.lineEnding, false, &ok);
    if(!ok) {
        return;
    }
    if(payload.isEmpty()) {
        showWarning(QStringLiteral("无法发送"), QStringLiteral("常用包内容为空"));
        return;
    }

    QString error;
    if(!m_serial.writeData(payload, &error)) {
        showError(QStringLiteral("发送失败"), error);
        return;
    }

    SendHistoryItem historyItem;
    historyItem.mode = packet.mode;
    historyItem.payload = packet.payload;
    historyItem.lineEnding = packet.lineEnding;
    addSendHistory(historyItem);
    appendRecord(RecordDirection::Tx, payload);
}

void WorkbenchPage::showInfo(const QString &title, const QString &message)
{
    InfoBar::info(title, message, Qt::Horizontal, true, 1800, InfoBarPosition::TopRight, window());
}

void WorkbenchPage::showSuccess(const QString &title, const QString &message)
{
    InfoBar::success(title, message, Qt::Horizontal, true, 1800, InfoBarPosition::TopRight, window());
}

void WorkbenchPage::showWarning(const QString &title, const QString &message)
{
    InfoBar::warning(title, message, Qt::Horizontal, true, 2500, InfoBarPosition::TopRight, window());
}

void WorkbenchPage::showError(const QString &title, const QString &message)
{
    InfoBar::error(title, message, Qt::Horizontal, true, 3500, InfoBarPosition::TopRight, window());
}

SerialPortConfig WorkbenchPage::currentSerialConfig() const
{
    bool baudOk = false;
    const qint32 baud = m_baudCombo->currentText().trimmed().toInt(&baudOk);

    SerialPortConfig config;
    config.portName = m_portCombo->currentData().toString();
    config.baudRate = baudOk && baud > 0 ? baud : 115200;
    config.dataBits = dataBitsFromText(m_dataBitsCombo->currentText());
    config.parity = parityFromData(m_parityCombo->currentData());
    config.stopBits = stopBitsFromData(m_stopBitsCombo->currentData());
    config.flowControl = flowControlFromData(m_flowControlCombo->currentData());
    config.requestToSend = m_rtsCheck->isChecked();
    config.dataTerminalReady = m_dtrCheck->isChecked();
    return config;
}

QByteArray WorkbenchPage::currentPayload(bool *ok)
{
    return payloadFromText(m_sendEdit->toPlainText(),
                           m_hexSendCheck->isChecked() ? QStringLiteral("hex") : QStringLiteral("text"),
                           selectedLineEndingKey(),
                           true,
                           ok);
}

QByteArray WorkbenchPage::payloadFromText(const QString &payloadText,
                                          const QString &mode,
                                          const QString &lineEndingKey,
                                          bool focusEditorOnError,
                                          bool *ok)
{
    if(ok) {
        *ok = false;
    }

    QByteArray data;
    if(mode == QStringLiteral("hex")) {
        const HexParseResult result = parseHexPayload(payloadText);
        if(!result.ok) {
            if(focusEditorOnError) {
                m_sendEdit->setFocus();
            }
            showWarning(QStringLiteral("HEX 输入无效"),
                        result.errorOffset >= 0
                            ? QStringLiteral("%1，位置 %2").arg(result.errorMessage).arg(result.errorOffset + 1)
                            : result.errorMessage);
            return data;
        }
        data = result.bytes;
    } else {
        data = payloadText.toUtf8();
    }

    data.append(lineEndingForKey(lineEndingKey));
    if(ok) {
        *ok = true;
    }
    return data;
}

QByteArray WorkbenchPage::selectedLineEnding() const
{
    return lineEndingForKey(selectedLineEndingKey());
}

QByteArray WorkbenchPage::lineEndingForKey(const QString &key) const
{
    return lineEndingBytes(key);
}

QString WorkbenchPage::selectedLineEndingKey() const
{
    return m_lineEndingCombo->currentData().toString();
}

QString WorkbenchPage::currentDisplayMode() const
{
    return m_displayModeSegment ? m_displayModeSegment->currentItem() : QStringLiteral("text");
}

QString WorkbenchPage::terminalSearchText() const
{
    return m_terminalSearchEdit ? m_terminalSearchEdit->text().trimmed() : QString();
}

QString WorkbenchPage::terminalDirectionFilter() const
{
    return m_terminalFilterCombo ? m_terminalFilterCombo->currentData().toString() : QStringLiteral("all");
}

QString WorkbenchPage::directionText(RecordDirection direction) const
{
    if(direction == RecordDirection::Rx) {
        return QStringLiteral("收到");
    }
    if(direction == RecordDirection::Tx) {
        return QStringLiteral("发送");
    }
    return {};
}

QString WorkbenchPage::historyLabel(const SendHistoryItem &item) const
{
    QString payload = item.payload.simplified();
    if(payload.size() > 42) {
        payload = payload.left(39) + QStringLiteral("...");
    }
    return QStringLiteral("%1 · %2 · %3").arg(modeLabel(item.mode), lineEndingLabel(item.lineEnding), payload);
}

QString WorkbenchPage::exportSuffix(ExportFormat format) const
{
    switch(format) {
    case ExportFormat::Txt:
        return QStringLiteral("txt");
    case ExportFormat::Csv:
        return QStringLiteral("csv");
    case ExportFormat::Bin:
        return QStringLiteral("bin");
    }
    return QStringLiteral("txt");
}

QString WorkbenchPage::formatRecordLine(const SessionRecord &record) const
{
    if(record.direction == RecordDirection::FrameBreak) {
        return {};
    }

    const QString mode = currentDisplayMode();
    QString payload;
    if(mode == QStringLiteral("hex")) {
        payload = bytesToHex(record.bytes);
    } else if(mode == QStringLiteral("mixed")) {
        payload = QStringLiteral("%1    | %2").arg(bytesToHex(record.bytes), record.displayText);
    } else {
        payload = (!m_timestampCheck || !m_timestampCheck->isChecked()) ? bytesToTerminalText(record.bytes)
                                                                        : record.displayText;
    }

    const QString marker = record.direction == RecordDirection::Tx ? QStringLiteral("»") : QStringLiteral("«");
    if(m_timestampCheck && m_timestampCheck->isChecked()) {
        return payload.isEmpty()
                   ? QStringLiteral("%1 %2").arg(record.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")), marker)
                   : QStringLiteral("%1 %2 %3")
                         .arg(record.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")), marker, payload);
    }

    return payload.isEmpty() ? marker : QStringLiteral("%1 %2").arg(marker, payload);
}

QColor WorkbenchPage::selectedTxColor() const
{
    QColor color = m_txColorButton ? m_txColorButton->color() : defaultTxColor();
    if(!color.isValid()) {
        color = defaultTxColor();
    }
    color.setAlpha(255);
    return color;
}

bool WorkbenchPage::recordMatchesTerminalFilter(const SessionRecord &record) const
{
    if(record.direction == RecordDirection::FrameBreak) {
        return terminalSearchText().isEmpty() && terminalDirectionFilter() == QStringLiteral("all");
    }

    const QString directionFilter = terminalDirectionFilter();
    if(directionFilter == QStringLiteral("rx") && record.direction != RecordDirection::Rx) {
        return false;
    }
    if(directionFilter == QStringLiteral("tx") && record.direction != RecordDirection::Tx) {
        return false;
    }

    const QString searchText = terminalSearchText();
    if(searchText.isEmpty()) {
        return true;
    }

    const QString line = formatRecordLine(record);
    return line.contains(searchText, Qt::CaseInsensitive) ||
           bytesToHex(record.bytes).contains(searchText, Qt::CaseInsensitive) ||
           record.displayText.contains(searchText, Qt::CaseInsensitive);
}

void WorkbenchPage::appendRecord(RecordDirection direction, const QByteArray &data)
{
    if(data.isEmpty()) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    if(direction == RecordDirection::Rx && m_autoFrameBreakCheck && m_autoFrameBreakCheck->isChecked() &&
       m_lastRxTimestamp.isValid()) {
        const int thresholdMs = m_frameBreakIntervalSpin ? m_frameBreakIntervalSpin->value() : 20;
        if(m_lastRxTimestamp.msecsTo(now) >= thresholdMs) {
            SessionRecord separator;
            separator.timestamp = now;
            separator.direction = RecordDirection::FrameBreak;
            m_records.append(separator);
            m_pendingRecordIndexes.append(m_records.size() - 1);
        }
    }

    SessionRecord record;
    record.timestamp = now;
    record.direction = direction;
    record.bytes = data;
    record.displayText = bytesToPrintableText(data);
    m_records.append(record);
    m_pendingRecordIndexes.append(m_records.size() - 1);

    if(direction == RecordDirection::Rx) {
        m_lastRxTimestamp = now;
        m_rxCount += data.size();
        if(m_saveReceiveCheck && m_saveReceiveCheck->isChecked()) {
            if(!m_receiveCaptureFile.isOpen()) {
                updateReceiveCapture(true);
            }
            if(m_receiveCaptureFile.isOpen()) {
                m_receiveCaptureFile.write(data);
                m_receiveCaptureFile.flush();
            }
        }
    } else {
        m_txCount += data.size();
    }

    trimRecords();
    updateCounters();
    flushPendingLines();
}

void WorkbenchPage::trimRecords()
{
    const int maxRecords = maxRecordCount();
    while(m_records.size() > maxRecords) {
        m_records.removeFirst();
        m_terminalStartRecord = qMax(0, m_terminalStartRecord - 1);

        QList<int> adjusted;
        adjusted.reserve(m_pendingRecordIndexes.size());
        for(int index : m_pendingRecordIndexes) {
            if(index > 0) {
                adjusted.append(index - 1);
            }
        }
        m_pendingRecordIndexes = adjusted;
    }
}

void WorkbenchPage::renderTerminal()
{
    if(!m_terminalView) {
        return;
    }

    m_terminalView->document()->setMaximumBlockCount(maxRecordCount());
    m_terminalView->clear();

    QTextCursor cursor = m_terminalView->textCursor();
    cursor.beginEditBlock();
    bool hasOutput = false;
    for(int i = m_terminalStartRecord; i < m_records.size(); ++i) {
        if(!recordMatchesTerminalFilter(m_records.at(i))) {
            continue;
        }
        if(m_records.at(i).direction == RecordDirection::Tx && m_showTxCheck && !m_showTxCheck->isChecked()) {
            continue;
        }
        if(appendRecordToTerminal(cursor, m_records.at(i), hasOutput)) {
            hasOutput = true;
        }
    }
    cursor.endEditBlock();

    m_pendingRecordIndexes.clear();
    if(!m_autoScrollCheck || m_autoScrollCheck->isChecked()) {
        cursor.movePosition(QTextCursor::End);
        m_terminalView->setTextCursor(cursor);
    }
    updateCounters();
}

bool WorkbenchPage::appendRecordToTerminal(QTextCursor &cursor, const SessionRecord &record, bool hasPrevious)
{
    if(record.direction == RecordDirection::FrameBreak) {
        if(hasPrevious) {
            cursor.insertBlock();
            return true;
        }
        return false;
    }

    if(hasPrevious) {
        cursor.insertBlock();
    }

    QTextCharFormat format;
    if(record.direction == RecordDirection::Tx) {
        format.setForeground(selectedTxColor());
    }
    const QString line = formatRecordLine(record);
    const QString searchText = terminalSearchText();
    if(!searchText.isEmpty() && line.contains(searchText, Qt::CaseInsensitive)) {
        format.setBackground(QColor(255, 214, 10, 72));
    }

    const bool showTimestamp = m_timestampCheck && m_timestampCheck->isChecked();
    const QString timestamp = record.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString marker = record.direction == RecordDirection::Tx ? QStringLiteral("»") : QStringLiteral("«");
    int position = 0;
    if(showTimestamp && line.startsWith(timestamp)) {
        QTextCharFormat timestampFormat = format;
        timestampFormat.setForeground(terminalTimestampColor());
        cursor.insertText(timestamp, timestampFormat);
        position = timestamp.size();
    }

    const int markerIndex = line.indexOf(marker, position);
    if(markerIndex >= position) {
        QTextCharFormat markerFormat = format;
        markerFormat.setForeground(terminalDirectionMarkerColor(record.direction == RecordDirection::Tx));
        cursor.insertText(line.mid(position, markerIndex - position), format);
        cursor.insertText(marker, markerFormat);

        const int afterMarker = markerIndex + marker.size();
        int prefixStart = afterMarker;
        while(prefixStart < line.size() && line.at(prefixStart).isSpace()) {
            ++prefixStart;
        }

        const int prefixEnd = espIdfLogPrefixEnd(line, prefixStart);
        if(prefixEnd > prefixStart) {
            QTextCharFormat levelFormat = format;
            levelFormat.setForeground(terminalEspIdfLogLevelColor(line.at(prefixStart)));
            cursor.insertText(line.mid(afterMarker, prefixStart - afterMarker), format);
            cursor.insertText(line.mid(prefixStart, prefixEnd - prefixStart), levelFormat);
            cursor.insertText(line.mid(prefixEnd), format);
        } else {
            cursor.insertText(line.mid(afterMarker), format);
        }
        return true;
    }

    if(position > 0) {
        cursor.insertText(line.mid(position), format);
        return true;
    }

    cursor.insertText(line, format);
    return true;
}

void WorkbenchPage::flushPendingLines()
{
    if(m_pauseCheck->isChecked() || m_pendingRecordIndexes.isEmpty()) {
        return;
    }

    QTextCursor cursor = m_terminalView->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    bool hasOutput = !m_terminalView->document()->isEmpty();
    bool wrote = false;
    for(int index : m_pendingRecordIndexes) {
        if(index >= m_terminalStartRecord && index >= 0 && index < m_records.size()) {
            if(!recordMatchesTerminalFilter(m_records.at(index))) {
                continue;
            }
            if(m_records.at(index).direction == RecordDirection::Tx && m_showTxCheck && !m_showTxCheck->isChecked()) {
                continue;
            }
            if(appendRecordToTerminal(cursor, m_records.at(index), hasOutput)) {
                hasOutput = true;
                wrote = true;
            }
        }
    }
    cursor.endEditBlock();
    m_pendingRecordIndexes.clear();
    if(!wrote) {
        return;
    }

    if(!m_autoScrollCheck || m_autoScrollCheck->isChecked()) {
        cursor.movePosition(QTextCursor::End);
        m_terminalView->setTextCursor(cursor);
    }
    updateCounters();
}

void WorkbenchPage::sendCurrentPayload()
{
    if(!m_serial.isOpen()) {
        if(m_loopCheck->isChecked()) {
            m_loopCheck->setChecked(false);
        }
        showWarning(QStringLiteral("无法发送"), QStringLiteral("请先连接串口"));
        return;
    }

    bool ok = false;
    const QByteArray payload = currentPayload(&ok);
    if(!ok) {
        return;
    }
    if(payload.isEmpty()) {
        showWarning(QStringLiteral("无法发送"), QStringLiteral("发送内容为空"));
        return;
    }

    QString error;
    if(!m_serial.writeData(payload, &error)) {
        showError(QStringLiteral("发送失败"), error);
        return;
    }

    SendHistoryItem historyItem;
    historyItem.mode = m_hexSendCheck->isChecked() ? QStringLiteral("hex") : QStringLiteral("text");
    historyItem.payload = m_sendEdit->toPlainText();
    historyItem.lineEnding = selectedLineEndingKey();
    addSendHistory(historyItem);
    appendRecord(RecordDirection::Tx, payload);
}

void WorkbenchPage::addSendHistory(const SendHistoryItem &item)
{
    if(item.payload.trimmed().isEmpty()) {
        return;
    }

    for(int i = m_sendHistory.size() - 1; i >= 0; --i) {
        const SendHistoryItem existing = m_sendHistory.at(i);
        if(existing.mode == item.mode && existing.payload == item.payload && existing.lineEnding == item.lineEnding) {
            m_sendHistory.removeAt(i);
        }
    }
    m_sendHistory.prepend(item);
    while(m_sendHistory.size() > MaxSendHistoryItems) {
        m_sendHistory.removeLast();
    }
    updateHistoryCombo();
    saveSendHistory();
}

void WorkbenchPage::loadSendHistory()
{
    m_sendHistory.clear();
    QSettings settings;
    const QByteArray json = settings.value(QStringLiteral("send/history")).toString().toUtf8();
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if(!document.isArray()) {
        return;
    }

    const QJsonArray array = document.array();
    for(const QJsonValue &value : array) {
        if(!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        SendHistoryItem item;
        item.mode = object.value(QStringLiteral("mode")).toString(QStringLiteral("text"));
        item.payload = object.value(QStringLiteral("payload")).toString();
        item.lineEnding = object.value(QStringLiteral("lineEnding")).toString(QStringLiteral("none"));
        if(!item.payload.isEmpty()) {
            m_sendHistory.append(item);
        }
        if(m_sendHistory.size() >= MaxSendHistoryItems) {
            break;
        }
    }
}

void WorkbenchPage::saveSendHistory() const
{
    QJsonArray array;
    for(const SendHistoryItem &item : m_sendHistory) {
        QJsonObject object;
        object.insert(QStringLiteral("mode"), item.mode);
        object.insert(QStringLiteral("payload"), item.payload);
        object.insert(QStringLiteral("lineEnding"), item.lineEnding);
        array.append(object);
    }

    QSettings settings;
    settings.setValue(QStringLiteral("send/history"),
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

void WorkbenchPage::loadSendPackets()
{
    m_sendPackets.clear();
    QSettings settings;
    const QByteArray json = settings.value(QStringLiteral("send/packets")).toString().toUtf8();
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if(!document.isArray()) {
        updatePacketTable();
        return;
    }

    const QJsonArray array = document.array();
    for(const QJsonValue &value : array) {
        if(!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        SendPacket packet;
        packet.name = object.value(QStringLiteral("name")).toString().trimmed();
        packet.mode = object.value(QStringLiteral("mode")).toString(QStringLiteral("text"));
        packet.payload = object.value(QStringLiteral("payload")).toString();
        packet.lineEnding = object.value(QStringLiteral("lineEnding")).toString(QStringLiteral("none"));
        packet.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        if(packet.name.isEmpty() || packet.payload.isEmpty()) {
            continue;
        }
        if(packet.mode != QStringLiteral("hex")) {
            packet.mode = QStringLiteral("text");
        }
        if(packet.lineEnding.isEmpty()) {
            packet.lineEnding = QStringLiteral("none");
        }
        m_sendPackets.append(packet);
        if(m_sendPackets.size() >= MaxSendPacketItems) {
            break;
        }
    }
    updatePacketTable();
}

void WorkbenchPage::saveSendPackets() const
{
    QJsonArray array;
    for(const SendPacket &packet : m_sendPackets) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), packet.name);
        object.insert(QStringLiteral("mode"), packet.mode);
        object.insert(QStringLiteral("payload"), packet.payload);
        object.insert(QStringLiteral("lineEnding"), packet.lineEnding);
        object.insert(QStringLiteral("enabled"), packet.enabled);
        array.append(object);
    }

    QSettings settings;
    settings.setValue(QStringLiteral("send/packets"),
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

void WorkbenchPage::exportRecords(ExportFormat format)
{
    if(m_records.isEmpty()) {
        showWarning(QStringLiteral("没有可导出的记录"), QStringLiteral("当前会话还没有收发数据"));
        return;
    }

    QSettings settings;
    const QString initialFolder =
        settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
    FolderPickerDialog dialog(initialFolder, window());
    if(dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString folder = dialog.selectedFolder();
    if(folder.isEmpty()) {
        return;
    }
    settings.setValue(QStringLiteral("export/folder"), folder);

    const QString fileName = QStringLiteral("serial_session_%1.%2")
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")),
                                      exportSuffix(format));
    const QString path = QDir(folder).filePath(fileName);
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showError(QStringLiteral("导出失败"), file.errorString());
        return;
    }

    if(format == ExportFormat::Bin) {
        for(const SessionRecord &record : m_records) {
            if(record.direction == RecordDirection::FrameBreak) {
                continue;
            }
            file.write(record.bytes);
        }
    } else if(format == ExportFormat::Csv) {
        QByteArray output("\xEF\xBB\xBF", 3);
        output.append("timestamp,direction,length,hex,text\n");
        for(const SessionRecord &record : m_records) {
            if(record.direction == RecordDirection::FrameBreak) {
                continue;
            }
            const QString line = QStringLiteral("%1,%2,%3,%4,%5\n")
                                     .arg(csvEscape(record.timestamp.toString(Qt::ISODateWithMs)),
                                          csvEscape(directionText(record.direction)))
                                     .arg(record.bytes.size())
                                     .arg(csvEscape(bytesToHex(record.bytes)),
                                          csvEscape(record.displayText));
            output.append(line.toUtf8());
        }
        file.write(output);
    } else {
        QStringList lines;
        lines.reserve(m_records.size());
        for(const SessionRecord &record : m_records) {
            lines.append(formatRecordLine(record));
        }
        file.write(lines.join(QLatin1Char('\n')).toUtf8());
    }

    file.close();
    showSuccess(QStringLiteral("导出完成"), path);
}

void WorkbenchPage::updateReceiveCapture(bool enabled)
{
    QSettings settings;
    settings.setValue(QStringLiteral("receive/saveToFile"), enabled);

    if(!enabled) {
        closeReceiveCapture();
        return;
    }
    if(m_receiveCaptureFile.isOpen()) {
        return;
    }

    const QString folderPath =
        settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
    QDir folder(folderPath);
    if(!folder.exists() && !folder.mkpath(QStringLiteral("."))) {
        const QSignalBlocker blocker(m_saveReceiveCheck);
        m_saveReceiveCheck->setChecked(false);
        showError(QStringLiteral("接收保存失败"), QStringLiteral("无法创建目录：%1").arg(folderPath));
        return;
    }

    const QString fileName = QStringLiteral("serial_rx_%1.bin")
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = folder.filePath(fileName);
    m_receiveCaptureFile.setFileName(path);
    if(!m_receiveCaptureFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
        const QSignalBlocker blocker(m_saveReceiveCheck);
        m_saveReceiveCheck->setChecked(false);
        showError(QStringLiteral("接收保存失败"), m_receiveCaptureFile.errorString());
        return;
    }

    m_receiveCaptureLabel->setText(QStringLiteral("保存至：%1").arg(path));
}

void WorkbenchPage::closeReceiveCapture()
{
    if(m_receiveCaptureFile.isOpen()) {
        m_receiveCaptureFile.close();
    }
    if(m_receiveCaptureLabel) {
        m_receiveCaptureLabel->setText(m_saveReceiveCheck && m_saveReceiveCheck->isChecked()
                                           ? QStringLiteral("连接后继续保存接收")
                                           : QStringLiteral("接收保存未启用"));
    }
}

void WorkbenchPage::browseSendFile()
{
    QSettings settings;
    const QString initialPath = m_filePathEdit && !m_filePathEdit->text().isEmpty()
                                    ? QFileInfo(m_filePathEdit->text()).absolutePath()
                                    : settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
    const QString path = QFileDialog::getOpenFileName(window(), QStringLiteral("选择发送文件"), initialPath);
    if(path.isEmpty()) {
        return;
    }

    const QFileInfo info(path);
    settings.setValue(QStringLiteral("fileSend/path"), path);
    settings.setValue(QStringLiteral("export/folder"), info.absolutePath());
    m_filePathEdit->setText(path);
    m_fileStatusLabel->setText(QStringLiteral("%1 · %2").arg(info.fileName(), formatBytes(info.size())));
    updateFileProgress();
}

void WorkbenchPage::startFileSend()
{
    if(!m_serial.isOpen()) {
        showWarning(QStringLiteral("无法发送文件"), QStringLiteral("请先连接串口"));
        return;
    }
    if(m_fileSendFile.isOpen()) {
        return;
    }

    const QString path = m_filePathEdit->text().trimmed();
    if(path.isEmpty()) {
        showWarning(QStringLiteral("未选择文件"), QStringLiteral("请先选择待发送文件"));
        return;
    }

    QFileInfo info(path);
    if(!info.exists() || !info.isFile()) {
        showError(QStringLiteral("文件不可用"), QStringLiteral("无法读取：%1").arg(path));
        return;
    }
    if(info.size() <= 0) {
        showWarning(QStringLiteral("文件为空"), QStringLiteral("请选择包含数据的文件"));
        return;
    }

    m_fileSendFile.setFileName(path);
    if(!m_fileSendFile.open(QIODevice::ReadOnly)) {
        showError(QStringLiteral("打开文件失败"), m_fileSendFile.errorString());
        return;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("fileSend/path"), path);
    settings.setValue(QStringLiteral("fileSend/chunkSize"), m_fileChunkSizeSpin->value());
    settings.setValue(QStringLiteral("fileSend/intervalMs"), m_fileIntervalSpin->value());

    m_fileSendTotal = m_fileSendFile.size();
    m_fileSendSent = 0;
    updateFileSendUi(true);
    updateFileProgress();
    sendNextFileChunk();
}

void WorkbenchPage::cancelFileSend()
{
    if(!m_fileSendFile.isOpen()) {
        return;
    }
    m_fileSendTimer.stop();
    m_fileSendFile.close();
    updateFileSendUi(false);
    updateFileProgress();
    showInfo(QStringLiteral("文件发送已取消"), QStringLiteral("已停止发送当前文件"));
}

void WorkbenchPage::sendNextFileChunk()
{
    if(!m_fileSendFile.isOpen()) {
        return;
    }
    if(!m_serial.isOpen()) {
        cancelFileSend();
        showWarning(QStringLiteral("文件发送中止"), QStringLiteral("串口已断开"));
        return;
    }

    if(m_fileSendFile.atEnd()) {
        const QString name = QFileInfo(m_fileSendFile.fileName()).fileName();
        m_fileSendFile.close();
        updateFileSendUi(false);
        updateFileProgress();
        showSuccess(QStringLiteral("文件发送完成"), name);
        return;
    }

    const int chunkSize = qBound(1, m_fileChunkSizeSpin->value(), 65536);
    const QByteArray data = m_fileSendFile.read(chunkSize);
    if(data.isEmpty()) {
        const QString error = m_fileSendFile.errorString();
        m_fileSendFile.close();
        updateFileSendUi(false);
        updateFileProgress();
        showError(QStringLiteral("读取文件失败"), error);
        return;
    }

    QString error;
    if(!m_serial.writeData(data, &error)) {
        m_fileSendFile.close();
        updateFileSendUi(false);
        updateFileProgress();
        showError(QStringLiteral("文件发送失败"), error);
        return;
    }

    m_fileSendSent += data.size();
    appendRecord(RecordDirection::Tx, data);
    updateFileProgress();

    if(m_fileSendFile.atEnd()) {
        sendNextFileChunk();
        return;
    }
    m_fileSendTimer.start(qBound(0, m_fileIntervalSpin->value(), 60000));
}

void WorkbenchPage::updateFileSendUi(bool sending)
{
    if(m_fileBrowseButton) {
        m_fileBrowseButton->setEnabled(!sending);
    }
    if(m_filePathEdit) {
        m_filePathEdit->setEnabled(!sending);
    }
    if(m_fileChunkSizeSpin) {
        m_fileChunkSizeSpin->setEnabled(!sending);
    }
    if(m_fileIntervalSpin) {
        m_fileIntervalSpin->setEnabled(!sending);
    }
    if(m_fileSendButton) {
        m_fileSendButton->setEnabled(!sending && m_serial.isOpen());
    }
    if(m_fileCancelButton) {
        m_fileCancelButton->setEnabled(sending);
    }
}

void WorkbenchPage::updateFileProgress()
{
    const int percent = m_fileSendTotal > 0 ? static_cast<int>((m_fileSendSent * 100) / m_fileSendTotal) : 0;
    if(m_fileProgressBar) {
        m_fileProgressBar->setValue(qBound(0, percent, 100));
    }
    if(!m_fileStatusLabel) {
        return;
    }

    if(m_fileSendFile.isOpen()) {
        m_fileStatusLabel->setText(QStringLiteral("发送中：%1 / %2")
                                       .arg(formatBytes(m_fileSendSent), formatBytes(m_fileSendTotal)));
        return;
    }

    const QString path = m_filePathEdit ? m_filePathEdit->text().trimmed() : QString();
    if(path.isEmpty()) {
        m_fileStatusLabel->setText(QStringLiteral("未选择文件"));
        return;
    }
    const QFileInfo info(path);
    m_fileStatusLabel->setText(info.exists() ? QStringLiteral("%1 · %2").arg(info.fileName(), formatBytes(info.size()))
                                             : QStringLiteral("文件不存在：%1").arg(path));
}

void WorkbenchPage::scheduleReconnect()
{
    if(m_lastConfig.portName.isEmpty() || m_manualDisconnect || !m_autoReconnectCheck->isChecked()) {
        return;
    }
    if(!m_reconnectTimer.isActive()) {
        m_reconnectTimer.start();
    }
    showWarning(QStringLiteral("连接中断"), QStringLiteral("正在尝试自动重连"));
}

void WorkbenchPage::attemptReconnect()
{
    if(m_serial.isOpen()) {
        m_reconnectTimer.stop();
        return;
    }
    if(m_lastConfig.portName.isEmpty() || !m_autoReconnectCheck->isChecked()) {
        m_reconnectTimer.stop();
        return;
    }

    refreshPorts();
    m_serial.openPort(m_lastConfig);
}

void WorkbenchPage::onConnectClicked()
{
    if(m_serial.isOpen()) {
        m_manualDisconnect = true;
        m_reconnectTimer.stop();
        m_serial.closePort();
        return;
    }

    const SerialPortConfig config = currentSerialConfig();
    if(config.portName.isEmpty()) {
        showWarning(QStringLiteral("未选择端口"), QStringLiteral("请先刷新并选择可用串口"));
        return;
    }

    bool baudOk = false;
    const int baud = m_baudCombo->currentText().trimmed().toInt(&baudOk);
    if(!baudOk || baud <= 0) {
        showWarning(QStringLiteral("波特率无效"), QStringLiteral("请输入大于 0 的波特率"));
        return;
    }

    m_manualDisconnect = false;
    m_lastConfig = config;
    m_serial.openPort(config);
}

void WorkbenchPage::onLoopChanged(bool checked)
{
    m_loopIntervalSpin->setEnabled(checked);
    if(!checked) {
        m_loopTimer.stop();
        return;
    }
    if(!m_serial.isOpen()) {
        m_loopCheck->setChecked(false);
        showWarning(QStringLiteral("无法循环发送"), QStringLiteral("请先连接串口"));
        return;
    }
    m_loopTimer.start(m_loopIntervalSpin->value());
}

void WorkbenchPage::setControlsEnabledForConnection(bool connected)
{
    m_sendButton->setEnabled(connected);
    m_sendEdit->setEnabled(connected);
    m_loopCheck->setEnabled(connected);
    m_loopIntervalSpin->setEnabled(connected && m_loopCheck->isChecked());
    if(m_packetSendButton) {
        m_packetSendButton->setEnabled(connected && !m_sendPackets.isEmpty());
    }
    if(m_fileSendButton) {
        m_fileSendButton->setEnabled(connected && !m_fileSendFile.isOpen());
    }
    if(m_fileCancelButton) {
        m_fileCancelButton->setEnabled(m_fileSendFile.isOpen());
    }
    m_rtsCheck->setEnabled(connected);
    m_dtrCheck->setEnabled(connected);

    m_portCombo->setEnabled(!connected);
    m_baudCombo->setEnabled(!connected);
    m_dataBitsCombo->setEnabled(!connected);
    m_parityCombo->setEnabled(!connected);
    m_stopBitsCombo->setEnabled(!connected);
    m_flowControlCombo->setEnabled(!connected);
    m_refreshButton->setEnabled(!connected);

    if(!connected) {
        m_loopCheck->setChecked(false);
        m_loopTimer.stop();
    }
}

int WorkbenchPage::maxRecordCount() const
{
    QSettings settings;
    return qBound(1000,
                  settings.value(QStringLiteral("terminal/maxRecords"), DefaultMaxTerminalRecords).toInt(),
                  50000);
}
