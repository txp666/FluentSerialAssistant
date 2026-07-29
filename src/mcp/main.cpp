#include "mcp/mcp_server.h"

#include <QtCore/QCoreApplication>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("fluentserial-mcp"));
    QCoreApplication::setApplicationVersion(QStringLiteral(FLUENT_SERIAL_ASSISTANT_VERSION));
    McpServer server;
    return server.run();
}
