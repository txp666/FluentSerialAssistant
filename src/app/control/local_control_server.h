#pragma once

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtNetwork/QLocalServer>

class QLocalSocket;

namespace AppControl {

class WorkbenchControlService;

class LocalControlServer : public QObject
{
    Q_OBJECT

  public:
    explicit LocalControlServer(WorkbenchControlService *service, QObject *parent = nullptr);

    bool start(QString *error = nullptr);

  private:
    void acceptConnections();
    void readRequest(QLocalSocket *socket);
    void sendAndClose(QLocalSocket *socket, const QJsonObject &response);

    WorkbenchControlService *m_service = nullptr;
    QLocalServer m_server;
    QHash<QLocalSocket *, QByteArray> m_buffers;
};

} // namespace AppControl
