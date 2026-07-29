#include "mcp/mcp_server.h"

#include "app/control/local_control_client.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QTextStream>

#include <cstdio>

namespace {

QJsonObject jsonRpcResult(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("result"), result);
    return response;
}

QJsonObject jsonRpcError(const QJsonValue &id, int code, const QString &message)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("error"), error);
    return response;
}

QJsonObject stringProperty(const QString &description, const QJsonArray &values = {})
{
    QJsonObject property;
    property.insert(QStringLiteral("type"), QStringLiteral("string"));
    property.insert(QStringLiteral("description"), description);
    if (!values.isEmpty()) {
        property.insert(QStringLiteral("enum"), values);
    }
    return property;
}

QJsonObject integerProperty(const QString &description, int minimum, int maximum, int defaultValue)
{
    QJsonObject property;
    property.insert(QStringLiteral("type"), QStringLiteral("integer"));
    property.insert(QStringLiteral("description"), description);
    property.insert(QStringLiteral("minimum"), minimum);
    property.insert(QStringLiteral("maximum"), maximum);
    property.insert(QStringLiteral("default"), defaultValue);
    return property;
}

QJsonObject booleanProperty(const QString &description, bool defaultValue)
{
    QJsonObject property;
    property.insert(QStringLiteral("type"), QStringLiteral("boolean"));
    property.insert(QStringLiteral("description"), description);
    property.insert(QStringLiteral("default"), defaultValue);
    return property;
}

QJsonObject numberProperty(const QString &description, double defaultValue)
{
    QJsonObject property;
    property.insert(QStringLiteral("type"), QStringLiteral("number"));
    property.insert(QStringLiteral("description"), description);
    property.insert(QStringLiteral("default"), defaultValue);
    return property;
}

QJsonObject objectSchema(const QJsonObject &properties = {}, const QJsonArray &required = {})
{
    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), properties);
    if (!required.isEmpty()) {
        schema.insert(QStringLiteral("required"), required);
    }
    schema.insert(QStringLiteral("additionalProperties"), false);
    return schema;
}

QJsonObject tool(const QString &name, const QString &title, const QString &description, const QJsonObject &inputSchema,
                 bool readOnly, bool destructive = false, bool idempotent = false, bool openWorld = false)
{
    QJsonObject annotations;
    annotations.insert(QStringLiteral("readOnlyHint"), readOnly);
    annotations.insert(QStringLiteral("destructiveHint"), destructive);
    annotations.insert(QStringLiteral("idempotentHint"), readOnly || idempotent);
    annotations.insert(QStringLiteral("openWorldHint"), openWorld);

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("title"), title);
    result.insert(QStringLiteral("description"), description);
    result.insert(QStringLiteral("inputSchema"), inputSchema);
    result.insert(QStringLiteral("annotations"), annotations);
    return result;
}

QString actionForTool(const QString &name)
{
    static const QHash<QString, QString> actions = {
        {QStringLiteral("session_list"), QStringLiteral("session.list")},
        {QStringLiteral("session_select"), QStringLiteral("session.select")},
        {QStringLiteral("serial_list_ports"), QStringLiteral("serial.ports")},
        {QStringLiteral("serial_get_status"), QStringLiteral("session.status")},
        {QStringLiteral("serial_connect"), QStringLiteral("serial.connect")},
        {QStringLiteral("serial_disconnect"), QStringLiteral("serial.disconnect")},
        {QStringLiteral("serial_send_text"), QStringLiteral("serial.send_text")},
        {QStringLiteral("serial_send_hex"), QStringLiteral("serial.send_hex")},
        {QStringLiteral("serial_get_records"), QStringLiteral("serial.records")},
        {QStringLiteral("protocol_list"), QStringLiteral("protocol.list")},
        {QStringLiteral("protocol_select"), QStringLiteral("protocol.select")},
        {QStringLiteral("plot_open"), QStringLiteral("plot.open")},
    };
    return actions.value(name);
}

QJsonObject optionalSessionProperties()
{
    QJsonObject properties;
    properties.insert(QStringLiteral("session"),
                      stringProperty(QStringLiteral("Session ID. Omit to target the current GUI session.")));
    return properties;
}

} // namespace

int McpServer::run()
{
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream(stderr) << "Failed to open MCP stdin" << Qt::endl;
        return 1;
    }

    while (!input.atEnd()) {
        const QByteArray line = input.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            writeMessage(jsonRpcError({}, -32700, QStringLiteral("Parse error")));
            continue;
        }

        bool hasResponse = false;
        const QJsonObject response = handleMessage(document.object(), &hasResponse);
        if (hasResponse) {
            writeMessage(response);
        }
    }
    return 0;
}

QJsonObject McpServer::handleMessage(const QJsonObject &message, bool *hasResponse)
{
    *hasResponse = false;
    const bool hasId = message.contains(QStringLiteral("id"));
    const QJsonValue id = message.value(QStringLiteral("id"));
    const QString method = message.value(QStringLiteral("method")).toString();
    if (message.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0") || method.isEmpty()) {
        if (hasId) {
            *hasResponse = true;
            return jsonRpcError(id, -32600, QStringLiteral("Invalid Request"));
        }
        return {};
    }

    if (method == QStringLiteral("initialize")) {
        if (!hasId || !message.value(QStringLiteral("params")).isObject()) {
            *hasResponse = hasId;
            return jsonRpcError(id, -32602, QStringLiteral("Invalid initialize parameters"));
        }
        static const QStringList supportedVersions = {QStringLiteral("2025-11-25"), QStringLiteral("2025-06-18"),
                                                      QStringLiteral("2025-03-26"), QStringLiteral("2024-11-05")};
        const QString requested =
            message.value(QStringLiteral("params")).toObject().value(QStringLiteral("protocolVersion")).toString();
        m_protocolVersion = supportedVersions.contains(requested) ? requested : supportedVersions.first();
        m_initializeResponded = true;

        QJsonObject capabilities;
        capabilities.insert(QStringLiteral("tools"), QJsonObject());
        QJsonObject serverInfo;
        serverInfo.insert(QStringLiteral("name"), QStringLiteral("fluentserial-mcp"));
        serverInfo.insert(QStringLiteral("title"), QStringLiteral("Fluent Serial Assistant"));
        serverInfo.insert(QStringLiteral("version"), QStringLiteral(FLUENT_SERIAL_ASSISTANT_VERSION));
        QJsonObject result;
        result.insert(QStringLiteral("protocolVersion"), m_protocolVersion);
        result.insert(QStringLiteral("capabilities"), capabilities);
        result.insert(QStringLiteral("serverInfo"), serverInfo);
        result.insert(QStringLiteral("instructions"),
                      QStringLiteral("Control the running Fluent Serial Assistant GUI. The GUI owns serial ports; "
                                     "use session_list before targeting a non-current session."));
        *hasResponse = true;
        return jsonRpcResult(id, result);
    }

    if (method == QStringLiteral("notifications/initialized")) {
        if (m_initializeResponded) {
            m_initialized = true;
        }
        return {};
    }
    if (method.startsWith(QStringLiteral("notifications/"))) {
        return {};
    }
    if (!hasId) {
        return {};
    }
    *hasResponse = true;

    if (method == QStringLiteral("ping")) {
        return jsonRpcResult(id, {});
    }
    if (!m_initialized) {
        return jsonRpcError(id, -32002, QStringLiteral("Server is not initialized"));
    }
    if (method == QStringLiteral("tools/list")) {
        QJsonObject result;
        result.insert(QStringLiteral("tools"), tools());
        return jsonRpcResult(id, result);
    }
    if (method == QStringLiteral("tools/call")) {
        if (!message.value(QStringLiteral("params")).isObject()) {
            return jsonRpcError(id, -32602, QStringLiteral("Invalid tool call parameters"));
        }
        return handleToolCall(id, message.value(QStringLiteral("params")).toObject());
    }
    return jsonRpcError(id, -32601, QStringLiteral("Method not found"));
}

QJsonObject McpServer::handleToolCall(const QJsonValue &id, const QJsonObject &params)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QString action = actionForTool(name);
    if (action.isEmpty()) {
        return jsonRpcError(id, -32602, QStringLiteral("Unknown tool: %1").arg(name));
    }
    const QJsonValue argumentsValue = params.value(QStringLiteral("arguments"));
    if (!argumentsValue.isUndefined() && !argumentsValue.isObject()) {
        return jsonRpcError(id, -32602, QStringLiteral("Tool arguments must be an object"));
    }

    const AppControl::ClientReply clientReply =
        AppControl::LocalControlClient::call(action, argumentsValue.toObject(), 5000);
    QJsonObject controlResponse;
    if (clientReply.transportOk) {
        controlResponse = clientReply.response;
    } else {
        QJsonObject error;
        error.insert(QStringLiteral("code"), QStringLiteral("APP_UNAVAILABLE"));
        error.insert(QStringLiteral("message"), clientReply.errorMessage);
        controlResponse.insert(QStringLiteral("ok"), false);
        controlResponse.insert(QStringLiteral("error"), error);
    }

    const bool ok = controlResponse.value(QStringLiteral("ok")).toBool(false);
    QJsonObject structured;
    structured.insert(QStringLiteral("ok"), ok);
    if (ok) {
        const QJsonObject result = controlResponse.value(QStringLiteral("result")).toObject();
        for (auto it = result.constBegin(); it != result.constEnd(); ++it) {
            structured.insert(it.key(), it.value());
        }
    } else {
        structured.insert(QStringLiteral("error"), controlResponse.value(QStringLiteral("error")));
    }

    QJsonObject textContent;
    textContent.insert(QStringLiteral("type"), QStringLiteral("text"));
    textContent.insert(QStringLiteral("text"),
                       QString::fromUtf8(QJsonDocument(structured).toJson(QJsonDocument::Compact)));
    QJsonObject toolResult;
    toolResult.insert(QStringLiteral("content"), QJsonArray{textContent});
    toolResult.insert(QStringLiteral("structuredContent"), structured);
    toolResult.insert(QStringLiteral("isError"), !ok);
    return jsonRpcResult(id, toolResult);
}

QJsonArray McpServer::tools() const
{
    QJsonArray result;
    result.append(
        tool(QStringLiteral("session_list"), QStringLiteral("List serial sessions"),
             QStringLiteral("List GUI sessions, their IDs, connection state, counters, and current selection."),
             objectSchema(), true));

    QJsonObject sessionSelectProperties;
    sessionSelectProperties.insert(QStringLiteral("session"), stringProperty(QStringLiteral("Session ID to select.")));
    result.append(tool(QStringLiteral("session_select"), QStringLiteral("Select serial session"),
                       QStringLiteral("Make a GUI serial session current."),
                       objectSchema(sessionSelectProperties, QJsonArray{QStringLiteral("session")}), false, false,
                       true));

    result.append(tool(QStringLiteral("serial_list_ports"), QStringLiteral("List serial ports"),
                       QStringLiteral("List serial ports visible to the running GUI, including USB identifiers."),
                       objectSchema(), true));
    result.append(tool(QStringLiteral("serial_get_status"), QStringLiteral("Get serial status"),
                       QStringLiteral("Get connection settings, byte counters, record count, and protocol state."),
                       objectSchema(optionalSessionProperties()), true));

    QJsonObject connectProperties = optionalSessionProperties();
    connectProperties.insert(QStringLiteral("port"), stringProperty(QStringLiteral("Serial port name.")));
    connectProperties.insert(QStringLiteral("baudRate"),
                             integerProperty(QStringLiteral("Baud rate."), 1, 100000000, 115200));
    QJsonObject dataBits = integerProperty(QStringLiteral("Number of data bits."), 5, 8, 8);
    dataBits.insert(QStringLiteral("enum"), QJsonArray{5, 6, 7, 8});
    connectProperties.insert(QStringLiteral("dataBits"), dataBits);
    connectProperties.insert(
        QStringLiteral("parity"),
        stringProperty(QStringLiteral("Parity mode."),
                       QJsonArray{QStringLiteral("none"), QStringLiteral("even"), QStringLiteral("odd"),
                                  QStringLiteral("space"), QStringLiteral("mark")}));
    QJsonObject stopBits;
    stopBits.insert(QStringLiteral("type"), QStringLiteral("number"));
    stopBits.insert(QStringLiteral("description"), QStringLiteral("Number of stop bits."));
    stopBits.insert(QStringLiteral("enum"), QJsonArray{1, 1.5, 2});
    stopBits.insert(QStringLiteral("default"), 1);
    connectProperties.insert(QStringLiteral("stopBits"), stopBits);
    connectProperties.insert(
        QStringLiteral("flowControl"),
        stringProperty(QStringLiteral("Flow control mode."),
                       QJsonArray{QStringLiteral("none"), QStringLiteral("hardware"), QStringLiteral("software")}));
    connectProperties.insert(QStringLiteral("rts"), booleanProperty(QStringLiteral("Enable RTS."), false));
    connectProperties.insert(QStringLiteral("dtr"), booleanProperty(QStringLiteral("Enable DTR."), false));
    result.append(tool(QStringLiteral("serial_connect"), QStringLiteral("Connect serial port"),
                       QStringLiteral("Open a serial port in the target GUI session. This replaces any existing "
                                      "connection in that session."),
                       objectSchema(connectProperties, QJsonArray{QStringLiteral("port")}), false, true, false, true));

    result.append(tool(QStringLiteral("serial_disconnect"), QStringLiteral("Disconnect serial port"),
                       QStringLiteral("Close the serial port in the target GUI session."),
                       objectSchema(optionalSessionProperties()), false, false, true, true));

    QJsonObject textProperties = optionalSessionProperties();
    textProperties.insert(QStringLiteral("text"), stringProperty(QStringLiteral("Text to send.")));
    textProperties.insert(QStringLiteral("encoding"),
                          stringProperty(QStringLiteral("Text encoding."),
                                         QJsonArray{QStringLiteral("utf-8"), QStringLiteral("gbk"),
                                                    QStringLiteral("ascii"), QStringLiteral("latin1")}));
    textProperties.insert(QStringLiteral("lineEnding"),
                          stringProperty(QStringLiteral("Optional line ending."),
                                         QJsonArray{QStringLiteral("none"), QStringLiteral("cr"), QStringLiteral("lf"),
                                                    QStringLiteral("crlf")}));
    result.append(tool(QStringLiteral("serial_send_text"), QStringLiteral("Send serial text"),
                       QStringLiteral("Encode and send text through the target GUI serial session."),
                       objectSchema(textProperties, QJsonArray{QStringLiteral("text")}), false, true, false, true));

    QJsonObject hexProperties = optionalSessionProperties();
    hexProperties.insert(QStringLiteral("hex"),
                         stringProperty(QStringLiteral("HEX bytes, for example '01 03 00 00 00 02 C4 0B'.")));
    hexProperties.insert(QStringLiteral("lineEnding"),
                         stringProperty(QStringLiteral("Optional line ending appended as bytes."),
                                        QJsonArray{QStringLiteral("none"), QStringLiteral("cr"), QStringLiteral("lf"),
                                                   QStringLiteral("crlf")}));
    result.append(tool(QStringLiteral("serial_send_hex"), QStringLiteral("Send serial bytes"),
                       QStringLiteral("Parse and send exact HEX bytes through the target GUI serial session."),
                       objectSchema(hexProperties, QJsonArray{QStringLiteral("hex")}), false, true, false, true));

    QJsonObject recordProperties = optionalSessionProperties();
    recordProperties.insert(QStringLiteral("limit"),
                            integerProperty(QStringLiteral("Maximum records to return."), 1, 1000, 100));
    recordProperties.insert(
        QStringLiteral("direction"),
        stringProperty(QStringLiteral("Traffic direction filter."),
                       QJsonArray{QStringLiteral("all"), QStringLiteral("rx"), QStringLiteral("tx")}));
    result.append(tool(QStringLiteral("serial_get_records"), QStringLiteral("Read serial records"),
                       QStringLiteral("Read recent RX/TX records. When a binary protocol template is enabled, RX "
                                      "records include structured command, payload, and checksum parsing."),
                       objectSchema(recordProperties), true));

    result.append(tool(QStringLiteral("protocol_list"), QStringLiteral("List protocol templates"),
                       QStringLiteral("List binary protocol templates and the current enabled selection."),
                       objectSchema(optionalSessionProperties()), true));

    QJsonObject protocolProperties = optionalSessionProperties();
    protocolProperties.insert(QStringLiteral("name"), stringProperty(QStringLiteral("Protocol template name.")));
    protocolProperties.insert(QStringLiteral("enabled"),
                              booleanProperty(QStringLiteral("Enable parsing after selection."), true));
    result.append(tool(QStringLiteral("protocol_select"), QStringLiteral("Select protocol template"),
                       QStringLiteral("Select a saved binary protocol template and enable or disable frame parsing."),
                       objectSchema(protocolProperties, QJsonArray{QStringLiteral("name")}), false, false, true));

    QJsonObject plotProperties = optionalSessionProperties();
    plotProperties.insert(
        QStringLiteral("protocol"),
        stringProperty(QStringLiteral("How received data is converted to curve values."),
                       QJsonArray{QStringLiteral("numbers"), QStringLiteral("delimited"), QStringLiteral("keyValue"),
                                  QStringLiteral("json"), QStringLiteral("binary")}));
    QJsonObject fieldsProperty;
    fieldsProperty.insert(QStringLiteral("type"), QStringLiteral("array"));
    fieldsProperty.insert(QStringLiteral("description"),
                          QStringLiteral("Optional field names to keep. Multi-word and Unicode names are supported."));
    fieldsProperty.insert(QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
    plotProperties.insert(QStringLiteral("fields"), fieldsProperty);
    plotProperties.insert(
        QStringLiteral("binarySource"),
        stringProperty(QStringLiteral("Read binary fields from the complete frame or the active protocol payload."),
                       QJsonArray{QStringLiteral("frame"), QStringLiteral("payload")}));

    QJsonObject binaryFieldProperties;
    binaryFieldProperties.insert(QStringLiteral("name"), stringProperty(QStringLiteral("Curve and field name.")));
    binaryFieldProperties.insert(QStringLiteral("byteOffset"),
                                 integerProperty(QStringLiteral("Zero-based byte offset."), 0, 100000000, 0));
    binaryFieldProperties.insert(
        QStringLiteral("type"),
        stringProperty(QStringLiteral("Numeric field type."),
                       QJsonArray{QStringLiteral("u8"), QStringLiteral("i8"), QStringLiteral("u16"),
                                  QStringLiteral("i16"), QStringLiteral("u32"), QStringLiteral("i32"),
                                  QStringLiteral("u64"), QStringLiteral("i64"), QStringLiteral("f32"),
                                  QStringLiteral("f64")}));
    binaryFieldProperties.insert(QStringLiteral("byteOrder"),
                                 stringProperty(QStringLiteral("Byte order for multi-byte fields."),
                                                QJsonArray{QStringLiteral("little"), QStringLiteral("big")}));
    binaryFieldProperties.insert(QStringLiteral("scale"),
                                 numberProperty(QStringLiteral("Multiplier applied after decoding."), 1.0));
    binaryFieldProperties.insert(QStringLiteral("add"),
                                 numberProperty(QStringLiteral("Offset added after scaling."), 0.0));
    QJsonObject binaryFieldsProperty;
    binaryFieldsProperty.insert(QStringLiteral("type"), QStringLiteral("array"));
    binaryFieldsProperty.insert(
        QStringLiteral("description"),
        QStringLiteral("Typed binary fields. When omitted, each source byte is plotted as CH1, CH2, and so on."));
    binaryFieldsProperty.insert(
        QStringLiteral("items"),
        objectSchema(binaryFieldProperties,
                     QJsonArray{QStringLiteral("name"), QStringLiteral("byteOffset"), QStringLiteral("type")}));
    plotProperties.insert(QStringLiteral("binaryFields"), binaryFieldsProperty);
    plotProperties.insert(QStringLiteral("clear"),
                          booleanProperty(QStringLiteral("Clear existing curve data after opening."), false));
    result.append(tool(QStringLiteral("plot_open"), QStringLiteral("Open live plot"),
                       QStringLiteral("Open the target session's live curve window. Supports line-aware text, "
                                      "multi-word key-value fields, nested JSON, field selection, and typed binary "
                                      "frame or protocol-payload fields."),
                       objectSchema(plotProperties), false, false, true));
    return result;
}

void McpServer::writeMessage(const QJsonObject &message) const
{
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
    std::fflush(stdout);
}
