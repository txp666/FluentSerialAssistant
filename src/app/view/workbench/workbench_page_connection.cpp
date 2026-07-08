#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

void WorkbenchPage::scheduleReconnect()
{
    if (m_lastConfig.portName.isEmpty() || m_manualDisconnect || !m_autoReconnectCheck->isChecked()) {
        return;
    }
    if (!m_reconnectTimer.isActive()) {
        m_reconnectTimer.start();
    }
    showWarning(QStringLiteral("连接中断"), QStringLiteral("正在尝试自动重连"));
}

void WorkbenchPage::attemptReconnect()
{
    if (m_serial.isOpen()) {
        m_reconnectTimer.stop();
        return;
    }
    if (m_lastConfig.portName.isEmpty() || !m_autoReconnectCheck->isChecked()) {
        m_reconnectTimer.stop();
        return;
    }

    refreshPorts();
    m_serial.openPort(m_lastConfig);
}

void WorkbenchPage::onConnectClicked()
{
    if (m_serial.isOpen()) {
        m_manualDisconnect = true;
        m_reconnectTimer.stop();
        m_serial.closePort();
        return;
    }

    const SerialPortConfig config = currentSerialConfig();
    if (config.portName.isEmpty()) {
        showWarning(QStringLiteral("未选择端口"), QStringLiteral("请先刷新并选择可用串口"));
        return;
    }

    bool baudOk = false;
    const int baud = m_baudCombo->currentText().trimmed().toInt(&baudOk);
    if (!baudOk || baud <= 0) {
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
    if (!checked) {
        m_loopTimer.stop();
        return;
    }
    if (!m_serial.isOpen()) {
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
    if (m_packetSendButton) {
        m_packetSendButton->setEnabled(connected && !m_sendPackets.isEmpty());
    }
    if (m_fileSendButton) {
        m_fileSendButton->setEnabled(connected && !m_fileSendFile.isOpen());
    }
    if (m_fileCancelButton) {
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

    if (!connected) {
        m_loopCheck->setChecked(false);
        m_loopTimer.stop();
    }
}

int WorkbenchPage::maxRecordCount() const
{
    QSettings settings;
    return qBound(1000, settings.value(QStringLiteral("terminal/maxRecords"), DefaultMaxTerminalRecords).toInt(),
                  50000);
}
