#pragma once

#include <FluentQtWidgets/FluentQtWidgets.h>

class WorkbenchPage;

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

    WorkbenchPage *m_workbenchPage = nullptr;
};
