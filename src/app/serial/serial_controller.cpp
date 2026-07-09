#include "app/serial/serial_controller.h"
#include "app/core/app_i18n.h"

#include <QtSerialPort/QSerialPortInfo>

QString SerialPortDescriptor::displayName() const
{
    if (description.isEmpty()) {
        return portName;
    }
    return QStringLiteral("%1  %2").arg(portName, description);
}

QString SerialPortDescriptor::detailText() const
{
    QStringList parts;
    if (!description.isEmpty()) {
        parts.append(description);
    }
    if (!manufacturer.isEmpty()) {
        parts.append(manufacturer);
    }
    if (hasVendorIdentifier || hasProductIdentifier) {
        parts.append(QStringLiteral("VID:PID %1:%2")
                         .arg(hasVendorIdentifier
                                  ? QStringLiteral("%1").arg(vendorIdentifier, 4, 16, QLatin1Char('0')).toUpper()
                                  : QStringLiteral("----"))
                         .arg(hasProductIdentifier
                                  ? QStringLiteral("%1").arg(productIdentifier, 4, 16, QLatin1Char('0')).toUpper()
                                  : QStringLiteral("----")));
    }
    if (!serialNumber.isEmpty()) {
        parts.append(QStringLiteral("SN %1").arg(serialNumber));
    }
    return parts.join(QStringLiteral(" | "));
}

SerialController::SerialController(QObject *parent) : QObject(parent)
{
    connect(&m_port, &QSerialPort::readyRead, this, [this]() {
        const QByteArray data = m_port.readAll();
        if (!data.isEmpty()) {
            emit dataReceived(data);
        }
    });

    connect(&m_port, &QSerialPort::bytesWritten, this, &SerialController::writeQueued);
    connect(&m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError) {
            return;
        }
        emit errorOccurred(m_port.errorString());
        if (error == QSerialPort::ResourceError) {
            closePort();
        }
    });
}

QList<SerialPortDescriptor> SerialController::availablePorts()
{
    QList<SerialPortDescriptor> descriptors;
    const auto ports = QSerialPortInfo::availablePorts();
    descriptors.reserve(ports.size());
    for (const QSerialPortInfo &info : ports) {
        SerialPortDescriptor descriptor;
        descriptor.portName = info.portName();
        descriptor.description = info.description();
        descriptor.manufacturer = info.manufacturer();
        descriptor.serialNumber = info.serialNumber();
        descriptor.hasVendorIdentifier = info.hasVendorIdentifier();
        descriptor.hasProductIdentifier = info.hasProductIdentifier();
        descriptor.vendorIdentifier = info.vendorIdentifier();
        descriptor.productIdentifier = info.productIdentifier();
        descriptors.append(descriptor);
    }
    return descriptors;
}

bool SerialController::isOpen() const { return m_port.isOpen(); }

QString SerialController::portName() const { return m_port.portName(); }

QString SerialController::errorString() const { return m_port.errorString(); }

bool SerialController::openPort(const SerialPortConfig &config)
{
    if (m_port.isOpen()) {
        closePort();
    }

    m_port.setPortName(config.portName);
    m_port.setBaudRate(config.baudRate);
    m_port.setDataBits(config.dataBits);
    m_port.setParity(config.parity);
    m_port.setStopBits(config.stopBits);
    m_port.setFlowControl(config.flowControl);

    if (!m_port.open(QIODevice::ReadWrite)) {
        emit errorOccurred(m_port.errorString());
        return false;
    }

    setRequestToSend(config.requestToSend);
    setDataTerminalReady(config.dataTerminalReady);
    emit opened(config.portName);
    return true;
}

void SerialController::closePort()
{
    if (!m_port.isOpen()) {
        return;
    }
    m_port.close();
    emit closed();
}

bool SerialController::writeData(const QByteArray &data, QString *error)
{
    if (!m_port.isOpen()) {
        if (error) {
            *error = AppI18n::text("串口未连接");
        }
        return false;
    }
    const qint64 written = m_port.write(data);
    if (written < 0) {
        if (error) {
            *error = m_port.errorString();
        }
        emit errorOccurred(m_port.errorString());
        return false;
    }
    return true;
}

bool SerialController::setRequestToSend(bool enabled)
{
    if (!m_port.isOpen()) {
        return false;
    }
    const bool ok = m_port.setRequestToSend(enabled);
    if (!ok) {
        emit errorOccurred(m_port.errorString());
    }
    return ok;
}

bool SerialController::setDataTerminalReady(bool enabled)
{
    if (!m_port.isOpen()) {
        return false;
    }
    const bool ok = m_port.setDataTerminalReady(enabled);
    if (!ok) {
        emit errorOccurred(m_port.errorString());
    }
    return ok;
}
