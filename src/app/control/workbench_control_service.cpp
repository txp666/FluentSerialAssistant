#include "app/control/workbench_control_service.h"

#include "app/control/control_protocol.h"
#include "app/control/session_control.h"
#include "app/core/hex_utils.h"
#include "app/core/protocol_template.h"
#include "app/core/text_encoding.h"
#include "app/serial/serial_controller.h"
#include "app/view/workbench_sessions_page.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QtGlobal>
#include <QtSerialPort/QSerialPort>

namespace {

QJsonObject serialConfigToJson(const SerialPortConfig &config)
{
    const auto parityKey = [](QSerialPort::Parity parity) {
        switch (parity) {
        case QSerialPort::EvenParity:
            return QStringLiteral("even");
        case QSerialPort::OddParity:
            return QStringLiteral("odd");
        case QSerialPort::SpaceParity:
            return QStringLiteral("space");
        case QSerialPort::MarkParity:
            return QStringLiteral("mark");
        case QSerialPort::NoParity:
        default:
            return QStringLiteral("none");
        }
    };
    const auto flowKey = [](QSerialPort::FlowControl flowControl) {
        switch (flowControl) {
        case QSerialPort::HardwareControl:
            return QStringLiteral("hardware");
        case QSerialPort::SoftwareControl:
            return QStringLiteral("software");
        case QSerialPort::NoFlowControl:
        default:
            return QStringLiteral("none");
        }
    };

    QJsonObject object;
    object.insert(QStringLiteral("port"), config.portName);
    object.insert(QStringLiteral("baudRate"), config.baudRate);
    object.insert(QStringLiteral("dataBits"), static_cast<int>(config.dataBits));
    object.insert(QStringLiteral("parity"), parityKey(config.parity));
    object.insert(QStringLiteral("stopBits"),
                  config.stopBits == QSerialPort::OneAndHalfStop ? 1.5 : static_cast<int>(config.stopBits));
    object.insert(QStringLiteral("flowControl"), flowKey(config.flowControl));
    object.insert(QStringLiteral("rts"), config.requestToSend);
    object.insert(QStringLiteral("dtr"), config.dataTerminalReady);
    return object;
}

QJsonObject statusToJson(const AppControl::SessionStatus &status)
{
    QJsonObject object;
    object.insert(QStringLiteral("session"), status.id);
    object.insert(QStringLiteral("connected"), status.connected);
    object.insert(QStringLiteral("serial"), serialConfigToJson(status.serialConfig));
    object.insert(QStringLiteral("receivedBytes"), static_cast<double>(status.receivedBytes));
    object.insert(QStringLiteral("transmittedBytes"), static_cast<double>(status.transmittedBytes));
    object.insert(QStringLiteral("recordCount"), status.recordCount);
    QJsonObject protocol;
    protocol.insert(QStringLiteral("name"), status.protocolName);
    protocol.insert(QStringLiteral("enabled"), status.protocolEnabled);
    object.insert(QStringLiteral("protocol"), protocol);
    return object;
}

QString sessionIdFromParams(const QJsonObject &params)
{
    return params.value(QStringLiteral("session")).toString(QStringLiteral("current")).trimmed();
}

AppControl::SessionControl *resolveSession(WorkbenchSessionsPage *sessions, const QJsonObject &params, QString *error)
{
    const QString sessionId = sessionIdFromParams(params);
    AppControl::SessionControl *session = sessions ? sessions->controlSession(sessionId) : nullptr;
    if (!session && error) {
        *error = QStringLiteral("Session was not found: %1").arg(sessionId);
    }
    return session;
}

bool parseDataBits(const QJsonValue &value, QSerialPort::DataBits *dataBits)
{
    const int bits = value.toInt(8);
    if (bits < 5 || bits > 8) {
        return false;
    }
    *dataBits = static_cast<QSerialPort::DataBits>(bits);
    return true;
}

bool parseParity(const QString &key, QSerialPort::Parity *parity)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QStringLiteral("none")) {
        *parity = QSerialPort::NoParity;
    } else if (normalized == QStringLiteral("even")) {
        *parity = QSerialPort::EvenParity;
    } else if (normalized == QStringLiteral("odd")) {
        *parity = QSerialPort::OddParity;
    } else if (normalized == QStringLiteral("space")) {
        *parity = QSerialPort::SpaceParity;
    } else if (normalized == QStringLiteral("mark")) {
        *parity = QSerialPort::MarkParity;
    } else {
        return false;
    }
    return true;
}

bool parseStopBits(double value, QSerialPort::StopBits *stopBits)
{
    if (qFuzzyCompare(value, 1.0)) {
        *stopBits = QSerialPort::OneStop;
    } else if (qFuzzyCompare(value, 1.5)) {
        *stopBits = QSerialPort::OneAndHalfStop;
    } else if (qFuzzyCompare(value, 2.0)) {
        *stopBits = QSerialPort::TwoStop;
    } else {
        return false;
    }
    return true;
}

bool parseFlowControl(const QString &key, QSerialPort::FlowControl *flowControl)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QStringLiteral("none")) {
        *flowControl = QSerialPort::NoFlowControl;
    } else if (normalized == QStringLiteral("hardware")) {
        *flowControl = QSerialPort::HardwareControl;
    } else if (normalized == QStringLiteral("software")) {
        *flowControl = QSerialPort::SoftwareControl;
    } else {
        return false;
    }
    return true;
}

QByteArray lineEndingBytes(const QString &key, bool *ok)
{
    const QString normalized = key.trimmed().toLower();
    *ok = true;
    if (normalized.isEmpty() || normalized == QStringLiteral("none")) {
        return {};
    }
    if (normalized == QStringLiteral("cr")) {
        return QByteArrayLiteral("\r");
    }
    if (normalized == QStringLiteral("lf")) {
        return QByteArrayLiteral("\n");
    }
    if (normalized == QStringLiteral("crlf")) {
        return QByteArrayLiteral("\r\n");
    }
    *ok = false;
    return {};
}

QJsonObject parseResultToJson(const AppProtocol::ParseResult &result)
{
    QJsonObject object;
    object.insert(QStringLiteral("ok"), result.ok);
    if (!result.ok) {
        object.insert(QStringLiteral("error"), result.errorMessage);
        return object;
    }
    object.insert(QStringLiteral("summary"), result.summary);
    object.insert(QStringLiteral("frameLength"), result.frameLength);
    object.insert(QStringLiteral("lengthValue"), static_cast<double>(result.lengthValue));
    object.insert(QStringLiteral("commandHex"), bytesToHex(result.command));
    object.insert(QStringLiteral("payloadHex"), bytesToHex(result.payload));
    object.insert(QStringLiteral("payloadBase64"), QString::fromLatin1(result.payload.toBase64()));
    object.insert(QStringLiteral("checksumHex"), bytesToHex(result.checksum));
    object.insert(QStringLiteral("checksumChecked"), result.checksumChecked);
    object.insert(QStringLiteral("checksumValid"), result.checksumValid);
    return object;
}

} // namespace

namespace AppControl {

WorkbenchControlService::WorkbenchControlService(WorkbenchSessionsPage *sessions, QObject *parent)
    : QObject(parent), m_sessions(sessions)
{
}

QJsonObject WorkbenchControlService::handleRequest(const QJsonObject &request)
{
    const QJsonValue id = request.value(QStringLiteral("id"));
    if (request.value(QStringLiteral("version")).toInt() != ProtocolVersion) {
        return errorResponse(id, QStringLiteral("UNSUPPORTED_VERSION"),
                             QStringLiteral("Unsupported control protocol version"));
    }
    const QString action = request.value(QStringLiteral("action")).toString().trimmed();
    if (action.isEmpty()) {
        return errorResponse(id, QStringLiteral("INVALID_REQUEST"), QStringLiteral("Action is required"));
    }
    const QJsonValue paramsValue = request.value(QStringLiteral("params"));
    if (!paramsValue.isUndefined() && !paramsValue.isObject()) {
        return errorResponse(id, QStringLiteral("INVALID_PARAMS"), QStringLiteral("Params must be an object"));
    }
    const QJsonObject params = paramsValue.toObject();

    if (action == QStringLiteral("app.ping")) {
        QJsonObject result;
        result.insert(QStringLiteral("name"), QStringLiteral("Fluent Serial Assistant"));
        result.insert(QStringLiteral("version"), QStringLiteral(FLUENT_SERIAL_ASSISTANT_VERSION));
        result.insert(QStringLiteral("controlProtocolVersion"), ProtocolVersion);
        return successResponse(id, result);
    }

    if (action == QStringLiteral("serial.ports")) {
        QJsonArray ports;
        for (const SerialPortDescriptor &descriptor : SerialController::availablePorts()) {
            QJsonObject port;
            port.insert(QStringLiteral("name"), descriptor.portName);
            port.insert(QStringLiteral("description"), descriptor.description);
            port.insert(QStringLiteral("manufacturer"), descriptor.manufacturer);
            port.insert(QStringLiteral("serialNumber"), descriptor.serialNumber);
            if (descriptor.hasVendorIdentifier) {
                port.insert(QStringLiteral("vendorId"), descriptor.vendorIdentifier);
            }
            if (descriptor.hasProductIdentifier) {
                port.insert(QStringLiteral("productId"), descriptor.productIdentifier);
            }
            ports.append(port);
        }
        QJsonObject result;
        result.insert(QStringLiteral("ports"), ports);
        return successResponse(id, result);
    }

    if (action == QStringLiteral("session.list")) {
        QJsonArray items;
        if (m_sessions) {
            for (const WorkbenchSessionsPage::ControlSessionEntry &entry : m_sessions->controlSessions()) {
                QJsonObject item = statusToJson(entry.control->controlStatus());
                item.insert(QStringLiteral("title"), entry.title);
                item.insert(QStringLiteral("current"), entry.current);
                items.append(item);
            }
        }
        QJsonObject result;
        result.insert(QStringLiteral("sessions"), items);
        return successResponse(id, result);
    }

    if (action == QStringLiteral("session.select")) {
        const QString sessionId = params.value(QStringLiteral("session")).toString().trimmed();
        if (sessionId.isEmpty()) {
            return errorResponse(id, QStringLiteral("INVALID_PARAMS"), QStringLiteral("Session is required"));
        }
        if (!m_sessions || !m_sessions->selectControlSession(sessionId)) {
            return errorResponse(id, QStringLiteral("SESSION_NOT_FOUND"),
                                 QStringLiteral("Session was not found: %1").arg(sessionId));
        }
        QJsonObject result;
        result.insert(QStringLiteral("session"), sessionId);
        return successResponse(id, result);
    }

    QString sessionError;
    SessionControl *session = resolveSession(m_sessions, params, &sessionError);
    if (!session) {
        return errorResponse(id, QStringLiteral("SESSION_NOT_FOUND"), sessionError);
    }

    if (action == QStringLiteral("session.status")) {
        return successResponse(id, statusToJson(session->controlStatus()));
    }

    if (action == QStringLiteral("serial.connect")) {
        SerialPortConfig config;
        config.portName = params.value(QStringLiteral("port")).toString().trimmed();
        config.baudRate = params.value(QStringLiteral("baudRate")).toInt(115200);
        if (config.portName.isEmpty() || config.baudRate <= 0) {
            return errorResponse(id, QStringLiteral("INVALID_PARAMS"),
                                 QStringLiteral("Port and a positive baudRate are required"));
        }
        if (!parseDataBits(params.value(QStringLiteral("dataBits")), &config.dataBits) ||
            !parseParity(params.value(QStringLiteral("parity")).toString(QStringLiteral("none")), &config.parity) ||
            !parseStopBits(params.value(QStringLiteral("stopBits")).toDouble(1.0), &config.stopBits) ||
            !parseFlowControl(params.value(QStringLiteral("flowControl")).toString(QStringLiteral("none")),
                              &config.flowControl)) {
            return errorResponse(id, QStringLiteral("INVALID_PARAMS"),
                                 QStringLiteral("Invalid serial connection parameters"));
        }
        config.requestToSend = params.value(QStringLiteral("rts")).toBool(false);
        config.dataTerminalReady = params.value(QStringLiteral("dtr")).toBool(false);

        QString error;
        if (!session->controlConnect(config, &error)) {
            return errorResponse(id, QStringLiteral("SERIAL_OPEN_FAILED"), error);
        }
        return successResponse(id, statusToJson(session->controlStatus()));
    }

    if (action == QStringLiteral("serial.disconnect")) {
        session->controlDisconnect();
        return successResponse(id, statusToJson(session->controlStatus()));
    }

    if (action == QStringLiteral("serial.send_text") || action == QStringLiteral("serial.send_hex")) {
        const QString payloadKey =
            action.endsWith(QStringLiteral("text")) ? QStringLiteral("text") : QStringLiteral("hex");
        const QString payloadText = params.value(payloadKey).toString();
        QByteArray payload;
        if (payloadKey == QStringLiteral("hex")) {
            const HexParseResult parsed = parseHexPayload(payloadText);
            if (!parsed.ok) {
                return errorResponse(id, QStringLiteral("INVALID_PAYLOAD"), parsed.errorMessage);
            }
            payload = parsed.bytes;
        } else {
            const QString encoding =
                params.value(QStringLiteral("encoding")).toString(QStringLiteral("utf-8")).trimmed().toLower();
            static const QStringList supportedEncodings = {QStringLiteral("utf-8"), QStringLiteral("gbk"),
                                                           QStringLiteral("ascii"), QStringLiteral("latin1")};
            if (!supportedEncodings.contains(encoding)) {
                return errorResponse(id, QStringLiteral("INVALID_PARAMS"),
                                     QStringLiteral("encoding must be utf-8, gbk, ascii, or latin1"));
            }
            const AppTextEncoding::EncodeResult encoded = AppTextEncoding::encode(payloadText, encoding);
            if (!encoded.ok) {
                return errorResponse(id, QStringLiteral("INVALID_PAYLOAD"), encoded.errorMessage);
            }
            payload = encoded.bytes;
        }
        bool lineEndingOk = false;
        payload.append(lineEndingBytes(params.value(QStringLiteral("lineEnding")).toString(QStringLiteral("none")),
                                       &lineEndingOk));
        if (!lineEndingOk) {
            return errorResponse(id, QStringLiteral("INVALID_PARAMS"),
                                 QStringLiteral("lineEnding must be none, cr, lf, or crlf"));
        }
        if (payload.isEmpty()) {
            return errorResponse(id, QStringLiteral("INVALID_PAYLOAD"), QStringLiteral("Payload is empty"));
        }

        QString error;
        if (!session->controlSendBytes(payload, QStringLiteral("AI"), &error)) {
            return errorResponse(id, QStringLiteral("SERIAL_WRITE_FAILED"), error);
        }
        QJsonObject result;
        result.insert(QStringLiteral("session"), session->controlStatus().id);
        result.insert(QStringLiteral("bytesSent"), payload.size());
        result.insert(QStringLiteral("hex"), bytesToHex(payload));
        return successResponse(id, result);
    }

    if (action == QStringLiteral("serial.records")) {
        const int limit = qBound(1, params.value(QStringLiteral("limit")).toInt(100), 1000);
        const QString direction = params.value(QStringLiteral("direction")).toString(QStringLiteral("all")).toLower();
        if (direction != QStringLiteral("all") && direction != QStringLiteral("rx") &&
            direction != QStringLiteral("tx")) {
            return errorResponse(id, QStringLiteral("INVALID_PARAMS"),
                                 QStringLiteral("direction must be all, rx, or tx"));
        }

        QJsonArray records;
        for (const RecordSnapshot &snapshot : session->controlRecords(limit, direction)) {
            QJsonObject record;
            record.insert(QStringLiteral("time"), snapshot.timestamp.toString(Qt::ISODateWithMs));
            record.insert(QStringLiteral("direction"), snapshot.direction);
            record.insert(QStringLiteral("source"), snapshot.source);
            record.insert(QStringLiteral("length"), snapshot.bytes.size());
            record.insert(QStringLiteral("hex"), bytesToHex(snapshot.bytes));
            record.insert(QStringLiteral("base64"), QString::fromLatin1(snapshot.bytes.toBase64()));
            record.insert(QStringLiteral("text"), snapshot.text);
            if (snapshot.hasProtocolResult) {
                record.insert(QStringLiteral("protocol"), parseResultToJson(snapshot.protocolResult));
            }
            records.append(record);
        }
        QJsonObject result;
        result.insert(QStringLiteral("session"), session->controlStatus().id);
        result.insert(QStringLiteral("records"), records);
        return successResponse(id, result);
    }

    if (action == QStringLiteral("protocol.list")) {
        QJsonArray protocols;
        for (const ProtocolSnapshot &snapshot : session->controlProtocols()) {
            QJsonObject protocol = AppProtocol::toJson(snapshot.definition);
            protocol.insert(QStringLiteral("selected"), snapshot.selected);
            protocol.insert(QStringLiteral("enabled"), snapshot.enabled);
            protocols.append(protocol);
        }
        QJsonObject result;
        result.insert(QStringLiteral("session"), session->controlStatus().id);
        result.insert(QStringLiteral("protocols"), protocols);
        return successResponse(id, result);
    }

    if (action == QStringLiteral("protocol.select")) {
        const QString name = params.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) {
            return errorResponse(id, QStringLiteral("INVALID_PARAMS"), QStringLiteral("Protocol name is required"));
        }
        QString error;
        const bool enabled = params.value(QStringLiteral("enabled")).toBool(true);
        if (!session->controlSelectProtocol(name, enabled, &error)) {
            return errorResponse(id, QStringLiteral("PROTOCOL_NOT_FOUND"), error);
        }
        return successResponse(id, statusToJson(session->controlStatus()));
    }

    if (action == QStringLiteral("plot.open")) {
        AppPlot::ParserConfig config;
        QString error;
        if (!AppPlot::parserConfigFromJson(params, &config, &error)) {
            return errorResponse(id, QStringLiteral("INVALID_PARAMS"), error);
        }
        if (!session->controlShowPlot(config, params.value(QStringLiteral("clear")).toBool(false), &error)) {
            return errorResponse(id, QStringLiteral("PLOT_CONFIG_FAILED"), error);
        }
        QJsonObject result;
        result.insert(QStringLiteral("session"), session->controlStatus().id);
        result.insert(QStringLiteral("visible"), true);
        result.insert(QStringLiteral("protocol"), AppPlot::protocolKey(config.protocol));
        result.insert(QStringLiteral("fields"), QJsonArray::fromStringList(config.fields));
        if (config.protocol == AppPlot::Protocol::Binary) {
            result.insert(QStringLiteral("binarySource"), AppPlot::binarySourceKey(config.binarySource));
            result.insert(QStringLiteral("binaryFieldCount"), config.binaryFields.size());
        }
        return successResponse(id, result);
    }

    return errorResponse(id, QStringLiteral("UNKNOWN_ACTION"), QStringLiteral("Unknown action: %1").arg(action));
}

} // namespace AppControl
