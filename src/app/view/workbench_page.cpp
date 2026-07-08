#include "app/view/workbench_page.h"

#include "app/core/font_preferences.h"
#include "app/core/hex_utils.h"

#include <FluentQtWidgets/Dialogs/FolderListDialog.h>
#include <FluentQtWidgets/Settings/SettingCard.h>
#include <FluentQtWidgets/StyleSheet.h>

#include <QtCore/QDir>
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
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
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
constexpr int SidePanelWidth = 324;
constexpr int TerminalPanelMinWidth = 560;
constexpr int ReconnectIntervalMs = 2000;
constexpr int CompactControlHeight = 32;

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

    refreshPorts();
    loadSendHistory();
    restoreSettings();
    updateConnectionUi(false);
    updateCounters();
    updateHistoryCombo();
}

WorkbenchPage::~WorkbenchPage()
{
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

    root->addWidget(createCheckRow({QStringLiteral("RTS"), QStringLiteral("DTR")},
                                   {&m_rtsCheck, &m_dtrCheck},
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
        updateConnectionUi(true);
        if(m_saveReceiveCheck->isChecked()) {
            updateReceiveCapture(true);
        }
        showSuccess(QStringLiteral("连接成功"), QStringLiteral("%1 已打开").arg(portName));
    });
    connect(&m_serial, &SerialController::closed, this, [this]() {
        closeReceiveCapture();
        m_lastRxTimestamp = QDateTime();
        updateConnectionUi(false);
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
    settings.setValue(QStringLiteral("serial/autoReconnect"), m_autoReconnectCheck->isChecked());
    saveSendHistory();
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
    m_rxCounterLabel->setText(QStringLiteral("%1 B").arg(m_rxCount));
    m_txCounterLabel->setText(QStringLiteral("%1 B").arg(m_txCount));
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
    if(ok) {
        *ok = false;
    }

    QByteArray data;
    const QString payloadText = m_sendEdit->toPlainText();
    const QString mode = m_hexSendCheck->isChecked() ? QStringLiteral("hex") : QStringLiteral("text");
    if(mode == QStringLiteral("hex")) {
        const HexParseResult result = parseHexPayload(payloadText);
        if(!result.ok) {
            m_sendEdit->setFocus();
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

    data.append(selectedLineEnding());
    if(ok) {
        *ok = true;
    }
    return data;
}

QByteArray WorkbenchPage::selectedLineEnding() const
{
    const QString eol = selectedLineEndingKey();
    if(eol == QStringLiteral("cr")) {
        return QByteArray("\r", 1);
    }
    if(eol == QStringLiteral("lf")) {
        return QByteArray("\n", 1);
    }
    if(eol == QStringLiteral("crlf")) {
        return QByteArray("\r\n", 2);
    }
    return {};
}

QString WorkbenchPage::selectedLineEndingKey() const
{
    return m_lineEndingCombo->currentData().toString();
}

QString WorkbenchPage::currentDisplayMode() const
{
    return m_displayModeSegment ? m_displayModeSegment->currentItem() : QStringLiteral("text");
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
    cursor.insertText(formatRecordLine(record), format);
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
