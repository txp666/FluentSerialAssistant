#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>

namespace AppControl {

struct ClientReply
{
    bool transportOk = false;
    QJsonObject response;
    QString errorMessage;
};

class LocalControlClient
{
  public:
    static ClientReply call(const QString &action, const QJsonObject &params = {}, int timeoutMs = 5000);
};

} // namespace AppControl
