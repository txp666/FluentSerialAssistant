#include "app/control/control_protocol.h"

#include <QtCore/QJsonDocument>

namespace AppControl {

QString localServerName() { return QStringLiteral("tech.zhangshu.FluentSerialAssistant.control.v1"); }

QByteArray encodeMessage(const QJsonObject &message)
{
    QByteArray output = QJsonDocument(message).toJson(QJsonDocument::Compact);
    output.append('\n');
    return output;
}

QJsonObject successResponse(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject response;
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("ok"), true);
    response.insert(QStringLiteral("result"), result);
    return response;
}

QJsonObject errorResponse(const QJsonValue &id, const QString &code, const QString &message)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);

    QJsonObject response;
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("ok"), false);
    response.insert(QStringLiteral("error"), error);
    return response;
}

} // namespace AppControl
