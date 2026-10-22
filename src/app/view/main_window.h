#pragma once

#include <FluentQtWidgets/FluentQtWidgets.h>
#include <QtCore/QPointer>

class QScreen;
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
    void showEvent(QShowEvent *event) override;

  private:
    void populateInterfaces();
    void showUpdateDialog();
    void trackScreen(QScreen *screen);
    void fitMinimumSizeToScreen();

    WorkbenchSessionsPage *m_workbenchPage = nullptr;
    AppControl::WorkbenchControlService *m_controlService = nullptr;
    AppControl::LocalControlServer *m_controlServer = nullptr;
    AppUpdate::UpdateManager *m_updateManager = nullptr;
    QPointer<UpdateDialog> m_updateDialog;
    QPointer<QScreen> m_trackedScreen;
    bool m_startupUpdateCheckStarted = false;
    bool m_installationReady = false;
};
