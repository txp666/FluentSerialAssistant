#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QObject>

class WorkbenchSessionsPage;

namespace AppControl {

class WorkbenchControlService : public QObject
{
    Q_OBJECT

  public:
    explicit WorkbenchControlService(WorkbenchSessionsPage *sessions, QObject *parent = nullptr);

    QJsonObject handleRequest(const QJsonObject &request);

  private:
    WorkbenchSessionsPage *m_sessions = nullptr;
};

} // namespace AppControl
