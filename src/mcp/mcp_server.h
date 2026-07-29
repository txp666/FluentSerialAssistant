#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

class McpServer
{
  public:
    int run();

  private:
    QJsonObject handleMessage(const QJsonObject &message, bool *hasResponse);
    QJsonObject handleToolCall(const QJsonValue &id, const QJsonObject &params);
    QJsonArray tools() const;
    void writeMessage(const QJsonObject &message) const;

    bool m_initializeResponded = false;
    bool m_initialized = false;
    QString m_protocolVersion = QStringLiteral("2025-11-25");
};
