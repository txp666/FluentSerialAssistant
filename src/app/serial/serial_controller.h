#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtSerialPort/QSerialPort>

struct SerialPortDescriptor
{
    QString portName;
    QString description;
    QString manufacturer;
    QString serialNumber;
    bool hasVendorIdentifier = false;
    bool hasProductIdentifier = false;
    quint16 vendorIdentifier = 0;
    quint16 productIdentifier = 0;

    QString displayName() const;
    QString detailText() const;
};

struct SerialPortConfig
{
    QString portName;
    qint32 baudRate = 115200;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    bool requestToSend = false;
    bool dataTerminalReady = false;
};

class SerialController : public QObject
{
    Q_OBJECT

  public:
    explicit SerialController(QObject *parent = nullptr);

    static QList<SerialPortDescriptor> availablePorts();

    bool isOpen() const;
    QString portName() const;
    QString errorString() const;

    bool openPort(const SerialPortConfig &config);
    void closePort();
    bool writeData(const QByteArray &data, QString *error = nullptr);
    bool setRequestToSend(bool enabled);
    bool setDataTerminalReady(bool enabled);

  signals:
    void opened(const QString &portName);
    void closed();
    void dataReceived(const QByteArray &data);
    void writeQueued(qint64 bytes);
    void errorOccurred(const QString &message);

  private:
    QSerialPort m_port;
};
