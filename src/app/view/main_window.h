#pragma once

#include <FluentQtWidgets/FluentQtWidgets.h>

class WorkbenchSessionsPage;

namespace AppControl {
class LocalControlServer;
class WorkbenchControlService;
} // namespace AppControl

class MainWindow : public FluentQt::MSFluentWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    void populateInterfaces();

    WorkbenchSessionsPage *m_workbenchPage = nullptr;
    AppControl::WorkbenchControlService *m_controlService = nullptr;
    AppControl::LocalControlServer *m_controlServer = nullptr;
};
