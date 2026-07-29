#pragma once

#include "app/core/plot_value_parser.h"
#include "app/core/protocol_template.h"
#include "app/serial/serial_controller.h"

#include <QtCore/QDateTime>
#include <QtCore/QList>
#include <QtCore/QString>

namespace AppControl {

struct SessionStatus
{
    QString id;
    bool connected = false;
    SerialPortConfig serialConfig;
    qint64 receivedBytes = 0;
    qint64 transmittedBytes = 0;
    int recordCount = 0;
    QString protocolName;
    bool protocolEnabled = false;
};

struct RecordSnapshot
{
    QDateTime timestamp;
    QString direction;
    QString source;
    QByteArray bytes;
    QString text;
    bool hasProtocolResult = false;
    AppProtocol::ParseResult protocolResult;
};

struct ProtocolSnapshot
{
    AppProtocol::ProtocolTemplate definition;
    bool selected = false;
    bool enabled = false;
};

class SessionControl
{
  public:
    virtual ~SessionControl() = default;

    virtual SessionStatus controlStatus() const = 0;
    virtual bool controlConnect(const SerialPortConfig &config, QString *error) = 0;
    virtual void controlDisconnect() = 0;
    virtual bool controlSendBytes(const QByteArray &data, const QString &source, QString *error) = 0;
    virtual QList<RecordSnapshot> controlRecords(int limit, const QString &direction) const = 0;
    virtual QList<ProtocolSnapshot> controlProtocols() const = 0;
    virtual bool controlSelectProtocol(const QString &name, bool enabled, QString *error) = 0;
    virtual bool controlShowPlot(const AppPlot::ParserConfig &config, bool clear, QString *error) = 0;
};

} // namespace AppControl
