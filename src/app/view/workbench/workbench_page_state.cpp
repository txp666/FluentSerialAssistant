#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

void WorkbenchPage::setupSerialSignals()
{
    connect(&m_serial, &SerialController::opened, this, [this](const QString &portName) {
        m_reconnectTimer.stop();
        m_manualDisconnect = false;
        m_lastRxTimestamp = QDateTime();
        m_rxFrameBuffer.clear();
        m_connectionStartedAt = QDateTime::currentDateTime();
        m_lastStatsRxCount = m_rxCount;
        m_lastStatsTxCount = m_txCount;
        m_statsTimer.start();
        resetAutoLogSession();
        updateConnectionUi(true);
        updateRateStats();
        updateAutoLogStatus();
        if (m_saveReceiveCheck->isChecked()) {
            updateReceiveCapture(true);
        }
        showSuccess(QStringLiteral("连接成功"), QStringLiteral("%1 已打开").arg(portName));
    });
    connect(&m_serial, &SerialController::closed, this, [this]() {
        flushRxFrameBuffer();
        if (m_fileSendFile.isOpen()) {
            m_fileSendTimer.stop();
            m_fileSendFile.close();
            updateFileSendUi(false);
            updateFileProgress();
        }
        m_statsTimer.stop();
        closeAutoLog();
        closeReceiveCapture();
        m_lastRxTimestamp = QDateTime();
        updateConnectionUi(false);
        updateRateStats();
        if (!m_manualDisconnect && m_autoReconnectCheck && m_autoReconnectCheck->isChecked()) {
            scheduleReconnect();
            return;
        }
        showInfo(QStringLiteral("连接已关闭"), QStringLiteral("串口已断开"));
    });
    connect(&m_serial, &SerialController::dataReceived, this,
            [this](const QByteArray &data) { handleReceivedData(data); });
    connect(&m_serial, &SerialController::errorOccurred, this, [this](const QString &message) {
        if (!message.isEmpty()) {
            if (m_reconnectTimer.isActive()) {
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
    for (int i = 0; i < m_ports.size(); ++i) {
        const SerialPortDescriptor &descriptor = m_ports.at(i);
        m_portCombo->addItem(descriptor.displayName(), QIcon(), descriptor.portName);
        if (descriptor.portName == previousPort) {
            selectedIndex = i;
        }
    }

    if (m_ports.isEmpty()) {
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
    const int stopBits =
        settings.value(QStringLiteral("serial/stopBits"), static_cast<int>(QSerialPort::OneStop)).toInt();
    const int flow =
        settings.value(QStringLiteral("serial/flowControl"), static_cast<int>(QSerialPort::NoFlowControl)).toInt();
    const QString displayMode =
        settings.value(QStringLiteral("terminal/displayMode"), QStringLiteral("text")).toString();
    const QString receiveEncoding =
        settings.value(QStringLiteral("receive/encoding"), AppTextEncoding::defaultKey()).toString();
    const QString sendMode = settings.value(QStringLiteral("send/mode"), QStringLiteral("text")).toString();
    const QString sendEncoding =
        settings.value(QStringLiteral("send/encoding"), AppTextEncoding::defaultKey()).toString();
    const QString checksumAlgorithm =
        settings.value(QStringLiteral("checksum/algorithm"), AppChecksum::defaultAlgorithmKey()).toString();
    const QString checksumByteOrder = settings
                                          .value(QStringLiteral("checksum/byteOrder"),
                                                 AppChecksum::byteOrderKey(AppChecksum::ByteOrder::LittleEndian))
                                          .toString();
    const QString lineEnding = settings.value(QStringLiteral("send/lineEnding"), QStringLiteral("none")).toString();
    const int loopInterval = settings.value(QStringLiteral("send/loopIntervalMs"), 1000).toInt();
    const QString frameMode = settings.value(QStringLiteral("receive/frameMode"), QStringLiteral("timeout")).toString();
    const QString framePattern = settings.value(QStringLiteral("receive/framePattern")).toString();
    const int frameFixedLength = settings.value(QStringLiteral("receive/frameFixedLength"), 8).toInt();
    const int frameBreakMs = settings.value(QStringLiteral("receive/frameBreakMs"), 20).toInt();
    const QString sendPayload = settings.value(QStringLiteral("send/currentPayload")).toString();
    const QString packetGroup = settings.value(QStringLiteral("send/currentPacketGroup")).toString();
    const QString packetName = settings.value(QStringLiteral("send/currentPacketName")).toString();
    const QString packetNote = settings.value(QStringLiteral("send/currentPacketNote")).toString();
    const QString packetPayload = settings.value(QStringLiteral("send/currentPacketPayload")).toString();
    const QString packetMode =
        settings.value(QStringLiteral("send/currentPacketMode"), QStringLiteral("text")).toString();
    const QString packetLineEnding =
        settings.value(QStringLiteral("send/currentPacketLineEnding"), QStringLiteral("none")).toString();
    const bool packetEnabled = settings.value(QStringLiteral("send/currentPacketEnabled"), true).toBool();
    const QString modbusFunction =
        settings.value(QStringLiteral("modbus/function"), AppModbus::defaultFunctionKey()).toString();
    const int modbusSlave = settings.value(QStringLiteral("modbus/slave"), 1).toInt();
    const int modbusAddress = settings.value(QStringLiteral("modbus/address"), 0).toInt();
    const int modbusQuantity = settings.value(QStringLiteral("modbus/quantity"), 1).toInt();
    const QString modbusValues = settings.value(QStringLiteral("modbus/values")).toString();
    const int macroLoopCount = settings.value(QStringLiteral("macro/loopCount"), 1).toInt();
    const bool macroAbortOnFailure = settings.value(QStringLiteral("macro/abortOnFailure"), true).toBool();
    const QString filePath = settings.value(QStringLiteral("fileSend/path")).toString();
    const int fileChunkSize = settings.value(QStringLiteral("fileSend/chunkSize"), DefaultFileChunkSize).toInt();
    const int fileInterval = settings.value(QStringLiteral("fileSend/intervalMs"), DefaultFileChunkIntervalMs).toInt();
    const QString autoLogFormat = settings.value(QStringLiteral("log/format"), QStringLiteral("txt")).toString();
    const int autoLogMaxFileSizeMb = settings.value(QStringLiteral("log/maxFileSizeMb"), 16).toInt();

    const int portIndex = m_portCombo->findData(portName);
    if (portIndex >= 0) {
        m_portCombo->setCurrentIndex(portIndex);
    }
    m_baudCombo->setCurrentText(baudRate);
    m_dataBitsCombo->setCurrentText(dataBits);
    const int parityIndex = m_parityCombo->findData(parity);
    if (parityIndex >= 0) {
        m_parityCombo->setCurrentIndex(parityIndex);
    }
    const int stopIndex = m_stopBitsCombo->findData(stopBits);
    if (stopIndex >= 0) {
        m_stopBitsCombo->setCurrentIndex(stopIndex);
    }
    const int flowIndex = m_flowControlCombo->findData(flow);
    if (flowIndex >= 0) {
        m_flowControlCombo->setCurrentIndex(flowIndex);
    }
    if (m_displayModeSegment->contains(displayMode)) {
        m_displayModeSegment->setCurrentItem(displayMode);
    }
    selectEncodingOption(m_receiveEncodingCombo, receiveEncoding);
    m_hexSendCheck->setChecked(sendMode == QStringLiteral("hex"));
    selectEncodingOption(m_sendEncodingCombo, sendEncoding);
    selectChecksumAlgorithm(m_checksumAlgorithmCombo, checksumAlgorithm);
    const int checksumByteOrderIndex =
        m_checksumByteOrderCombo->findData(AppChecksum::byteOrderKey(AppChecksum::byteOrderFromKey(checksumByteOrder)));
    if (checksumByteOrderIndex >= 0) {
        m_checksumByteOrderCombo->setCurrentIndex(checksumByteOrderIndex);
    }
    m_checksumAppendCheck->setChecked(settings.value(QStringLiteral("checksum/autoAppend"), false).toBool());
    const int eolIndex = m_lineEndingCombo->findData(lineEnding);
    if (eolIndex >= 0) {
        m_lineEndingCombo->setCurrentIndex(eolIndex);
    }
    m_loopIntervalSpin->setValue(qBound(10, loopInterval, 600000));
    m_rtsCheck->setChecked(settings.value(QStringLiteral("serial/rts"), false).toBool());
    m_dtrCheck->setChecked(settings.value(QStringLiteral("serial/dtr"), false).toBool());
    m_autoOpenCheck->setChecked(settings.value(QStringLiteral("serial/autoOpen"), false).toBool());
    m_saveReceiveCheck->setChecked(settings.value(QStringLiteral("receive/saveToFile"), false).toBool());
    m_autoScrollCheck->setChecked(settings.value(QStringLiteral("receive/autoScroll"), true).toBool());
    m_timestampCheck->setChecked(settings.value(QStringLiteral("receive/timestamp"), false).toBool());
    const int frameModeIndex = m_frameModeCombo->findData(frameMode);
    m_frameModeCombo->setCurrentIndex(frameModeIndex >= 0 ? frameModeIndex
                                                          : m_frameModeCombo->findData(QStringLiteral("timeout")));
    m_framePatternEdit->setText(framePattern);
    m_frameFixedLengthSpin->setValue(qBound(1, frameFixedLength, 65536));
    m_autoFrameBreakCheck->setChecked(settings.value(QStringLiteral("receive/autoFrameBreak"), false).toBool());
    m_frameBreakIntervalSpin->setValue(qBound(1, frameBreakMs, 60000));
    updateFrameControlState();
    const int autoLogFormatIndex = m_autoLogFormatCombo->findData(autoLogFormat);
    if (autoLogFormatIndex >= 0) {
        m_autoLogFormatCombo->setCurrentIndex(autoLogFormatIndex);
    }
    m_autoLogMaxSizeSpin->setValue(qBound(1, autoLogMaxFileSizeMb, 4096));
    m_autoLogCheck->setChecked(settings.value(QStringLiteral("log/enabled"), false).toBool());
    updateAutoLogStatus();
    m_showTxCheck->setChecked(settings.value(QStringLiteral("send/showTx"), true).toBool());
    const QColor txColor(
        settings.value(QStringLiteral("send/txColor"), defaultTxColor().name(QColor::HexRgb)).toString());
    m_txColorButton->setColor(txColor.isValid() ? txColor : defaultTxColor());
    m_txColorButton->setEnabled(m_showTxCheck->isChecked());
    m_autoReconnectCheck->setChecked(settings.value(QStringLiteral("serial/autoReconnect"), true).toBool());
    m_sendEdit->setPlainText(sendPayload);
    m_packetGroupEdit->setText(packetGroup);
    m_packetNameEdit->setText(packetName);
    m_packetNoteEdit->setText(packetNote);
    m_packetEnabledCheck->setChecked(packetEnabled);
    m_packetPayloadEdit->setPlainText(packetPayload);
    const int packetModeIndex = m_packetModeCombo->findData(packetMode);
    if (packetModeIndex >= 0) {
        m_packetModeCombo->setCurrentIndex(packetModeIndex);
    }
    const int packetEolIndex = m_packetLineEndingCombo->findData(packetLineEnding);
    if (packetEolIndex >= 0) {
        m_packetLineEndingCombo->setCurrentIndex(packetEolIndex);
    }
    const int modbusFunctionIndex = m_modbusFunctionCombo->findData(AppModbus::normalizedFunctionKey(modbusFunction));
    if (modbusFunctionIndex >= 0) {
        m_modbusFunctionCombo->setCurrentIndex(modbusFunctionIndex);
    }
    m_modbusSlaveSpin->setValue(qBound(1, modbusSlave, 247));
    m_modbusAddressSpin->setValue(qBound(0, modbusAddress, 65535));
    m_modbusQuantitySpin->setValue(qBound(1, modbusQuantity, 2000));
    m_modbusValuesEdit->setPlainText(modbusValues);
    updateModbusUi();
    m_macroLoopCountSpin->setValue(qBound(1, macroLoopCount, 100000));
    m_macroAbortOnFailureCheck->setChecked(macroAbortOnFailure);
    updateMacroActionState();
    m_filePathEdit->setText(filePath);
    m_fileChunkSizeSpin->setValue(qBound(1, fileChunkSize, 65536));
    m_fileIntervalSpin->setValue(qBound(0, fileInterval, 60000));
    if (!filePath.isEmpty()) {
        const QFileInfo info(filePath);
        m_fileStatusLabel->setText(info.exists()
                                       ? QStringLiteral("%1 · %2").arg(info.fileName(), formatBytes(info.size()))
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
    settings.setValue(QStringLiteral("receive/frameMode"), frameBreakModeKey());
    settings.setValue(QStringLiteral("receive/framePattern"), m_framePatternEdit->text());
    settings.setValue(QStringLiteral("receive/frameFixedLength"), m_frameFixedLengthSpin->value());
    settings.setValue(QStringLiteral("receive/frameBreakMs"), m_frameBreakIntervalSpin->value());
    settings.setValue(QStringLiteral("receive/encoding"), receiveEncodingKey());
    settings.setValue(QStringLiteral("log/enabled"), m_autoLogCheck->isChecked());
    settings.setValue(QStringLiteral("log/format"), autoLogFormatKey());
    settings.setValue(QStringLiteral("log/maxFileSizeMb"), m_autoLogMaxSizeSpin->value());
    settings.setValue(QStringLiteral("terminal/displayMode"), currentDisplayMode());
    settings.setValue(QStringLiteral("send/mode"),
                      m_hexSendCheck->isChecked() ? QStringLiteral("hex") : QStringLiteral("text"));
    settings.setValue(QStringLiteral("send/encoding"), sendEncodingKey());
    settings.setValue(QStringLiteral("checksum/algorithm"), checksumAlgorithmKey());
    settings.setValue(QStringLiteral("checksum/byteOrder"), AppChecksum::byteOrderKey(checksumByteOrder()));
    settings.setValue(QStringLiteral("checksum/autoAppend"), m_checksumAppendCheck->isChecked());
    settings.setValue(QStringLiteral("send/lineEnding"), selectedLineEndingKey());
    settings.setValue(QStringLiteral("send/showTx"), m_showTxCheck->isChecked());
    settings.setValue(QStringLiteral("send/txColor"), selectedTxColor().name(QColor::HexRgb));
    settings.setValue(QStringLiteral("send/loopIntervalMs"), m_loopIntervalSpin->value());
    settings.setValue(QStringLiteral("send/currentPayload"), m_sendEdit->toPlainText());
    settings.setValue(QStringLiteral("send/currentPacketGroup"), m_packetGroupEdit->text());
    settings.setValue(QStringLiteral("send/currentPacketName"), m_packetNameEdit->text());
    settings.setValue(QStringLiteral("send/currentPacketNote"), m_packetNoteEdit->text());
    settings.setValue(QStringLiteral("send/currentPacketEnabled"), m_packetEnabledCheck->isChecked());
    settings.setValue(QStringLiteral("send/currentPacketPayload"), m_packetPayloadEdit->toPlainText());
    settings.setValue(QStringLiteral("send/currentPacketMode"), m_packetModeCombo->currentData().toString());
    settings.setValue(QStringLiteral("send/currentPacketLineEnding"),
                      m_packetLineEndingCombo->currentData().toString());
    settings.setValue(QStringLiteral("modbus/function"), m_modbusFunctionCombo->currentData().toString());
    settings.setValue(QStringLiteral("modbus/slave"), m_modbusSlaveSpin->value());
    settings.setValue(QStringLiteral("modbus/address"), m_modbusAddressSpin->value());
    settings.setValue(QStringLiteral("modbus/quantity"), m_modbusQuantitySpin->value());
    settings.setValue(QStringLiteral("modbus/values"), m_modbusValuesEdit->toPlainText());
    settings.setValue(QStringLiteral("macro/loopCount"), m_macroLoopCountSpin->value());
    settings.setValue(QStringLiteral("macro/abortOnFailure"), m_macroAbortOnFailureCheck->isChecked());
    settings.setValue(QStringLiteral("serial/autoReconnect"), m_autoReconnectCheck->isChecked());
    settings.setValue(QStringLiteral("fileSend/path"), m_filePathEdit->text());
    settings.setValue(QStringLiteral("fileSend/chunkSize"), m_fileChunkSizeSpin->value());
    settings.setValue(QStringLiteral("fileSend/intervalMs"), m_fileIntervalSpin->value());
    saveSendHistory();
    saveSendPackets();
    saveMacroSteps();
    saveAutoReplyRules();
}

void WorkbenchPage::copySessionConfigFrom(const WorkbenchPage &source)
{
    refreshPorts();

    const int portIndex = m_portCombo->findData(source.m_portCombo->currentData());
    if (portIndex >= 0) {
        m_portCombo->setCurrentIndex(portIndex);
    }
    m_baudCombo->setCurrentText(source.m_baudCombo->currentText());
    m_dataBitsCombo->setCurrentText(source.m_dataBitsCombo->currentText());
    const int parityIndex = m_parityCombo->findData(source.m_parityCombo->currentData());
    if (parityIndex >= 0) {
        m_parityCombo->setCurrentIndex(parityIndex);
    }
    const int stopIndex = m_stopBitsCombo->findData(source.m_stopBitsCombo->currentData());
    if (stopIndex >= 0) {
        m_stopBitsCombo->setCurrentIndex(stopIndex);
    }
    const int flowIndex = m_flowControlCombo->findData(source.m_flowControlCombo->currentData());
    if (flowIndex >= 0) {
        m_flowControlCombo->setCurrentIndex(flowIndex);
    }
    m_rtsCheck->setChecked(source.m_rtsCheck->isChecked());
    m_dtrCheck->setChecked(source.m_dtrCheck->isChecked());
    m_autoOpenCheck->setChecked(source.m_autoOpenCheck->isChecked());

    if (m_displayModeSegment->contains(source.currentDisplayMode())) {
        m_displayModeSegment->setCurrentItem(source.currentDisplayMode());
    }
    selectEncodingOption(m_receiveEncodingCombo, source.receiveEncodingKey());
    {
        const QSignalBlocker blocker(m_saveReceiveCheck);
        m_saveReceiveCheck->setChecked(source.m_saveReceiveCheck->isChecked());
    }
    closeReceiveCapture();
    m_autoScrollCheck->setChecked(source.m_autoScrollCheck->isChecked());
    m_timestampCheck->setChecked(source.m_timestampCheck->isChecked());
    m_pauseCheck->setChecked(false);
    const int frameModeIndex = m_frameModeCombo->findData(source.frameBreakModeKey());
    if (frameModeIndex >= 0) {
        m_frameModeCombo->setCurrentIndex(frameModeIndex);
    }
    m_framePatternEdit->setText(source.m_framePatternEdit->text());
    m_frameFixedLengthSpin->setValue(source.m_frameFixedLengthSpin->value());
    m_frameBreakIntervalSpin->setValue(source.m_frameBreakIntervalSpin->value());
    m_autoFrameBreakCheck->setChecked(source.m_autoFrameBreakCheck->isChecked());
    updateFrameControlState();

    const int autoLogFormatIndex = m_autoLogFormatCombo->findData(source.autoLogFormatKey());
    if (autoLogFormatIndex >= 0) {
        m_autoLogFormatCombo->setCurrentIndex(autoLogFormatIndex);
    }
    m_autoLogMaxSizeSpin->setValue(source.m_autoLogMaxSizeSpin->value());
    {
        const QSignalBlocker blocker(m_autoLogCheck);
        m_autoLogCheck->setChecked(source.m_autoLogCheck->isChecked());
    }
    resetAutoLogSession();
    updateAutoLogStatus();

    m_hexSendCheck->setChecked(source.m_hexSendCheck->isChecked());
    m_showTxCheck->setChecked(source.m_showTxCheck->isChecked());
    m_txColorButton->setColor(source.selectedTxColor());
    m_txColorButton->setEnabled(m_showTxCheck->isChecked());
    m_loopCheck->setChecked(false);
    m_autoReconnectCheck->setChecked(source.m_autoReconnectCheck->isChecked());
    m_loopIntervalSpin->setValue(source.m_loopIntervalSpin->value());
    selectEncodingOption(m_sendEncodingCombo, source.sendEncodingKey());
    const int checksumAlgorithmIndex = m_checksumAlgorithmCombo->findData(source.checksumAlgorithmKey());
    if (checksumAlgorithmIndex >= 0) {
        m_checksumAlgorithmCombo->setCurrentIndex(checksumAlgorithmIndex);
    }
    const int checksumByteOrderIndex =
        m_checksumByteOrderCombo->findData(AppChecksum::byteOrderKey(source.checksumByteOrder()));
    if (checksumByteOrderIndex >= 0) {
        m_checksumByteOrderCombo->setCurrentIndex(checksumByteOrderIndex);
    }
    m_checksumAppendCheck->setChecked(source.m_checksumAppendCheck->isChecked());
    const int lineEndingIndex = m_lineEndingCombo->findData(source.selectedLineEndingKey());
    if (lineEndingIndex >= 0) {
        m_lineEndingCombo->setCurrentIndex(lineEndingIndex);
    }

    m_sendEdit->setPlainText(source.m_sendEdit->toPlainText());
    m_sendHistory = source.m_sendHistory;
    updateHistoryCombo();

    m_sendPackets = source.m_sendPackets;
    m_packetGroupEdit->setText(source.m_packetGroupEdit->text());
    m_packetNameEdit->setText(source.m_packetNameEdit->text());
    m_packetNoteEdit->setText(source.m_packetNoteEdit->text());
    m_packetPayloadEdit->setPlainText(source.m_packetPayloadEdit->toPlainText());
    const int packetModeIndex = m_packetModeCombo->findData(source.m_packetModeCombo->currentData());
    if (packetModeIndex >= 0) {
        m_packetModeCombo->setCurrentIndex(packetModeIndex);
    }
    const int packetLineEndingIndex = m_packetLineEndingCombo->findData(source.m_packetLineEndingCombo->currentData());
    if (packetLineEndingIndex >= 0) {
        m_packetLineEndingCombo->setCurrentIndex(packetLineEndingIndex);
    }
    m_packetEnabledCheck->setChecked(source.m_packetEnabledCheck->isChecked());
    updatePacketTable();

    const int modbusFunctionIndex = m_modbusFunctionCombo->findData(source.m_modbusFunctionCombo->currentData());
    if (modbusFunctionIndex >= 0) {
        m_modbusFunctionCombo->setCurrentIndex(modbusFunctionIndex);
    }
    m_modbusSlaveSpin->setValue(source.m_modbusSlaveSpin->value());
    m_modbusAddressSpin->setValue(source.m_modbusAddressSpin->value());
    m_modbusQuantitySpin->setValue(source.m_modbusQuantitySpin->value());
    m_modbusValuesEdit->setPlainText(source.m_modbusValuesEdit->toPlainText());
    updateModbusUi();

    m_macroSteps = source.m_macroSteps;
    m_macroNameEdit->setText(source.m_macroNameEdit->text());
    m_macroPayloadEdit->setPlainText(source.m_macroPayloadEdit->toPlainText());
    m_macroExpectedEdit->setText(source.m_macroExpectedEdit->text());
    const int macroModeIndex = m_macroModeCombo->findData(source.m_macroModeCombo->currentData());
    if (macroModeIndex >= 0) {
        m_macroModeCombo->setCurrentIndex(macroModeIndex);
    }
    const int macroLineEndingIndex = m_macroLineEndingCombo->findData(source.m_macroLineEndingCombo->currentData());
    if (macroLineEndingIndex >= 0) {
        m_macroLineEndingCombo->setCurrentIndex(macroLineEndingIndex);
    }
    const int macroResponseModeIndex =
        m_macroResponseModeCombo->findData(source.m_macroResponseModeCombo->currentData());
    if (macroResponseModeIndex >= 0) {
        m_macroResponseModeCombo->setCurrentIndex(macroResponseModeIndex);
    }
    m_macroTimeoutSpin->setValue(source.m_macroTimeoutSpin->value());
    m_macroDelaySpin->setValue(source.m_macroDelaySpin->value());
    m_macroLoopCountSpin->setValue(source.m_macroLoopCountSpin->value());
    m_macroAbortOnFailureCheck->setChecked(source.m_macroAbortOnFailureCheck->isChecked());
    updateMacroTable();

    m_autoReplyRules = source.m_autoReplyRules;
    m_autoReplyNameEdit->setText(source.m_autoReplyNameEdit->text());
    m_autoReplyPatternEdit->setText(source.m_autoReplyPatternEdit->text());
    m_autoReplyPayloadEdit->setPlainText(source.m_autoReplyPayloadEdit->toPlainText());
    const int autoReplyMatchIndex =
        m_autoReplyMatchModeCombo->findData(source.m_autoReplyMatchModeCombo->currentData());
    if (autoReplyMatchIndex >= 0) {
        m_autoReplyMatchModeCombo->setCurrentIndex(autoReplyMatchIndex);
    }
    const int autoReplyResponseIndex =
        m_autoReplyResponseModeCombo->findData(source.m_autoReplyResponseModeCombo->currentData());
    if (autoReplyResponseIndex >= 0) {
        m_autoReplyResponseModeCombo->setCurrentIndex(autoReplyResponseIndex);
    }
    const int autoReplyLineEndingIndex =
        m_autoReplyLineEndingCombo->findData(source.m_autoReplyLineEndingCombo->currentData());
    if (autoReplyLineEndingIndex >= 0) {
        m_autoReplyLineEndingCombo->setCurrentIndex(autoReplyLineEndingIndex);
    }
    m_autoReplyDelaySpin->setValue(source.m_autoReplyDelaySpin->value());
    m_autoReplyEnabledCheck->setChecked(source.m_autoReplyEnabledCheck->isChecked());
    updateAutoReplyTable();

    m_filePathEdit->setText(source.m_filePathEdit->text());
    m_fileChunkSizeSpin->setValue(source.m_fileChunkSizeSpin->value());
    m_fileIntervalSpin->setValue(source.m_fileIntervalSpin->value());
    updateFileSendUi(false);
    updateFileProgress();

    m_records.clear();
    m_pendingRecordIndexes.clear();
    m_terminalSearchMatches.clear();
    m_terminalCurrentSearchMatch = -1;
    m_terminalStartRecord = 0;
    m_rxCount = 0;
    m_txCount = 0;
    m_lastStatsRxCount = 0;
    m_lastStatsTxCount = 0;
    m_terminalView->clear();
    updateCounters();
    updateRateStats();
}

void WorkbenchPage::setTerminalFontFamily(const QString &family) { applyTerminalFont(family); }

void WorkbenchPage::applyTerminalFont(const QString &family)
{
    if (m_terminalView) {
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
    if (m_terminalSummaryLabel) {
        int visibleRecords = 0;
        for (const SessionRecord &record : m_records) {
            if (record.direction == RecordDirection::Tx && m_showTxCheck && !m_showTxCheck->isChecked()) {
                continue;
            }
            if (record.direction != RecordDirection::FrameBreak && recordMatchesTerminalFilter(record)) {
                ++visibleRecords;
            }
        }

        const TerminalSearchQuery query = terminalSearchQuery();
        const bool canNavigate = !query.text.isEmpty() && query.valid && !m_terminalSearchMatches.isEmpty();
        if (m_terminalSearchPrevButton) {
            m_terminalSearchPrevButton->setEnabled(canNavigate);
        }
        if (m_terminalSearchNextButton) {
            m_terminalSearchNextButton->setEnabled(canNavigate);
        }

        if (query.text.isEmpty()) {
            m_terminalSummaryLabel->setText(QStringLiteral("显示 %1 条").arg(visibleRecords));
            m_terminalSummaryLabel->setToolTip(QString());
        } else if (!query.valid) {
            m_terminalSummaryLabel->setText(QStringLiteral("正则无效"));
            m_terminalSummaryLabel->setToolTip(query.errorMessage);
        } else if (m_terminalSearchMatches.isEmpty()) {
            m_terminalSummaryLabel->setText(QStringLiteral("匹配 0 · %1 条").arg(visibleRecords));
            m_terminalSummaryLabel->setToolTip(QString());
        } else {
            const int current = qBound(0, m_terminalCurrentSearchMatch, m_terminalSearchMatches.size() - 1) + 1;
            m_terminalSummaryLabel->setText(
                QStringLiteral("匹配 %1/%2").arg(current).arg(m_terminalSearchMatches.size()));
            m_terminalSummaryLabel->setToolTip(QStringLiteral("显示 %1 条").arg(visibleRecords));
        }
    }
}

void WorkbenchPage::updateRateStats()
{
    if (m_rxRateLabel) {
        m_rxRateLabel->setText(formatBytesPerSecond(qMax<qint64>(0, m_rxCount - m_lastStatsRxCount)));
    }
    if (m_txRateLabel) {
        m_txRateLabel->setText(formatBytesPerSecond(qMax<qint64>(0, m_txCount - m_lastStatsTxCount)));
    }
    m_lastStatsRxCount = m_rxCount;
    m_lastStatsTxCount = m_txCount;

    if (!m_connectionTimeLabel) {
        return;
    }
    if (m_serial.isOpen() && m_connectionStartedAt.isValid()) {
        const qint64 seconds = m_connectionStartedAt.secsTo(QDateTime::currentDateTime());
        m_connectionTimeLabel->setText(QStringLiteral("已连接 %1").arg(formatDuration(seconds)));
    } else {
        m_connectionTimeLabel->setText(QStringLiteral("未连接"));
    }
}

void WorkbenchPage::updateHistoryCombo()
{
    if (!m_historyCombo) {
        return;
    }

    const QSignalBlocker blocker(m_historyCombo);
    m_historyCombo->clear();
    m_historyCombo->addItem(QStringLiteral("发送历史"), QIcon(), QVariant());
    m_historyCombo->setItemEnabled(0, false);
    for (int i = 0; i < m_sendHistory.size(); ++i) {
        m_historyCombo->addItem(historyLabel(m_sendHistory.at(i)), QIcon(), i);
    }
    m_historyCombo->setCurrentIndex(0);
}

void WorkbenchPage::applyHistoryItem(int index)
{
    bool ok = false;
    const int historyIndex = m_historyCombo->itemData(index).toInt(&ok);
    if (!ok || historyIndex < 0 || historyIndex >= m_sendHistory.size()) {
        return;
    }

    const SendHistoryItem item = m_sendHistory.at(historyIndex);
    m_hexSendCheck->setChecked(item.mode == QStringLiteral("hex"));
    const int lineEndingIndex = m_lineEndingCombo->findData(item.lineEnding);
    if (lineEndingIndex >= 0) {
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
