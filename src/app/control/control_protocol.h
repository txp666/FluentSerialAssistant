#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

namespace AppControl {

inline constexpr int ProtocolVersion = 1;
inline constexpr qsizetype MaximumMessageBytes = 1024 * 1024;

QString localServerName();
QByteArray encodeMessage(const QJsonObject &message);
QJsonObject successResponse(const QJsonValue &id, const QJsonObject &result = {});
QJsonObject errorResponse(const QJsonValue &id, const QString &code, const QString &message);

} // namespace AppControl
