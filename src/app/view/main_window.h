#pragma once

#include <FluentQtWidgets/FluentQtWidgets.h>
#include <QtCore/QPointer>

class WorkbenchSessionsPage;
class UpdateDialog;

namespace AppUpdate {
class UpdateManager;
}

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

    void startUpdateCheck();

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    void populateInterfaces();
    void showUpdateDialog();

    WorkbenchSessionsPage *m_workbenchPage = nullptr;
    AppControl::WorkbenchControlService *m_controlService = nullptr;
    AppControl::LocalControlServer *m_controlServer = nullptr;
    AppUpdate::UpdateManager *m_updateManager = nullptr;
    QPointer<UpdateDialog> m_updateDialog;
    bool m_startupUpdateCheckStarted = false;
    bool m_installationReady = false;
};
