#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

QWidget *WorkbenchPage::createConnectionSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Connect, QStringLiteral("连接"), QString(), this);
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
                                   {&m_rtsCheck, &m_dtrCheck, &m_autoOpenCheck}, section));

    m_connectButton = new PrimaryPushButton(icon(FluentIcon::Connect), QStringLiteral("连接"), section);
    root->addWidget(m_connectButton);

    connect(m_refreshButton, &ToolButton::clicked, this, &WorkbenchPage::refreshPorts);
    connect(m_connectButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::onConnectClicked);
    connect(m_rtsCheck, &CheckBox::toggled, this, [this](bool checked) {
        if (m_serial.isOpen()) {
            m_serial.setRequestToSend(checked);
        }
    });
    connect(m_dtrCheck, &CheckBox::toggled, this, [this](bool checked) {
        if (m_serial.isOpen()) {
            m_serial.setDataTerminalReady(checked);
        }
    });
    connect(m_autoOpenCheck, &CheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("serial/autoOpen"), checked);
    });

    makeCollapsibleCard(section, QStringLiteral("connection"));
    return section;
}

QWidget *WorkbenchPage::createReceiveSettingsSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Download, QStringLiteral("接收设置"), QString(), this);
    auto *root = cardBody(section);

    m_displayModeSegment = new SegmentedWidget(section);
    m_displayModeSegment->addItem(QStringLiteral("text"), QStringLiteral("文本"));
    m_displayModeSegment->addItem(QStringLiteral("hex"), QStringLiteral("HEX"));
    m_displayModeSegment->addItem(QStringLiteral("mixed"), QStringLiteral("混合"));
    m_displayModeSegment->setCurrentItem(QStringLiteral("text"));
    root->addWidget(m_displayModeSegment);

    m_receiveEncodingCombo = new ComboBox(section);
    addEncodingOptions(m_receiveEncodingCombo);
    makeCompactControl(m_receiveEncodingCombo);
    addFormRow(root, QStringLiteral("编码"), m_receiveEncodingCombo);

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
    for (CheckBox *check : {m_saveReceiveCheck, m_autoScrollCheck, m_timestampCheck, m_pauseCheck}) {
        check->setMinimumWidth(0);
        check->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    receiveOptionsGrid->addWidget(m_saveReceiveCheck, 0, 0);
    receiveOptionsGrid->addWidget(m_autoScrollCheck, 0, 1);
    receiveOptionsGrid->addWidget(m_timestampCheck, 1, 0);
    receiveOptionsGrid->addWidget(m_pauseCheck, 1, 1);
    root->addWidget(receiveOptions);

    m_frameModeCombo = new ComboBox(section);
    addFrameModeOptions(m_frameModeCombo);
    makeCompactControl(m_frameModeCombo);
    m_autoFrameBreakCheck = new CheckBox(QStringLiteral("启用"), section);
    m_autoFrameBreakCheck->setMinimumWidth(0);
    m_autoFrameBreakCheck->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    addFormRow(root, QStringLiteral("断帧"), m_frameModeCombo, m_autoFrameBreakCheck);

    m_framePatternEdit = new LineEdit(section);
    m_framePatternEdit->setPlaceholderText(QStringLiteral("HEX 边界，如 AA 55 或 0D 0A"));
    makeCompactControl(m_framePatternEdit);
    addFormRow(root, QStringLiteral("边界"), m_framePatternEdit);

    m_frameFixedLengthSpin = new SpinBox(section);
    m_frameFixedLengthSpin->setRange(1, 65536);
    m_frameFixedLengthSpin->setValue(8);
    m_frameFixedLengthSpin->setSuffix(QStringLiteral(" B"));
    addFormRow(root, QStringLiteral("长度"), m_frameFixedLengthSpin);

    m_frameBreakIntervalSpin = new SpinBox(section);
    m_frameBreakIntervalSpin->setRange(1, 60000);
    m_frameBreakIntervalSpin->setValue(20);
    m_frameBreakIntervalSpin->setSymbolVisible(false);
    m_frameBreakIntervalSpin->setFixedHeight(CompactControlHeight);
    auto *frameBreakUnitLabel = new CaptionLabel(QStringLiteral("ms"), section);
    frameBreakUnitLabel->setFixedHeight(CompactControlHeight);
    frameBreakUnitLabel->setFixedWidth(22);
    frameBreakUnitLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    addFormRow(root, QStringLiteral("超时"), m_frameBreakIntervalSpin, frameBreakUnitLabel);

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
    connect(m_receiveEncodingCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        flushRxFrameBuffer();
        QSettings settings;
        settings.setValue(QStringLiteral("receive/encoding"), receiveEncodingKey());
    });
    connect(m_frameModeCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        flushRxFrameBuffer();
        QSettings settings;
        settings.setValue(QStringLiteral("receive/frameMode"), frameBreakModeKey());
        updateFrameControlState();
    });
    connect(m_framePatternEdit, &LineEdit::textChanged, this, [this](const QString &text) {
        flushRxFrameBuffer();
        QSettings settings;
        settings.setValue(QStringLiteral("receive/framePattern"), text);
    });
    connect(m_frameFixedLengthSpin, &SpinBox::valueChanged, this, [this](int value) {
        flushRxFrameBuffer();
        QSettings settings;
        settings.setValue(QStringLiteral("receive/frameFixedLength"), value);
    });
    connect(m_saveReceiveCheck, &CheckBox::toggled, this, &WorkbenchPage::updateReceiveCapture);
    connect(m_autoScrollCheck, &CheckBox::toggled, this, [this](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("receive/autoScroll"), checked);
        if (checked) {
            renderTerminal();
        }
    });
    connect(m_timestampCheck, &CheckBox::toggled, this, [this](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("receive/timestamp"), checked);
        renderTerminal();
    });
    connect(m_pauseCheck, &CheckBox::toggled, this, [this](bool checked) {
        if (!checked) {
            flushPendingLines();
        }
    });
    connect(m_autoFrameBreakCheck, &CheckBox::toggled, this, [this](bool checked) {
        flushRxFrameBuffer();
        QSettings settings;
        settings.setValue(QStringLiteral("receive/autoFrameBreak"), checked);
        if (!checked) {
            m_lastRxTimestamp = QDateTime();
        }
        updateFrameControlState();
    });
    connect(m_frameBreakIntervalSpin, &SpinBox::valueChanged, this, [](int value) {
        QSettings settings;
        settings.setValue(QStringLiteral("receive/frameBreakMs"), value);
    });
    connect(m_clearButton, &PushButton::clicked, this, [this]() {
        m_rxFrameBuffer.clear();
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

    makeCollapsibleCard(section, QStringLiteral("receive"));
    return section;
}

QWidget *WorkbenchPage::createSendSettingsSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Send, QStringLiteral("发送设置"), QString(), this);
    auto *root = cardBody(section);

    m_lineEndingCombo = new ComboBox(section);
    makeCompactControl(m_lineEndingCombo);
    m_lineEndingCombo->addItem(QStringLiteral("None"), QIcon(), QStringLiteral("none"));
    m_lineEndingCombo->addItem(QStringLiteral("CR"), QIcon(), QStringLiteral("cr"));
    m_lineEndingCombo->addItem(QStringLiteral("LF"), QIcon(), QStringLiteral("lf"));
    m_lineEndingCombo->addItem(QStringLiteral("CRLF"), QIcon(), QStringLiteral("crlf"));
    m_lineEndingCombo->setCurrentIndex(0);
    addFormRow(root, QStringLiteral("换行"), m_lineEndingCombo);

    m_sendEncodingCombo = new ComboBox(section);
    addEncodingOptions(m_sendEncodingCombo);
    makeCompactControl(m_sendEncodingCombo);
    addFormRow(root, QStringLiteral("编码"), m_sendEncodingCombo);

    m_checksumAlgorithmCombo = new ComboBox(section);
    addChecksumAlgorithmOptions(m_checksumAlgorithmCombo);
    makeCompactControl(m_checksumAlgorithmCombo);
    m_checksumCalcButton = new PushButton(icon(FluentIcon::Asterisk), QStringLiteral("计算"), section);
    m_checksumCalcButton->setFixedHeight(CompactControlHeight);
    addFormRow(root, QStringLiteral("校验"), m_checksumAlgorithmCombo, m_checksumCalcButton);

    m_checksumByteOrderCombo = new ComboBox(section);
    addChecksumByteOrderOptions(m_checksumByteOrderCombo);
    makeCompactControl(m_checksumByteOrderCombo);
    m_checksumAppendCheck = new CheckBox(QStringLiteral("自动追加"), section);
    m_checksumAppendCheck->setMinimumWidth(0);
    m_checksumAppendCheck->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    addFormRow(root, QStringLiteral("字节序"), m_checksumByteOrderCombo, m_checksumAppendCheck);

    m_checksumResultLabel = new CaptionLabel(QStringLiteral("校验未计算"), section);
    m_checksumResultLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_checksumResultLabel->setWordWrap(true);
    root->addWidget(m_checksumResultLabel);

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
    for (CheckBox *check : {m_hexSendCheck, m_showTxCheck, m_loopCheck, m_autoReconnectCheck}) {
        check->setMinimumWidth(0);
        check->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    m_txColorButton = new ColorPickerButton(defaultTxColor(), QStringLiteral("TX 颜色"), sendOptions);
    m_txColorButton->setFixedSize(32, CompactControlHeight);
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
    connect(m_sendEncodingCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        QSettings settings;
        settings.setValue(QStringLiteral("send/encoding"), sendEncodingKey());
    });
    connect(m_checksumAlgorithmCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        QSettings settings;
        settings.setValue(QStringLiteral("checksum/algorithm"), checksumAlgorithmKey());
    });
    connect(m_checksumByteOrderCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        QSettings settings;
        settings.setValue(QStringLiteral("checksum/byteOrder"), AppChecksum::byteOrderKey(checksumByteOrder()));
    });
    connect(m_checksumAppendCheck, &CheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("checksum/autoAppend"), checked);
    });
    connect(m_checksumCalcButton, &PushButton::clicked, this, &WorkbenchPage::calculateChecksumForCurrentPayload);
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
        if (m_loopTimer.isActive()) {
            m_loopTimer.start(value);
        }
    });

    makeCollapsibleCard(section, QStringLiteral("send"));
    return section;
}

QWidget *WorkbenchPage::createPacketSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Library, QStringLiteral("常用包"), QString(), this);
    auto *root = cardBody(section);

    m_packetNameEdit = new LineEdit(section);
    m_packetNameEdit->setPlaceholderText(QStringLiteral("包名称"));
    makeCompactControl(m_packetNameEdit);
    addFormRow(root, QStringLiteral("名称"), m_packetNameEdit);

    m_packetGroupEdit = new LineEdit(section);
    m_packetGroupEdit->setPlaceholderText(QStringLiteral("分组"));
    makeCompactControl(m_packetGroupEdit);
    m_packetEnabledCheck = new CheckBox(QStringLiteral("启用"), section);
    m_packetEnabledCheck->setChecked(true);
    m_packetEnabledCheck->setFixedHeight(CompactControlHeight);
    addFormRow(root, QStringLiteral("分组"), m_packetGroupEdit, m_packetEnabledCheck);

    m_packetNoteEdit = new LineEdit(section);
    m_packetNoteEdit->setPlaceholderText(QStringLiteral("备注"));
    makeCompactControl(m_packetNoteEdit);
    addFormRow(root, QStringLiteral("备注"), m_packetNoteEdit);

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
    m_packetList->setSelectionMode(QAbstractItemView::ExtendedSelection);
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
    for (ToolButton *button : {m_packetUpButton, m_packetDownButton}) {
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

    auto *ioRow = new QHBoxLayout;
    ioRow->setSpacing(8);
    m_packetImportButton = new PushButton(icon(FluentIcon::Download), QStringLiteral("导入 JSON"), section);
    m_packetExportButton = new PushButton(icon(FluentIcon::SaveAs), QStringLiteral("导出 JSON"), section);
    setButtonRowControlPolicy(m_packetImportButton);
    setButtonRowControlPolicy(m_packetExportButton);
    ioRow->addWidget(m_packetImportButton);
    ioRow->addWidget(m_packetExportButton);
    root->addLayout(ioRow);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    m_packetSendButton = new PrimaryPushButton(icon(FluentIcon::Send), QStringLiteral("发送"), section);
    m_packetBatchSendButton = new PushButton(icon(FluentIcon::SendFill), QStringLiteral("批量发送"), section);
    m_packetDeleteButton = new PushButton(icon(FluentIcon::Delete), QStringLiteral("删除"), section);
    setButtonRowControlPolicy(m_packetSendButton);
    setButtonRowControlPolicy(m_packetBatchSendButton);
    setButtonRowControlPolicy(m_packetDeleteButton);
    actionRow->addWidget(m_packetSendButton);
    actionRow->addWidget(m_packetBatchSendButton);
    actionRow->addWidget(m_packetDeleteButton);
    root->addLayout(actionRow);

    connect(m_packetList, &ListWidget::currentRowChanged, this, [this](int row) { applyPacket(row); });
    connect(m_packetList, &ListWidget::itemSelectionChanged, this, &WorkbenchPage::updatePacketActionState);
    connect(m_packetSaveButton, &PushButton::clicked, this, &WorkbenchPage::saveCurrentPacket);
    connect(m_packetLoadButton, &PushButton::clicked, this, [this]() { applyPacket(m_packetList->currentRow()); });
    connect(m_packetDeleteButton, &PushButton::clicked, this, &WorkbenchPage::removeSelectedPacket);
    connect(m_packetSendButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::sendSelectedPacket);
    connect(m_packetBatchSendButton, &PushButton::clicked, this, &WorkbenchPage::sendSelectedPackets);
    connect(m_packetImportButton, &PushButton::clicked, this, &WorkbenchPage::importSendPackets);
    connect(m_packetExportButton, &PushButton::clicked, this, &WorkbenchPage::exportSendPackets);
    connect(m_packetUpButton, &ToolButton::clicked, this, [this]() { moveSelectedPacket(-1); });
    connect(m_packetDownButton, &ToolButton::clicked, this, [this]() { moveSelectedPacket(1); });

    updatePacketTable();
    makeCollapsibleCard(section, QStringLiteral("packets"));
    return section;
}

QWidget *WorkbenchPage::createMacroSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Play, QStringLiteral("宏命令"), QString(), this);
    auto *root = cardBody(section);

    m_macroNameEdit = new LineEdit(section);
    m_macroNameEdit->setPlaceholderText(QStringLiteral("步骤名称"));
    makeCompactControl(m_macroNameEdit);
    addFormRow(root, QStringLiteral("步骤"), m_macroNameEdit);

    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(8);
    m_macroModeCombo = new ComboBox(section);
    m_macroModeCombo->addItem(QStringLiteral("文本"), QIcon(), QStringLiteral("text"));
    m_macroModeCombo->addItem(QStringLiteral("HEX"), QIcon(), QStringLiteral("hex"));
    makeCompactControl(m_macroModeCombo);
    m_macroLineEndingCombo = new ComboBox(section);
    m_macroLineEndingCombo->addItem(QStringLiteral("None"), QIcon(), QStringLiteral("none"));
    m_macroLineEndingCombo->addItem(QStringLiteral("CR"), QIcon(), QStringLiteral("cr"));
    m_macroLineEndingCombo->addItem(QStringLiteral("LF"), QIcon(), QStringLiteral("lf"));
    m_macroLineEndingCombo->addItem(QStringLiteral("CRLF"), QIcon(), QStringLiteral("crlf"));
    makeCompactControl(m_macroLineEndingCombo);
    modeRow->addWidget(m_macroModeCombo);
    modeRow->addWidget(m_macroLineEndingCombo);
    root->addLayout(modeRow);

    m_macroPayloadEdit = new PlainTextEdit(section);
    m_macroPayloadEdit->setPlaceholderText(QStringLiteral("发送内容"));
    m_macroPayloadEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_macroPayloadEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_macroPayloadEdit->setFixedHeight(64);
    m_macroPayloadEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_macroPayloadEdit);

    m_macroExpectedEdit = new LineEdit(section);
    m_macroExpectedEdit->setPlaceholderText(QStringLiteral("响应包含，留空不等待"));
    makeCompactControl(m_macroExpectedEdit);
    m_macroResponseModeCombo = new ComboBox(section);
    m_macroResponseModeCombo->addItem(QStringLiteral("文"), QIcon(), QStringLiteral("text"));
    m_macroResponseModeCombo->addItem(QStringLiteral("HEX"), QIcon(), QStringLiteral("hex"));
    m_macroResponseModeCombo->setFixedHeight(CompactControlHeight);
    m_macroResponseModeCombo->setFixedWidth(78);
    addFormRow(root, QStringLiteral("响应"), m_macroExpectedEdit, m_macroResponseModeCombo);

    m_macroTimeoutSpin = new SpinBox(section);
    m_macroTimeoutSpin->setRange(1, 600000);
    m_macroTimeoutSpin->setValue(1000);
    m_macroTimeoutSpin->setSuffix(QStringLiteral(" ms"));
    m_macroTimeoutSpin->setFixedHeight(CompactControlHeight);
    addFormRow(root, QStringLiteral("超时"), m_macroTimeoutSpin);

    m_macroDelaySpin = new SpinBox(section);
    m_macroDelaySpin->setRange(0, 600000);
    m_macroDelaySpin->setValue(0);
    m_macroDelaySpin->setSuffix(QStringLiteral(" ms"));
    m_macroDelaySpin->setFixedHeight(CompactControlHeight);
    addFormRow(root, QStringLiteral("延时"), m_macroDelaySpin);

    m_macroList = new ListWidget(section);
    m_macroList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_macroList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_macroList->setMinimumHeight(132);
    m_macroList->setMaximumHeight(220);
    m_macroList->setBorderRadius(8);
    root->addWidget(m_macroList);

    auto *stepRow = new QHBoxLayout;
    stepRow->setSpacing(8);
    m_macroUpButton = new TransparentToolButton(icon(FluentIcon::Up), section);
    m_macroUpButton->setToolTip(QStringLiteral("上移"));
    m_macroDownButton = new TransparentToolButton(icon(FluentIcon::Download), section);
    m_macroDownButton->setToolTip(QStringLiteral("下移"));
    for (ToolButton *button : {m_macroUpButton, m_macroDownButton}) {
        button->setFixedSize(CompactControlHeight, CompactControlHeight);
        button->setIconSize(QSize(16, 16));
    }
    m_macroSaveButton = new PushButton(icon(FluentIcon::Save), QStringLiteral("保存"), section);
    m_macroLoadButton = new PushButton(icon(FluentIcon::Edit), QStringLiteral("填入"), section);
    m_macroDeleteButton = new PushButton(icon(FluentIcon::Delete), QStringLiteral("删除"), section);
    setButtonRowControlPolicy(m_macroSaveButton);
    stepRow->addWidget(m_macroUpButton);
    stepRow->addWidget(m_macroDownButton);
    stepRow->addWidget(m_macroSaveButton);
    root->addLayout(stepRow);

    auto *editRow = new QHBoxLayout;
    editRow->setSpacing(8);
    setButtonRowControlPolicy(m_macroLoadButton);
    setButtonRowControlPolicy(m_macroDeleteButton);
    editRow->addWidget(m_macroLoadButton);
    editRow->addWidget(m_macroDeleteButton);
    root->addLayout(editRow);

    m_macroLoopCountSpin = new SpinBox(section);
    m_macroLoopCountSpin->setRange(1, 100000);
    m_macroLoopCountSpin->setValue(1);
    m_macroLoopCountSpin->setFixedHeight(CompactControlHeight);
    m_macroAbortOnFailureCheck = new CheckBox(QStringLiteral("失败中止"), section);
    m_macroAbortOnFailureCheck->setChecked(true);
    m_macroAbortOnFailureCheck->setFixedHeight(CompactControlHeight);
    addFormRow(root, QStringLiteral("循环"), m_macroLoopCountSpin, m_macroAbortOnFailureCheck);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    m_macroRunButton = new PrimaryPushButton(icon(FluentIcon::Play), QStringLiteral("运行"), section);
    m_macroStopButton = new PushButton(icon(FluentIcon::Cancel), QStringLiteral("停止"), section);
    m_macroExportButton = new PushButton(icon(FluentIcon::SaveAs), QStringLiteral("导出结果"), section);
    setButtonRowControlPolicy(m_macroRunButton);
    setButtonRowControlPolicy(m_macroStopButton);
    setButtonRowControlPolicy(m_macroExportButton);
    actionRow->addWidget(m_macroRunButton);
    actionRow->addWidget(m_macroStopButton);
    actionRow->addWidget(m_macroExportButton);
    root->addLayout(actionRow);

    m_macroStatusLabel = new CaptionLabel(QStringLiteral("未运行"), section);
    m_macroStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_macroStatusLabel->setWordWrap(true);
    root->addWidget(m_macroStatusLabel);

    connect(m_macroList, &ListWidget::currentRowChanged, this, [this](int row) {
        applyMacroStep(row);
        updateMacroActionState();
    });
    connect(m_macroSaveButton, &PushButton::clicked, this, &WorkbenchPage::saveCurrentMacroStep);
    connect(m_macroLoadButton, &PushButton::clicked, this, [this]() { applyMacroStep(m_macroList->currentRow()); });
    connect(m_macroDeleteButton, &PushButton::clicked, this, &WorkbenchPage::removeSelectedMacroStep);
    connect(m_macroUpButton, &ToolButton::clicked, this, [this]() { moveSelectedMacroStep(-1); });
    connect(m_macroDownButton, &ToolButton::clicked, this, [this]() { moveSelectedMacroStep(1); });
    connect(m_macroRunButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::startMacroSequence);
    connect(m_macroStopButton, &PushButton::clicked, this, [this]() { stopMacroSequence(QStringLiteral("用户停止")); });
    connect(m_macroExportButton, &PushButton::clicked, this, &WorkbenchPage::exportMacroResults);

    updateMacroTable();
    makeCollapsibleCard(section, QStringLiteral("macros"));
    return section;
}

QWidget *WorkbenchPage::createAutoReplySection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Feedback, QStringLiteral("自动应答"), QString(), this);
    auto *root = cardBody(section);

    m_autoReplyNameEdit = new LineEdit(section);
    m_autoReplyNameEdit->setPlaceholderText(QStringLiteral("规则名称"));
    makeCompactControl(m_autoReplyNameEdit);
    m_autoReplyEnabledCheck = new CheckBox(QStringLiteral("启用"), section);
    m_autoReplyEnabledCheck->setChecked(true);
    m_autoReplyEnabledCheck->setFixedHeight(CompactControlHeight);
    addFormRow(root, QStringLiteral("名称"), m_autoReplyNameEdit, m_autoReplyEnabledCheck);

    m_autoReplyPatternEdit = new LineEdit(section);
    m_autoReplyPatternEdit->setPlaceholderText(QStringLiteral("匹配内容"));
    makeCompactControl(m_autoReplyPatternEdit);
    m_autoReplyMatchModeCombo = new ComboBox(section);
    m_autoReplyMatchModeCombo->addItem(QStringLiteral("文本"), QIcon(), QStringLiteral("text"));
    m_autoReplyMatchModeCombo->addItem(QStringLiteral("HEX"), QIcon(), QStringLiteral("hex"));
    m_autoReplyMatchModeCombo->addItem(QStringLiteral("正则"), QIcon(), QStringLiteral("regex"));
    m_autoReplyMatchModeCombo->setFixedHeight(CompactControlHeight);
    m_autoReplyMatchModeCombo->setFixedWidth(78);
    addFormRow(root, QStringLiteral("匹配"), m_autoReplyPatternEdit, m_autoReplyMatchModeCombo);

    auto *responseModeRow = new QHBoxLayout;
    responseModeRow->setSpacing(8);
    m_autoReplyResponseModeCombo = new ComboBox(section);
    m_autoReplyResponseModeCombo->addItem(QStringLiteral("文本"), QIcon(), QStringLiteral("text"));
    m_autoReplyResponseModeCombo->addItem(QStringLiteral("HEX"), QIcon(), QStringLiteral("hex"));
    makeCompactControl(m_autoReplyResponseModeCombo);
    m_autoReplyLineEndingCombo = new ComboBox(section);
    m_autoReplyLineEndingCombo->addItem(QStringLiteral("None"), QIcon(), QStringLiteral("none"));
    m_autoReplyLineEndingCombo->addItem(QStringLiteral("CR"), QIcon(), QStringLiteral("cr"));
    m_autoReplyLineEndingCombo->addItem(QStringLiteral("LF"), QIcon(), QStringLiteral("lf"));
    m_autoReplyLineEndingCombo->addItem(QStringLiteral("CRLF"), QIcon(), QStringLiteral("crlf"));
    makeCompactControl(m_autoReplyLineEndingCombo);
    responseModeRow->addWidget(m_autoReplyResponseModeCombo);
    responseModeRow->addWidget(m_autoReplyLineEndingCombo);
    root->addLayout(responseModeRow);

    m_autoReplyPayloadEdit = new PlainTextEdit(section);
    m_autoReplyPayloadEdit->setPlaceholderText(QStringLiteral("应答内容"));
    m_autoReplyPayloadEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_autoReplyPayloadEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_autoReplyPayloadEdit->setFixedHeight(64);
    m_autoReplyPayloadEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_autoReplyPayloadEdit);

    m_autoReplyDelaySpin = new SpinBox(section);
    m_autoReplyDelaySpin->setRange(0, 600000);
    m_autoReplyDelaySpin->setValue(0);
    m_autoReplyDelaySpin->setSuffix(QStringLiteral(" ms"));
    m_autoReplyDelaySpin->setFixedHeight(CompactControlHeight);
    addFormRow(root, QStringLiteral("延时"), m_autoReplyDelaySpin);

    m_autoReplyList = new ListWidget(section);
    m_autoReplyList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_autoReplyList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_autoReplyList->setMinimumHeight(132);
    m_autoReplyList->setMaximumHeight(220);
    m_autoReplyList->setBorderRadius(8);
    root->addWidget(m_autoReplyList);

    auto *saveRow = new QHBoxLayout;
    saveRow->setSpacing(8);
    m_autoReplyUpButton = new TransparentToolButton(icon(FluentIcon::Up), section);
    m_autoReplyUpButton->setToolTip(QStringLiteral("上移"));
    m_autoReplyDownButton = new TransparentToolButton(icon(FluentIcon::Download), section);
    m_autoReplyDownButton->setToolTip(QStringLiteral("下移"));
    for (ToolButton *button : {m_autoReplyUpButton, m_autoReplyDownButton}) {
        button->setFixedSize(CompactControlHeight, CompactControlHeight);
        button->setIconSize(QSize(16, 16));
    }
    m_autoReplySaveButton = new PushButton(icon(FluentIcon::Save), QStringLiteral("保存"), section);
    setButtonRowControlPolicy(m_autoReplySaveButton);
    saveRow->addWidget(m_autoReplyUpButton);
    saveRow->addWidget(m_autoReplyDownButton);
    saveRow->addWidget(m_autoReplySaveButton);
    root->addLayout(saveRow);

    auto *editRow = new QHBoxLayout;
    editRow->setSpacing(8);
    m_autoReplyLoadButton = new PushButton(icon(FluentIcon::Edit), QStringLiteral("填入"), section);
    m_autoReplyDeleteButton = new PushButton(icon(FluentIcon::Delete), QStringLiteral("删除"), section);
    setButtonRowControlPolicy(m_autoReplyLoadButton);
    setButtonRowControlPolicy(m_autoReplyDeleteButton);
    editRow->addWidget(m_autoReplyLoadButton);
    editRow->addWidget(m_autoReplyDeleteButton);
    root->addLayout(editRow);

    m_autoReplyStatusLabel = new CaptionLabel(QStringLiteral("未触发"), section);
    m_autoReplyStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_autoReplyStatusLabel->setWordWrap(true);
    root->addWidget(m_autoReplyStatusLabel);

    connect(m_autoReplyList, &ListWidget::currentRowChanged, this, [this](int row) {
        applyAutoReplyRule(row);
        updateAutoReplyActionState();
    });
    connect(m_autoReplySaveButton, &PushButton::clicked, this, &WorkbenchPage::saveCurrentAutoReplyRule);
    connect(m_autoReplyLoadButton, &PushButton::clicked, this,
            [this]() { applyAutoReplyRule(m_autoReplyList->currentRow()); });
    connect(m_autoReplyDeleteButton, &PushButton::clicked, this, &WorkbenchPage::removeSelectedAutoReplyRule);
    connect(m_autoReplyUpButton, &ToolButton::clicked, this, [this]() { moveSelectedAutoReplyRule(-1); });
    connect(m_autoReplyDownButton, &ToolButton::clicked, this, [this]() { moveSelectedAutoReplyRule(1); });

    updateAutoReplyTable();
    makeCollapsibleCard(section, QStringLiteral("autoReply"));
    return section;
}

QWidget *WorkbenchPage::createFileSendSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Folder, QStringLiteral("文件发送"), QString(), this);
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
    makeCollapsibleCard(section, QStringLiteral("fileSend"));
    return section;
}
