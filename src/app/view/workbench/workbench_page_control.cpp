#include "app/view/workbench/workbench_page_internal.h"

#include "app/view/quick_plot_window.h"

using namespace WorkbenchPagePrivate;

namespace {

void selectComboData(FluentQt::ComboBox *combo, int value)
{
    if (!combo) {
        return;
    }
    const int index = combo->findData(value);
    if (index >= 0) {
        combo->setCurrentIndex(index);
    }
}

} // namespace

AppControl::SessionStatus WorkbenchPage::controlStatus() const
{
    AppControl::SessionStatus status;
    status.id = objectName();
    status.connected = m_serial.isOpen();
    status.serialConfig = currentSerialConfig();
    status.receivedBytes = m_rxCount;
    status.transmittedBytes = m_txCount;
    status.recordCount = m_records.size();
    status.protocolEnabled = m_protocolEnabledCheck && m_protocolEnabledCheck->isChecked();
    if (m_protocolTemplateCombo && m_protocolTemplateCombo->currentIndex() >= 0) {
        status.protocolName = m_protocolTemplateCombo->currentText();
    }
    return status;
}

bool WorkbenchPage::controlConnect(const SerialPortConfig &config, QString *error)
{
    if (error) {
        error->clear();
    }
    if (config.portName.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Port name is required");
        }
        return false;
    }
    if (config.baudRate <= 0) {
        if (error) {
            *error = QStringLiteral("Baud rate must be greater than zero");
        }
        return false;
    }

    if (m_serial.isOpen()) {
        m_manualDisconnect = true;
        m_reconnectTimer.stop();
        m_serial.closePort();
    }

    refreshPorts();
    int portIndex = m_portCombo ? m_portCombo->findData(config.portName) : -1;
    if (m_portCombo && portIndex < 0) {
        m_portCombo->addItem(config.portName, QIcon(), config.portName);
        portIndex = m_portCombo->count() - 1;
    }
    if (m_portCombo && portIndex >= 0) {
        m_portCombo->setCurrentIndex(portIndex);
    }
    if (m_baudCombo) {
        m_baudCombo->setCurrentText(QString::number(config.baudRate));
    }
    if (m_dataBitsCombo) {
        m_dataBitsCombo->setCurrentText(QString::number(static_cast<int>(config.dataBits)));
    }
    selectComboData(m_parityCombo, static_cast<int>(config.parity));
    selectComboData(m_stopBitsCombo, static_cast<int>(config.stopBits));
    selectComboData(m_flowControlCombo, static_cast<int>(config.flowControl));
    if (m_rtsCheck) {
        m_rtsCheck->setChecked(config.requestToSend);
    }
    if (m_dtrCheck) {
        m_dtrCheck->setChecked(config.dataTerminalReady);
    }

    m_manualDisconnect = false;
    m_lastConfig = config;
    if (!m_serial.openPort(config)) {
        if (error) {
            *error = m_serial.errorString();
        }
        return false;
    }
    return true;
}

void WorkbenchPage::controlDisconnect()
{
    m_manualDisconnect = true;
    m_reconnectTimer.stop();
    m_serial.closePort();
}

bool WorkbenchPage::controlSendBytes(const QByteArray &data, const QString &source, QString *error)
{
    if (error) {
        error->clear();
    }
    if (data.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Payload is empty");
        }
        return false;
    }
    if (!m_serial.isOpen()) {
        if (error) {
            *error = QStringLiteral("Serial port is not connected");
        }
        return false;
    }
    if (!m_serial.writeData(data, error)) {
        return false;
    }
    appendRecord(RecordDirection::Tx, data, true, source);
    return true;
}

QList<AppControl::RecordSnapshot> WorkbenchPage::controlRecords(int limit, const QString &direction) const
{
    QList<AppControl::RecordSnapshot> snapshots;
    const int boundedLimit = qBound(1, limit, 1000);
    const bool protocolEnabled = m_protocolEnabledCheck && m_protocolEnabledCheck->isChecked() &&
                                 m_protocolTemplateCombo && m_protocolTemplateCombo->currentIndex() >= 0;
    const AppProtocol::ProtocolTemplate protocol =
        protocolEnabled ? currentProtocolTemplateFromUi() : AppProtocol::ProtocolTemplate();

    for (int index = m_records.size() - 1; index >= 0 && snapshots.size() < boundedLimit; --index) {
        const SessionRecord &record = m_records.at(index);
        if (record.direction == RecordDirection::FrameBreak) {
            continue;
        }
        const QString recordDirection =
            record.direction == RecordDirection::Tx ? QStringLiteral("tx") : QStringLiteral("rx");
        if (direction != QStringLiteral("all") && direction != recordDirection) {
            continue;
        }

        AppControl::RecordSnapshot snapshot;
        snapshot.timestamp = record.timestamp;
        snapshot.direction = recordDirection;
        snapshot.source = record.sourceLabel;
        snapshot.bytes = record.bytes;
        snapshot.text = record.displayText;
        if (protocolEnabled && record.direction == RecordDirection::Rx) {
            snapshot.hasProtocolResult = true;
            snapshot.protocolResult = AppProtocol::parseFrame(record.bytes, protocol);
        }
        snapshots.prepend(snapshot);
    }
    return snapshots;
}

QList<AppControl::ProtocolSnapshot> WorkbenchPage::controlProtocols() const
{
    QList<AppControl::ProtocolSnapshot> snapshots;
    snapshots.reserve(m_protocolTemplates.size());
    const int selectedIndex = m_protocolTemplateCombo ? m_protocolTemplateCombo->currentIndex() : -1;
    const bool enabled = m_protocolEnabledCheck && m_protocolEnabledCheck->isChecked();
    for (int index = 0; index < m_protocolTemplates.size(); ++index) {
        AppControl::ProtocolSnapshot snapshot;
        snapshot.definition = m_protocolTemplates.at(index);
        snapshot.selected = index == selectedIndex;
        snapshot.enabled = snapshot.selected && enabled;
        snapshots.append(snapshot);
    }
    return snapshots;
}

bool WorkbenchPage::controlSelectProtocol(const QString &name, bool enabled, QString *error)
{
    if (error) {
        error->clear();
    }
    int selectedIndex = -1;
    for (int index = 0; index < m_protocolTemplates.size(); ++index) {
        if (m_protocolTemplates.at(index).name.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
            selectedIndex = index;
            break;
        }
    }
    if (selectedIndex < 0) {
        if (error) {
            *error = QStringLiteral("Protocol template was not found: %1").arg(name);
        }
        return false;
    }

    if (m_protocolTemplateCombo) {
        m_protocolTemplateCombo->setCurrentIndex(selectedIndex);
    }
    applyProtocolTemplate(selectedIndex);
    if (m_protocolEnabledCheck) {
        m_protocolEnabledCheck->setChecked(enabled);
    }
    saveProtocolTemplates();
    return true;
}

bool WorkbenchPage::controlShowPlot(const AppPlot::ParserConfig &config, bool clear, QString *error)
{
    if (error) {
        error->clear();
    }
    if (config.protocol == AppPlot::Protocol::Binary && config.binarySource == AppPlot::BinarySource::Payload &&
        (!m_protocolEnabledCheck || !m_protocolEnabledCheck->isChecked() || !m_protocolTemplateCombo ||
         m_protocolTemplateCombo->currentIndex() < 0)) {
        if (error) {
            *error = QStringLiteral("Binary payload plotting requires an enabled protocol template");
        }
        return false;
    }

    showQuickPlotWindow();
    if (!m_quickPlotWindow || !m_quickPlotWindow->configureParser(config)) {
        if (error) {
            *error = QStringLiteral("Failed to configure the plot window");
        }
        return false;
    }
    if (clear) {
        m_quickPlotWindow->clearData();
    }
    return true;
}
