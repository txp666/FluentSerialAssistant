#include "app/control/local_control_server.h"

#include "app/control/control_protocol.h"
#include "app/control/workbench_control_service.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtNetwork/QLocalSocket>

namespace AppControl {

LocalControlServer::LocalControlServer(WorkbenchControlService *service, QObject *parent)
    : QObject(parent), m_service(service)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, &LocalControlServer::acceptConnections);
}

bool LocalControlServer::start(QString *error)
{
    if (error) {
        error->clear();
    }
    if (m_server.isListening()) {
        return true;
    }
    if (m_server.listen(localServerName())) {
        return true;
    }

    QLocalSocket probe;
    probe.connectToServer(localServerName());
    if (probe.waitForConnected(150)) {
        if (error) {
            *error = QStringLiteral("Another Fluent Serial Assistant instance already owns the control service");
        }
        return false;
    }

    QLocalServer::removeServer(localServerName());
    if (m_server.listen(localServerName())) {
        return true;
    }
    if (error) {
        *error = m_server.errorString();
    }
    return false;
}

void LocalControlServer::acceptConnections()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        m_buffers.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { readRequest(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void LocalControlServer::readRequest(QLocalSocket *socket)
{
    if (!socket || !m_buffers.contains(socket)) {
        return;
    }
    QByteArray &buffer = m_buffers[socket];
    buffer.append(socket->readAll());
    if (buffer.size() > MaximumMessageBytes) {
        sendAndClose(socket, errorResponse({}, QStringLiteral("MESSAGE_TOO_LARGE"),
                                           QStringLiteral("Control request exceeded the size limit")));
        return;
    }

    const qsizetype lineEnd = buffer.indexOf('\n');
    if (lineEnd < 0) {
        return;
    }
    const QByteArray line = buffer.left(lineEnd).trimmed();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        sendAndClose(socket,
                     errorResponse({}, QStringLiteral("INVALID_JSON"),
                                   QStringLiteral("Invalid control request JSON: %1").arg(parseError.errorString())));
        return;
    }
    sendAndClose(socket, m_service ? m_service->handleRequest(document.object())
                                   : errorResponse(document.object().value(QStringLiteral("id")),
                                                   QStringLiteral("SERVICE_UNAVAILABLE"),
                                                   QStringLiteral("Control service is unavailable")));
}

void LocalControlServer::sendAndClose(QLocalSocket *socket, const QJsonObject &response)
{
    if (!socket) {
        return;
    }
    m_buffers.remove(socket);
    socket->write(encodeMessage(response));
    socket->flush();
    socket->disconnectFromServer();
}

} // namespace AppControl
