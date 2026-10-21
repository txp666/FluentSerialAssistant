#include "app/control/control_protocol.h"

#include <QtCore/QDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QStandardPaths>

namespace AppControl {

QString localServerName()
{
#ifdef Q_OS_UNIX
    // A bare name puts the socket in the temporary directory, which Linux shares
    // between users: another user could create it first and receive the
    // commands. The runtime directory is private to the user.
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral("fluentserial-control-v1"));
#else
    return QStringLiteral("tech.zhangshu.FluentSerialAssistant.control.v1");
#endif
}

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
