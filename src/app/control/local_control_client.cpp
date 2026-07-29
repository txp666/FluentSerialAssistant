#include "app/control/local_control_client.h"

#include "app/control/control_protocol.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QUuid>
#include <QtCore/QtGlobal>
#include <QtNetwork/QLocalSocket>

namespace AppControl {

ClientReply LocalControlClient::call(const QString &action, const QJsonObject &params, int timeoutMs)
{
    ClientReply reply;
    const int boundedTimeout = qBound(100, timeoutMs, 60000);

    QLocalSocket socket;
    socket.connectToServer(localServerName(), QIODevice::ReadWrite);
    if (!socket.waitForConnected(boundedTimeout)) {
        reply.errorMessage =
            QStringLiteral("Fluent Serial Assistant is not running or its control service is unavailable: %1")
                .arg(socket.errorString());
        return reply;
    }

    QJsonObject request;
    request.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    request.insert(QStringLiteral("version"), ProtocolVersion);
    request.insert(QStringLiteral("action"), action);
    request.insert(QStringLiteral("params"), params);

    const QByteArray payload = encodeMessage(request);
    if (socket.write(payload) != payload.size() || !socket.waitForBytesWritten(boundedTimeout)) {
        reply.errorMessage = QStringLiteral("Failed to send the control request: %1").arg(socket.errorString());
        return reply;
    }

    QByteArray input;
    while (!input.contains('\n')) {
        if (!socket.waitForReadyRead(boundedTimeout)) {
            reply.errorMessage =
                QStringLiteral("Timed out waiting for the control response: %1").arg(socket.errorString());
            return reply;
        }
        input.append(socket.readAll());
        if (input.size() > MaximumMessageBytes) {
            reply.errorMessage = QStringLiteral("The control response exceeded the size limit");
            return reply;
        }
    }

    const QByteArray line = input.left(input.indexOf('\n')).trimmed();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        reply.errorMessage =
            QStringLiteral("The control service returned invalid JSON: %1").arg(parseError.errorString());
        return reply;
    }

    reply.transportOk = true;
    reply.response = document.object();
    return reply;
}

} // namespace AppControl
