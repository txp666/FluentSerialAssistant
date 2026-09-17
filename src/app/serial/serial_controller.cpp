#include "app/serial/serial_controller.h"
#include "app/core/app_i18n.h"
#include "app/serial/virtual_serial_pair.h"

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

SerialController::~SerialController()
{
    if (m_virtualOpen) {
        VirtualSerialPair::instance()->release(this);
    }
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
    if (VirtualSerialPair::instance()->isEnabled()) {
        SerialPortDescriptor first;
        first.portName = VirtualSerialPair::portAName();
        first.description = AppI18n::text("内置虚拟串口 A（连接 B）");
        descriptors.append(first);
        SerialPortDescriptor second;
        second.portName = VirtualSerialPair::portBName();
        second.description = AppI18n::text("内置虚拟串口 B（连接 A）");
        descriptors.append(second);
    }
    return descriptors;
}

bool SerialController::isOpen() const { return m_virtualOpen || m_port.isOpen(); }

QString SerialController::portName() const
{
    return m_virtualPortName.isEmpty() ? m_port.portName() : m_virtualPortName;
}

QString SerialController::errorString() const
{
    return m_virtualPortName.isEmpty() ? m_port.errorString() : m_virtualError;
}

bool SerialController::openPort(const SerialPortConfig &config)
{
    if (isOpen()) {
        closePort();
    }

    m_virtualError.clear();
    if (VirtualSerialPair::isVirtualPort(config.portName)) {
        m_virtualPortName = config.portName;
        if (!VirtualSerialPair::instance()->acquire(config.portName, this, &m_virtualError)) {
            emit errorOccurred(m_virtualError);
            return false;
        }
        m_virtualOpen = true;
        emit opened(config.portName);
        return true;
    }
    m_virtualPortName.clear();

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
    if (m_virtualOpen) {
        m_virtualOpen = false;
        VirtualSerialPair::instance()->release(this);
        emit closed();
        return;
    }
    if (!m_port.isOpen()) {
        return;
    }
    m_port.close();
    emit closed();
}

bool SerialController::writeData(const QByteArray &data, QString *error)
{
    if (!isOpen()) {
        if (error) {
            *error = AppI18n::text("串口未连接");
        }
        return false;
    }
    if (m_virtualOpen) {
        if (!VirtualSerialPair::instance()->write(this, data, &m_virtualError)) {
            if (error) {
                *error = m_virtualError;
            }
            emit errorOccurred(m_virtualError);
            return false;
        }
        m_virtualError.clear();
        if (!data.isEmpty()) {
            emit writeQueued(data.size());
        }
        return true;
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
    if (m_virtualOpen) {
        return true;
    }
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
    if (m_virtualOpen) {
        return true;
    }
    if (!m_port.isOpen()) {
        return false;
    }
    const bool ok = m_port.setDataTerminalReady(enabled);
    if (!ok) {
        emit errorOccurred(m_port.errorString());
    }
    return ok;
}
