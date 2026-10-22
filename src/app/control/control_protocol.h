#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

class QLocalSocket;

namespace AppControl {

inline constexpr int ProtocolVersion = 1;
inline constexpr qsizetype MaximumMessageBytes = 1024 * 1024;

// Empty when no name private to the user is available.
QString localServerName();
// Whether the service at the other end of a connected socket runs as this user.
bool serverRunsAsCurrentUser(const QLocalSocket &socket);
QByteArray encodeMessage(const QJsonObject &message);
QJsonObject successResponse(const QJsonValue &id, const QJsonObject &result = {});
QJsonObject errorResponse(const QJsonValue &id, const QString &code, const QString &message);

} // namespace AppControl
