#pragma once

#include <FluentQtWidgets/FluentQtWidgets.h>

class WorkbenchPage;

class WorkbenchSessionsPage : public QWidget
{
    Q_OBJECT

  public:
    explicit WorkbenchSessionsPage(QWidget *parent = nullptr);

    void saveSettings() const;
    void installTitleBarTabs(FluentQt::FluentTitleBar *titleBar);
    void setTitleBarTabsVisible(bool visible);

  public slots:
    void setTerminalFontFamily(const QString &family);

  signals:
    void settingsRequested();

  private:
    WorkbenchPage *addSession(WorkbenchPage *source = nullptr, bool restoreSavedSession = false);
    WorkbenchPage *currentSession() const;
    void closeSession(int index);
    QString nextRouteKey() const;
    QString nextTitle() const;

    FluentQt::TabWidget *m_tabs = nullptr;
    FluentQt::FluentTitleBar *m_titleBar = nullptr;
    int m_nextSession = 1;
};
