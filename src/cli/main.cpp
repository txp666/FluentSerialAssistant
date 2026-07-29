#include "app/control/control_protocol.h"
#include "app/control/local_control_client.h"

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QTextStream>

namespace {

int printCliError(const QString &code, const QString &message)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);
    QJsonObject response;
    response.insert(QStringLiteral("ok"), false);
    response.insert(QStringLiteral("error"), error);
    QTextStream(stderr) << QJsonDocument(response).toJson(QJsonDocument::Compact) << Qt::endl;
    return 2;
}

void insertSession(QJsonObject *params, const QCommandLineParser &parser)
{
    if (parser.isSet(QStringLiteral("session"))) {
        params->insert(QStringLiteral("session"), parser.value(QStringLiteral("session")));
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("fluentserial-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral(FLUENT_SERIAL_ASSISTANT_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Machine-readable CLI for controlling a running Fluent Serial Assistant instance."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("command"),
        QStringLiteral("ping, ports, sessions, select, status, connect, disconnect, send-text, send-hex, records, "
                       "protocols, protocol-use, plot, or call"));
    parser.addPositionalArgument(QStringLiteral("action"), QStringLiteral("IPC action name used by the call command"),
                                 QStringLiteral("[action]"));

    parser.addOption(
        {QStringLiteral("session"), QStringLiteral("Target session ID (defaults to current)"), QStringLiteral("id")});
    parser.addOption({QStringLiteral("port"), QStringLiteral("Serial port name"), QStringLiteral("name")});
    parser.addOption(
        {QStringLiteral("baud"), QStringLiteral("Baud rate"), QStringLiteral("rate"), QStringLiteral("115200")});
    parser.addOption({QStringLiteral("data-bits"), QStringLiteral("Data bits: 5, 6, 7, or 8"), QStringLiteral("bits"),
                      QStringLiteral("8")});
    parser.addOption({QStringLiteral("parity"), QStringLiteral("Parity: none, even, odd, space, or mark"),
                      QStringLiteral("mode"), QStringLiteral("none")});
    parser.addOption({QStringLiteral("stop-bits"), QStringLiteral("Stop bits: 1, 1.5, or 2"), QStringLiteral("bits"),
                      QStringLiteral("1")});
    parser.addOption({QStringLiteral("flow-control"), QStringLiteral("Flow control: none, hardware, or software"),
                      QStringLiteral("mode"), QStringLiteral("none")});
    parser.addOption({QStringLiteral("rts"), QStringLiteral("Enable RTS when connecting")});
    parser.addOption({QStringLiteral("dtr"), QStringLiteral("Enable DTR when connecting")});
    parser.addOption({QStringLiteral("text"), QStringLiteral("Text payload"), QStringLiteral("value")});
    parser.addOption({QStringLiteral("hex"), QStringLiteral("HEX payload"), QStringLiteral("value")});
    parser.addOption({QStringLiteral("encoding"), QStringLiteral("Text encoding: utf-8, gbk, ascii, or latin1"),
                      QStringLiteral("name"), QStringLiteral("utf-8")});
    parser.addOption({QStringLiteral("line-ending"), QStringLiteral("Line ending: none, cr, lf, or crlf"),
                      QStringLiteral("mode"), QStringLiteral("none")});
    parser.addOption({QStringLiteral("limit"), QStringLiteral("Maximum records to return (1-1000)"),
                      QStringLiteral("count"), QStringLiteral("100")});
    parser.addOption({QStringLiteral("direction"), QStringLiteral("Record direction: all, rx, or tx"),
                      QStringLiteral("value"), QStringLiteral("all")});
    parser.addOption({QStringLiteral("name"), QStringLiteral("Protocol template name"), QStringLiteral("value")});
    parser.addOption({QStringLiteral("disable"), QStringLiteral("Select the protocol template but disable parsing")});
    parser.addOption({QStringLiteral("plot-protocol"),
                      QStringLiteral("Plot protocol: numbers, delimited, keyValue, or json"), QStringLiteral("name"),
                      QStringLiteral("numbers")});
    parser.addOption({QStringLiteral("clear"), QStringLiteral("Clear plot data after opening")});
    parser.addOption({QStringLiteral("params"), QStringLiteral("Raw JSON object for the call command"),
                      QStringLiteral("json"), QStringLiteral("{}")});
    parser.addOption({QStringLiteral("timeout"), QStringLiteral("IPC timeout in milliseconds (100-60000)"),
                      QStringLiteral("ms"), QStringLiteral("5000")});
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        parser.showHelp(2);
    }

    const QString command = positional.first().trimmed().toLower();
    QString action;
    QJsonObject params;
    insertSession(&params, parser);

    if (command == QStringLiteral("ping")) {
        action = QStringLiteral("app.ping");
    } else if (command == QStringLiteral("ports")) {
        action = QStringLiteral("serial.ports");
    } else if (command == QStringLiteral("sessions")) {
        action = QStringLiteral("session.list");
    } else if (command == QStringLiteral("select")) {
        action = QStringLiteral("session.select");
        if (!parser.isSet(QStringLiteral("session"))) {
            return printCliError(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("--session is required"));
        }
    } else if (command == QStringLiteral("status")) {
        action = QStringLiteral("session.status");
    } else if (command == QStringLiteral("connect")) {
        action = QStringLiteral("serial.connect");
        if (!parser.isSet(QStringLiteral("port"))) {
            return printCliError(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("--port is required"));
        }
        bool baudOk = false;
        bool dataBitsOk = false;
        bool stopBitsOk = false;
        const int baudRate = parser.value(QStringLiteral("baud")).toInt(&baudOk);
        const int dataBits = parser.value(QStringLiteral("data-bits")).toInt(&dataBitsOk);
        const double stopBits = parser.value(QStringLiteral("stop-bits")).toDouble(&stopBitsOk);
        if (!baudOk || !dataBitsOk || !stopBitsOk) {
            return printCliError(QStringLiteral("INVALID_ARGUMENT"),
                                 QStringLiteral("Invalid numeric serial connection option"));
        }
        params.insert(QStringLiteral("port"), parser.value(QStringLiteral("port")));
        params.insert(QStringLiteral("baudRate"), baudRate);
        params.insert(QStringLiteral("dataBits"), dataBits);
        params.insert(QStringLiteral("parity"), parser.value(QStringLiteral("parity")));
        params.insert(QStringLiteral("stopBits"), stopBits);
        params.insert(QStringLiteral("flowControl"), parser.value(QStringLiteral("flow-control")));
        params.insert(QStringLiteral("rts"), parser.isSet(QStringLiteral("rts")));
        params.insert(QStringLiteral("dtr"), parser.isSet(QStringLiteral("dtr")));
    } else if (command == QStringLiteral("disconnect")) {
        action = QStringLiteral("serial.disconnect");
    } else if (command == QStringLiteral("send-text")) {
        action = QStringLiteral("serial.send_text");
        if (!parser.isSet(QStringLiteral("text"))) {
            return printCliError(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("--text is required"));
        }
        params.insert(QStringLiteral("text"), parser.value(QStringLiteral("text")));
        params.insert(QStringLiteral("encoding"), parser.value(QStringLiteral("encoding")));
        params.insert(QStringLiteral("lineEnding"), parser.value(QStringLiteral("line-ending")));
    } else if (command == QStringLiteral("send-hex")) {
        action = QStringLiteral("serial.send_hex");
        if (!parser.isSet(QStringLiteral("hex"))) {
            return printCliError(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("--hex is required"));
        }
        params.insert(QStringLiteral("hex"), parser.value(QStringLiteral("hex")));
        params.insert(QStringLiteral("lineEnding"), parser.value(QStringLiteral("line-ending")));
    } else if (command == QStringLiteral("records")) {
        action = QStringLiteral("serial.records");
        bool limitOk = false;
        const int limit = parser.value(QStringLiteral("limit")).toInt(&limitOk);
        if (!limitOk) {
            return printCliError(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("--limit must be an integer"));
        }
        params.insert(QStringLiteral("limit"), limit);
        params.insert(QStringLiteral("direction"), parser.value(QStringLiteral("direction")));
    } else if (command == QStringLiteral("protocols")) {
        action = QStringLiteral("protocol.list");
    } else if (command == QStringLiteral("protocol-use")) {
        action = QStringLiteral("protocol.select");
        if (!parser.isSet(QStringLiteral("name"))) {
            return printCliError(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("--name is required"));
        }
        params.insert(QStringLiteral("name"), parser.value(QStringLiteral("name")));
        params.insert(QStringLiteral("enabled"), !parser.isSet(QStringLiteral("disable")));
    } else if (command == QStringLiteral("plot")) {
        action = QStringLiteral("plot.open");
        params.insert(QStringLiteral("protocol"), parser.value(QStringLiteral("plot-protocol")));
        params.insert(QStringLiteral("clear"), parser.isSet(QStringLiteral("clear")));
    } else if (command == QStringLiteral("call")) {
        if (positional.size() < 2) {
            return printCliError(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("The call action is required"));
        }
        action = positional.at(1);
        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(parser.value(QStringLiteral("params")).toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return printCliError(QStringLiteral("INVALID_ARGUMENT"),
                                 QStringLiteral("--params must be a JSON object: %1").arg(parseError.errorString()));
        }
        params = document.object();
        insertSession(&params, parser);
    } else {
        return printCliError(QStringLiteral("UNKNOWN_COMMAND"), QStringLiteral("Unknown command: %1").arg(command));
    }

    bool timeoutOk = false;
    const int timeoutMs = parser.value(QStringLiteral("timeout")).toInt(&timeoutOk);
    if (!timeoutOk) {
        return printCliError(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("--timeout must be an integer"));
    }

    const AppControl::ClientReply reply = AppControl::LocalControlClient::call(action, params, timeoutMs);
    if (!reply.transportOk) {
        return printCliError(QStringLiteral("APP_UNAVAILABLE"), reply.errorMessage);
    }
    QTextStream(stdout) << QJsonDocument(reply.response).toJson(QJsonDocument::Compact) << Qt::endl;
    return reply.response.value(QStringLiteral("ok")).toBool(false) ? 0 : 4;
}
